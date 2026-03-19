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

set(ROS_DEPS
  # Base
  rclcpp
  # Messages
  sensor_msgs
  # Image processing
  # cv_bridge
  # image_transport
  # Web
  # async_web_server_cpp
)

foreach(DEP IN LISTS ROS_DEPS)
  find_package(${DEP} REQUIRED)
endforeach()


set(EXTERNAL_DEPS
  LibDataChannel
  nlohmann_json
)

foreach(DEP IN LISTS EXTERNAL_DEPS)
  find_package(${DEP} REQUIRED)
endforeach()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED
  gstreamer-1.0
  gstreamer-app-1.0
  gstreamer-video-1.0
  gstreamer-webrtc-1.0
  gstreamer-sdp-1.0
)

## | ----------------- Build executable ----------------- |

add_executable(webrtc_streamer
  src/webrtc_streamer.cpp
)

ament_target_dependencies(webrtc_streamer
  ${ROS_DEPS}
)

target_include_directories(webrtc_streamer PRIVATE
  include
  ${GST_INCLUDE_DIRS}
)

target_link_libraries(webrtc_streamer
  LibDataChannel::LibDataChannel
  nlohmann_json::nlohmann_json
  ${GST_LIBRARIES}
)

## --------------------------------------------------------------
## |                           install                          |
## --------------------------------------------------------------

install(
  TARGETS webrtc_streamer
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
