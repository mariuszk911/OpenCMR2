/*
 * The fullscreen resolutions for the MiniGL renderer, from the display
 * database. MiniGL's vid_OpenDisplay opens a screen only in a PIXFMT_BGRA32
 * cybergraphics mode of exactly the requested size, so the list offers those
 * sizes and nothing else.
 */
#include "platform/amiga/amiga_modes.h"
#include "port/sys.h"

#include "platform/amiga/amiga_os_begin.h"
#include <exec/types.h>

/* Private bases, opened for the walk only. Declared static before the proto/
 * headers so their extern declarations keep internal linkage (as in
 * render/gles2/ogles2_display.c); the inline stubs use these names. */
struct GfxBase;
struct Library;
static struct GfxBase *GfxBase;
static struct Library *CyberGfxBase;

#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include "platform/amiga/amiga_os_end.h"

int AmigaModes_ListBGRA32(DisplayModeSize *list, int capacity)
{
    ULONG modeId;
    int count = 0;
    int seen = 0;

    if (list == NULL || capacity <= 0)
        return 0;
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (GfxBase == NULL || CyberGfxBase == NULL) {
        Sys_Log("gfx: modes: %s missing; using the fixed list",
                GfxBase == NULL ? "graphics.library 39" : "cybergraphics.library 40");
    } else {
        for (modeId = NextDisplayInfo(INVALID_ID); modeId != INVALID_ID; modeId = NextDisplayInfo(modeId)) {
            if (!IsCyberModeID(modeId))
                continue;
            if (GetCyberIDAttr(CYBRIDATTR_PIXFMT, modeId) != PIXFMT_BGRA32)
                continue;
            ++seen;
            count = DisplayModes_Add(list, count, capacity, (int)GetCyberIDAttr(CYBRIDATTR_WIDTH, modeId),
                                     (int)GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeId));
        }
        Sys_Log("gfx: modes: %d BGRA32 modes, %d sizes of at least 640x480", seen, count);
    }
    if (CyberGfxBase != NULL)
        CloseLibrary(CyberGfxBase);
    if (GfxBase != NULL)
        CloseLibrary((struct Library *)GfxBase);
    CyberGfxBase = NULL;
    GfxBase = NULL;
    return count;
}
