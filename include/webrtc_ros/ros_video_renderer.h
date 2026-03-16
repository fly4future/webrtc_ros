#pragma once

#include <webrtc/api/media_stream_interface.h>
#include <image_transport/image_transport.hpp>

namespace webrtc_ros
{

class RosVideoRenderer : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  RosVideoRenderer(std::shared_ptr<image_transport::ImageTransport> it, const std::string &topic);
  virtual void OnFrame(const webrtc::VideoFrame &frame) override;

 private:
  // Non-copyable
  RosVideoRenderer(const RosVideoRenderer &)            = delete;
  RosVideoRenderer &operator=(const RosVideoRenderer &) = delete;

  std::shared_ptr<image_transport::ImageTransport> it_;
  const std::string                                topic_;
  image_transport::Publisher                       pub_;
};

} // namespace webrtc_ros
