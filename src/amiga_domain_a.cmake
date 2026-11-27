# Domain A sources shared by the two Amiga targets (amiga.cmake, warpos.cmake):
# the game and the fork-owned codecs that read its files. Included after
# GAME_SOURCES is set (CrtTypeInfo.cpp already removed).
set(AMIGA_DOMAIN_A_SOURCES
    ${GAME_SOURCES}
    diagnostics/observer.cpp
    diagnostics/replay_trace.cpp
    port/ai_disk.cpp
    port/c3d_stage.cpp
    port/car_info_disk.cpp
    port/credits_text.cpp
    port/cod_disk.cpp
    port/controller_disk.cpp
    port/disk.cpp
    port/finish_record_disk.cpp
    port/font_pcf.cpp
    port/leaderboard_disk.cpp
    port/light_preset_disk.cpp
    port/member_decode.cpp
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
