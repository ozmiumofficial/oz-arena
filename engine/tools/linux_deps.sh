#!/bin/sh
# What engine/build_engine.py needs on Linux (WSL2 Ubuntu 22.04 or 24.04, Debian): the compilers, CMake,
# Ninja, and the -dev packages SDL2 and OpenAL Soft build their back ends against (both load them at run
# time, so players need none of these). OpenSSL is libcurl's TLS.
set -e
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git python3 pkg-config libssl-dev \
	libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxxf86vm-dev libxfixes-dev \
	libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev libgles-dev \
	libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-jackd2-dev libdbus-1-dev libudev-dev libibus-1.0-dev \
	zenity file
