/*
 * M1 platform lifecycle smoke for AmigaOS. Run the binary from its own
 * directory; PROGDIR: must be the installation root (or a temporary fixture
 * root created here).
 */
#include "platform/amiga/amiga_platform.h"
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
    if (CreateDir((STRPTR)path))
        return 1;
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
    static const char *const dirs[] = {
        "PROGDIR:Game", "PROGDIR:Game/Bigfiles", "PROGDIR:FrontEnd",
        "PROGDIR:Sounds", "PROGDIR:CountrySpecific", "PROGDIR:SetupRep",
    };
    static const char *const files[] = {
        "PROGDIR:Game/Bigfiles/common.bfl",
        "PROGDIR:FrontEnd/common.bfl",
        "PROGDIR:Sounds/surface.bfl",
        "PROGDIR:CountrySpecific/language.txt",
        "PROGDIR:SetupRep/setup.txt",
    };

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
        if (!EnsureDir(dirs[i]))
            return Fail("fixture directory");
    }
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        if (!EnsureFile(files[i]))
            return Fail("fixture file");
    }

    if (!AmigaPlatform_Init(argc, argv))
        return Fail("init");
    if (strcmp(AmigaPlatform_GetDataDir(), "PROGDIR:") != 0)
        return Fail("data directory");
    if (strcmp(AmigaPlatform_GetUserDir(), "PROGDIR:") != 0)
        return Fail("user directory");
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
