#pragma once

#include <rclcpp/rclcpp.hpp>
#include <image_transport/image_transport.hpp>

#include <webrtc_ros/ros_video_renderer.h>
#include <api/media_stream_interface.h>
#include <api/peer_connection_interface.h>
#include <api/audio_options.h>
#include <pc/peer_connection_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>

#include <media/engine/internal_decoder_factory.h>
#include <media/engine/internal_encoder_factory.h>

#include <media/base/adapted_video_track_source.h>

#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/candidate.h>
#include <webrtc_ros/configure_message.h>
#include <webrtc_ros/webrtc_web_server.h>
#include <webrtc_ros/image_transport_factory.h>
#include <rtc_base/thread.h>

namespace webrtc_ros
{

class WebrtcClient;
typedef std::shared_ptr<WebrtcClient> WebrtcClientPtr;
typedef std::weak_ptr<WebrtcClient>   WebrtcClientWeakPtr;

class WebrtcClientObserverProxy : public webrtc::PeerConnectionObserver, public webrtc::CreateSessionDescriptionObserver {
 public:
  WebrtcClientObserverProxy(WebrtcClientWeakPtr client_weak);

  void OnSuccess(webrtc::SessionDescriptionInterface *) override;
  void OnFailure(webrtc::RTCError error) override;
  void OnAddStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>) override;
  void OnRemoveStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>) override;
  void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override;
  void OnRenegotiationNeeded() override;
  void OnIceCandidate(const webrtc::IceCandidateInterface *) override;
  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override;
  void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState) override;
  void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override;

 private:
  WebrtcClientWeakPtr client_weak_;
};

class MessageHandlerImpl;
class WebrtcClient {
 public:
  WebrtcClient(rclcpp::Node::SharedPtr nh, const ImageTransportFactory &itf, const std::string &transport, SignalingChannel *signaling_channel);
  ~WebrtcClient();
  MessageHandler *createMessageHandler();

  void init(std::shared_ptr<WebrtcClient> &keep_alive_ptr);
  void invalidate();
  bool valid();

 private:
  WebrtcClientPtr keep_alive_this_;

  bool initPeerConnection();

  void ping_timer_callback();

  void handle_message(MessageHandler::Type type, const std::string &message);

  void OnSessionDescriptionSuccess(webrtc::SessionDescriptionInterface *);
  void OnSessionDescriptionFailure(const std::string &);
  void OnAddRemoteStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>);
  void OnRemoveRemoteStream(webrtc::scoped_refptr<webrtc::MediaStreamInterface>);
  void OnIceCandidate(const webrtc::IceCandidateInterface *);

  rclcpp::Node::SharedPtr                          nh_;
  std::shared_ptr<image_transport::ImageTransport> it_;
  ImageTransportFactory                            itf_;
  std::string                                      transport_;
  std::unique_ptr<SignalingChannel>                signaling_channel_;

  webrtc::Thread                 *signaling_thread_;
  std::unique_ptr<webrtc::Thread> worker_thread_;

  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>         peer_connection_factory_;
  std::map<std::string, std::vector<std::shared_ptr<RosVideoRenderer>>> video_renderers_;
  webrtc::scoped_refptr<WebrtcClientObserverProxy>                      webrtc_observer_proxy_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface>                peer_connection_;

  std::map<std::string, std::map<std::string, std::string>> expected_streams_;

  rclcpp::TimerBase::SharedPtr ping_timer_;

  friend WebrtcClientObserverProxy;
  friend MessageHandlerImpl;
};

} // namespace webrtc_ros
