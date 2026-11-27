# Portable fixed-point helpers vs the original inline assembly. Needs a
# 32-bit x86 build: clang compiles the MSVC-style assembly with -fasm-blocks.
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|x86")
    add_executable(fixedpoint_equiv fixedpoint_equiv.cpp ${PROJECT_SOURCE_DIR}/src/port/intdiv.cpp)
    target_include_directories(fixedpoint_equiv PRIVATE ${PROJECT_SOURCE_DIR}/game ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(fixedpoint_equiv PRIVATE
        -include ${PROJECT_SOURCE_DIR}/src/port/types.h -fms-extensions -fasm-blocks -Wno-everything)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # The original assembly uses absolute addresses (offset g_atanTable).
        # Ubuntu's Clang defaults to PIE, which cannot encode that operand.
        target_compile_options(fixedpoint_equiv PRIVATE -fno-pic -fno-pie)
        target_link_options(fixedpoint_equiv PRIVATE -no-pie)
    endif()
    add_test(NAME fixedpoint_equiv COMMAND fixedpoint_equiv)
endif()

# Little-endian helpers (src/port/endian.h). Built from both C and C++ because
# the header is shared by the game and the OS glue.
add_executable(endian_test endian.cpp endian_c.c endian_forced_big.cpp)
target_include_directories(endian_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME endian COMMAND endian_test)

# M0.5 layout reference. The linux-x86 build prints the original 32-bit
# sizeof/offsetof values; the 68k probe records the same expressions.
add_executable(layout_probe amiga/layout_probe.cpp)
target_include_directories(layout_probe PRIVATE
    ${PROJECT_SOURCE_DIR}/game
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_options(layout_probe PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
    -fms-extensions)
target_compile_definitions(layout_probe PRIVATE OPENCMR2_LAYOUT_PRINT)
add_test(NAME layout_probe COMMAND layout_probe)

add_executable(layout_probe_shared amiga/layout_probe.cpp)
target_include_directories(layout_probe_shared PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_definitions(layout_probe_shared PRIVATE
    OPENCMR2_LAYOUT_PRINT OPENCMR2_LAYOUT_SHARED_ONLY)
add_test(NAME layout_probe_shared COMMAND layout_probe_shared)

# 32-bit host drift guard for the fork-owned Menu layout override. The Amiga
# target uses src/port/menu_layout.h; this checks the real game/Menu.h against
# the same expected sizes, offsets and prototypes.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(menu_layout_host_test menu_layout_host.cpp)
    target_include_directories(menu_layout_host_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    add_test(NAME menu_layout_host COMMAND menu_layout_host_test)
endif()

# M0.8: native copies are released by their owning file buffer, without a
# malloc_usable_size/_msize/malloc_size dependency.
add_executable(disk_ownership_test disk_ownership.cpp
    ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
target_include_directories(disk_ownership_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME disk_ownership COMMAND disk_ownership_test)

# M0.7 port-owned slice: file offsets and pointer writes stay little-endian.
add_executable(disk_endian_test disk_endian.cpp
    ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
target_include_directories(disk_endian_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME disk_endian COMMAND disk_endian_test)

# M0.7 swap-table check. The forced big-endian branch runs on the LE host so
# the generated offsets and the Pack/Unpack round trip are covered here.
add_executable(disk_swaps_test disk_swaps.cpp
    ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
target_include_directories(disk_swaps_test PRIVATE
    ${PROJECT_SOURCE_DIR}/game
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_options(disk_swaps_test PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
    -fms-extensions)
target_compile_definitions(disk_swaps_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)
add_test(NAME disk_swaps COMMAND disk_swaps_test)

# M0.7 save-image codec: the file is little-endian and its bitfield words are
# LSB-first; the normal build checks the raw bytes, the forced build proves the
# swap path and bitfield round trip.
add_executable(save_image_disk_test save_image_disk.cpp
    ${PROJECT_SOURCE_DIR}/src/port/save_image_disk.cpp)
target_include_directories(save_image_disk_test PRIVATE
    ${PROJECT_SOURCE_DIR}/game
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_options(save_image_disk_test PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
    -fms-extensions)
add_test(NAME save_image_disk COMMAND save_image_disk_test)

add_executable(save_image_disk_be_test save_image_disk.cpp
    ${PROJECT_SOURCE_DIR}/src/port/save_image_disk.cpp)
target_include_directories(save_image_disk_be_test PRIVATE
    ${PROJECT_SOURCE_DIR}/game
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_options(save_image_disk_be_test PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
    -fms-extensions)
target_compile_definitions(save_image_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)
add_test(NAME save_image_disk_be COMMAND save_image_disk_be_test)

# Amiga-native saved-game image: big-endian words, signature, version and
# checksum. The first target uses the explicit-format API as any host build
# sees it; the second compiles the codec as the Amiga build does (native
# format = Amiga) to cover the native entry points and the reject handler.
foreach(variant IN ITEMS "" "_native")
    add_executable(save_image_amiga${variant}_test save_image_amiga.cpp
        ${PROJECT_SOURCE_DIR}/src/port/save_image_disk.cpp)
    target_include_directories(save_image_amiga${variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(save_image_amiga${variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME save_image_amiga${variant} COMMAND save_image_amiga${variant}_test)
endforeach()
target_compile_definitions(save_image_amiga_native_test PRIVATE OPENCMR2_TEST_FORCE_AMIGA_SAVES)

# Controller.rcf: six little-endian ControllerData records and the slot
# mappings. The forced variant proves the swap path on the little-endian host.
foreach(controller_variant controller_disk controller_disk_be)
    add_executable(${controller_variant}_test controller_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/controller_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
    target_include_directories(${controller_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${controller_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${controller_variant} COMMAND ${controller_variant}_test)
endforeach()
target_compile_definitions(controller_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# Archive members decoded in one place (CGenericFileLoader::FindFile): .cod,
# .tsc, .csp, .bsp and .hor come back as native copies, other names untouched,
# malformed members as missing. The forced variant proves the swap path.
foreach(member_decode_variant member_decode member_decode_be)
    add_executable(${member_decode_variant}_test member_decode.cpp
        font_pcf_buffer_stub.cpp
        ${PROJECT_SOURCE_DIR}/src/port/member_decode.cpp
        ${PROJECT_SOURCE_DIR}/src/port/cod_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/stage_camera_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/start_table_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/stage_light_transform_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/light_preset_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/font_pcf.cpp
        ${PROJECT_SOURCE_DIR}/src/port/car_info_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
    target_include_directories(${member_decode_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${member_decode_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${member_decode_variant} COMMAND ${member_decode_variant}_test)
endforeach()
target_compile_definitions(member_decode_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .cin car-info sections: little-endian directory and section scalars. The
# forced variant proves the decoder branch on the little-endian host.
foreach(car_info_variant car_info_disk car_info_disk_be)
    add_executable(${car_info_variant}_test car_info_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/car_info_disk.cpp)
    target_include_directories(${car_info_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${car_info_variant} COMMAND ${car_info_variant}_test)
endforeach()
target_compile_definitions(car_info_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .tsc trackside cameras: count-bounded little-endian 0x6c-byte records.
foreach(stage_camera_variant stage_camera_disk stage_camera_disk_be)
    add_executable(${stage_camera_variant}_test stage_camera_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/stage_camera_disk.cpp)
    target_include_directories(${stage_camera_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${stage_camera_variant} COMMAND ${stage_camera_variant}_test)
endforeach()
target_compile_definitions(stage_camera_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# fin.dat: pointer/scalar union records decoded with the generated layout. The
# forced variant proves the swap path; disk_swaps covers the layout itself.
foreach(fin_record_variant finish_record_disk finish_record_disk_be)
    add_executable(${fin_record_variant}_test finish_record_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/finish_record_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
    target_include_directories(${fin_record_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${fin_record_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${fin_record_variant} COMMAND ${fin_record_variant}_test)
endforeach()
target_compile_definitions(finish_record_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .csp start table: six ints, the FixAngles at 0x18 and the tail points.
foreach(start_table_variant start_table_disk start_table_disk_be)
    add_executable(${start_table_variant}_test start_table_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/start_table_disk.cpp)
    target_include_directories(${start_table_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${start_table_variant} COMMAND ${start_table_variant}_test)
endforeach()
target_compile_definitions(start_table_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .cat route nodes: little-endian positions/limits, byte flags/call ids. The
# forced variant proves the swap path on the little-endian host.
foreach(route_node_variant route_node_disk route_node_disk_be)
    add_executable(${route_node_variant}_test route_node_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/route_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
    target_include_directories(${route_node_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${route_node_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${route_node_variant} COMMAND ${route_node_variant}_test)
endforeach()
target_compile_definitions(route_node_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .ai? CPU-driver tables: the six car tables, route masks/counts, network
# layer counts, weights, activation table, node keys and reference routes.
foreach(ai_disk_variant ai_disk ai_disk_be)
    add_executable(${ai_disk_variant}_test ai_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/ai_disk.cpp)
    target_include_directories(${ai_disk_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${ai_disk_variant} COMMAND ${ai_disk_variant}_test)
endforeach()
target_compile_definitions(ai_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .hor light presets: seven little-endian ints per 0x4e-byte record.
foreach(light_preset_variant light_preset_disk light_preset_disk_be)
    add_executable(${light_preset_variant}_test light_preset_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/light_preset_disk.cpp)
    target_include_directories(${light_preset_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${light_preset_variant} COMMAND ${light_preset_variant}_test)
endforeach()
target_compile_definitions(light_preset_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .bsp stage-light transform: four little-endian FixVectors.
foreach(stage_light_variant stage_light_transform_disk stage_light_transform_disk_be)
    add_executable(${stage_light_variant}_test stage_light_transform_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/stage_light_transform_disk.cpp)
    target_include_directories(${stage_light_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${stage_light_variant} COMMAND ${stage_light_variant}_test)
endforeach()
target_compile_definitions(stage_light_transform_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .tm? split tables: signed-byte prefix preserved, 32-bit region swapped.
foreach(split_table_variant split_table_disk split_table_disk_be)
    add_executable(${split_table_variant}_test split_table_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/split_table_disk.cpp)
    target_include_directories(${split_table_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${split_table_variant} COMMAND ${split_table_variant}_test)
endforeach()
target_compile_definitions(split_table_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .cod co-driver calls: an array of little-endian 32-bit call records.
foreach(cod_disk_variant cod_disk cod_disk_be)
    add_executable(${cod_disk_variant}_test cod_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/cod_disk.cpp)
    target_include_directories(${cod_disk_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${cod_disk_variant} COMMAND ${cod_disk_variant}_test)
endforeach()
target_compile_definitions(cod_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# .nlb leaderboards: native image <-> little-endian file form.
foreach(leaderboard_variant leaderboard_disk leaderboard_disk_be)
    add_executable(${leaderboard_variant}_test leaderboard_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/leaderboard_disk.cpp)
    target_include_directories(${leaderboard_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${leaderboard_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${leaderboard_variant} COMMAND ${leaderboard_variant}_test)
endforeach()
target_compile_definitions(leaderboard_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# M3.1: the C3D stage header is little-endian. The forced-swap variant
# exercises the decoder branch on a little-endian host; real BE/alignment
# coverage is the amiga_c3d_header_test built for the 68060 target.
foreach(c3d_variant c3d_header c3d_header_forced)
    add_executable(${c3d_variant}_test c3d_header.cpp)
    target_include_directories(${c3d_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${c3d_variant} COMMAND ${c3d_variant}_test)
endforeach()
target_compile_definitions(c3d_header_forced_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# The -malign-int workaround keeps 64-bit division in a Domain B TU; these
# helpers must be bit-exact against the original expressions.
add_executable(intdiv_test intdiv.cpp ${PROJECT_SOURCE_DIR}/src/port/intdiv.cpp)
target_include_directories(intdiv_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME intdiv COMMAND intdiv_test)

# M3.1: the preview-panel weather map index stays inside the four maps and the
# maps actually loaded for the rally group.
add_executable(weather_map_test weather_map.cpp)
target_include_directories(weather_map_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME weather_map COMMAND weather_map_test)

# Software BC1/BC3 decode for the MiniGL backend: a BC3 colour half is always
# four-colour.
add_executable(bc_decode_test bc_decode.cpp)
target_include_directories(bc_decode_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME bc_decode COMMAND bc_decode_test)

add_executable(display_modes_test display_modes.cpp)
target_include_directories(display_modes_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME display_modes COMMAND display_modes_test)

# A texture stage that passes the colour through is left off on the MiniGL
# backend (the car's Standard environment pass).
add_executable(stage_passthrough_test stage_passthrough.cpp)
target_include_directories(stage_passthrough_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME stage_passthrough COMMAND stage_passthrough_test)

# A bump stage modulates by its luminance on the MiniGL backend, read from the
# channel the host shader uses (the car's Bump environment pass).
add_executable(bump_luminance_test bump_luminance.cpp)
target_include_directories(bump_luminance_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME bump_luminance COMMAND bump_luminance_test)

# A backdrop draw's fog, expressed as GL linear fog on the MiniGL backend, stays
# the vertex's specular alpha at every eye distance.
add_executable(backdrop_fog_test backdrop_fog.cpp)
target_include_directories(backdrop_fog_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME backdrop_fog COMMAND backdrop_fog_test)

# MiniGL has no light range: a ranged light is switched off for a draw wholly
# beyond it, from the draw's bounding sphere in world space.
add_executable(light_range_test light_range.cpp)
target_include_directories(light_range_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME light_range COMMAND light_range_test)

# MiniGL applies only one directional light, so draws that a point or spot
# light reaches are lit on the CPU with Direct3D's vertex lighting.
add_executable(d3d_light_test d3d_light.cpp)
target_include_directories(d3d_light_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME d3d_light COMMAND d3d_light_test)

# M0.7 core-slice fixture: WAV fmt fields are little-endian on disk.
add_executable(wave_format_test wave_format.cpp)
target_include_directories(wave_format_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME wave_format COMMAND wave_format_test)

add_executable(wave_format_be_test wave_format_be.cpp)
target_include_directories(wave_format_be_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(wave_format_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)
add_test(NAME wave_format_be COMMAND wave_format_be_test)

# M0.7 BFL directory. The normal build parses a little-endian fixture; the
# forced variant pre-swaps it to prove every field is decoded as little-endian.
foreach(bfl_variant bfl_archive bfl_archive_be)
    add_executable(${bfl_variant}_test bfl_archive.cpp
        ${PROJECT_SOURCE_DIR}/game/GenericFileLoader.cpp)
    target_include_directories(${bfl_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${bfl_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${bfl_variant} COMMAND ${bfl_variant}_test)
endforeach()
target_compile_definitions(bfl_archive_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# M1 stubs: no-op Amiga profiling. The Amiga
# target compiles the same translation units.
add_executable(amiga_stubs_test amiga_stubs.cpp
    ${PROJECT_SOURCE_DIR}/src/platform/amiga/amiga_diagnostics.cpp
    ${PROJECT_SOURCE_DIR}/src/diagnostics/observer.cpp)
target_include_directories(amiga_stubs_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_stubs COMMAND amiga_stubs_test)

# The fixed-point mixer and ADPCM decoder behind the Amiga audio backend. The
# AHI output itself is target-only.
add_executable(audio_mixer_test audio_mixer.cpp
    ${PROJECT_SOURCE_DIR}/src/audio/mixer.cpp
    ${PROJECT_SOURCE_DIR}/src/audio/msadpcm.cpp)
target_include_directories(audio_mixer_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(audio_mixer_test PRIVATE m)
add_test(NAME audio_mixer COMMAND audio_mixer_test)

# Portable Sys time logic: the fixed-step clock and the Amiga E-clock/DateStamp
# conversions. The timer.device calls are target-only.
add_executable(sys_time_test sys_time.cpp)
target_include_directories(sys_time_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME sys_time COMMAND sys_time_test)

# Pure game-path helpers for the Amiga Sys layer.
add_executable(amiga_keys_test amiga_keys.cpp)
target_include_directories(amiga_keys_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_keys COMMAND amiga_keys_test)

# The Amiga build's bounds-safe qsort, under its own name (the host keeps its
# libc qsort, which the test also compares it with).
add_executable(amiga_qsort_test amiga_qsort.cpp ${PROJECT_SOURCE_DIR}/src/platform/amiga/amiga_qsort.c)
target_include_directories(amiga_qsort_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(amiga_qsort_test PRIVATE AMIGA_QSORT_HOST_TEST)
add_test(NAME amiga_qsort COMMAND amiga_qsort_test)

add_executable(amiga_paths_test amiga_paths.cpp)
target_include_directories(amiga_paths_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_paths COMMAND amiga_paths_test)

# opencmr2.ini parsing and line-preserving rewriting for the Amiga Sys layer.
add_executable(amiga_save_notice_test amiga_save_notice.cpp)
target_include_directories(amiga_save_notice_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_save_notice COMMAND amiga_save_notice_test)

add_executable(amiga_minigl_notice_test amiga_minigl_notice.cpp)
target_include_directories(amiga_minigl_notice_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_minigl_notice COMMAND amiga_minigl_notice_test)

add_executable(amiga_settings_test amiga_settings.cpp)
target_include_directories(amiga_settings_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_settings COMMAND amiga_settings_test)

add_executable(net_payload_test net_payload.cpp)
target_link_libraries(net_payload_test PRIVATE opencmr2_net)
target_compile_options(net_payload_test PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")
add_test(NAME net_payload COMMAND net_payload_test)

add_executable(net_session_test net_session.cpp)
target_link_libraries(net_session_test PRIVATE opencmr2_net)
target_compile_options(net_session_test PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")
add_test(NAME net_session COMMAND net_session_test)
set_tests_properties(net_session PROPERTIES TIMEOUT 60)

# Manual cross-play peer: talks to tests/amiga/net_peer_test.cpp on another machine.
add_executable(net_cross_peer net_cross_peer.cpp)
target_link_libraries(net_cross_peer PRIVATE opencmr2_net)
target_include_directories(net_cross_peer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(net_cross_peer PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")

# The same suite against the cooperative service mode (AmigaOS): the test is the game's main loop.
add_executable(net_session_cooperative_test net_session.cpp)
target_link_libraries(net_session_cooperative_test PRIVATE opencmr2_net_cooperative)
target_compile_options(net_session_cooperative_test PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")
add_test(NAME net_session_cooperative COMMAND net_session_cooperative_test)
set_tests_properties(net_session_cooperative PROPERTIES TIMEOUT 60)

add_executable(bink_decoder_test bink_decoder.cpp)
target_link_libraries(bink_decoder_test PRIVATE opencmr2_bink)
add_test(NAME bink_decoder COMMAND bink_decoder_test)

add_executable(yuv_rgba_test yuv_rgba.cpp)
target_link_libraries(yuv_rgba_test PRIVATE opencmr2_bink)
add_test(NAME yuv_rgba COMMAND yuv_rgba_test)

add_executable(movie_playback_test movie_playback.cpp ${PROJECT_SOURCE_DIR}/src/video/movie.cpp)
target_link_libraries(movie_playback_test PRIVATE opencmr2_bink)
add_test(NAME movie_playback COMMAND movie_playback_test)

# Movie timing and layout rules shared by the Amiga player.
add_executable(movie_pacing_test movie_pacing.cpp)
target_include_directories(movie_pacing_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME movie_pacing COMMAND movie_pacing_test)

# The Amiga movie player against fake renderer, audio and file layers.
add_executable(movie_amiga_test movie_amiga.cpp ${PROJECT_SOURCE_DIR}/src/video/movie_amiga.cpp)
target_include_directories(movie_amiga_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(movie_amiga_test PRIVATE opencmr2_bink)
add_test(NAME movie_amiga COMMAND movie_amiga_test)

add_executable(movie_render_state_test movie_render_state.cpp
    ${PROJECT_SOURCE_DIR}/src/diagnostics/observer.cpp
    ${PROJECT_SOURCE_DIR}/src/video/movie.cpp ${PROJECT_SOURCE_DIR}/src/render/gfx_core.cpp)
target_link_libraries(movie_render_state_test PRIVATE opencmr2_bink SDL3::SDL3)
add_test(NAME movie_render_state COMMAND movie_render_state_test)

# FTOI vs a real x87 FISTP (the /QIfist conversion of the original).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|x86|AMD64|amd64")
    add_executable(ftoi_fistp_test ftoi_fistp.cpp)
    target_include_directories(ftoi_fistp_test PRIVATE ${PROJECT_SOURCE_DIR}/game)
    target_compile_options(ftoi_fistp_test PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")
    add_test(NAME ftoi_fistp COMMAND ftoi_fistp_test)
endif()

# The same contract where FTOI is llrint: ties to even, including the exact
# 24-bit halves the fixed-point maths produces. Also built for the Amiga target
# (src/amiga.cmake), where llrint is the hard-float shim.
add_executable(ftoi_ties_test ftoi_ties.cpp)
target_include_directories(ftoi_ties_test PRIVATE ${PROJECT_SOURCE_DIR}/game)
target_compile_options(ftoi_ties_test PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")
add_test(NAME ftoi_ties COMMAND ftoi_ties_test)

# The bit-exact fast path of the x87 single-precision arithmetic
# (src/port/x87single.h) against upstream's game/X87Single.h. Also built for
# the Amiga target (src/amiga.cmake), where the FPU is Emu68's.
add_executable(x87_single_test x87_single.cpp)
target_include_directories(x87_single_test PRIVATE ${PROJECT_SOURCE_DIR}/game ${PROJECT_SOURCE_DIR}/src)
add_test(NAME x87_single COMMAND x87_single_test)

# Replay input packet run checks read the same bits on every byte order. Also
# built for the Amiga target (src/amiga.cmake).
add_executable(replay_packet_test replay_packet.cpp)
target_include_directories(replay_packet_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/game)
target_compile_options(replay_packet_test PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")
add_test(NAME replay_packet COMMAND replay_packet_test)

# Tests that run game code, on every target.
add_executable(math_tables_test math_tables.cpp)
target_link_libraries(math_tables_test PRIVATE opencmr2_platform)
add_test(NAME math_tables COMMAND math_tables_test
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/x87-single/math-tables.bin)
add_executable(game_data_setup_test game_data_setup.cpp)
target_link_libraries(game_data_setup_test PRIVATE opencmr2_platform)
add_test(NAME game_data_setup COMMAND game_data_setup_test)
set_tests_properties(game_data_setup PROPERTIES TIMEOUT 30)
add_executable(car_scheduler_test car_scheduler.cpp)
target_link_libraries(car_scheduler_test PRIVATE opencmr2_platform)
add_test(NAME car_scheduler COMMAND car_scheduler_test)
add_executable(stage_probe stage_probe.cpp)
target_link_libraries(stage_probe PRIVATE opencmr2_platform)
# The monkey test's watchdog uses POSIX alarm().
if(NOT WIN32)
    add_executable(monkey_probe monkey_probe.cpp)
    target_link_libraries(monkey_probe PRIVATE opencmr2_platform)
endif()
add_executable(preview_cleanup_test preview_cleanup.cpp)
target_include_directories(preview_cleanup_test PRIVATE ${PROJECT_SOURCE_DIR}/game)
target_link_libraries(preview_cleanup_test PRIVATE opencmr2_platform)
add_test(NAME preview_cleanup COMMAND preview_cleanup_test)
add_executable(preview_mapping_test preview_mapping.cpp)
target_include_directories(preview_mapping_test PRIVATE ${PROJECT_SOURCE_DIR}/game)
target_link_libraries(preview_mapping_test PRIVATE opencmr2_platform)
add_test(NAME preview_mapping COMMAND preview_mapping_test)
add_executable(service_park_weather_test service_park_weather.cpp)
target_link_libraries(service_park_weather_test PRIVATE opencmr2_platform)
add_test(NAME service_park_weather COMMAND service_park_weather_test)
add_executable(input_actions_test input_actions.cpp)
target_include_directories(input_actions_test PRIVATE ${PROJECT_SOURCE_DIR}/game)
target_link_libraries(input_actions_test PRIVATE opencmr2_platform)
add_test(NAME input_actions COMMAND input_actions_test)
# The uinput wheels of ffb_wheel are real devices that input_actions would
# see among its virtual ones: never at the same time.
set_tests_properties(input_actions PROPERTIES RESOURCE_LOCK input_devices)

# A force feedback wheel made with uinput (Linux): skipped without access to
# /dev/uinput.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)
    add_executable(ffb_wheel_test ffb_wheel.cpp)
    target_include_directories(ffb_wheel_test PRIVATE ${PROJECT_SOURCE_DIR}/game)
    target_link_libraries(ffb_wheel_test PRIVATE opencmr2_platform)
    add_test(NAME ffb_wheel COMMAND ffb_wheel_test)
    set_tests_properties(ffb_wheel PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 RESOURCE_LOCK input_devices)
endif()

add_executable(frontend_language_test frontend_language.cpp)
target_link_libraries(frontend_language_test PRIVATE opencmr2_platform)
add_test(NAME frontend_language COMMAND frontend_language_test)
set_tests_properties(frontend_language PROPERTIES TIMEOUT 30)

add_executable(country_flags_test country_flags.cpp)
target_link_libraries(country_flags_test PRIVATE opencmr2_platform)
add_test(NAME country_flags COMMAND country_flags_test ${CMAKE_CURRENT_SOURCE_DIR}/reference/menu-flags.txt)

# The menu and split-panel colours are packed ints read back as R, G, B, A
# bytes; the original x86 literals put the alpha byte first on 68k.
add_executable(menu_colours_test menu_colours.cpp)
target_link_libraries(menu_colours_test PRIVATE opencmr2_platform)
add_test(NAME menu_colours COMMAND menu_colours_test)

add_executable(vector_sprites_test vector_sprites.cpp)
target_link_libraries(vector_sprites_test PRIVATE opencmr2_platform)
add_test(NAME vector_sprites COMMAND vector_sprites_test)

# The fork-owned .pcf parser (src/port/font_pcf.cpp): little-endian tables,
# bounds checks and char-map sanitisation.
add_executable(font_pcf_test font_pcf.cpp)
target_link_libraries(font_pcf_test PRIVATE opencmr2_platform)
add_test(NAME font_pcf COMMAND font_pcf_test)

# The forced variant compiles the parser itself to prove the swap path on the
# little-endian host.
add_executable(font_pcf_be_test font_pcf.cpp font_pcf_buffer_stub.cpp
    ${PROJECT_SOURCE_DIR}/src/port/font_pcf.cpp)
target_include_directories(font_pcf_be_test PRIVATE
    ${PROJECT_SOURCE_DIR}/game
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_options(font_pcf_be_test PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
    -fms-extensions)
target_compile_definitions(font_pcf_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)
add_test(NAME font_pcf_be COMMAND font_pcf_be_test)

# .dds header words and C3D texture records. The forced variant proves the
# swap path on the little-endian host.
foreach(texture_disk_variant texture_disk texture_disk_be)
    add_executable(${texture_disk_variant}_test texture_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/texture_disk.cpp)
    target_include_directories(${texture_disk_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${texture_disk_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${texture_disk_variant} COMMAND ${texture_disk_variant}_test)
endforeach()
target_compile_definitions(texture_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# Stage collision block: little-endian tables, size-validated counts, native
# aligned copy. The forced variant proves the swap path on the host.
foreach(track_collision_variant track_collision_disk track_collision_disk_be)
    add_executable(${track_collision_variant}_test track_collision_disk.cpp
        ${PROJECT_SOURCE_DIR}/src/port/track_collision_disk.cpp)
    target_include_directories(${track_collision_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${track_collision_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${track_collision_variant} COMMAND ${track_collision_variant}_test)
endforeach()
target_compile_definitions(track_collision_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# Stage lighting (.tre): size-validated zones, items, shadow meshes and
# payload decoded into native blocks. The forced variant proves the swap path.
foreach(scene_lighting_variant scene_lighting_disk scene_lighting_disk_be)
    add_executable(${scene_lighting_variant}_test scene_lighting_disk.cpp
        font_pcf_buffer_stub.cpp
        ${PROJECT_SOURCE_DIR}/src/port/scene_lighting_disk.cpp)
    target_include_directories(${scene_lighting_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${scene_lighting_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${scene_lighting_variant} COMMAND ${scene_lighting_variant}_test)
endforeach()
target_compile_definitions(scene_lighting_disk_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# Stage C3D layout, bounds, payload decode and mesh range check. The forced
# variant proves the swap path on the little-endian host.
foreach(c3d_stage_variant c3d_stage c3d_stage_be)
    add_executable(${c3d_stage_variant}_test c3d_stage.cpp
        ${PROJECT_SOURCE_DIR}/src/port/c3d_stage.cpp)
    target_include_directories(${c3d_stage_variant}_test PRIVATE
        ${PROJECT_SOURCE_DIR}/game
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/port/compat)
    target_compile_options(${c3d_stage_variant}_test PRIVATE
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
        "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
        -fms-extensions)
    add_test(NAME ${c3d_stage_variant} COMMAND ${c3d_stage_variant}_test)
endforeach()
target_compile_definitions(c3d_stage_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)
add_executable(outline_font_test outline_font.cpp)
target_include_directories(outline_font_test PRIVATE ${PROJECT_BINARY_DIR}/src/generated ${PROJECT_SOURCE_DIR}/third_party/stb)
target_link_libraries(outline_font_test PRIVATE opencmr2_platform)
add_test(NAME outline_font COMMAND outline_font_test)
set_tests_properties(outline_font PROPERTIES TIMEOUT 30)


add_executable(saved_game_test saved_game.cpp)
target_link_libraries(saved_game_test PRIVATE opencmr2_platform)
add_test(NAME saved_game COMMAND saved_game_test)
set_tests_properties(saved_game PROPERTIES TIMEOUT 30)

add_executable(bink_decode bink_decode.cpp)
target_link_libraries(bink_decode PRIVATE opencmr2_bink)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME reference_capture_format
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/reference_capture.py)
    add_test(NAME raw_access_audit
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/raw_access_audit.py)
    add_test(NAME portability_source_guard
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/portability_source_guard.py)
endif()

set(OPENCMR2_BINK_TEST_DATA_DIR "" CACHE PATH "Optional original game data for movie and physics regression tests")
if(OPENCMR2_BINK_TEST_DATA_DIR)
    if(Python3_Interpreter_FOUND AND TARGET stage_probe)
        add_test(NAME physics_original_parity COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/physics_parity.py --probe $<TARGET_FILE:stage_probe>
            --data ${OPENCMR2_BINK_TEST_DATA_DIR})
        add_test(NAME runtime_scenarios COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/runtime_scenarios.py --probe $<TARGET_FILE:stage_probe>
            --data ${OPENCMR2_BINK_TEST_DATA_DIR})
        set_tests_properties(runtime_scenarios PROPERTIES TIMEOUT 300)
        add_test(NAME cpu_runs COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/runtime_scenarios.py --probe $<TARGET_FILE:stage_probe>
            --data ${OPENCMR2_BINK_TEST_DATA_DIR} --cpu-runs)
        set_tests_properties(cpu_runs PROPERTIES TIMEOUT 900)
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME lan_game COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/lan_game.py --host-probe $<TARGET_FILE:stage_probe>
                --data ${OPENCMR2_BINK_TEST_DATA_DIR})
            set_tests_properties(lan_game PROPERTIES TIMEOUT 120)
            # Cars from 2 have no view or event slot of their own.
            add_test(NAME lan_game_3 COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/lan_game.py --host-probe $<TARGET_FILE:stage_probe>
                --data ${OPENCMR2_BINK_TEST_DATA_DIR} --players 3)
            set_tests_properties(lan_game_3 PROPERTIES TIMEOUT 150)
        endif()
        add_test(NAME stage_lighting COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/stage_lighting.py --probe $<TARGET_FILE:stage_probe>
            --data ${OPENCMR2_BINK_TEST_DATA_DIR})
        set_tests_properties(stage_lighting PROPERTIES TIMEOUT 300)
        # Rally stage 3 has the residue-3, two-panel weather map boundary.
        add_test(NAME stage_entry_boundary COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/stage_boundary.py --probe $<TARGET_FILE:stage_probe>
            --data ${OPENCMR2_BINK_TEST_DATA_DIR})
        set_tests_properties(stage_entry_boundary PROPERTIES TIMEOUT 120)
    endif()
    # bink_decoder also checks the changed-macroblock map against cm.bik.
    set_tests_properties(bink_decoder yuv_rgba PROPERTIES
        ENVIRONMENT "OPENCMR2_BINK_TEST_DATA_DIR=${OPENCMR2_BINK_TEST_DATA_DIR}")
    file(GLOB_RECURSE BINK_TEST_FILES "${OPENCMR2_BINK_TEST_DATA_DIR}/*.bik" "${OPENCMR2_BINK_TEST_DATA_DIR}/*.BIK")
    foreach(movie IN LISTS BINK_TEST_FILES)
        get_filename_component(name "${movie}" NAME_WE)
        add_test(NAME "bink_file_${name}" COMMAND bink_decode "${movie}")
    endforeach()
endif()

add_executable(replay_disk_test replay_disk.cpp)
target_link_libraries(replay_disk_test PRIVATE opencmr2_platform)
add_test(NAME replay_disk COMMAND replay_disk_test)

# M0.7 replay payload: lane events/samples/race records and the type-2
# sample bitfields. The normal build checks little-endian file bytes; the
# forced build proves the swap path on the little-endian host.
add_executable(replay_payload_test replay_payload.cpp
    ${PROJECT_SOURCE_DIR}/src/port/replay_disk.cpp
    ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
target_include_directories(replay_payload_test PRIVATE
    ${PROJECT_SOURCE_DIR}/game
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_options(replay_payload_test PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
    -fms-extensions)
add_test(NAME replay_payload COMMAND replay_payload_test)

add_executable(replay_payload_be_test replay_payload.cpp
    ${PROJECT_SOURCE_DIR}/src/port/replay_disk.cpp
    ${PROJECT_SOURCE_DIR}/src/port/disk.cpp)
target_include_directories(replay_payload_be_test PRIVATE
    ${PROJECT_SOURCE_DIR}/game
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/port/compat)
target_compile_options(replay_payload_be_test PRIVATE
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h"
    "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/msvcrt.h"
    -fms-extensions)
target_compile_definitions(replay_payload_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)
add_test(NAME replay_payload_be COMMAND replay_payload_be_test)

add_executable(replay_events_test replay_events.cpp)
target_link_libraries(replay_events_test PRIVATE opencmr2_platform)
add_test(NAME replay_events COMMAND replay_events_test)

add_executable(sector_culling_test sector_culling.cpp)
target_link_libraries(sector_culling_test PRIVATE opencmr2_platform)
add_test(NAME sector_culling COMMAND sector_culling_test)

add_executable(sector_geometry_test sector_geometry.cpp)
target_link_libraries(sector_geometry_test PRIVATE opencmr2_platform)
add_test(NAME sector_geometry COMMAND sector_geometry_test)

# Bump maps are created in the renderer's bump format (SelectTextureFormats).
add_executable(bump_format_test bump_format.cpp)
target_link_libraries(bump_format_test PRIVATE opencmr2_platform)
add_test(NAME bump_format COMMAND bump_format_test)

add_executable(headlight_collision_test headlight_collision.cpp)
target_link_libraries(headlight_collision_test PRIVATE opencmr2_platform)
add_test(NAME headlight_collision COMMAND headlight_collision_test)

add_executable(credits_text_test credits_text.cpp
    ${PROJECT_SOURCE_DIR}/src/port/credits_text.cpp)
target_include_directories(credits_text_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME credits_text COMMAND credits_text_test)

add_library(high_address_fixture STATIC high_address.c)
add_executable(net_payload_game_test net_payload_game.cpp)
target_link_libraries(net_payload_game_test PRIVATE opencmr2_platform)
add_test(NAME net_payload_game COMMAND net_payload_game_test)
set_tests_properties(net_payload_game PROPERTIES TIMEOUT 30)

add_executable(net_frontend_test net_frontend.cpp)
target_link_libraries(net_frontend_test PRIVATE opencmr2_platform high_address_fixture)
add_test(NAME net_frontend COMMAND net_frontend_test)
set_tests_properties(net_frontend PROPERTIES TIMEOUT 30)
target_link_libraries(headlight_collision_test PRIVATE high_address_fixture)
target_link_libraries(sector_culling_test PRIVATE high_address_fixture)

if(WIN32)
    # Windows searches beside each test executable for SDL3.dll. Copy once
    # before linking the tests, including when only stage_probe is requested.
    add_custom_target(opencmr2_test_runtime
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3-shared> ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS SDL3::SDL3-shared)
    get_property(test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS test_targets)
        get_target_property(kind ${target} TYPE)
        if(kind STREQUAL "EXECUTABLE")
            add_dependencies(${target} opencmr2_test_runtime)
        endif()
    endforeach()
endif()

# Bounded C3D word conversion for the .xhi member extent.
foreach(c3d_payload_bound_variant c3d_payload_bound c3d_payload_bound_be)
    add_executable(${c3d_payload_bound_variant}_test c3d_payload_bound.cpp)
    target_include_directories(${c3d_payload_bound_variant}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    add_test(NAME ${c3d_payload_bound_variant} COMMAND ${c3d_payload_bound_variant}_test)
endforeach()
target_compile_definitions(c3d_payload_bound_be_test PRIVATE OPENCMR2_TEST_FORCE_BIG_ENDIAN)

# [video] renderer = minigl | gles2: ini parsing and the fall-back decision.
add_executable(gfx_renderer_test gfx_renderer.cpp)
target_include_directories(gfx_renderer_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME gfx_renderer COMMAND gfx_renderer_test)

# src/render/gfx_api.inc (the Amiga dispatcher's X-macro list) matches port/gfx.h.
add_executable(gfx_api_list_test gfx_api_list.cpp)
target_compile_definitions(gfx_api_list_test PRIVATE SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME gfx_api_list COMMAND gfx_api_list_test)

# Byte-wise texel conversions for the GLES2 renderer (port/gfx_texel.h).
add_executable(gfx_texel_test gfx_texel.cpp)
target_include_directories(gfx_texel_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME gfx_texel COMMAND gfx_texel_test)

# Packing of a recorded draw's state into the GLES2 shaders' vec4 arrays.
add_executable(gles2_uniforms_test gles2_uniforms.cpp)
target_include_directories(gles2_uniforms_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME gles2_uniforms COMMAND gles2_uniforms_test)

# Specialised fragment shaders of the GLES2 renderer: the key and the text.
add_executable(gles2_fragment_test gles2_fragment.cpp)
target_include_directories(gles2_fragment_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME gles2_fragment COMMAND gles2_fragment_test)

# Draw batching for the GLES2 executor (2D/TL consecutive + 3D opaque buckets).
add_executable(gles2_batch_test gles2_batch.cpp)
target_include_directories(gles2_batch_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME gles2_batch COMMAND gles2_batch_test)

# gfx_core's recorded states: no-op setters and GfxDrawState::vertexSerial
# (which the GLES2 executor trusts instead of comparing states).
add_executable(gfx_core_serial_test gfx_core_serial.cpp
    ${PROJECT_SOURCE_DIR}/src/diagnostics/observer.cpp ${PROJECT_SOURCE_DIR}/src/render/gfx_core.cpp)
target_include_directories(gfx_core_serial_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gfx_core_serial_test PRIVATE SDL3::SDL3)
add_test(NAME gfx_core_serial COMMAND gfx_core_serial_test)

# [video] cube_faces_per_frame: one cube face per pass, in turn.
add_executable(gfx_core_cube_test gfx_core_cube.cpp
    ${PROJECT_SOURCE_DIR}/src/diagnostics/observer.cpp ${PROJECT_SOURCE_DIR}/src/render/gfx_core.cpp)
target_include_directories(gfx_core_cube_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(gfx_core_cube_test PRIVATE SDL3::SDL3)
add_test(NAME gfx_core_cube COMMAND gfx_core_cube_test)
