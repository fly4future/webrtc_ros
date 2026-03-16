#include "webrtc_ros/sdp_message.h"

namespace webrtc_ros
{

bool SdpMessage::isSdpOffer(const Json::Value &message_json) {
  return WebrtcRosMessage::isType(message_json, kSdpOfferType);
}
bool SdpMessage::isSdpAnswer(const Json::Value &message_json) {
  return WebrtcRosMessage::isType(message_json, kSdpAnswerType);
}

bool SdpMessage::fromJson(const Json::Value &message_json) {
  if (isSdpOffer(message_json) || isSdpAnswer(message_json)) {
    if (!WebrtcRosJsonParser::GetStringFromJsonObject(message_json, WebrtcRosMessage::kMessageTypeFieldName, &type))
      return false;
    if (!WebrtcRosJsonParser::GetStringFromJsonObject(message_json, kSdpFieldName, &sdp))
      return false;
    return true;
  } else
    return false;
}

bool SdpMessage::fromSessionDescription(const webrtc::SessionDescriptionInterface &description) {
  type = description.type();
  description.ToString(&sdp);
  return true;
}


webrtc::SessionDescriptionInterface *SdpMessage::createSessionDescription() {
  std::optional<webrtc::SdpType> sdp_type_opt = webrtc::SdpTypeFromString(type);
  if (!sdp_type_opt) {
    return nullptr;
  }
  return webrtc::CreateSessionDescription(*sdp_type_opt, sdp, nullptr).release();
}

std::string SdpMessage::toJson() {
  Json::FastWriter writer;
  Json::Value      message_json;
  message_json[WebrtcRosMessage::kMessageTypeFieldName] = type;
  message_json[kSdpFieldName]                           = sdp;
  return writer.write(message_json);
}

SdpMessage::SdpMessage() {
}

std::string SdpMessage::kSdpFieldName  = "sdp";
std::string SdpMessage::kSdpOfferType  = webrtc::SessionDescriptionInterface::kOffer;
std::string SdpMessage::kSdpAnswerType = webrtc::SessionDescriptionInterface::kAnswer;

} // namespace webrtc_ros
