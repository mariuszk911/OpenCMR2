# AmigaOS 68k cross build: Bebbo GCC 6.5, libnix nix20.
#
# Only tools/amiga/build.sh uses this file. Host presets stay on Clang and
# C++17, and CMakePresets.json is untouched. Domain-specific flags (Domain A's
# -malign-int) belong in src/amiga.cmake, never here.
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
set(OPENCMR2_AMIGA_STACK "2097152" CACHE STRING "Amiga stack size swapped in by libnix swapstack.o")

# Shared by both compilation domains. -fwrapv, -fno-strict-aliasing,
# -fsigned-char and -ffp-contract=off come from the top-level file for every
# target. Match the working reVC AmigaOS port: plain hard-float, no
# -ffloat-store/-fexcess-precision; only suppress the builtin *f math names so
# the libnix *f overrides are not rewritten.
# -fno-ivopts: GCC 6.5's induction-variable optimisation miscompiles some
# loops on 68k. In StageObject_InterpolateSecondaryRampRecords it loaded a
# field with (a0)+ and addressed the next one from the incremented register
# with the pre-increment offset, reading drawBlend (0x1c) for previousBlend
# (0x18); the weather lighting blend then diverged (TODO.md, Kenya stage 3).
set(OPENCMR2_AMIGA_COMMON_FLAGS "-m${OPENCMR2_AMIGA_CPU} -m${OPENCMR2_AMIGA_FPU} -mhard-float -mcrt=nix20 -fno-omit-frame-pointer -fno-ivopts")
set(CMAKE_C_FLAGS_INIT "${OPENCMR2_AMIGA_COMMON_FLAGS} -fno-builtin-sin -fno-builtin-cos -fno-builtin-sinf -fno-builtin-cosf ${OPENCMR2_AMIGA_OPT}")
set(CMAKE_CXX_FLAGS_INIT "${OPENCMR2_AMIGA_COMMON_FLAGS} -fno-builtin-sin -fno-builtin-cos -fno-builtin-sinf -fno-builtin-cosf ${OPENCMR2_AMIGA_OPT}")
