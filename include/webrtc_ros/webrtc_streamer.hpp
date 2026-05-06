#pragma once

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <nlohmann/json.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <gst/gst.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/webrtc/rtcsessiondescription.h>

using json     = nlohmann::json;
using WsClient = websocketpp::client<websocketpp::config::asio_client>;

struct TrackInfo
{
  std::string topic_name;
  GstElement *appsrc = nullptr;

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub;
};

struct PeerSession
{
  std::string peer_id;
  GstElement *pipeline;
  GstElement *webrtc;

  // Streams
  std::map<std::string, TrackInfo> tracks;
  std::vector<std::string>         stream_labels_ordered;
  std::vector<std::string>         requested_streams;

  ~PeerSession() {
    if (pipeline) {
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(pipeline);
    }
  }
};

class WebRTCStreamer : public rclcpp::Node {
 public:
  WebRTCStreamer();
  ~WebRTCStreamer();

 private:
  // Signaling server (WebSocket)
  WsClient                    signaling_ws_client_;
  websocketpp::connection_hdl ws_hdl_;
  std::thread                 ws_thread_;

  rclcpp::TimerBase::SharedPtr reconnect_timer_;

  void setupSignaling_();
  void connectSignaling_();
  void scheduleReconnect_();
  void handleSignalingMessage_(const std::string &msg);

 public:
  void sendSignaling(const json &msg);

 private:
  // WebRTC session management
  std::map<std::string, std::shared_ptr<PeerSession>> sessions_;
  std::shared_mutex                                   mtx_sessions_;

  void createPeerSession_(const std::string &peer_id, const std::vector<std::string> &requested_streams);

  // Image streaming
  void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg, const std::string &peer_id,
                     const std::string &stream);

  static void onNegotiationNeeded_(GstElement *webrtc, gpointer user_data);
  static void onOfferCreated_(GstPromise *promise, gpointer user_data);
  static void onICECandidate_(GstElement *webrtc, guint mline_index, gchar *candidate, gpointer user_data);

  // Utility
  bool                     existsImageTopic_(const std::string &topic_name);
  std::vector<std::string> getAvailableImageTopics_();
};
