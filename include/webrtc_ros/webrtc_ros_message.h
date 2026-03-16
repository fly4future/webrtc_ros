#pragma once

#include <json/json.h>
#include <webrtc_ros/webrtc_ros_json_parser.h>
#include <string>

namespace webrtc_ros
{

class WebrtcRosMessage {
public:
  static std::string kMessageTypeFieldName;

  static bool isType(const Json::Value &message_json, const std::string &type);
  static bool getType(const Json::Value &message_json, std::string *type);
};

} // namespace webrtc_ros