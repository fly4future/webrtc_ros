#!/bin/bash
set -e

# Resolve the full path of the script
MY_PATH="$( cd "$( dirname "$0" )" && pwd )"

# List of required ROS packages
REQUIRED_PACKAGES=(
    ros-noetic-async-web-server-cpp
    ros-noetic-webrtc
)

# URL to the PPA setup script
PPA_SETUP_URL="https://ctu-mrs.github.io/ppa-unstable/add_ppa.sh"

# Function to check if a package is installed
is_installed() {
    dpkg -s "$1" &> /dev/null
}

# Function to check if PPA is already added
is_ppa_added() {
    grep -h "^deb .*$1" /etc/apt/sources.list /etc/apt/sources.list.d/* 2>/dev/null | grep -q "$1"
}

# Track missing packages
MISSING_PACKAGES=()

# Check for missing packages
for package in "${REQUIRED_PACKAGES[@]}"; do
    if is_installed "$package"; then
        echo "$package is already installed."
    else
        echo "$package is missing."
        MISSING_PACKAGES+=("$package")
    fi
done

# Exit if everything is already installed
if [ ${#MISSING_PACKAGES[@]} -eq 0 ]; then
    echo "All required packages are already installed. Nothing to do."
    exit 0
fi

# Update package lists
echo "Updating package lists..."
sudo apt-get update

# Install ros-noetic-async-web-server-cpp if needed
if [[ " ${MISSING_PACKAGES[*]} " == *" ros-noetic-async-web-server-cpp "* ]]; then
    sudo apt-get install -y ros-noetic-async-web-server-cpp
fi

# For ros-noetic-webrtc, check if PPA is added
PPA_IDENTIFIER="ctu-mrs.github.io/ppa-unstable"

if ! is_ppa_added "$PPA_IDENTIFIER"; then
    echo "PPA not found. Adding PPA for MRS unstable packages..."
    curl -fsSL "$PPA_SETUP_URL" | bash
    echo "Updating package lists after adding PPA..."
    sudo apt-get update
else
    echo "PPA for MRS unstable packages already added."
fi

# Install ros-noetic-webrtc if needed
if [[ " ${MISSING_PACKAGES[*]} " == *" ros-noetic-webrtc "* ]]; then
    sudo apt-get install -y ros-noetic-webrtc
fi

echo "Installation complete!"
