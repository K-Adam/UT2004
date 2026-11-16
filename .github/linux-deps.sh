#!/bin/sh
# Installs what the linux presets need on Debian and Ubuntu. Extra arguments are more packages.
set -e
SUDO=$(command -v sudo || true)
export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends ca-certificates git cmake patch \
  build-essential ninja-build pkg-config zlib1g-dev libvorbis-dev libopenal-dev libbullet-dev \
  libbullet-extras-dev libgles-dev libsquish-dev libssl-dev libcurl4-openssl-dev nlohmann-json3-dev libspeexdsp-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev libegl-dev libdrm-dev libgbm-dev \
  libpulse-dev libasound2-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev "$@"
# Ubuntu LTS has no SDL 3 package. The version matches the one vcpkg gives the other platforms.
if [ ! -e /usr/local/lib/cmake/SDL3 ]; then
  git clone --depth 1 -b release-3.2.8 https://github.com/libsdl-org/SDL /tmp/SDL
  cmake -S /tmp/SDL -B /tmp/SDL/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_INSTALL_LIBDIR=lib -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
  cmake --build /tmp/SDL/build
  $SUDO cmake --install /tmp/SDL/build
fi
