<!--
MARKDOWN IMAGES & BADGES
* https://www.markdownguide.org/basic-syntax/#reference-style-links
* https://github.com/Ileriayo/markdown-badges

EMOJIS
* https://gist.github.com/rxaviers/7360908
-->

<div align="center" id="readme-top">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://github.com/user-attachments/assets/610ea197-7de0-46fc-a1a2-2c91cfe48397">
  <source media="(prefers-color-scheme: light)" srcset="https://github.com/user-attachments/assets/ece6ad5f-e44d-4196-805d-9daa7fa6fe29">
  <img alt="F4F Logo" src="https://github.com/user-attachments/assets/610ea197-7de0-46fc-a1a2-2c91cfe48397" width="80"/>
</picture>

# WebRTC ROS

Package to stream ROS video topics using WebRTC, with a custom signaling server and protocol.

</div>

## :pushpin:About The Project

![Demo](https://github.com/user-attachments/assets/cbe1fa32-3658-43bd-8e14-e161b81c4f14)

WebRTC is a powerful technology that enables real-time communication of audio, video, and data between web browsers and other clients. The `webrtc_ros` package provides a custom signaling server and protocol specifically designed for streaming ROS video topics using WebRTC. This allows ROS users to easily set up real-time video streaming from their robots to web browsers or other WebRTC-enabled clients, without needing to worry about the complexities of WebRTC signaling. The package is lightweight and flexible, making it a great choice for ROS users who want to leverage the power of WebRTC for their video streaming needs.

## :checkered_flag:Getting Started

This is an example of how you may give instructions on setting up your project locally.
To get a local copy up and running, follow these simple example steps.

### Installation

1. Clone and move it into your `ROS2` workspace

   ```sh
   cd ~/git # or the folder where you want to keep your git repositories (e.g., `~/Develop`)
   git clone git@github.com:fly4future/webrtc_ros.git

   cd ~/ros2_ws/src
   ln -sf ~/git/webrtc_ros .
   ```

   > [!NOTE]
   > Don't forget to have your SSH key added to your GitHub account. If you don't have one, you can follow this [guide](https://wiki.fly4future.com/docs/prerequisities/git-setup).

2. Install dependencies and build the workspace

   ```sh
   cd ~/ros2_ws
   sudo apt update
   rosdep update
   rosdep install --from-paths src --ignore-src -r -y
   ```

3. Build the workspace

   ```sh
   colcon build
   ```

> [!NOTE]
> To run the av1 encoder, you need to install the rtp plugin from https://github.com/GStreamer/gst-plugins-rs

## :balloon:Usage

You have to run the signaling server first, and then the streamer node. You can use the provided launch file to start both:

1. Run the signaling server

   ```sh
   python3 src/webrtc_ros/scripts/signaling_server.py
   ```

   > [!NOTE]
   > You should have the `websockets` Python package installed to run the signaling server.

2. Source the workspace and run the streamer node

   ```sh
   source install/setup.bash
   ros2 run webrtc_ros webrtc_streamer
   ```

   > [!IMPORTANT]
   > Optionally, you can specify parameters for the streamer node, such as the signaling server URL, the encoding format, and the hardware acceleration method. For example:
   >
   > ```sh
   > ros2 run webrtc_ros webrtc_streamer --ros-args \
   >  -p signaling_server_url:=ws://localhost:8173/uav124 \ # The URL of the signaling server, including the unique client ID (e.g., "uav124")
   >  -p encoder:=h264 \                                    # The video encoding format ("h264" or "av1")
   >  -p hw_acceleration:=vapi                              # The hardware acceleration method ("cpu", "nv" for NVIDIA, "vaapi" for Intel/AMD)
   > ```
