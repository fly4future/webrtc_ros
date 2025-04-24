#!/bin/bash

MY_PATH=`dirname "$0"`
MY_PATH=`( cd "$MY_PATH" && pwd )`

# install dependencies
sudo apt-get install ros-noetic-async-web-server-cpp 
curl https://ctu-mrs.github.io/ppa-unstable/add_ppa.sh | bash
sudo apt-get install ros-noetic-webrtc
