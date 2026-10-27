# Reproducible Linux host build/test image.
#
# Build:
#   docker build -t opencmr2-host -f tools/host/Dockerfile tools/host
#
# Run from the repository root:
#   docker run --rm -v "$PWD":/src -w /src --user "$(id -u):$(id -g)" \
#       -e HOME=/tmp opencmr2-host sh -c 'cmake --preset linux && cmake --build --preset linux && ctest --preset linux'
#
# The 32-bit reference build additionally needs PKG_CONFIG_LIBDIR:
#   docker run --rm -v "$PWD":/src -w /src --user "$(id -u):$(id -g)" \
#       -e HOME=/tmp \
#       -e PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig \
#       opencmr2-host sh -c 'cmake --preset linux-x86 -DOPENCMR2_VENDORED_SDL=ON && cmake --build --preset linux-x86 && ctest --preset linux-x86'
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture i386 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential clang-15 g++-multilib git ninja-build pkg-config \
      python3-pip glslang-tools \
      libasound2-dev libasound2-dev:i386 \
      libdbus-1-dev libdbus-1-dev:i386 \
      libegl1-mesa-dev libegl1-mesa-dev:i386 \
      libgles2-mesa-dev libgles2-mesa-dev:i386 \
      libgl1-mesa-dev libgl1-mesa-dev:i386 \
      libudev-dev libudev-dev:i386 \
      libwayland-dev libwayland-dev:i386 wayland-protocols \
      libx11-dev libx11-dev:i386 \
      libxext-dev libxext-dev:i386 \
      libxfixes-dev libxfixes-dev:i386 \
      libxi-dev libxi-dev:i386 \
      libxinerama-dev libxinerama-dev:i386 \
      libxkbcommon-dev libxkbcommon-dev:i386 \
      libxkbcommon-x11-dev libxkbcommon-x11-dev:i386 \
      libxrandr-dev libxrandr-dev:i386 \
      libxcursor-dev libxcursor-dev:i386 \
      libxss-dev libxss-dev:i386 \
      libxtst-dev libxtst-dev:i386 \
 && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04 ships CMake 3.22; the project requires 3.24 or newer.
# Clang 15 matches the reference build and handles the i386 fixedpoint_equiv
# -fasm-blocks fixture; Clang 14 leaves its table symbol unresolved.
RUN python3 -m pip install --no-cache-dir cmake==3.31.6 \
 && ln -sf /usr/bin/clang-15 /usr/local/bin/clang \
 && ln -sf /usr/bin/clang++-15 /usr/local/bin/clang++

CMD ["sh"]
