/*
 * Link-time check for the Amiga stubs.
 *
 * Supplies the Gfx_Clear/Gfx_Present and Sys_Log symbols the stub object
 * library calls, plus a main, so the target linker must resolve the stubs' C++
 * runtime (new/delete) without the game. It is not staged.
 */
#include "port/gfx.h"
#include "port/sys.h"

void Gfx_Clear(uint32_t flags, uint32_t color, float z)
{
    (void)flags;
    (void)color;
    (void)z;
}

void Gfx_Present(int vsync)
{
    (void)vsync;
}

void Sys_Log(const char *format, ...)
{
    (void)format;
}

int main(void)
{
    return 0;
}
