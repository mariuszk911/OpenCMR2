/*
 * M1 Sys settings smoke for AmigaOS. Run the binary from its drawer; the
 * test derives its own path, so no volume name is hardcoded.
 *
 * The drawer must contain syssettings-user/opencmr2.ini. An emulator run is
 * emulator evidence, not PiStorm evidence.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"
#include "test_paths.h"

#include <stdio.h>
#include <string.h>

static int Fail(const char *what)
{
    Sys_Log("sys_settings: FAIL %s", what);
    printf("sys_settings: FAIL %s\n", what);
    Sys_Exit(1);
    return 1;
}

int main(void)
{
    char user[512];
    if (!TestPath(user, sizeof(user), "syssettings-user"))
        return Fail("test drawer path");
    AmigaSys_SetUserDir(user);

    if (strcmp(Sys_GetInstallSetting("Language"), "French") != 0)
        return Fail("install.language");
    if (strcmp(Sys_GetInstallSetting("Sku_Type"), "EUROPE") != 0)
        return Fail("install.sku default");
    if (strcmp(Sys_GetInstallSetting("Game_HDPath"), ".") != 0)
        return Fail("Game_HDPath");
    if (Sys_GetOption("video.fullscreen", 1) != 0)
        return Fail("read video.fullscreen");
    if (strcmp(Sys_GetOptionText("paths.data", ""), "game-data") != 0)
        return Fail("read paths.data");

    if (!Sys_SetOption("video.fullscreen", 1))
        return Fail("write video.fullscreen");
    if (Sys_GetOption("video.fullscreen", 0) != 1)
        return Fail("updated video.fullscreen");
    if (!Sys_SetOption("input.port", 28620))
        return Fail("insert input.port");
    if (Sys_GetOption("input.port", 0) != 28620)
        return Fail("read input.port");
    if (Sys_SetOption("nodot", 1))
        return Fail("option without a section");

    Sys_Log("sys_settings: OK");
    printf("sys_settings: OK\n");
    Sys_Exit(0);
    return 1;
}
