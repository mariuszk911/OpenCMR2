# AmigaOS 68k targets. Included from CMakeLists.txt when AMIGAOS is set;
# GAME_SOURCES, ZLIB_C_SOURCES and GAME_QIFIST_UNITS are defined there.

set(MINIGL_INCLUDE_DIR "${PISTORM3D}/SDK/minigl-shared-library/include")
set(MINIGL_LIBRARY_DIR "${PISTORM3D}/SDK/lib")

# libnix swapstack.o plus the __stack size. The Shell stack is too small for the
# C++ game and the STL-heavy Sys layer; the reference Amiga port uses the same
# mechanism. Every executable that runs game/Sys code gets this.
execute_process(COMMAND ${CMAKE_C_COMPILER} -mcrt=nix20 -print-file-name=swapstack.o
                OUTPUT_VARIABLE OPENCMR2_SWAPSTACK_OBJ OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT EXISTS "${OPENCMR2_SWAPSTACK_OBJ}")
    message(FATAL_ERROR "libnix swapstack.o not found")
endif()
set(OPENCMR2_AMIGA_STACK_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/platform/amiga/amiga_stack.c)
function(opencmr2_amiga_stack target)
    target_sources(${target} PRIVATE ${OPENCMR2_AMIGA_STACK_SOURCE})
    target_compile_definitions(${target} PRIVATE OPENCMR2_AMIGA_STACK=${OPENCMR2_AMIGA_STACK})
    target_link_libraries(${target} PRIVATE ${OPENCMR2_SWAPSTACK_OBJ})
endfunction()

# CrtTypeInfo.cpp is an MSVC CRT shim with a local type_info destructor; the
# Amiga build has no RTTI and does not use it.
list(REMOVE_ITEM GAME_SOURCES ${GAME_DIR}/CrtTypeInfo.cpp)

# ---- Domain A: game layout -------------------------------------------------
# The flag set that defines the original 32-bit/MSVC layout. Domain B must never
# see -malign-int.
add_library(opencmr2_domain_a INTERFACE)
target_compile_options(opencmr2_domain_a INTERFACE
    -malign-int
    $<$<COMPILE_LANGUAGE:C>:-x c>
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/types.h"
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/msvcrt.h"
    "$<$<COMPILE_LANGUAGE:CXX>:-include>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/port/menu_layout.h>")

# port/disk.cpp is Domain A too.
add_library(cmr2_game OBJECT
    ${GAME_SOURCES}
    diagnostics/observer.cpp
    port/ai_disk.cpp
    port/car_info_disk.cpp
    port/credits_text.cpp
    port/cod_disk.cpp
    port/controller_disk.cpp
    port/disk.cpp
    port/finish_record_disk.cpp
    port/font_pcf.cpp
    port/leaderboard_disk.cpp
    port/light_preset_disk.cpp
    port/replay_disk.cpp
    port/route_disk.cpp
    port/save_image_disk.cpp
    port/split_table_disk.cpp
    port/stage_camera_disk.cpp
    port/stage_light_transform_disk.cpp
    port/start_table_disk.cpp
    port/scene_lighting_disk.cpp
    port/texture_disk.cpp
    port/track_collision_disk.cpp)
# Built through the executable that links it, not on its own.
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
    platform/amiga/amiga_libfix.c
    platform/amiga/amiga_sys.cpp
    platform/amiga/amiga_platform.cpp
    render/minigl/gfx_minigl.cpp
    input/input_amiga.cpp
    audio/audio_ahi.cpp
    audio/mixer.cpp
    audio/msadpcm.cpp
    platform/amiga/amiga_ahi.c
    port/intdiv.cpp
    platform/amiga/minigl_abi_check.cpp)
target_include_directories(opencmr2_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
target_include_directories(opencmr2_platform PRIVATE ${MINIGL_INCLUDE_DIR})
# The float libm overrides must not be folded back into *f calls by GCC, and
# __builtin_sqrt must expand to fsqrt instead of a fallback call to sqrt.
set_source_files_properties(platform/amiga/amiga_libfix.c PROPERTIES COMPILE_FLAGS "-fno-builtin -fno-math-errno")

# ---- Stubs (Domain B) ------------------------------------------------------
# The features left out on this target: movies, network, profiling and the
# host-only render enhancements.
add_library(opencmr2_amiga_stubs OBJECT
    video/movie_stub.cpp
    net/net_stub.cpp
    platform/amiga/amiga_diagnostics.cpp
    render/amiga_enhancement_stubs.cpp)
target_include_directories(opencmr2_amiga_stubs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)

# Links the stubs alone against throwaway Gfx/Sys definitions, proving their
# new/delete and libnix dependencies without the game.
add_executable(amiga_stub_link_check
    ${PROJECT_SOURCE_DIR}/tests/amiga/stub_link_check.c
    $<TARGET_OBJECTS:opencmr2_amiga_stubs>)
set_target_properties(amiga_stub_link_check PROPERTIES LINKER_LANGUAGE CXX)
target_include_directories(amiga_stub_link_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ---- Executable ------------------------------------------------------------
add_executable(opencmr2
    platform/amiga/main_amiga.cpp
    $<TARGET_OBJECTS:cmr2_game>
    $<TARGET_OBJECTS:opencmr2_amiga_stubs>)
opencmr2_amiga_stack(opencmr2)
target_compile_definitions(opencmr2 PRIVATE OPENCMR2_AMIGA_STACK=${OPENCMR2_AMIGA_STACK})
target_link_libraries(opencmr2 PRIVATE opencmr2_domain_a opencmr2_platform)
# bridge_client.c is compiled into opencmr2_platform; no prebuilt library
# (the SDK .a is not in a format the GCC hunk linker accepts).
target_link_directories(opencmr2 PRIVATE ${MINIGL_LIBRARY_DIR})
target_link_libraries(opencmr2 PRIVATE minigl m)

# ---- Startup smoke harness (Domain B) --------------------------------------
# Standalone: MiniGL 29.1, timer.device and AHI, no game or port-layer code.
set(AHI_SDK "${PROJECT_SOURCE_DIR}/SDKs/m68k-amigaos-ahi")
if(NOT EXISTS "${AHI_SDK}/Developer/Include/C/devices/ahi.h")
    message(FATAL_ERROR "OpenCMR2: AHI SDK headers not found under ${AHI_SDK}/Developer/Include/C")
endif()
add_executable(amiga_smoke platform/amiga/smoke_main.c)
target_include_directories(amiga_smoke PRIVATE
    ${MINIGL_INCLUDE_DIR}
    ${AHI_SDK}/Developer/Include/C)
target_link_directories(amiga_smoke PRIVATE ${MINIGL_LIBRARY_DIR})
target_link_libraries(amiga_smoke PRIVATE minigl)

# MiniGL zero-area 2D rectangle reproducer for the option-screen separators.
add_executable(amiga_minigl_tl_rects_test ${PROJECT_SOURCE_DIR}/tests/amiga/minigl_tl_rects_test.c)
target_include_directories(amiga_minigl_tl_rects_test PRIVATE ${MINIGL_INCLUDE_DIR})
target_link_directories(amiga_minigl_tl_rects_test PRIVATE ${MINIGL_LIBRARY_DIR})
target_link_libraries(amiga_minigl_tl_rects_test PRIVATE minigl)

# Sys time/logging smoke: Domain B, no MiniGL. Not staged.
add_executable(amiga_sys_test ${PROJECT_SOURCE_DIR}/tests/amiga/sys_time_test.c
    platform/amiga/amiga_sys.cpp)
target_include_directories(amiga_sys_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_sys_test)

add_executable(amiga_sys_file_test ${PROJECT_SOURCE_DIR}/tests/amiga/sys_file_test.c
    platform/amiga/amiga_sys.cpp)
target_include_directories(amiga_sys_file_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_sys_file_test)

# Saved-controller path: the game's ".\Configuration\Controller.rcf" must
# resolve under the user directory and the 0x11a4-byte file must open.
add_executable(amiga_controller_path_test ${PROJECT_SOURCE_DIR}/tests/amiga/controller_path_test.c
    platform/amiga/amiga_sys.cpp)
target_include_directories(amiga_controller_path_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_controller_path_test)

# input.device adapter: a real activated window plus a bridge-injected
# Return proves the held-key path the game reads. Not a game launch.
add_executable(amiga_input_device_test ${PROJECT_SOURCE_DIR}/tests/amiga/input_device_test.c
    platform/amiga/amiga_sys.cpp)
target_include_directories(amiga_input_device_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_input_device_test)

add_executable(amiga_sys_settings_test ${PROJECT_SOURCE_DIR}/tests/amiga/sys_settings_test.c
    platform/amiga/amiga_sys.cpp)
target_include_directories(amiga_sys_settings_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_sys_settings_test)

add_executable(amiga_platform_test ${PROJECT_SOURCE_DIR}/tests/amiga/platform_test.c
    platform/amiga/amiga_platform.cpp
    platform/amiga/amiga_sys.cpp)
target_include_directories(amiga_platform_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_platform_test)

# Save-image codec: native 68k struct -> little-endian .rcs and back.
add_executable(amiga_save_image_test ${PROJECT_SOURCE_DIR}/tests/amiga/save_image_test.cpp
    port/save_image_disk.cpp)
target_include_directories(amiga_save_image_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/port/compat
    ${GAME_DIR})
target_link_libraries(amiga_save_image_test PRIVATE opencmr2_domain_a)
opencmr2_amiga_stack(amiga_save_image_test)

# Replay payload: native stream -> little-endian file -> native stream,
# including the type-2 sample bitfields.
add_executable(amiga_replay_payload_test ${PROJECT_SOURCE_DIR}/tests/replay_payload.cpp
    port/replay_disk.cpp
    port/disk.cpp)
target_include_directories(amiga_replay_payload_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/port/compat
    ${GAME_DIR})
target_link_libraries(amiga_replay_payload_test PRIVATE opencmr2_domain_a)
opencmr2_amiga_stack(amiga_replay_payload_test)

# Controller.rcf: real 68060 decode of the little-endian records and mappings.
add_executable(amiga_controller_disk_test ${PROJECT_SOURCE_DIR}/tests/amiga/controller_disk_test.cpp
    port/controller_disk.cpp
    port/disk.cpp)
target_include_directories(amiga_controller_disk_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/port/compat
    ${GAME_DIR})
target_link_libraries(amiga_controller_disk_test PRIVATE opencmr2_domain_a)
opencmr2_amiga_stack(amiga_controller_disk_test)

# .cin car-info: real 68060 decode of the section directory and scalars.
add_executable(amiga_car_info_test ${PROJECT_SOURCE_DIR}/tests/amiga/car_info_disk_test.cpp
    port/car_info_disk.cpp)
target_include_directories(amiga_car_info_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_car_info_test)

# .tsc trackside cameras: real 68060 decode of the count and 0x6c records.
add_executable(amiga_stage_camera_test ${PROJECT_SOURCE_DIR}/tests/amiga/stage_camera_disk_test.cpp
    port/stage_camera_disk.cpp)
target_include_directories(amiga_stage_camera_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_stage_camera_test)

# fin.dat: real 68060 decode of the pointer/scalar-union records.
add_executable(amiga_fin_record_test ${PROJECT_SOURCE_DIR}/tests/amiga/fin_record_test.cpp
    port/finish_record_disk.cpp
    port/disk.cpp)
target_include_directories(amiga_fin_record_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/port/compat
    ${GAME_DIR})
target_link_libraries(amiga_fin_record_test PRIVATE opencmr2_domain_a)
opencmr2_amiga_stack(amiga_fin_record_test)

# .csp start table: real 68060 decode of the start points and heading angles.
add_executable(amiga_start_table_test ${PROJECT_SOURCE_DIR}/tests/amiga/start_table_disk_test.cpp
    port/start_table_disk.cpp)
target_include_directories(amiga_start_table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_start_table_test)

# .cat route nodes: real 68060 decode of positions, flags, call ids and limits.
add_executable(amiga_route_node_test ${PROJECT_SOURCE_DIR}/tests/amiga/route_node_test.cpp
    port/route_disk.cpp
    port/disk.cpp)
target_include_directories(amiga_route_node_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/port/compat
    ${GAME_DIR})
target_link_libraries(amiga_route_node_test PRIVATE opencmr2_domain_a)
opencmr2_amiga_stack(amiga_route_node_test)

# .ai? CPU-driver tables: real 68060 decode of every converted region.
add_executable(amiga_ai_disk_test ${PROJECT_SOURCE_DIR}/tests/amiga/ai_disk_test.cpp
    port/ai_disk.cpp)
target_include_directories(amiga_ai_disk_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_ai_disk_test)

# .hor light presets: real 68060 decode of the seven ints and the byte tail.
add_executable(amiga_light_preset_test ${PROJECT_SOURCE_DIR}/tests/amiga/light_preset_test.cpp
    port/light_preset_disk.cpp)
target_include_directories(amiga_light_preset_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_light_preset_test)

# .bsp stage-light transform: real 68060 decode of the four FixVectors.
add_executable(amiga_stage_light_transform_test ${PROJECT_SOURCE_DIR}/tests/amiga/stage_light_transform_test.cpp
    port/stage_light_transform_disk.cpp)
target_include_directories(amiga_stage_light_transform_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_stage_light_transform_test)

# .tm? split tables: real 68060 decode of the mixed byte/32-bit record.
add_executable(amiga_split_table_test ${PROJECT_SOURCE_DIR}/tests/amiga/split_table_test.cpp
    port/split_table_disk.cpp)
target_include_directories(amiga_split_table_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_split_table_test)

# .cod co-driver calls: real 68060 decode of the 32-bit call records.
add_executable(amiga_cod_disk_test ${PROJECT_SOURCE_DIR}/tests/amiga/cod_disk_test.cpp
    port/cod_disk.cpp)
target_include_directories(amiga_cod_disk_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_cod_disk_test)

# .nlb leaderboards: real 68060 round-trip of the native image and file form.
add_executable(amiga_leaderboard_test ${PROJECT_SOURCE_DIR}/tests/amiga/leaderboard_test.cpp
    port/leaderboard_disk.cpp)
target_include_directories(amiga_leaderboard_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/port/compat
    ${GAME_DIR})
target_link_libraries(amiga_leaderboard_test PRIVATE opencmr2_domain_a)
opencmr2_amiga_stack(amiga_leaderboard_test)

add_executable(amiga_event_test ${PROJECT_SOURCE_DIR}/tests/amiga/event_test.c
    platform/amiga/amiga_sys.cpp)
target_include_directories(amiga_event_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_event_test)

# Development helper: remove a stuck game task without resetting the machine.
# RemTask does not close the MiniGL screen, so this is for test iterations only.
add_executable(amiga_kill_task_test ${PROJECT_SOURCE_DIR}/tests/amiga/kill_task_test.c)
target_include_directories(amiga_kill_task_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_kill_task_test)

# The callback state's packed DWORD must match the MSVC layout on 68k too.
add_executable(amiga_callback_state_test ${PROJECT_SOURCE_DIR}/tests/amiga/callback_state_test.cpp)
target_include_directories(amiga_callback_state_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
target_compile_options(amiga_callback_state_test PRIVATE -malign-int)
opencmr2_amiga_stack(amiga_callback_state_test)

# Menu/MenuItem must keep the MSVC LSB bitfield layout on 68k; the game
# compiles them through port/menu_layout.h, which declares the bitfields in
# reverse so GCC's MSB-first allocation stores the original byte
# (scalar_storage_order does not change bitfield order on GCC 6.5).
# SaveSlot keeps the MSVC LSB-first bitfield word on 68k; the profile
# code reads category/car both by name and from the packed slot DWORD.
add_executable(amiga_save_slot_test ${PROJECT_SOURCE_DIR}/tests/amiga/save_slot_test.cpp)
target_include_directories(amiga_save_slot_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
target_compile_options(amiga_save_slot_test PRIVATE -malign-int)
opencmr2_amiga_stack(amiga_save_slot_test)

# Real 68060 BE/alignment coverage for the C3D header and payload.
# The host forced-swap test is only a branch test.
add_executable(amiga_c3d_header_test ${PROJECT_SOURCE_DIR}/tests/amiga/c3d_header_test.cpp)
target_include_directories(amiga_c3d_header_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_c3d_header_test)

# The packed menu/split-panel colour word must put R, G, B, A in memory
# on 68k; the host test can only check its own byte order.
add_executable(amiga_menu_colour_test ${PROJECT_SOURCE_DIR}/tests/amiga/menu_colour_test.cpp)
target_include_directories(amiga_menu_colour_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
opencmr2_amiga_stack(amiga_menu_colour_test)

add_executable(amiga_menu_layout_test ${PROJECT_SOURCE_DIR}/tests/amiga/menu_layout_test.cpp)
target_link_libraries(amiga_menu_layout_test PRIVATE opencmr2_domain_a)
target_include_directories(amiga_menu_layout_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
opencmr2_amiga_stack(amiga_menu_layout_test)

# The FTOI contract where the conversion is llrint: the hard-float shim must
# round to nearest, ties to even, like the x87 FISTP the game was built with
# (tests/ftoi_fistp.cpp on x86). Rounding ties away from zero made the attract
# replay diverge from the recorded line.
add_executable(amiga_ftoi_ties_test ${PROJECT_SOURCE_DIR}/tests/ftoi_ties.cpp
    platform/amiga/amiga_libfix.c)
target_include_directories(amiga_ftoi_ties_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR})
target_compile_options(amiga_ftoi_ties_test PRIVATE "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/types.h")
set_source_files_properties(platform/amiga/amiga_libfix.c TARGET_DIRECTORY amiga_ftoi_ties_test
    PROPERTIES COMPILE_FLAGS "-fno-builtin -fno-math-errno")
opencmr2_amiga_stack(amiga_ftoi_ties_test)
