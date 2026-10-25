/*
 * Link-time check for the M1 Amiga stubs.
 *
 * The stub object library calls Gfx_Clear/Gfx_Present and Sys_Log, which the
 * M1 renderer and system services will provide. Until then this throwaway
 * program supplies those three symbols and a main, so the target linker must
 * resolve the stubs' C++ runtime (new/delete) today. It is not staged.
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
