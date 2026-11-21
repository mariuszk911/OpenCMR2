cmake_minimum_required(VERSION 3.24)
project(OpenCMR2 LANGUAGES C CXX)

# Targets (see CMakePresets.json):
#  - linux-x86: 32-bit x86, the reference build. The game's data structures
#    have the original's layout there, which the golden tests compare against.
#  - linux, windows-x64, macos: native 64-bit builds. Raw access review and
#    original-game parity checks are tracked in docs/64bit-raw-access.md.
#  - Android: built by the Gradle project in android/, which passes the NDK
#    toolchain and the SDL3 source it also takes SDLActivity from.

# The game source needs Clang with GNU-style options: -fms-extensions for the
# MSVC dialect it was written in, forced includes and the semantics below.
# On Windows that is clang++ (not clang-cl) targeting the MSVC runtime.
# The AmigaOS target is a cross build with Bebbo GCC; tools/amiga/build.sh is
# its entry point and selects this compiler through cmake/amigaos.toolchain.cmake.
if(AMIGAOS)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "The AmigaOS target builds with m68k-amigaos-gcc/g++ (found ${CMAKE_CXX_COMPILER_ID}); use tools/amiga/build.sh")
    endif()
else()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "OpenCMR2 builds with Clang (found ${CMAKE_CXX_COMPILER_ID}); use one of the CMake presets")
    endif()
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(FATAL_ERROR "Use clang++, not clang-cl: the build passes GNU-style options")
    endif()
endif()

option(OPENCMR2_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(OPENCMR2_VENDORED_SDL "Download and build SDL3 instead of using an installed one" ${ANDROID})
option(AMIGAOS "Build the native AmigaOS 68k target" OFF)
# Task 0.1 section timing: off by default so normal builds contain no clock
# call or timing accounting in the draw path. Only meaningful for AMIGAOS.
option(OPENCMR2_MINIGL_SECTION_TIMING "Amiga MiniGL per-section timing counters (diagnostic build)" OFF)

# Target identity for the fork-owned platform layer. The host builds leave
# both macros undefined, so no ad-hoc #ifdef in the sources decides this.
if(AMIGAOS)
    add_compile_definitions(OPENCMR2_AMIGA BIGENDIAN)
    if(OPENCMR2_MINIGL_SECTION_TIMING)
        add_compile_definitions(OPENCMR2_MINIGL_SECTION_TIMING)
    endif()
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)

    # The SDK is gitignored and may live elsewhere; the wrapper passes
    # -DPISTORM3D=/pistorm3d from its read-only mount.
    set(PISTORM3D "${CMAKE_SOURCE_DIR}/SDKs/PiStorm3D" CACHE PATH "PiStorm3D SDK root (SDK/, bin/, licences/)")
    foreach(required
            "${PISTORM3D}/SDK/lib/libminigl.a"
            "${PISTORM3D}/SDK/minigl-shared-library/include/proto/minigl.h"
            "${PISTORM3D}/bin/minigl.library")
        if(NOT EXISTS "${required}")
            message(FATAL_ERROR "OpenCMR2: missing ${required}; set PISTORM3D to the PiStorm3D SDK root")
        endif()
    endforeach()

    # ogles2.library SDK 1.3.3 (headers only: its libogles2.a cannot be linked
    # by the toolchain's GNU ld, src/render/gles2/ogles2_glue.c replaces it).
    set(OGLES2_SDK "${CMAKE_SOURCE_DIR}/SDKs/ogles2-sdk-1.3.3" CACHE PATH "ogles2 SDK root (include/)")
    if(NOT EXISTS "${OGLES2_SDK}/include/proto/ogles2.h")
        message(FATAL_ERROR "OpenCMR2: missing ${OGLES2_SDK}/include/proto/ogles2.h; set OGLES2_SDK to the ogles2 SDK root")
    endif()

    # tools/amiga/build.sh expects the executable directly in build/amiga.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

if(AMIGAOS)
    # The shared STL-heavy renderer files still build with GCC 6.5 in C++14.
    set(CMAKE_CXX_STANDARD 14)
else()
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Semantics the decompiled code relies on:
#  - signed overflow wraps (16.16 fixed point overflows on purpose);
#  - type punning through casts (the code was written against MSVC6);
#  - plain char is signed, as in MSVC (it is unsigned on ARM Linux);
#  - no fused multiply-add: the original rounds every product, and ARM64
#    compilers would otherwise fuse a * b + c and change physics results;
#  - explicit float stores use single precision, doubles use double precision;
#    the original's x87 nearest-even integer conversions are written explicitly
#    at audited physics sites (ordinary C++ casts would truncate).
add_compile_options(-fwrapv -fno-strict-aliasing -fsigned-char -ffp-contract=off)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86|i[3-6]86|AMD64")
    add_compile_options(-msse2 -mfpmath=sse)
endif()
if(WIN32)
    # The MSVC runtime warns about its own POSIX names and "unsafe" functions.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS NOMINMAX)
endif()

if(OPENCMR2_SANITIZE)
    if(AMIGAOS)
        message(FATAL_ERROR "OpenCMR2: sanitizers are not available for the AmigaOS target")
    endif()
    # The game calls its callback tables through generic function types, as
    # the original does, so UBSan's function type check is off.
    add_compile_options(-fsanitize=address,undefined -fno-sanitize=function -fno-omit-frame-pointer
                        -fsanitize-ignorelist=${CMAKE_SOURCE_DIR}/tools/sanitizer-ignorelist.txt)
    add_link_options(-fsanitize=address,undefined)
endif()

if(NOT AMIGAOS)
    if(NOT OPENCMR2_VENDORED_SDL)
        find_package(SDL3 3.4 CONFIG COMPONENTS SDL3-shared)
    endif()
    if(NOT TARGET SDL3::SDL3)
        message(STATUS "OpenCMR2: building SDL3 from source")
        set(OPENCMR2_BUILT_SDL ON)
        include(FetchContent)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.4.18
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(SDL3)
        # SDL's own warnings, not ours to fix: its EGL typedefs are declared again
        # by the NDK's headers, and the Android sensor code uses a deprecated call.
        if(TARGET SDL3-shared AND CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_compile_options(SDL3-shared PRIVATE -Wno-typedef-redefinition -Wno-deprecated-declarations)
        endif()
    endif()
endif()

add_subdirectory(src)

# The tests run on the build machine; the Android build is the game alone and
# the Amiga target has no test harness yet.
if(NOT ANDROID AND NOT AMIGAOS)
    enable_testing()
    add_subdirectory(tests)
endif()
