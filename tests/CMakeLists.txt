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

# M1 stubs: a black movie frame, an unavailable network and no-op Amiga
# profiling. The Amiga target compiles the same translation units.
add_executable(amiga_stubs_test amiga_stubs.cpp
    ${PROJECT_SOURCE_DIR}/src/video/movie_stub.cpp
    ${PROJECT_SOURCE_DIR}/src/net/net_stub.cpp
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

add_executable(amiga_paths_test amiga_paths.cpp)
target_include_directories(amiga_paths_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_paths COMMAND amiga_paths_test)

# opencmr2.ini parsing and line-preserving rewriting for the Amiga Sys layer.
add_executable(amiga_settings_test amiga_settings.cpp)
target_include_directories(amiga_settings_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME amiga_settings COMMAND amiga_settings_test)

add_executable(net_session_test net_session.cpp)
target_link_libraries(net_session_test PRIVATE opencmr2_net)
target_compile_options(net_session_test PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/src/port/types.h")
add_test(NAME net_session COMMAND net_session_test)
set_tests_properties(net_session PROPERTIES TIMEOUT 60)

add_executable(bink_decoder_test bink_decoder.cpp)
target_link_libraries(bink_decoder_test PRIVATE opencmr2_bink)
add_test(NAME bink_decoder COMMAND bink_decoder_test)

add_executable(movie_playback_test movie_playback.cpp ${PROJECT_SOURCE_DIR}/src/video/movie.cpp)
target_link_libraries(movie_playback_test PRIVATE opencmr2_bink)
add_test(NAME movie_playback COMMAND movie_playback_test)

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
add_executable(gamepad_layout_test gamepad_layout.cpp)
target_include_directories(gamepad_layout_test PRIVATE ${PROJECT_SOURCE_DIR}/game)
target_link_libraries(gamepad_layout_test PRIVATE opencmr2_platform)
add_test(NAME gamepad_layout COMMAND gamepad_layout_test)

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
        set_tests_properties(runtime_scenarios PROPERTIES TIMEOUT 180)
        add_test(NAME cpu_runs COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/runtime_scenarios.py --probe $<TARGET_FILE:stage_probe>
            --data ${OPENCMR2_BINK_TEST_DATA_DIR} --cpu-runs)
        set_tests_properties(cpu_runs PROPERTIES TIMEOUT 900)
        if(NOT CMAKE_CROSSCOMPILING)
            add_test(NAME lan_game COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/lan_game.py --host-probe $<TARGET_FILE:stage_probe>
                --data ${OPENCMR2_BINK_TEST_DATA_DIR})
            set_tests_properties(lan_game PROPERTIES TIMEOUT 120)
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

add_executable(headlight_collision_test headlight_collision.cpp)
target_link_libraries(headlight_collision_test PRIVATE opencmr2_platform)
add_test(NAME headlight_collision COMMAND headlight_collision_test)

add_executable(credits_text_test credits_text.cpp
    ${PROJECT_SOURCE_DIR}/src/port/credits_text.cpp)
target_include_directories(credits_text_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
add_test(NAME credits_text COMMAND credits_text_test)

add_library(high_address_fixture STATIC high_address.c)
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
