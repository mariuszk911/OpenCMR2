# Build environment for the OpenCMR2 AmigaOS target.
#
# Bebbo GCC 6.5 toolchain from the published :latest tag (the only tag on
# Docker Hub; the earlier :with-make tag was local-only and is gone). The build
# uses Ninja, so GNU make is not needed. The image's 2021 apt keyring cannot
# verify the current Debian repositories, so CMake and Ninja come from pinned
# wheels instead of repairing apt inside the image. CMake must be 3.24 or
# newer; 3.31.6 is the pinned version. The SDK is not baked in:
# tools/amiga/build.sh mounts it read-only.
FROM amigadev/m68k-amigaos-gcc:latest

USER root
ENV PATH=/usr/local/bin:$PATH

RUN pip3 install --no-cache-dir "cmake==3.31.6" "ninja==1.11.1.4" \
 && cmake --version \
 && ninja --version

WORKDIR /src
