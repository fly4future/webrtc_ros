#!/bin/bash
set -e

# 1. Install depot_tools, which is required for fetching the Chromium source code (gn, ninja, etc.)
if [ ! -d depot_tools ]; then
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
else
    cd depot_tools
    git pull origin main
    cd ..
fi
export PATH="$PATH:$(pwd)/depot_tools"

# 2. Fetch the Chromium source code using depot_tools
mkdir webrtc-checkout
cd webrtc-checkout
fetch --nohooks webrtc
cd src

# 3. Update and sync with gclient to ensure all dependencies are downloaded
# https://chromiumdash.appspot.com/fetch_milestones (Next release)
# https://api.github.com/repos/stasel/WebRTC/releases (Check for the latest release)
git fetch --all
git checkout branch-heads/7727 # or a specific branch/tag/commit
cd ..
gclient sync --with_branch_heads --with_tags

# # 4. Install additional dependencies using the provided script (android, ios)
# ./build/install-build-deps-android.sh

# 5. Build the project using gn and ninja
gn gen out/Default --args="is_debug=false rtc_include_tests=false rtc_build_examples=false is_clang=false is_desktop_linux=true use_system_libjpeg=true treat_warnings_as_errors=false fatal_linker_warnings=false use_gio=false use_rtti=true rtc_enable_protobuf=false use_sysroot=false use_custom_libcxx=false rtc_build_json=true symbol_level=0 target_os=\"linux\" target_cpu=\"x64\" extra_cxx_flags=\"-Wno-free-nonheap-object -Wno-return-type\""
ninja -C out/Default all