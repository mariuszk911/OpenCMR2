/*
 * ogles2.library client glue (Domain B, AmigaOS only).
 *
 * The SDK's libogles2.a is a hunk-format archive with a record type the
 * toolchain's GNU ld 2.35 cannot read, so this file replaces its import
 * library: OGLES2Open/OGLES2Close, the context globals the SDK's inline GL
 * wrappers in <proto/ogles2.h> expect (OGLES2Dispatch, OGLES2CurrentContext,
 * OGLES2Base) and the agl* calls. Every GL call then goes through the
 * dispatch table the library hands out at its LVO -30 (GetDispatchTable;
 * found by disassembling libogles2.a's lvo.o: move.l OGLES2Base,a6;
 * jsr -30(a6); the result in d0).
 *
 * Include <proto/ogles2.h> for the GL names; this header adds what the SDK's
 * import library offered beyond it and the diagnostics the renderer needs.
 */
#ifndef OPENCMR2_RENDER_GLES2_OGLES2_GLUE_H
#define OPENCMR2_RENDER_GLES2_OGLES2_GLUE_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Why the last OGLES2Open failed (static text), or "" after a success. */
const char *OGLES2OpenError(void);
/* Which file the open library came from: "ogles2.library",
   "PROGDIR:libs/ogles2.library" or "PROGDIR:ogles2.library"; "" when closed. */
const char *OGLES2OpenedFrom(void);

#ifdef __cplusplus
}
#endif

#endif
