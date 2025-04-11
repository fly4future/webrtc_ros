# webrtc_ros

#### Streaming of ROS Image Topics using WebRTC

This node provides a WebRTC peer that can be configured to stream a ROS image topic and receive a stream that is published to a ROS image topic.
The node hosts a webserver that serves a simple test page and offers a websocket server that can be used to create and configure a WebRTC peer.

For full documentation, see [the ROS wiki](http://wiki.ros.org/webrtc_ros).

This project is released as part of the [Robot Web Tools](https://robotwebtools.github.io/) effort.

### Installation
To install `webrtc_ros`, you will need to have [ROS](http://wiki.ros.org/noetic/Installation) installed on your system and the following dependencies:

- [async_web_server_cpp](https://wiki.ros.org/async_web_server_cpp): `sudo apt-get install ros-noetic-async-web-server-cpp`. You can also install it with `rosdep` from your workspace:
  ```bash
  rosdep install --from-paths src --ignore-src -r -y
  ```
- [WebRTC library](https://webrtc.googlesource.com/src/) installed and built.
  > [!TIP]
  > The WebRTC library can be add the [unstable PPA](https://github.com/ctu-mrs/ppa-unstable) of the MRS group to your system. This will add the `webrtc` package to your system.
  > ```bash
  > curl https://ctu-mrs.github.io/ppa-unstable/add_ppa.sh | bash
  > sudo apt-get install ros-noetic-webrtc
  > ```

### Usage
To use this package, you will need to have a ROS environment set up and run the launch file:

```bash
roslaunch webrtc_ros webrtc_ros.launch
```

This will start the webrtc_ros node and the webserver. You can then navigate to `http://localhost:9090` in your web browser to see the test page.

> [!NOTE]
> The default port is 8080, but it can be changed by setting the `port` parameter in the launch file.

#### WebRTC Client
To create a custom WebRTC client, you could follow the [WebRTC in js/browser](web/TUTORIAL.md) tutorial or take a look at the [web folder](web/) with the web page used by the web server.

### License

webrtc_ros is released with a BSD license. For full terms and conditions, see the [LICENSE](LICENSE) file.

### Authors

See the [AUTHORS](AUTHORS.md) file for a full list of contributors.
