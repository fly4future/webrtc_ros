#!/bin/bash
set -e

# 1. Install depot_tools, which is required for fetching the Chromium source code (gn, ninja, etc.)
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH="$PATH:$(pwd)/depot_tools"

# 2. Fetch the Chromium source code using depot_tools
mkdir webrtc-checkout
cd webrtc-checkout
fetch --nohooks webrtc
cd src

# 3. Run gclient sync to ensure all dependencies are downloaded
git checkout master
git pull origin master
gclient sync

# # 4. Install additional dependencies using the provided script (android, ios)
# ./build/install-build-deps-android.sh

# 5. Build the project using gn and ninja
gn gen out/Default --args='is_debug=false is_component_build=false'
ninja -C out/Default all