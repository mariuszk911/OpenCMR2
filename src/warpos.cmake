# WarpOS (PowerPC) target. Included from src/CMakeLists.txt when WARPOS is set;
# GAME_SOURCES and GAME_QIFIST_UNITS are defined there. The platform layer is
# the 68k one (src/platform/amiga, Domain B) with WarpOS branches where the PPC
# differs (OPENCMR2_WARPOS), plus src/platform/warpos for what is PPC-only.

set(WARPOS_MINIGL "$ENV{HOME}/amiga/R200_MiniGL_PPC" CACHE PATH "PPC MiniGL tree (include/, lib/libminigl.a)")
foreach(required "${WARPOS_MINIGL}/include/proto/minigl.h" "${WARPOS_MINIGL}/lib/libminigl.a")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "OpenCMR2: missing ${required}; set WARPOS_MINIGL to the PPC MiniGL tree")
    endif()
endforeach()

list(REMOVE_ITEM GAME_SOURCES ${GAME_DIR}/CrtTypeInfo.cpp)

# ---- Domain A: game layout -------------------------------------------------
# No -malign-int: PPC GCC already aligns int and pointers to 4 like i386 (the
# layout probe, build/warpos/w1-gate.log); the forced includes are the 68k ones.
add_library(opencmr2_domain_a INTERFACE)
target_compile_options(opencmr2_domain_a INTERFACE
    $<$<COMPILE_LANGUAGE:C>:-x c>
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/types.h"
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/msvcrt.h"
    "$<$<COMPILE_LANGUAGE:CXX>:-include>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/port/menu_layout.h>")

include(${CMAKE_CURRENT_SOURCE_DIR}/amiga_domain_a.cmake)
add_library(cmr2_game OBJECT ${AMIGA_DOMAIN_A_SOURCES})
set_target_properties(cmr2_game PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_include_directories(cmr2_game PUBLIC
    ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
target_link_libraries(cmr2_game PRIVATE opencmr2_domain_a)
set_source_files_properties(${GAME_QIFIST_UNITS} TARGET_DIRECTORY cmr2_game
    PROPERTIES COMPILE_DEFINITIONS CMR2_QIFIST)
# GCC 11 rejects the DirectPlay HRESULT case labels above INT_MAX (GCC 6.5 and
# MSVC accept them); the conversion keeps the bit pattern, as on those.
set_source_files_properties(${GAME_DIR}/GameInfo.cpp TARGET_DIRECTORY cmr2_game
    PROPERTIES COMPILE_OPTIONS "-Wno-narrowing")

# ---- Domain B: OS and renderer glue ---------------------------------------
add_library(opencmr2_platform OBJECT
    platform/msvcrt.cpp
    platform/amiga/amiga_qsort.c
    platform/amiga/amiga_sys.cpp
    platform/amiga/amiga_platform.cpp
    platform/amiga/amiga_modes.c
    render/minigl/gfx_minigl.cpp
    render/gfx_dispatch_amiga.cpp
    input/input_amiga.cpp
    audio/audio_ahi.cpp
    audio/mixer.cpp
    audio/msadpcm.cpp
    video/movie_amiga.cpp
    port/intdiv.cpp
    platform/amiga/minigl_abi_check.cpp
    platform/warpos/wos_ahi.c
    platform/warpos/wos_net.cpp
    platform/warpos/wos_libitm.c
    platform/warpos/wos_diag.c
    platform/warpos/wos_clock.c
    platform/warpos/wos_pcprof.c
    platform/warpos/wos_libfix.c)
target_include_directories(opencmr2_platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
target_include_directories(opencmr2_platform PRIVATE ${WARPOS_MINIGL}/include)

add_library(opencmr2_amiga_stubs OBJECT
    platform/amiga/amiga_diagnostics.cpp
    render/amiga_enhancement_stubs.cpp)
target_include_directories(opencmr2_amiga_stubs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)

add_library(opencmr2_bink STATIC video/bink_decoder.cpp video/yuv_rgba.cpp)
target_include_directories(opencmr2_bink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The SDK's libstdc++ without its COMDAT groups (see the toolchain file).
set(WARPOS_LIBSTDCXX "${CMAKE_BINARY_DIR}/libstdc++_ungrouped.a")
add_custom_command(OUTPUT ${WARPOS_LIBSTDCXX}
    COMMAND ${CMAKE_OBJCOPY} -R .group ${WOS_SDK_ROOT}/lib/libstdc++.a ${WARPOS_LIBSTDCXX}
    DEPENDS ${WOS_SDK_ROOT}/lib/libstdc++.a
    COMMENT "libstdc++ without COMDAT groups")
add_custom_target(opencmr2_libstdcxx DEPENDS ${WARPOS_LIBSTDCXX})

# ---- Executable ------------------------------------------------------------
# opencmr2.debug.elf: the relocatable link with debug information, for
# addr2line. opencmr2.warpelf: the same without it, checked against the
# WarpOS contract. opencmr2: the hunk executable.
add_executable(opencmr2
    platform/amiga/main_amiga.cpp
    $<TARGET_OBJECTS:cmr2_game>
    $<TARGET_OBJECTS:opencmr2_platform>
    $<TARGET_OBJECTS:opencmr2_amiga_stubs>)
target_link_libraries(opencmr2 PRIVATE opencmr2_domain_a)
set_target_properties(opencmr2 PROPERTIES SUFFIX ".debug.elf")
target_include_directories(opencmr2 PRIVATE
    ${GAME_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/port/compat)
add_dependencies(opencmr2 opencmr2_libstdcxx)
# The link rule (toolchain file) takes these as plain file arguments.
target_link_libraries(opencmr2 PRIVATE
    $<TARGET_FILE:opencmr2_bink> ${WARPOS_MINIGL}/lib/libminigl.a ${WARPOS_LIBSTDCXX})
add_dependencies(opencmr2 opencmr2_bink)

set(WARPOS_STACK "2097152" CACHE STRING "Stack the converter's 68k stub swaps in (bytes)")
add_custom_command(TARGET opencmr2 POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} --strip-debug $<TARGET_FILE:opencmr2> ${CMAKE_BINARY_DIR}/opencmr2.warpelf
    COMMAND sh ${PROJECT_SOURCE_DIR}/tools/warpos/check_contract.sh
            ${WOS_TOOLCHAIN_BIN}/ppc-amigaos- ${CMAKE_BINARY_DIR}/opencmr2.warpelf
    COMMAND python3 ${PROJECT_SOURCE_DIR}/tools/warpos/elf2warpos/elf2warpos.py --stack ${WARPOS_STACK}
            ${CMAKE_BINARY_DIR}/opencmr2.warpelf ${CMAKE_BINARY_DIR}/opencmr2
    COMMENT "WarpOS contract check and hunk conversion")
