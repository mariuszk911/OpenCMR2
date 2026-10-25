/*
 * OpenCMR2 entry point on AmigaOS.
 *
 * Domain A: it may include the game headers, but no NDK or MiniGL headers.
 * The OS work is behind AmigaPlatform_* and the port/Sys calls.
 */
#include "platform/amiga/amiga_platform.h"
#include "port/sys.h"
#include "diagnostics/timing.h"
#include "main.h"

int main(int argc, char **argv)
{
    if (!AmigaPlatform_Init(argc, argv)) {
        Sys_Log("platform: initialization failed");
        Sys_Exit(1);
    }
    Diagnostics::StartProfileFromEnvironment();
    const unsigned int code = CMain::Initialize(Sys_GetCommandLine());
    Diagnostics::StopProfile();
    AmigaPlatform_Shutdown();
    Sys_Exit((int)code);
    return 0;
}
