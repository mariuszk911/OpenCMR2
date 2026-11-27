# WarpOS (PowerPC Amiga) cross build: ppc-amigaos-gcc 11 with the MOS2WOS SDK.
#
# Only tools/warpos/build.sh uses this file. The compiler targets AmigaOS 4;
# its SysV code generator and ELF binutils suit WarpOS once the OS 4 defaults
# are replaced by the MOS2WOS headers and runtime. The link is relocatable
# (ld -r) and tools/warpos/elf2warpos converts it into the hunk executable.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

set(WOS_TOOLCHAIN_BIN "/opt/ppc-amigaos/bin" CACHE PATH "ppc-amigaos-gcc binaries")
set(MOS2WOS_ROOT "$ENV{HOME}/amiga/toolchains/mos2wos/gg" CACHE PATH "MOS2WOS SDK root (ppc-warpos/ inside)")
set(WOS_SDK_ROOT "${MOS2WOS_ROOT}/ppc-warpos")

set(CMAKE_C_COMPILER "${WOS_TOOLCHAIN_BIN}/ppc-amigaos-gcc")
set(CMAKE_CXX_COMPILER "${WOS_TOOLCHAIN_BIN}/ppc-amigaos-g++")
set(CMAKE_LINKER "${WOS_TOOLCHAIN_BIN}/ppc-amigaos-ld")
set(CMAKE_OBJCOPY "${WOS_TOOLCHAIN_BIN}/ppc-amigaos-objcopy")
set(CMAKE_AR "${WOS_TOOLCHAIN_BIN}/ppc-amigaos-ar")
set(CMAKE_RANLIB "${WOS_TOOLCHAIN_BIN}/ppc-amigaos-ranlib")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(WARPOS ON CACHE BOOL "Build the WarpOS PowerPC target" FORCE)

# 603e code runs on every WarpOS board; scheduling is for the 7410 we test on.
set(OPENCMR2_WARPOS_CPU "603e" CACHE STRING "PowerPC CPU selected with -mcpu")
set(OPENCMR2_WARPOS_TUNE "7400" CACHE STRING "PowerPC CPU selected with -mtune")
set(OPENCMR2_WARPOS_OPT "-O2" CACHE STRING "Optimisation flags for the WarpOS target")

# -fno-jump-tables, no PIC, no unwind tables: the converter takes only
# ADDR32/ADDR16_LO/ADDR16_HA/REL24. -msdata=none -G0: no small-data
# relocations. -fno-common: the contract refuses COMMON symbols. -U...: the
# compiler's OS 4 and "amiga" predefinitions. The SDK's SDL 1.2 directory is
# left off the include path on purpose.
# A G4 build (tools/warpos/build.sh --g4, -mcpu=7400) schedules and selects
# instructions for the 7400/7410 but generates no vector code: WarpOS starts
# a task with a 4-byte aligned stack and the hunk loader aligns sections to
# 8 bytes, so compiler-generated AltiVec spills and constants would load the
# wrong 16 bytes. Vector code is written by hand and entered on an aligned
# stack, or not at all.
if(OPENCMR2_WARPOS_CPU MATCHES "^74")
    set(_wos_vector "-mno-altivec -mabi=no-altivec")
else()
    set(_wos_vector "")
endif()
set(_wos_arch "-mcpu=${OPENCMR2_WARPOS_CPU} -mtune=${OPENCMR2_WARPOS_TUNE} ${_wos_vector} -mcall-sysv -mbig-endian -mmultiple -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-jump-tables -fno-common -msdata=none -G0")
set(_wos_defs "-U__AMIGAOS4__ -U__AMIGAOS4 -UAMIGAOS4 -U__amigaos4__ -U__amigaos4 -Uamigaos4 -U__NEWLIB__ -Uamiga -Uamigaos -D__PPC__ -DWARPUP -DNEWLIB")
set(_wos_c_inc "-nostdinc -isystem ${WOS_SDK_ROOT}/newlib/include -isystem ${WOS_SDK_ROOT}/os-includeppc -isystem ${WOS_SDK_ROOT}/include -isystem ${WOS_SDK_ROOT}/netinclude -isystem ${WOS_SDK_ROOT}/gcc-include")
set(_wos_cxx_inc "-nostdinc -isystem ${WOS_SDK_ROOT}/newlib/include/c++ -isystem ${WOS_SDK_ROOT}/newlib/include/c++/ppc-morphos -isystem ${WOS_SDK_ROOT}/newlib/include -isystem ${WOS_SDK_ROOT}/os-includeppc -isystem ${WOS_SDK_ROOT}/include -isystem ${WOS_SDK_ROOT}/netinclude -isystem ${WOS_SDK_ROOT}/gcc-include")
# -g: the debug information stays in opencmr2.debug.elf (addr2line); the
# converter gets the link without it.
set(CMAKE_C_FLAGS_INIT "${_wos_arch} ${_wos_defs} ${_wos_c_inc} -g ${OPENCMR2_WARPOS_OPT}")
# -fno-use-cxa-atexit: startupwos runs the static destructors from .dtors.
set(CMAKE_CXX_FLAGS_INIT "${_wos_arch} ${_wos_defs} ${_wos_cxx_inc} -fno-use-cxa-atexit -g ${OPENCMR2_WARPOS_OPT}")

# Every C++ object loses its COMDAT .group sections, or the relocatable link
# keeps one section per inline function instead of merging them into .text.
set(CMAKE_CXX_COMPILE_OBJECT
    "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE> && ${CMAKE_OBJCOPY} -R .group <OBJECT>")

# startupwos.o FIRST: the loader jumps to the start of .text. end.o last.
# <LINK_LIBRARIES> carries the ungrouped libstdc++ (src/warpos.cmake).
set(_wos_runtime "-L${WOS_SDK_ROOT}/newlib/lib -L${WOS_SDK_ROOT}/lib --start-group -lamigawos -lgccwos -lm -lglosswos -lcwos -lglosswos -lgccwos --end-group")
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_LINKER> -r -EB -d -x -T ${CMAKE_CURRENT_LIST_DIR}/../tools/warpos/warpos-etrel.ld -o <TARGET> ${WOS_SDK_ROOT}/lib/startupwos.o <OBJECTS> <LINK_LIBRARIES> ${_wos_runtime} ${WOS_SDK_ROOT}/lib/end.o")
set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE}")
