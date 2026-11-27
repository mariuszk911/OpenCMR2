/*
 * MiniGL 29.1 is a hard pin. MiniGLOpen() checks dispatch->structSize against
 * sizeof(MGLDispatchTable), and 29.0 reports 836 bytes against this SDK's 840,
 * so a mismatched client fails silently on target. Reject the wrong SDK here.
 *
 * WarpOS uses the PPC MiniGL (API 29, dispatch ABI 5) with the same 29.1
 * table, 840 bytes: the R200 stack's and MiniGLWOS's DLLs both accept a
 * client built on it.
 */
#include <proto/minigl.h>

#if defined(OPENCMR2_WARPOS)
static_assert(MINIGL_DISPATCH_ABI_VERSION == 5,
              "PPC MiniGL is not dispatch ABI 5");
static_assert(sizeof(MGLDispatchTable) == 840,
              "PPC MiniGL is not API 29 / ABI 5: MGLDispatchTable must be 840 bytes");
static_assert(GL_COLOR_BUFFER_BIT == 0x4000 && GL_DEPTH_BUFFER_BIT == 0x100,
              "PPC MiniGL headers predate the 29.1 alignment (old glClear bits)");
#else
static_assert(sizeof(MGLDispatchTable) == 840,
              "MiniGL SDK is not 29.1: MGLDispatchTable must be 840 bytes");
#endif
