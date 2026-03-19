#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "rtc/rtc.hpp"

#include <nlohmann/json.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

#include <map>
#include <mutex>
#include <atomic>
#include <thread>

using json     = nlohmann::json;
using WsClient = websocketpp::client<websocketpp::config::asio_client>;

const std::string LOCAL_ID      = "ros-streamer";
const std::string SIGNALING_URL = "ws://localhost:8000/";

// -------------------------------------------------------
// Una sesión WebRTC por cada browser conectado
// -------------------------------------------------------
struct PeerSession
{
  std::string                          peer_id;
  std::shared_ptr<rtc::PeerConnection> pc;
  std::shared_ptr<rtc::Track>          track;
  rtc::SSRC                            ssrc;
  std::atomic<bool>                    ready{false}; // true cuando el track está abierto
};

class WebRTCImageStreamer : public rclcpp::Node {
 public:
  WebRTCImageStreamer()
      : Node("webrtc_image_streamer") {
    gst_init(nullptr, nullptr);
    setup_gstreamer_pipeline();
    connect_signaling();

    sub_ = create_subscription<sensor_msgs::msg::Image>(
        "/camera/image_raw", 10, std::bind(&WebRTCImageStreamer::imageCallback, this, std::placeholders::_1));

    RCLCPP_INFO(get_logger(), "Listo. Esperando peers en %s%s", SIGNALING_URL.c_str(), LOCAL_ID.c_str());
  }

  ~WebRTCImageStreamer() {
    if (ws_thread_.joinable())
      ws_thread_.join();
    if (pipeline_)
      gst_element_set_state(pipeline_, GST_STATE_NULL);
  }

 private:
  // -------------------------------------------------------
  // Crear sesión WebRTC para un nuevo peer
  // -------------------------------------------------------
  void create_peer_session(const std::string &peer_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    if (sessions_.count(peer_id)) {
      RCLCPP_WARN(get_logger(), "Sesión para '%s' ya existe, reemplazando", peer_id.c_str());
      sessions_.erase(peer_id);
    }

    auto session     = std::make_shared<PeerSession>();
    session->peer_id = peer_id;
    session->ssrc    = static_cast<rtc::SSRC>(std::hash<std::string>{}(peer_id) & 0xFFFFFFFF);

    rtc::Configuration config;
    // config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    session->pc = std::make_shared<rtc::PeerConnection>(config);

    // Track H.264 SendOnly
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(session->ssrc, "ros-cam-" + peer_id);
    session->track = session->pc->addTrack(media);

    // Callbacks de estado
    session->pc->onStateChange([this, peer_id](rtc::PeerConnection::State state) {
      RCLCPP_INFO(get_logger(), "[%s] State: %s", peer_id.c_str(), rtc::PeerConnection::stateToString(state).c_str());

      if (state == rtc::PeerConnection::State::Disconnected || state == rtc::PeerConnection::State::Failed ||
          state == rtc::PeerConnection::State::Closed) {
        remove_peer_session(peer_id);
      }
    });

    session->pc->onGatheringStateChange([this, peer_id](rtc::PeerConnection::GatheringState state) {
      if (state == rtc::PeerConnection::GatheringState::Complete) {
        std::shared_ptr<PeerSession> s;
        {
          std::lock_guard<std::mutex> lock(sessions_mutex_);
          auto                        it = sessions_.find(peer_id);
          if (it == sessions_.end())
            return;
          s = it->second;
        }
        auto desc = s->pc->localDescription();
        json msg  = {{"id", peer_id}, {"type", desc->typeString()}, {"sdp", std::string(desc.value())}};
        send_signaling(msg.dump());
        RCLCPP_INFO(get_logger(), "[%s] Offer enviado", peer_id.c_str());
      }
    });

    session->track->onOpen([this, peer_id]() {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      auto                        it = sessions_.find(peer_id);
      if (it != sessions_.end()) {
        it->second->ready = true;
        RCLCPP_INFO(get_logger(), "[%s] Track abierto ✓", peer_id.c_str());
      }
    });

    session->track->onClosed([this, peer_id]() {
      RCLCPP_WARN(get_logger(), "[%s] Track cerrado", peer_id.c_str());
      remove_peer_session(peer_id);
    });

    sessions_[peer_id] = session;

    // Iniciar negociación: genera offer → dispara onGatheringStateChange
    session->pc->setLocalDescription();
    RCLCPP_INFO(get_logger(), "[%s] Sesión creada, generando offer...", peer_id.c_str());
  }

  void remove_peer_session(const std::string &peer_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_.erase(peer_id);
    RCLCPP_INFO(get_logger(), "[%s] Sesión eliminada (%zu activas)", peer_id.c_str(), sessions_.size());
  }

  // -------------------------------------------------------
  // GStreamer: appsrc → encode H.264 → rtph264pay → appsink
  // El appsink callback distribuye el RTP a TODOS los tracks activos
  // -------------------------------------------------------
  void setup_gstreamer_pipeline() {
    // Ajusta width/height/framerate al topic real
    std::string desc = "appsrc name=src format=time is-live=true block=false "
                       "caps=video/x-raw,format=BGR,width=640,height=480,framerate=30/1 ! "
                       "videoconvert ! "
                       "x264enc tune=zerolatency bitrate=1000 speed-preset=ultrafast ! "
                       "rtph264pay config-interval=1 pt=96 ! "
                       "appsink name=sink sync=false emit-signals=true";

    GError *err = nullptr;
    pipeline_   = gst_parse_launch(desc.c_str(), &err);
    if (err) {
      RCLCPP_ERROR(get_logger(), "GStreamer error: %s", err->message);
      g_error_free(err);
      return;
    }

    appsrc_  = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
    appsink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");

    // Callback: un RTP packet listo → broadcast a todos los peers
    g_signal_connect(appsink_, "new-sample", G_CALLBACK(on_new_rtp_sample), this);

    gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  }

  static GstFlowReturn on_new_rtp_sample(GstElement *sink, WebRTCImageStreamer *self) {
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample)
      return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
      // Broadcast a todos los peers con track abierto
      std::lock_guard<std::mutex> lock(self->sessions_mutex_);
      for (auto &[id, session] : self->sessions_) {
        if (session->ready && session->track && session->track->isOpen()) {
          try {
            session->track->send(reinterpret_cast<const std::byte *>(map.data), map.size);
          }
          catch (const std::exception &e) {
            // No bloquear el loop si un peer falla
          }
        }
      }
      gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
  }

  // -------------------------------------------------------
  // Callback ROS: push raw frame → GStreamer appsrc
  // -------------------------------------------------------
  void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg) {
    if (!appsrc_)
      return;

    // Solo encodear si hay al menos un peer listo
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      bool                        any_ready = false;
      for (auto &[id, s] : sessions_) {
        if (s->ready) {
          any_ready = true;
          break;
        }
      }
      if (!any_ready)
        return; // no desperdiciar CPU encodando
    }

    GstBuffer *buf = gst_buffer_new_allocate(nullptr, msg->data.size(), nullptr);
    GstMapInfo map;
    gst_buffer_map(buf, &map, GST_MAP_WRITE);
    memcpy(map.data, msg->data.data(), msg->data.size());
    gst_buffer_unmap(buf, &map);

    GST_BUFFER_PTS(buf)      = rclcpp::Time(msg->header.stamp).nanoseconds();
    GST_BUFFER_DURATION(buf) = GST_SECOND / 30;

    GstFlowReturn ret;
    g_signal_emit_by_name(appsrc_, "push-buffer", buf, &ret);
    gst_buffer_unref(buf);
  }

  // -------------------------------------------------------
  // Signaling WebSocket
  // -------------------------------------------------------
  void connect_signaling() {
    ws_client_.init_asio();

    ws_client_.set_open_handler([this](websocketpp::connection_hdl hdl) {
      ws_hdl_ = hdl;
      RCLCPP_INFO(get_logger(), "Conectado al signaling server como '%s'", LOCAL_ID.c_str());
    });

    ws_client_.set_message_handler([this](websocketpp::connection_hdl, WsClient::message_ptr msg) {
      handle_signaling_message(msg->get_payload());
    });

    ws_client_.set_close_handler(
        [this](websocketpp::connection_hdl) { RCLCPP_WARN(get_logger(), "Desconectado del signaling server"); });

    websocketpp::lib::error_code ec;
    auto                         con = ws_client_.get_connection(SIGNALING_URL + LOCAL_ID, ec);
    ws_client_.connect(con);

    ws_thread_ = std::thread([this]() { ws_client_.run(); });
  }

  void send_signaling(const std::string &data) {
    ws_client_.send(ws_hdl_, data, websocketpp::frame::opcode::text);
  }

  void handle_signaling_message(const std::string &payload) {
    try {
      json        msg     = json::parse(payload);
      std::string type    = msg.value("type", "");
      std::string peer_id = msg.value("id", "");

      if (peer_id.empty())
        return;

      // ── "request": un nuevo browser pide stream ──
      if (type == "request") {
        RCLCPP_INFO(get_logger(), "[%s] Nuevo peer solicitando stream", peer_id.c_str());
        create_peer_session(peer_id); // ← aquí se crea dinámicamente

        // ── "answer": browser respondió al offer ──
      } else if (type == "answer") {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto                        it = sessions_.find(peer_id);
        if (it == sessions_.end()) {
          RCLCPP_WARN(get_logger(), "[%s] Answer de peer desconocido", peer_id.c_str());
          return;
        }
        rtc::Description answer(msg["sdp"].get<std::string>(), type);
        it->second->pc->setRemoteDescription(answer);
        RCLCPP_INFO(get_logger(), "[%s] Answer recibido", peer_id.c_str());

        // ── "candidate": ICE candidate del browser ──
      } else if (type == "candidate") {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto                        it = sessions_.find(peer_id);
        if (it == sessions_.end())
          return;
        rtc::Candidate candidate(msg["candidate"].get<std::string>(), msg["sdpMid"].get<std::string>());
        it->second->pc->addRemoteCandidate(candidate);
      }
    }
    catch (const std::exception &e) {
      RCLCPP_ERROR(get_logger(), "Error señalización: %s", e.what());
    }
  }

  // ---- Miembros ----
  std::map<std::string, std::shared_ptr<PeerSession>> sessions_;
  std::mutex                                          sessions_mutex_;

  GstElement *pipeline_ = nullptr;
  GstElement *appsrc_   = nullptr;
  GstElement *appsink_  = nullptr;

  WsClient                    ws_client_;
  websocketpp::connection_hdl ws_hdl_;
  std::thread                 ws_thread_;

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<WebRTCImageStreamer>());
  rclcpp::shutdown();
  return 0;
}
