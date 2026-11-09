/*
 * Display mode choice for the ogles2 renderer: see ogles2_display.h.
 *
 * The library bases are private to this file (__NOLIBBASE__), so it links
 * with or without the platform layer's own IntuitionBase.
 */
#define __NOLIBBASE__
#include "render/gles2/ogles2_display.h"

#include <exec/types.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>

#include <string.h>

extern struct ExecBase *SysBase; /* libnix */
static struct IntuitionBase *IntuitionBase;
static struct GfxBase *GfxBase;
static struct Library *CyberGfxBase;

static int is_32bit_cyber(ULONG modeId)
{
    return modeId != INVALID_ID && IsCyberModeID(modeId) &&
           GetCyberIDAttr(CYBRIDATTR_DEPTH, modeId) == 32;
}

static void fill(Ogles2Mode *out, ULONG modeId)
{
    out->modeId = modeId;
    out->width = GetCyberIDAttr(CYBRIDATTR_WIDTH, modeId);
    out->height = GetCyberIDAttr(CYBRIDATTR_HEIGHT, modeId);
    out->depth = 32;
}

BOOL Ogles2FindMode(ULONG wantWidth, ULONG wantHeight, Ogles2Mode *out, const char **why)
{
    BOOL found = FALSE;
    const char *reason = "";
    ULONG workbench = INVALID_ID;

    memset(out, 0, sizeof(*out));
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (IntuitionBase == NULL || GfxBase == NULL) {
        reason = "intuition.library or graphics.library 39 is missing";
    } else if (CyberGfxBase == NULL) {
        reason = "cybergraphics.library is not available (no RTG system)";
    } else {
        struct Screen *screen = LockPubScreen(NULL);
        if (screen != NULL) {
            workbench = GetVPModeID(&screen->ViewPort);
            UnlockPubScreen(NULL, screen);
        }
        if (is_32bit_cyber(workbench)) {
            fill(out, workbench);
            found = TRUE;
        } else {
            struct TagItem tags[4];
            ULONG best;
            tags[0].ti_Tag = CYBRBIDTG_Depth;
            tags[0].ti_Data = 32;
            tags[1].ti_Tag = CYBRBIDTG_NominalWidth;
            tags[1].ti_Data = wantWidth != 0 ? wantWidth : 640;
            tags[2].ti_Tag = CYBRBIDTG_NominalHeight;
            tags[2].ti_Data = wantHeight != 0 ? wantHeight : 480;
            tags[3].ti_Tag = TAG_DONE;
            tags[3].ti_Data = 0;
            best = BestCModeIDTagList((struct TagItem *)tags);
            if (is_32bit_cyber(best)) {
                fill(out, best);
                found = TRUE;
            } else {
                reason = "no 32-bit RTG screen mode (the Workbench screen is not one)";
            }
        }
    }
    if (CyberGfxBase != NULL)
        CloseLibrary(CyberGfxBase);
    if (GfxBase != NULL)
        CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase != NULL)
        CloseLibrary((struct Library *)IntuitionBase);
    CyberGfxBase = NULL;
    GfxBase = NULL;
    IntuitionBase = NULL;
    if (why != NULL)
        *why = reason;
    return found;
}
