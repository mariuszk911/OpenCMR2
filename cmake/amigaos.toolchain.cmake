# AmigaOS 68k cross build: Bebbo GCC 6.5, libnix nix20.
#
# Only tools/amiga/build.sh uses this file. Host presets stay on Clang and
# C++17, and CMakePresets.json is untouched. Domain-specific flags (Domain A's
# -malign-int after the M0.5 gate) belong in src/amiga.cmake, never here.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR m68k)
set(CMAKE_C_COMPILER m68k-amigaos-gcc)
set(CMAKE_CXX_COMPILER m68k-amigaos-g++)

# Do not try to run target binaries during configuration.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(AMIGAOS ON CACHE BOOL "Build the native AmigaOS 68k target" FORCE)

set(OPENCMR2_AMIGA_CPU "68040" CACHE STRING "Amiga CPU selected with -m")
set(OPENCMR2_AMIGA_FPU "68881" CACHE STRING "Amiga FPU selected with -m")
set(OPENCMR2_AMIGA_OPT "-O2" CACHE STRING "Optimisation flags for the Amiga target")
set(OPENCMR2_AMIGA_STACK "262144" CACHE STRING "Initial Amiga stack size in bytes")

# Shared by both compilation domains. -fwrapv, -fno-strict-aliasing,
# -fsigned-char and -ffp-contract=off come from the top-level file for every
# target. GCC 6.5 implements -fexcess-precision=standard for C only; for C++
# it reports "sorry, unimplemented", so C++ uses -ffloat-store instead and
# M0.4 records whether it actually changes the generated code.
set(OPENCMR2_AMIGA_COMMON_FLAGS "-m${OPENCMR2_AMIGA_CPU} -m${OPENCMR2_AMIGA_FPU} -mhard-float -mcrt=nix20 -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_INIT "${OPENCMR2_AMIGA_COMMON_FLAGS} -fexcess-precision=standard -fno-builtin-sin -fno-builtin-cos ${OPENCMR2_AMIGA_OPT}")
set(CMAKE_CXX_FLAGS_INIT "${OPENCMR2_AMIGA_COMMON_FLAGS} -ffloat-store -fno-builtin-sin -fno-builtin-cos ${OPENCMR2_AMIGA_OPT}")
