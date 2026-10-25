// One-frame black stand-in for movie.h on the AmigaOS target.
//
// Bink is out of scope for v1. GameInfo.cpp retries Movie_Open in a loop with
// a "no CD" requester, so a failing stub would block boot; this one always
// succeeds and plays a single black frame, which lets the intro loop finish
// and the menus start.
#include "port/gfx.h"
#include "port/movie.h"
#include "port/sys.h"

#include <new>

struct Movie {
    DWORD frame = 0;
};

namespace {
const DWORD kStubWidth = 640;
const DWORD kStubHeight = 480;
const DWORD kStubFrames = 1;
}

extern "C" Movie *Movie_Open(const char *path, int audioTrack)
{
    (void)audioTrack;
    Movie *movie = new (std::nothrow) Movie;
    if (movie)
        Sys_Log("movie %s: stub, one black frame", path ? path : "(null)");
    return movie;
}

extern "C" void Movie_Close(Movie *movie)
{
    delete movie;
}

extern "C" DWORD Movie_GetWidth(Movie *movie)
{
    (void)movie;
    return kStubWidth;
}

extern "C" DWORD Movie_GetHeight(Movie *movie)
{
    (void)movie;
    return kStubHeight;
}

extern "C" DWORD Movie_GetFrameNumber(Movie *movie)
{
    // Bink frame numbers count from 1.
    return movie->frame + 1;
}

extern "C" DWORD Movie_GetFrameCount(Movie *movie)
{
    (void)movie;
    return kStubFrames;
}

extern "C" void Movie_DecodeFrame(Movie *movie)
{
    (void)movie;
}

extern "C" void Movie_NextFrame(Movie *movie)
{
    if (movie->frame < kStubFrames)
        ++movie->frame;
}

extern "C" BOOL Movie_Wait(Movie *movie)
{
    (void)movie;
    // Never wait: the single frame is shown and the intro ends immediately.
    return FALSE;
}

extern "C" void Movie_Present(Movie *movie, int x, int y, int width, int height)
{
    (void)movie;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    Gfx_Clear(GFX_CLEAR_TARGET | GFX_CLEAR_ZBUFFER, 0xff000000, 1.0f);
    Gfx_Present(TRUE);
}
