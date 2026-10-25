# Build environment for the OpenCMR2 AmigaOS target.
#
# The with-make variant is the same Bebbo GCC 6.5 toolchain plus GNU make. The
# plain :latest image carries a 2021 apt keyring that cannot verify the current
# Debian repositories, so the remaining tools come from pinned wheels instead
# of repairing apt inside the image. CMake must be 3.24 or newer; 3.31.6 is
# the pinned version. The SDK is not baked in: tools/amiga/build.sh mounts it
# read-only.
FROM amigadev/m68k-amigaos-gcc:with-make

USER root
ENV PATH=/usr/local/bin:$PATH

RUN pip3 install --no-cache-dir "cmake==3.31.6" "ninja==1.11.1.4" \
 && cmake --version \
 && ninja --version

WORKDIR /src
