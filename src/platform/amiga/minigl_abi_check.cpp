/*
 * MiniGL 29.1 is a hard pin. MiniGLOpen() checks dispatch->structSize against
 * sizeof(MGLDispatchTable), and 29.0 reports 836 bytes against this SDK's 840,
 * so a mismatched client fails silently on target. Reject the wrong SDK here.
 */
#include <proto/minigl.h>

static_assert(sizeof(MGLDispatchTable) == 840,
              "MiniGL SDK is not 29.1: MGLDispatchTable must be 840 bytes");
