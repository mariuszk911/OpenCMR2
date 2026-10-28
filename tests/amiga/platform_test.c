/*
 * M1 platform lifecycle smoke for AmigaOS.
 *
 * This test never writes to PROGDIR: or to any game installation. The target's
 * only safe writable scratch is RAM:; the fixture tree lives under
 * RAM:cmr2test/ and the Sys data/user directories point there.
 * AmigaPlatform_Init's PROGDIR: default is covered by the target boot smoke.
 */
#include "platform/amiga/amiga_platform.h"
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

/* AmigaPlatform_Shutdown tears the renderer down; this lifecycle test links
 * the platform layer without a graphics backend, so provide the no-op. */
void Gfx_Shutdown(void)
{
}

static int Fail(const char *what)
{
    Sys_Log("platform: FAIL %s", what);
    printf("platform: FAIL %s\n", what);
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

static int EnsureFile(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL)
        return 0;
    fputc('x', file);
    fclose(file);
    return 1;
}

int main(int argc, char **argv)
{
    static const char *const root = "RAM:cmr2test";
    static const char *const dirs[] = {
        "RAM:cmr2test/Game", "RAM:cmr2test/Game/Bigfiles", "RAM:cmr2test/FrontEnd",
        "RAM:cmr2test/Sounds", "RAM:cmr2test/CountrySpecific", "RAM:cmr2test/SetupRep",
    };
    static const char *const files[] = {
        "RAM:cmr2test/Game/Bigfiles/common.bfl",
        "RAM:cmr2test/FrontEnd/common.bfl",
        "RAM:cmr2test/Sounds/surface.bfl",
        "RAM:cmr2test/CountrySpecific/language.txt",
        "RAM:cmr2test/SetupRep/setup.txt",
    };
    size_t i;

    (void)argc;
    (void)argv;

    // Never let an early Fail() create PROGDIR:opencmr2.log. RAM: always
    // exists; move the user directory to the fixture root once it exists.
    AmigaSys_SetDataDir(root);
    AmigaSys_SetUserDir("RAM:");
    if (!EnsureDir(root))
        return Fail("fixture root");
    AmigaSys_SetUserDir(root);
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (!EnsureDir(dirs[i]))
            return Fail("fixture directory");
    }
    for (i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        if (!EnsureFile(files[i]))
            return Fail("fixture file");
    }

    // The test's own data/user root, never PROGDIR:.
    if (strcmp(AmigaPlatform_GetDataDir(), root) != 0)
        return Fail("data directory");
    if (strcmp(AmigaPlatform_GetUserDir(), root) != 0)
        return Fail("user directory");
    if (!AmigaSys_EnsureSettingsFile())
        return Fail("settings file");
    if (!Sys_FileExists("Game/Bigfiles/common.bfl"))
        return Fail("data file through the Sys layer");
    if (strcmp(Sys_GetOptionText("install.language", ""), "English") != 0)
        return Fail("default language");
    if (Sys_GetOption("video.fullscreen", 0) != 1)
        return Fail("default fullscreen");
    if (AmigaPlatform_WasSetupCanceled())
        return Fail("setup canceled");

    Sys_Log("platform: OK");
    printf("platform: OK\n");
    AmigaPlatform_Shutdown();
    return 0;
}
