#include "webrtc_ros/webrtc_streamer.hpp"

WebRTCStreamer::WebRTCStreamer()
    : Node("webrtc_streamer") {
  RCLCPP_INFO(get_logger(), "Starting WebRTC Streamer...");

  // Initialize GStreamer once globally
  gst_init(nullptr, nullptr);

  // Setup WebSocket signaling
  this->declare_parameter("signaling_url", "ws://localhost:8000/uav999");
  setupSignaling_();
}

WebRTCStreamer::~WebRTCStreamer() {
  // Clean up WebSocket client and thread
  signaling_ws_client_.stop();
  if (ws_thread_.joinable())
    ws_thread_.join();

  // Clean up GStreamer sessions
  std::unique_lock lock(mtx_sessions_);
  sessions_.clear();
}

void WebRTCStreamer::connectSignaling_() {
  // Clean previous thread if exists
  if (ws_thread_.joinable())
    ws_thread_.join();

  signaling_ws_client_.reset();

  websocketpp::lib::error_code ec;
  std::string                  url = this->get_parameter("signaling_url").as_string();
  WsClient::connection_ptr     con = signaling_ws_client_.get_connection(url, ec);
  if (ec) {
    RCLCPP_ERROR(get_logger(), "WebSocket connection error: %s", ec.message().c_str());
    scheduleReconnect_();
    return;
  }

  // Run WebSocket event loop in a separate thread
  signaling_ws_client_.connect(con);
  ws_thread_ = std::thread([this]() {
    try {
      signaling_ws_client_.run();
    }
    catch (const std::exception &e) {
      RCLCPP_ERROR(get_logger(), "WebSocket client error: %s", e.what());
    }
  });
}

void WebRTCStreamer::scheduleReconnect_() {
  reconnect_timer_ = this->create_wall_timer(std::chrono::seconds(3), [this]() {
    // Cancel the timer so it only fires once per failure
    reconnect_timer_->cancel();
    connectSignaling_();
  });
}

void WebRTCStreamer::setupSignaling_() {
  signaling_ws_client_.clear_access_channels(websocketpp::log::alevel::all);
  signaling_ws_client_.init_asio();

  signaling_ws_client_.set_open_handler([this](websocketpp::connection_hdl hdl) {
    RCLCPP_INFO(get_logger(), "Connected to signaling server");
    ws_hdl_ = hdl;

    // Stop the reconnect timer once successfully connected
    if (reconnect_timer_)
      reconnect_timer_->cancel();
  });

  signaling_ws_client_.set_message_handler([this](websocketpp::connection_hdl, WsClient::message_ptr msg) {
    if (msg->get_opcode() == websocketpp::frame::opcode::text) {
      std::string payload = msg->get_payload();
      RCLCPP_INFO(get_logger(), "Received signaling message: %s", payload.c_str());
      handleSignalingMessage_(payload);
    } else {
      RCLCPP_WARN(get_logger(), "Received unsupported non-text message");
    }
  });

  signaling_ws_client_.set_close_handler([this](websocketpp::connection_hdl) {
    RCLCPP_WARN(get_logger(), "Disconnected from signaling server. Trying to reconnect...");
    scheduleReconnect_();
  });

  signaling_ws_client_.set_fail_handler([this](websocketpp::connection_hdl) {
    RCLCPP_ERROR(get_logger(), "Failed to connect to signaling server. Retrying...");
    scheduleReconnect_();
  });

  // Start the Initial connection attempt
  connectSignaling_();
}

void WebRTCStreamer::sendSignaling(const json &msg) {
  std::string msg_str = msg.dump();
  signaling_ws_client_.send(ws_hdl_, msg_str, websocketpp::frame::opcode::text);
  RCLCPP_DEBUG(get_logger(), "Sent signaling message: %s", msg_str.c_str());
}

void WebRTCStreamer::handleSignalingMessage_(const std::string &payload) {
  try {
    json        msg     = json::parse(payload);
    std::string type    = msg.value("type", "");
    std::string peer_id = msg.value("id", "");

    if (peer_id.empty())
      return;

    if (type == "request") {
      std::vector<std::string> requested;
      if (msg.contains("streams")) {
        if (!msg["streams"].is_array()) {
          RCLCPP_WARN(get_logger(), "Ignoring request from %s: missing 'streams' field", peer_id.c_str());

          json err;
          err["id"]      = peer_id;
          err["type"]    = "error";
          err["message"] = "Missing 'streams' field in request";

          sendSignaling(err);
          return;
        } else {
          for (auto &s : msg["streams"])
            requested.push_back(s.get<std::string>());
        }
      }
      createPeerSession_(peer_id, requested);
    } else if (type == "answer") {
      std::unique_lock lock(mtx_sessions_);
      if (sessions_.find(peer_id) == sessions_.end()) {
        RCLCPP_WARN(get_logger(), "Answer from unknown peer: %s", peer_id.c_str());
        return;
      }

      GstElement *webrtc  = sessions_[peer_id]->webrtc;
      std::string sdp_str = msg["sdp"].get<std::string>();

      // Parse the remote SDP and apply it to webrtcbin
      GstSDPMessage *sdp;
      gst_sdp_message_new(&sdp);
      gst_sdp_message_parse_buffer((guint8 *)sdp_str.c_str(), sdp_str.size(), sdp);
      GstWebRTCSessionDescription *answer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_ANSWER, sdp);

      GstPromise *promise = gst_promise_new();
      g_signal_emit_by_name(webrtc, "set-remote-description", answer, promise);
      gst_promise_unref(promise);
      gst_webrtc_session_description_free(answer);

      RCLCPP_INFO(get_logger(), "Answer received and applied for %s", peer_id.c_str());
    } else if (type == "candidate") {
      std::unique_lock lock(mtx_sessions_);
      if (sessions_.find(peer_id) == sessions_.end())
        return;

      GstElement *webrtc        = sessions_[peer_id]->webrtc;
      std::string candidate_str = msg["candidate"].get<std::string>();
      int         sdp_mid_index = std::stoi(msg["sdpMLineIndex"].get<std::string>()); // GStreamer needs the index

      // Ignore mDNS candidates to prevent resolution errors in GStreamer
      if (candidate_str.find(".local") != std::string::npos) {
        RCLCPP_DEBUG(get_logger(), "Ignoring mDNS candidate: %s", candidate_str.c_str());
        return;
      }

      g_signal_emit_by_name(webrtc, "add-ice-candidate", sdp_mid_index, candidate_str.c_str());
    } else if (type == "offer") {
      // Stream providers are the ones that create offers, so we don't expect to receive this type. Just log it.
      RCLCPP_WARN(get_logger(), "Received unexpected 'offer' message from %s", peer_id.c_str());
    } else {
      RCLCPP_WARN(get_logger(), "Unknown signaling message type: %s", type.c_str());
    }
  }
  catch (const std::exception &e) {
    RCLCPP_WARN(get_logger(), "Ignoring invalid signaling message: %s. Error: %s", payload.c_str(), e.what());
  }
}

void WebRTCStreamer::createPeerSession_(const std::string &peer_id, const std::vector<std::string> &requested_streams) {
  std::unique_lock lock(mtx_sessions_);

  if (sessions_.find(peer_id) != sessions_.end()) {
    RCLCPP_WARN(get_logger(), "Peer session already exists for %s, replacing...", peer_id.c_str());
    sessions_.erase(peer_id);
  }

  auto session     = std::make_shared<PeerSession>();
  session->peer_id = peer_id;

  // Add the webrtcbin element with the public STUN server for ICE candidates
  std::string pipeline_desc =
      "webrtcbin name=webrtc bundle-policy=max-bundle stun-server=stun://stun.l.google.com:19302 ";

  // Create tracks for requested streams
  auto streams = requested_streams.empty() ? getAvailableImageTopics_() : requested_streams;
  for (const auto &stream : streams) {
    if (!existsImageTopic_(stream)) {
      RCLCPP_WARN(get_logger(), "[%s] Requested stream '%s' does not exist or is not an Image topic", peer_id.c_str(),
                  stream.c_str());
      continue;
    }

    RCLCPP_INFO(get_logger(), "[%s] Adding track for stream '%s'", peer_id.c_str(), stream.c_str());
    std::string src_name = "src_" + stream;

    pipeline_desc += "appsrc name=" + src_name +
                     " format=time is-live=true do-timestamp=true ! "
                     "videoconvert ! "
                     "video/x-raw,format=I420 ! "
                     "av1enc target-bitrate=1000   usage-profile=realtime end-usage=cbr ! "
                     "av1parse ! "
                     "rtpav1pay pt=96 ! "
                     "application/x-rtp,media=video,encoding-name=AV1,payload=96,clock-rate=90000 ! webrtc. ";
  }

  GError *err       = nullptr;
  session->pipeline = gst_parse_launch(pipeline_desc.c_str(), &err);
  if (err) {
    RCLCPP_ERROR(get_logger(), "Failed to create GStreamer pipeline for session '%s': %s", peer_id.c_str(),
                 err->message);
    g_error_free(err);
    return;
  }

  session->webrtc = gst_bin_get_by_name(GST_BIN(session->pipeline), "webrtc");

  // Pass session context to callbacks using a struct or directly if you maintain a map
  // To avoid complex memory management in this example, we pass the WebRTCStreamer instance
  // and use webrtcbin's element name or map lookup to find the peer_id in the callback.
  g_object_set_data(G_OBJECT(session->webrtc), "peer_id", (gpointer)strdup(peer_id.c_str()));
  g_object_set_data(G_OBJECT(session->webrtc), "streamer", this);

  // 2. Connect WebRTC signaling callbacks
  g_signal_connect(session->webrtc, "on-negotiation-needed", G_CALLBACK(onNegotiationNeeded_), this);
  g_signal_connect(session->webrtc, "on-ice-candidate", G_CALLBACK(onICECandidate_), this);

  // 3. Connect AppSrcs to ROS Subscriptions
  for (const auto &stream : streams) {
    if (!existsImageTopic_(stream))
      continue;

    std::string src_name = "src_" + stream;

    TrackInfo track_info;
    track_info.topic_name = stream;
    track_info.appsrc     = gst_bin_get_by_name(GST_BIN(session->pipeline), src_name.c_str());

    track_info.sub = this->create_subscription<sensor_msgs::msg::Image>(
        stream, rclcpp::SensorDataQoS(), [this, peer_id, stream](const sensor_msgs::msg::Image::SharedPtr msg) {
          this->imageCallback(msg, peer_id, stream);
        });

    session->tracks[stream] = track_info;
  }

  sessions_[peer_id] = session;
  gst_element_set_state(session->pipeline, GST_STATE_PLAYING);
}

void WebRTCStreamer::onNegotiationNeeded_(GstElement *webrtc, [[maybe_unused]] gpointer user_data) {
  GstPromise *promise = gst_promise_new_with_change_func(onOfferCreated_, webrtc, nullptr);
  g_signal_emit_by_name(webrtc, "create-offer", nullptr, promise);
}

void WebRTCStreamer::onOfferCreated_(GstPromise *promise, gpointer user_data) {
  GstElement *webrtc   = GST_ELEMENT(user_data);
  auto        streamer = static_cast<WebRTCStreamer *>(g_object_get_data(G_OBJECT(webrtc), "streamer"));
  char       *peer_id  = (char *)g_object_get_data(G_OBJECT(webrtc), "peer_id");

  const GstStructure          *reply = gst_promise_get_reply(promise);
  GstWebRTCSessionDescription *offer = nullptr;
  gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr);
  gst_promise_unref(promise);

  // Set local description
  GstPromise *local_desc_promise = gst_promise_new();
  g_signal_emit_by_name(webrtc, "set-local-description", offer, local_desc_promise);
  gst_promise_unref(local_desc_promise);

  // Send offer to signaling server
  gchar *sdp_text = gst_sdp_message_as_text(offer->sdp);
  json   msg;
  msg["id"]   = std::string(peer_id);
  msg["type"] = "offer";
  msg["sdp"]  = std::string(sdp_text);

  streamer->sendSignaling(msg);

  g_free(sdp_text);
  gst_webrtc_session_description_free(offer);
}

void WebRTCStreamer::onICECandidate_(GstElement *webrtc, guint mline_index, gchar *candidate, gpointer user_data) {
  auto  streamer = static_cast<WebRTCStreamer *>(user_data);
  char *peer_id  = (char *)g_object_get_data(G_OBJECT(webrtc), "peer_id");

  json msg;
  msg["id"]            = std::string(peer_id);
  msg["type"]          = "candidate";
  msg["candidate"]     = std::string(candidate);
  msg["sdpMid"]        = "";
  msg["sdpMLineIndex"] = std::to_string(mline_index);

  streamer->sendSignaling(msg);
}

void WebRTCStreamer::imageCallback(const sensor_msgs::msg::Image::SharedPtr msg, const std::string &peer_id,
                                   const std::string &stream) {
  GstElement *target_appsrc = nullptr;
  {
    std::shared_lock lock(mtx_sessions_);
    const auto       it = sessions_.find(peer_id);

    if (it == sessions_.end() || //
        !it->second->tracks.count(stream))
      return;

    target_appsrc = it->second->tracks.at(stream).appsrc;
  }

  if (!target_appsrc)
    return;

  // Convert ROS Image message to raw buffer
  GstBuffer *buf = gst_buffer_new_allocate(nullptr, msg->data.size(), nullptr);
  GstMapInfo map;
  gst_buffer_map(buf, &map, GST_MAP_WRITE);
  memcpy(map.data, msg->data.data(), msg->data.size());
  gst_buffer_unmap(buf, &map);

  // Set caps based on image encoding
  std::map<std::string, std::string> encoding_map = {
    { "rgb8", "RGB" },         //
    { "bgr8", "BGR" },         //
    { "mono8", "GRAY8" },      //
    { "16UC1", "GRAY16_LE" },  //
    { "yuv422_yuy2", "YUY2" }, //
  };
  if (encoding_map.find(msg->encoding) == encoding_map.end()) {
    RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 5000, "Unsupported image encoding: %s", msg->encoding.c_str());
    gst_buffer_unref(buf);
    return;
  }

  GstCaps *caps = gst_caps_new_simple("video/x-raw", //
                                      "format", G_TYPE_STRING, encoding_map.at(msg->encoding).c_str(), "width",
                                      G_TYPE_INT, msg->width,               //
                                      "height", G_TYPE_INT, msg->height,    //
                                      "framerate", GST_TYPE_FRACTION, 0, 1, //
                                      nullptr);
  g_object_set(target_appsrc, "caps", caps, nullptr);
  gst_caps_unref(caps);

  // Push buffer to appsrc
  GstFlowReturn ret;
  g_signal_emit_by_name(target_appsrc, "push-buffer", buf, &ret);
  gst_buffer_unref(buf);
}

std::vector<std::string> WebRTCStreamer::getAvailableImageTopics_() {
  auto graph  = this->get_node_graph_interface();
  auto topics = graph->get_topic_names_and_types(true);

  std::string              image_message_type = "sensor_msgs/msg/Image";
  std::vector<std::string> result;

  for (const auto &t : topics) {
    if (std::find(t.second.begin(), t.second.end(), image_message_type) != t.second.end())
      result.push_back(t.first);
  }

  return result;
}

bool WebRTCStreamer::existsImageTopic_(const std::string &topic_name) {
  auto available_topics = getAvailableImageTopics_();
  return std::find(available_topics.begin(), available_topics.end(), topic_name) != available_topics.end();
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<WebRTCStreamer>());
  rclcpp::shutdown();
  return 0;
}
