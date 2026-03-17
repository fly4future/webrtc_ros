cmake_minimum_required(VERSION 3.5)
project(webrtc_ros)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compiler warnings
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -fno-omit-frame-pointer)
endif()

# Ros build tools
find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

set(ROS_DEPS
  # Base
  ament_index_cpp
  rclcpp
  rclcpp_components
  # Messages
  std_msgs
  sensor_msgs
  # Image processing
  cv_bridge
  image_transport
  # Web
  async_web_server_cpp
)

foreach(DEP IN LISTS ROS_DEPS)
  find_package(${DEP} REQUIRED)
endforeach()

set(EXTERNAL_DEPS
  webrtc
  OpenCV
  X11
  jsoncpp
)

foreach(DEP IN LISTS EXTERNAL_DEPS)
  find_package(${DEP} REQUIRED)
endforeach()

## | ----------------- Message and service generation ----------------- |

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/IceServer.msg"
  "srv/GetIceServers.srv"
)

## | ----------------- Build libray ----------------- |

# add_library(WebrtcRos_Server SHARED
add_executable(WebrtcRos_Server
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

# Link against the generated message type support libraries
rosidl_get_typesupport_target(cpp_typesupport_target
  ${PROJECT_NAME} rosidl_typesupport_cpp)
# target_link_libraries(WebrtcRos_Server PUBLIC "${cpp_typesupport_target}")

ament_target_dependencies(WebrtcRos_Server
  ${ROS_DEPS}
)

target_include_directories(WebrtcRos_Server PRIVATE
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
  # External
  ${OpenCV_INCLUDE_DIRS}
  /usr/include/jsoncpp
)

target_link_libraries(WebrtcRos_Server
  ${X11_LIBRARIES}
  X11::X11
  webrtc::webrtc
  jsoncpp_lib
  ${cpp_typesupport_target}
  # ${EXTERNAL_DEPS}
)

## | ----------------- Build executable ----------------- |

# add_executable(webrtc_ros_server_node
#   src/webrtc_ros_server_node.cpp
# )

# target_link_libraries(webrtc_ros_server_node PUBLIC
#   WebrtcRos_Server
# )

## --------------------------------------------------------------
## |                           install                          |
## --------------------------------------------------------------

install(
  TARGETS WebrtcRos_Server
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  DESTINATION lib/${PROJECT_NAME}
)

install(
  DIRECTORY include/${PROJECT_NAME}
  DESTINATION include/${PROJECT_NAME}
)

install(
  DIRECTORY launch scripts web
  DESTINATION share/${PROJECT_NAME}
)

ament_export_targets(
  export_${PROJECT_NAME} HAS_LIBRARY_TARGET
)
ament_export_include_directories(include)

ament_package()
