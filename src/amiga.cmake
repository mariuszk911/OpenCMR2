# AmigaOS 68k targets. Included from CMakeLists.txt when AMIGAOS is set;
# GAME_SOURCES, ZLIB_C_SOURCES and GAME_QIFIST_UNITS are defined there.

set(MINIGL_INCLUDE_DIR "${PISTORM3D}/SDK/minigl-shared-library/include")
set(MINIGL_LIBRARY_DIR "${PISTORM3D}/SDK/lib")

# ---- Domain A: game layout -------------------------------------------------
# The flag set that defines the original 32-bit/MSVC layout. M0.5 passed on the
# cross-compiled ABI (build/amiga/m0-layout.log), so -malign-int is now part of
# Domain A. Domain B must never see it.
add_library(opencmr2_domain_a INTERFACE)
target_compile_options(opencmr2_domain_a INTERFACE
    -malign-int
    $<$<COMPILE_LANGUAGE:C>:-x c>
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/types.h"
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/msvcrt.h")

# Not linked into the M0 scaffold yet: M1 adds the platform services, then the
# executable links this library. port/disk.cpp is Domain A too; M0.8 removed
# its host allocation-size calls.
add_library(cmr2_game OBJECT
    ${GAME_SOURCES}
    diagnostics/observer.cpp
    port/disk.cpp
    port/replay_disk.cpp)
# M0 does not link the game yet, so keep it out of the default `all` target.
# M1 removes this once the platform services exist and the executable links it.
set_target_properties(cmr2_game PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_include_directories(cmr2_game PUBLIC
    ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
target_link_libraries(cmr2_game PRIVATE opencmr2_domain_a)
set_source_files_properties(${GAME_QIFIST_UNITS} TARGET_DIRECTORY cmr2_game
    PROPERTIES COMPILE_DEFINITIONS CMR2_QIFIST)

# ---- Domain B: OS and renderer glue ---------------------------------------
# No forced game headers and no -malign-int. MiniGL/NDK headers are allowed
# here. The ABI check is compile-time only; msvcrt.cpp is the portable MSVC
# runtime behaviour the game calls.
add_library(opencmr2_platform OBJECT
    platform/msvcrt.cpp
    port/intdiv.cpp
    platform/amiga/minigl_abi_check.cpp)
target_include_directories(opencmr2_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
target_include_directories(opencmr2_platform PRIVATE ${MINIGL_INCLUDE_DIR})

# ---- M0 executable ---------------------------------------------------------
# A scaffold that proves the cross toolchain, libnix startup and linker before
# the game is wired in. M1 replaces it with the CMain entry point and the
# MiniGL/OS facade; M0.9 exercises MiniGL in its own smoke target.
add_executable(opencmr2 platform/amiga/main_amiga.cpp)
target_compile_definitions(opencmr2 PRIVATE OPENCMR2_AMIGA_STACK=${OPENCMR2_AMIGA_STACK})
target_link_libraries(opencmr2 PRIVATE opencmr2_domain_a opencmr2_platform)
target_link_directories(opencmr2 PRIVATE ${MINIGL_LIBRARY_DIR})
target_link_libraries(opencmr2 PRIVATE minigl)
