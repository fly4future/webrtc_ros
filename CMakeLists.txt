cmake_minimum_required(VERSION 3.5)
project(webrtc_ros)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(async_web_server_cpp REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(std_msgs REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(webrtc REQUIRED)
find_package(X11 REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/IceServer.msg"
  "srv/GetIceServers.srv"
)

###########
## Build ##
###########

include_directories(
  include
  include/${PROJECT_NAME}
  ${OpenCV_INCLUDE_DIRS}
  ${webrtc_INCLUDE_DIRS}
  /usr/include/webrtc/3rdparty
)
add_definitions(${webrtc_DEFINITIONS})


add_executable(${PROJECT_NAME}_server_node
  src/webrtc_ros_server_node.cpp
  src/webrtc_web_server.cpp
  src/configure_message.cpp
  src/ice_candidate_message.cpp
  src/image_transport_factory.cpp
  src/ros_video_capturer.cpp
  src/ros_video_renderer.cpp
  src/sdp_message.cpp
  src/webrtc_client.cpp
  src/webrtc_ros_message.cpp
  src/webrtc_ros_server.cpp
  src/webrtc_ros_json_parser.cpp
)

target_include_directories(${PROJECT_NAME}_server_node
  PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(
  ${PROJECT_NAME}_server_node
  async_web_server_cpp
  cv_bridge
  image_transport
  rclcpp
  std_msgs
  ament_index_cpp
)

rosidl_get_typesupport_target(cpp_typesupport_target
  ${PROJECT_NAME} rosidl_typesupport_cpp)

target_link_libraries(${PROJECT_NAME}_server_node
  ${webrtc_LIBRARIES}
  webrtc
  jsoncpp_lib
  ${X11_LIBRARIES}
  ${cpp_typesupport_target}
)

set_target_properties(${PROJECT_NAME}_server_node PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

#############
## Install ##
#############
install(TARGETS
  ${PROJECT_NAME}_server_node
  DESTINATION lib/${PROJECT_NAME})

# Install web files
install(DIRECTORY web
  DESTINATION share/${PROJECT_NAME}
  PATTERN "Gruntfile.js" EXCLUDE
  PATTERN "node_modules" EXCLUDE
  PATTERN "package.json" EXCLUDE
  PATTERN ".gitignore" EXCLUDE
)

# Install launch files
install(DIRECTORY
  launch
  DESTINATION share/${PROJECT_NAME}
)

# Install license files
install(FILES
  LICENSE
  DESTINATION share/${PROJECT_NAME}/
)
install(FILES
  LICENSE_webrtc_adapter
  DESTINATION share/${PROJECT_NAME}/
)

install(DIRECTORY scripts
  DESTINATION share/${PROJECT_NAME}/scripts
)


ament_package()
