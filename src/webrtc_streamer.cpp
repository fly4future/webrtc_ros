#include "webrtc_ros/webrtc_streamer.hpp"
#include <memory>
#include <mutex>
#include <rclcpp/logging.hpp>

WebRTCStreamer::WebRTCStreamer()
    : Node("webrtc_streamer") {
  RCLCPP_INFO(get_logger(), "Starting WebRTC Streamer...");

  // Initialize GStreamer once globally
  gst_init(nullptr, nullptr);

  // Setup WebSocket signaling
  this->declare_parameter("signaling_url", "ws://localhost:8000/uav999");
  setupSignaling_();
  connectSignaling_();
}

void WebRTCStreamer::connectSignaling_() {
  std::string url = this->get_parameter("signaling_url").as_string();
  RCLCPP_INFO(get_logger(), "Connecting to signaling server at %s", url.c_str());
  if (signaling_ws_client_.isOpen()) {
    signaling_ws_client_.close();
  }
  signaling_ws_client_.open(url);
}

void WebRTCStreamer::setupSignaling_() {
  signaling_ws_client_.onOpen([this]() {
    RCLCPP_INFO(get_logger(), "Connected to signaling server");
    if (reconnect_timer_)
      reconnect_timer_->cancel();
  });

  signaling_ws_client_.onMessage([this](std::variant<rtc::binary, rtc::string> message) {
    if (std::holds_alternative<rtc::string>(message)) {
      const std::string &msg_str = std::get<rtc::string>(message);
      RCLCPP_INFO(get_logger(), "Received signaling message: %s", msg_str.c_str());
      handleSignalingMessage_(msg_str);
    } else {
      RCLCPP_WARN(get_logger(), "Received unsupported binary message");
    }
  });

  signaling_ws_client_.onClosed([this]() {
    RCLCPP_WARN(get_logger(), "Disconnected from signaling server. Trying to reconnect...");

    if (reconnect_timer_) {
      reconnect_timer_->reset();
      return;
    }

    reconnect_timer_ = this->create_wall_timer(std::chrono::seconds(3), [this]() {
      if (!signaling_ws_client_.isOpen())
        connectSignaling_();
    });
  });

  signaling_ws_client_.onError(
      [this](const std::string &error) { RCLCPP_ERROR(get_logger(), "Signaling server error: %s", error.c_str()); });
}

void WebRTCStreamer::sendSignaling_(const json &msg) {
  std::string msg_str = msg.dump();
  signaling_ws_client_.send(msg_str);
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
      if (!msg.contains("streams") || !msg["streams"].is_array()) {
        RCLCPP_WARN(get_logger(), "Ignoring request from %s: missing 'streams' field", peer_id.c_str());

        json err;
        err["id"]      = peer_id;
        err["type"]    = "error";
        err["message"] = "Missing 'streams' field in request";

        sendSignaling_(err);
        return;
      }

      std::vector<std::string> requested;
      for (auto &s : msg["streams"])
        requested.push_back(s.get<std::string>());
      RCLCPP_DEBUG(get_logger(), "[%s] Requested streams: %s", peer_id.c_str(), msg["streams"].dump().c_str());
      createPeerSession_(peer_id, requested);
    } else if (type == "answer") {
      std::unique_lock lock(mtx_sessions_);

      auto it = sessions_.find(peer_id);
      if (it == sessions_.end()) {
        RCLCPP_WARN(get_logger(), "Answer from unknown peer: %s", peer_id.c_str());
        return;
      }

      rtc::Description answer(msg["sdp"].get<std::string>(), type);
      it->second->pc->setRemoteDescription(answer);
      RCLCPP_INFO(get_logger(), "Answer received from %s", peer_id.c_str());
    } else if (type == "candidate") {
      std::unique_lock lock(mtx_sessions_);

      auto it = sessions_.find(peer_id);
      if (it == sessions_.end())
        return;

      rtc::Candidate candidate(msg["candidate"].get<std::string>(), msg["sdpMid"].get<std::string>());
      it->second->pc->addRemoteCandidate(candidate);
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
  session->ssrc    = static_cast<rtc::SSRC>(std::hash<std::string>{}(peer_id) & 0xFFFFFFFF);

  rtc::Configuration config;
  // config.iceServers.emplace_back("stun:stun.l.google.com:19302");
  session->pc = std::make_shared<rtc::PeerConnection>(config);

  // PeerConnection callbacks
  session->pc->onStateChange([this, peer_id](rtc::PeerConnection::State state) {
    RCLCPP_INFO(get_logger(), "[%s] PeerConnection state: %i", peer_id.c_str(), static_cast<int>(state));

    if (state == rtc::PeerConnection::State::Disconnected || //
        state == rtc::PeerConnection::State::Failed ||       //
        state == rtc::PeerConnection::State::Closed) {
      RCLCPP_INFO(get_logger(), "[%s] Peer disconnected, removing session", peer_id.c_str());
      std::unique_lock lock(mtx_sessions_);
      sessions_.erase(peer_id);
    }
  });

  session->pc->onGatheringStateChange([this, peer_id](rtc::PeerConnection::GatheringState state) {
    if (state == rtc::PeerConnection::GatheringState::Complete) {
      std::shared_ptr<PeerSession> s;
      {
        std::unique_lock lock(mtx_sessions_);

        auto it = sessions_.find(peer_id);
        if (it == sessions_.end())
          return;
        s = it->second;
      }
      auto desc = s->pc->localDescription();

      json msg;
      msg["id"]   = peer_id;
      msg["type"] = desc->typeString();
      msg["sdp"]  = std::string(desc.value());

      sendSignaling_(msg);
      RCLCPP_INFO(get_logger(), "[%s] Offer sent", peer_id.c_str());
    }
  });

  // Create tracks for requested streams
  for (const auto &stream : requested_streams) {
    if (!existsImageTopic_(stream)) {
      RCLCPP_WARN(get_logger(), "[%s] Requested stream '%s' does not exist or is not an Image topic", peer_id.c_str(),
                  stream.c_str());
      continue;
    }

    RCLCPP_INFO(get_logger(), "[%s] Adding track for stream '%s'", peer_id.c_str(), stream.c_str());

    rtc::Description::Video mediaDescription("video", rtc::Description::Direction::SendOnly);
    mediaDescription.addH264Codec(96);

    uint32_t track_ssrc = static_cast<uint32_t>(session->ssrc + std::hash<std::string>{}(stream));
    mediaDescription.addSSRC(track_ssrc, peer_id, "stream-" + peer_id + "-" + stream, stream);

    TrackInfo &track_info = session->tracks[stream];
    track_info.track      = session->pc->addTrack(mediaDescription);
    track_info.topic_name = stream;
    track_info.is_ready   = false;

    // Setup GStreamer pipeline
    // 1. appsrc to push raw frames from ROS
    // 2. videoconvert to ensure format compatibility
    // 3. x264enc to encode to H264 (WebRTC compatible)
    // 4. rtph264pay to packetize for RTP streaming
    // 5. appsink to pull RTP packets and send via WebRTC track
    std::string desc = "appsrc name=src format=time is-live=true block=false do-timestamp=true ! "
                       "videoconvert ! "
                       "x264enc tune=zerolatency bitrate=1000 speed-preset=ultrafast ! "
                       "video/x-h264,profile=constrained-baseline ! "
                       "rtph264pay config-interval=1 pt=96 ssrc=" +
                       std::to_string(track_ssrc) +
                       " ! "
                       "appsink name=sink sync=false emit-signals=true";

    GError *err         = nullptr;
    track_info.pipeline = gst_parse_launch(desc.c_str(), &err);
    if (err) {
      RCLCPP_ERROR(get_logger(), "Failed to create GStreamer pipeline for track '%s': %s", stream.c_str(),
                   err->message);
      g_error_free(err);
      continue;
    }

    track_info.appsrc  = gst_bin_get_by_name(GST_BIN(track_info.pipeline), "src");
    track_info.appsink = gst_bin_get_by_name(GST_BIN(track_info.pipeline), "sink");

    g_signal_connect(track_info.appsink, "new-sample", G_CALLBACK(onNewRTPSample_), &track_info);
    gst_element_set_state(track_info.pipeline, GST_STATE_PLAYING);

    track_info.sub = this->create_subscription<sensor_msgs::msg::Image>(
        stream, rclcpp::SensorDataQoS(), [this, peer_id, stream](const sensor_msgs::msg::Image::SharedPtr msg) {
          this->imageCallback(msg, peer_id, stream);
        });

    // Track callbacks
    track_info.track->onOpen([this, peer_id, stream]() {
      std::unique_lock lock(mtx_sessions_);

      auto it = sessions_.find(peer_id);
      if (it != sessions_.end()) {
        it->second->tracks[stream].is_ready = true;
        RCLCPP_INFO(get_logger(), "[%s] Track for stream '%s' is open", peer_id.c_str(), stream.c_str());
      }
    });

    track_info.track->onClosed([this, peer_id, stream]() {
      RCLCPP_WARN(get_logger(), "[%s] Track for stream '%s' closed", peer_id.c_str(), stream.c_str());
      std::unique_lock lock(mtx_sessions_);

      auto it = sessions_.find(peer_id);
      if (it == sessions_.end())
        return;

      // Clean up pipeline when track closes
      if (it->second->tracks.count(stream)) {
        gst_element_set_state(it->second->tracks[stream].pipeline, GST_STATE_NULL);
        gst_object_unref(it->second->tracks[stream].pipeline);
      }
      it->second->tracks.erase(stream);
      if (it->second->tracks.empty()) {
        RCLCPP_INFO(get_logger(), "[%s] No more tracks, closing session", peer_id.c_str());
        sessions_.erase(peer_id);
      }
    });
  }

  // Append session and send offer to trigger onGatheringStateChange
  RCLCPP_INFO(get_logger(), "[%s] Peer session created, generating offer...", peer_id.c_str());
  sessions_[peer_id] = session;
  session->pc->setLocalDescription();
};

GstFlowReturn WebRTCStreamer::onNewRTPSample_(GstElement *sink, TrackInfo *track_info) {
  GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
  if (!sample)
    return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer(sample);
  GstMapInfo map;
  if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    try {
      track_info->track->send(reinterpret_cast<const std::byte *>(map.data), map.size);
    }
    catch (const std::exception &e) {
      RCLCPP_WARN(rclcpp::get_logger("WebRTCStreamer"), "Failed to send RTP packet for track '%s': %s",
                  track_info->topic_name.c_str(), e.what());
    }
    gst_buffer_unmap(buffer, &map);
  }
  gst_sample_unref(sample);
  return GST_FLOW_OK;
}

void WebRTCStreamer::imageCallback(const sensor_msgs::msg::Image::SharedPtr msg, const std::string &peer_id,
                                   const std::string &stream) {
  GstElement *target_appsrc = nullptr;
  {
    std::shared_lock lock(mtx_sessions_);
    const auto       it = sessions_.find(peer_id);

    if (it == sessions_.end() ||             //
        !it->second->tracks.count(stream) || //
        !it->second->tracks.at(stream).is_ready)
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

  GST_BUFFER_PTS(buf)      = rclcpp::Time(msg->header.stamp).nanoseconds();
  GST_BUFFER_DURATION(buf) = GST_SECOND / 30;

  // Set caps based on image encoding
  std::map<std::string, std::string> encoding_map = {
    { "rgb8", "RGB" },
    { "bgr8", "BGR" },
    { "mono8", "GRAY8" },
    { "yuv422_yuy2", "YUY2" },
  };
  if (encoding_map.find(msg->encoding) == encoding_map.end()) {
    RCLCPP_ERROR(get_logger(), "Unsupported image encoding: %s", msg->encoding.c_str());
    gst_buffer_unref(buf);
    return;
  }

  GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, encoding_map.at(msg->encoding).c_str(),
                                      "width", G_TYPE_INT, msg->width, "height", G_TYPE_INT, msg->height, "framerate",
                                      GST_TYPE_FRACTION, 30, 1, nullptr);
  g_object_set(target_appsrc, "caps", caps, nullptr);
  gst_caps_unref(caps);

  // Push buffer to appsrc
  GstFlowReturn ret;
  g_signal_emit_by_name(target_appsrc, "push-buffer", buf, &ret);
  gst_buffer_unref(buf);
}

bool WebRTCStreamer::existsImageTopic_(const std::string &topic_name) {
  auto graph  = this->get_node_graph_interface();
  auto topics = graph->get_topic_names_and_types(true);

  std::string image_message_type = "sensor_msgs/msg/Image";

  return std::any_of(topics.begin(), topics.end(), [&](const auto &t) {
    return t.first == topic_name && (std::find(t.second.begin(), t.second.end(), image_message_type) != t.second.end());
  });
}

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<WebRTCStreamer>());
  rclcpp::shutdown();
  return 0;
}
