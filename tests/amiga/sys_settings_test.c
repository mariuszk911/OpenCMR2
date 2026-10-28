/*
 * M1 Sys settings smoke for AmigaOS. All writable state is under RAM:; the
 * test builds its own opencmr2.ini and never opens PROGDIR:.
 *
 * An emulator run is emulator evidence, not PiStorm evidence.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

static int Fail(const char *what)
{
    Sys_Log("sys_settings: FAIL %s", what);
    printf("sys_settings: FAIL %s\n", what);
    return 1;
}

static int EnsureDir(const char *path)
{
    BPTR created = CreateDir((STRPTR)path);
    if (created != 0) {
        UnLock(created);
        return 1;
    }
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock == 0)
        return 0;
    UnLock(lock);
    return 1;
}

int main(void)
{
    static const char fixture[] =
        "[install]\n"
        "language = French\n"
        "\n"
        "[paths]\n"
        "data = game-data\n"
        "\n"
        "[video]\n"
        "fullscreen = 0\n";

    // RAM: is the only safe writable scratch on the target.
    AmigaSys_SetUserDir("RAM:");
    if (!EnsureDir("RAM:cmr2test-syssettings")) {
        Sys_Log("sys_settings: FAIL fixture directory");
        return 1;
    }
    {
        FILE *file = fopen("RAM:cmr2test-syssettings/opencmr2.ini", "wb");
        if (file == NULL || fwrite(fixture, 1, sizeof(fixture) - 1, file) != sizeof(fixture) - 1) {
            if (file != NULL)
                fclose(file);
            Sys_Log("sys_settings: FAIL fixture ini");
            return 1;
        }
        fclose(file);
    }
    AmigaSys_SetUserDir("RAM:cmr2test-syssettings");

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
    return 0;
}
