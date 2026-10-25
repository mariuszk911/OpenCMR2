/*
 * M1 platform lifecycle smoke for AmigaOS. Run from a Shell:
 *
 *     DH20:amiga_platform_test
 *
 * DH20: must contain gamedata/ with the required files and platform-user/.
 * An emulator run is emulator evidence, not PiStorm evidence.
 */
#include "platform/amiga/amiga_platform.h"
#include "port/sys.h"

#include <stdio.h>
#include <string.h>

static int Fail(const char *what)
{
    Sys_Log("platform: FAIL %s", what);
    printf("platform: FAIL %s\n", what);
    Sys_Exit(1);
    return 1;
}

int main(int argc, char **argv)
{
    static char *fallback[] = {
        (char *)"amiga_platform_test", (char *)"--data", (char *)"DH20:gamedata",
        (char *)"--user-dir", (char *)"DH20:platform-user", NULL,
    };

    if (argc < 2) {
        argc = 5;
        argv = fallback;
    }
    if (!AmigaPlatform_Init(argc, argv))
        return Fail("init");
    if (strcmp(AmigaPlatform_GetDataDir(), "DH20:gamedata") != 0)
        return Fail("data directory");
    if (strcmp(AmigaPlatform_GetUserDir(), "DH20:platform-user") != 0)
        return Fail("user directory");
    if (!Sys_FileExists("Game/Bigfiles/common.bfl"))
        return Fail("data file through the Sys layer");
    if (strcmp(Sys_GetOptionText("install.language", ""), "English") != 0)
        return Fail("default language");
    if (Sys_GetOption("video.fullscreen", 0) != 1)
        return Fail("default fullscreen");
    if (strcmp(Sys_GetCommandLine(), "") != 0)
        return Fail("option arguments leaked into the command line");
    if (AmigaPlatform_WasSetupCanceled())
        return Fail("setup canceled");

    AmigaPlatform_Shutdown();
    Sys_Log("platform: OK");
    printf("platform: OK\n");
    Sys_Exit(0);
    return 1;
}
