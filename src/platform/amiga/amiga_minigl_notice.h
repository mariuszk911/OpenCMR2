/*
 * What the player is told when the MiniGL renderer cannot start. The release
 * does not ship minigl.library, so a missing or old library is the likely
 * cause. Pure text selection, so the wording and the version rule are
 * host-testable; the requester itself is Sys_MessageBox. Lines are short
 * enough for the 400-pixel requester.
 */
#ifndef OPENCMR2_AMIGA_MINIGL_NOTICE_H
#define OPENCMR2_AMIGA_MINIGL_NOTICE_H

#include <stdio.h>

/* The client ABI needs 29.1 (29.0's dispatch table is one slot short), but the
   supported runtime is 30.0: the 29.1 library can stall in its present path
   right after the first picture. */
enum {
    AMIGA_MINIGL_MIN_VERSION = 30,
    AMIGA_MINIGL_MIN_REVISION = 0
};

static inline int AmigaMiniGLNotice_IsTooOld(unsigned version, unsigned revision)
{
    return version < AMIGA_MINIGL_MIN_VERSION ||
           (version == AMIGA_MINIGL_MIN_VERSION && revision < AMIGA_MINIGL_MIN_REVISION);
}

/*
 * Writes the requester text for a missing or unusable minigl.library into
 * text. present is 0 when OpenLibrary() could not find it; otherwise version
 * and revision are the library's lib_Version and lib_Revision.
 */
static inline void AmigaMiniGLNotice_Text(char *text, size_t size, int present,
                                          unsigned version, unsigned revision)
{
    if (!present) {
        snprintf(text, size,
                 "minigl.library was not found.\n"
                 "OpenCMR2 needs minigl.library 30.0\n"
                 "or newer in LIBS:. PiStorm3D is on\n"
                 "Aminet util/libs/PiStorm3D.\n"
                 "It is not included with the game.");
    } else if (AmigaMiniGLNotice_IsTooOld(version, revision)) {
        snprintf(text, size,
                 "minigl.library %u.%u is too old.\n"
                 "OpenCMR2 needs minigl.library 30.0\n"
                 "or newer in LIBS:. PiStorm3D is on\n"
                 "Aminet util/libs/PiStorm3D.",
                 version, revision);
    } else {
        snprintf(text, size,
                 "minigl.library %u.%u could not be\n"
                 "started. OpenCMR2 needs a PiStorm\n"
                 "running Emu68 with its 3D support.",
                 version, revision);
    }
}

#endif
