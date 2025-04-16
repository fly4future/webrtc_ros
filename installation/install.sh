#!/bin/bash

MY_PATH=`dirname "$0"`
MY_PATH=`( cd "$MY_PATH" && pwd )`

# install dependencies
sudo apt-get install ros-noetic-async-web-server-cpp 

