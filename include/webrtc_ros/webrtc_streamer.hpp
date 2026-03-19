#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include <rtc/rtc.hpp>

#include <nlohmann/json.hpp>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

using json = nlohmann::json;

struct TrackInfo
{
  std::shared_ptr<rtc::Track> track;
  bool                        is_ready = false;

  std::string                                              topic_name;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr sub;

  GstElement *pipeline = nullptr;
  GstElement *appsrc   = nullptr;
  GstElement *appsink  = nullptr;
};

struct PeerSession
{
  std::string                          peer_id;
  rtc::SSRC                            ssrc;
  std::shared_ptr<rtc::PeerConnection> pc;

  // Streams
  std::map<std::string, TrackInfo> tracks;
};

class WebRTCStreamer : public rclcpp::Node {
 public:
  WebRTCStreamer();

 private:
  // Signaling server (WebSocket)
  rtc::WebSocket               signaling_ws_client_;
  rclcpp::TimerBase::SharedPtr reconnect_timer_;

  void connectSignaling_();
  void setupSignaling_();
  void sendSignaling_(const json &msg);
  void handleSignalingMessage_(const std::string &msg);

  // WebRTC session management
  std::map<std::string, std::shared_ptr<PeerSession>> sessions_;
  std::shared_mutex                                   mtx_sessions_;

  void createPeerSession_(const std::string &peer_id, const std::vector<std::string> &requested_streams);

  // Image streaming
  void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg, const std::string &peer_id,
                     const std::string &stream);

  static GstFlowReturn onNewRTPSample_(GstElement *sink, TrackInfo *track_info);

  // Utility
  bool existsImageTopic_(const std::string &topic_name);
};
