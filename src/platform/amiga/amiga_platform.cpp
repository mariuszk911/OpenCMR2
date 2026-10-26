// AmigaOS platform lifecycle: data discovery, user directory and defaults.
//
// The executable lives in the installation directory, so PROGDIR: is the data
// and user directory. There is no SDL and no ASL picker in v1.
#include "platform/amiga/amiga_platform.h"
#include "platform/amiga/amiga_sys.h"
#include "port/gfx.h"
#include "port/sys.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

namespace {

const char *const kRequiredFiles[] = {
    "Game/Bigfiles/common.bfl",
    "FrontEnd/common.bfl",
    "Sounds/surface.bfl",
};
const char *const kRequiredDirs[] = {
    "CountrySpecific",
    "SetupRep",
};



int JoinNative(char *out, size_t size, const char *root, const char *relative)
{
    const size_t rootLen = strlen(root);
    const size_t relativeLen = strlen(relative);
    const int needsSlash = rootLen > 0 && root[rootLen - 1] != ':' && root[rootLen - 1] != '/';

    if (rootLen + (needsSlash ? 1u : 0u) + relativeLen + 1 > size)
        return 0;
    memcpy(out, root, rootLen);
    size_t length = rootLen;
    if (needsSlash)
        out[length++] = '/';
    memcpy(out + length, relative, relativeLen + 1);
    return 1;
}

int HasFile(const char *dir, const char *relative)
{
    char path[512];
    if (!JoinNative(path, sizeof(path), dir, relative))
        return 0;
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock == 0)
        return 0;
    struct FileInfoBlock info OPENCMR2_PORT_ALIGN4;
    const int found = Examine(lock, &info) && info.fib_DirEntryType < 0 && info.fib_Size > 0;
    UnLock(lock);
    return found;
}

int DirHasEntries(const char *dir, const char *relative)
{
    char path[512];
    if (!JoinNative(path, sizeof(path), dir, relative))
        return 0;
    BPTR lock = Lock((STRPTR)path, SHARED_LOCK);
    if (lock == 0)
        return 0;
    struct FileInfoBlock info OPENCMR2_PORT_ALIGN4;
    int found = Examine(lock, &info) && info.fib_DirEntryType > 0 && ExNext(lock, &info) != 0;
    UnLock(lock);
    return found;
}

int IsGameDataDir(const char *dir)
{
    for (size_t i = 0; i < sizeof(kRequiredFiles) / sizeof(kRequiredFiles[0]); ++i) {
        if (!HasFile(dir, kRequiredFiles[i]))
            return 0;
    }
    for (size_t i = 0; i < sizeof(kRequiredDirs) / sizeof(kRequiredDirs[0]); ++i) {
        if (!DirHasEntries(dir, kRequiredDirs[i]))
            return 0;
    }
    return 1;
}

void BuildCommandLine(int argc, char **argv, char *out, size_t size)
{
    out[0] = '\0';
    for (int i = 1; i < argc; ++i) {
        const size_t used = strlen(out);
        const size_t length = strlen(argv[i]);
        if (used + (used != 0 ? 1u : 0u) + length + 1 > size)
            break;
        if (used != 0)
            out[used] = ' ';
        strcpy(out + used + (used != 0 ? 1u : 0u), argv[i]);
    }
}

} // namespace

extern "C" int AmigaPlatform_Init(int argc, char **argv)
{
    Sys_Log("platform: init");
    // The executable always lives in the installation directory. Keep the
    // data, settings and saves beside it; no alternate layouts are supported.
    AmigaSys_SetUserDir("PROGDIR:");
    Sys_Log("platform: user dir");
    if (!AmigaSys_EnsureSettingsFile()) {
        Sys_Log("settings: cannot create the default opencmr2.ini");
        return 0;
    }
    Sys_Log("platform: data check");
    if (!IsGameDataDir("PROGDIR:")) {
        Sys_Log("game data: PROGDIR: is not a complete installation");
        return 0;
    }
    AmigaSys_SetDataDir("PROGDIR:");

    char commandLine[1024];
    BuildCommandLine(argc, argv, commandLine, sizeof(commandLine));
    AmigaSys_SetCommandLine(commandLine);

    Sys_Log("game data: PROGDIR:");
    Sys_Log("user directory: PROGDIR:");
    return 1;
}

extern "C" void AmigaPlatform_Shutdown(void)
{
    // The renderer owns the MiniGL context and, in fullscreen mode, the screen
    // that hides Workbench. Tear it down before Intuition itself is closed.
    Gfx_Shutdown();
    AmigaSys_Shutdown();
}

extern "C" const char *AmigaPlatform_GetDataDir(void)
{
    return AmigaSys_GetDataDir();
}

extern "C" const char *AmigaPlatform_GetUserDir(void)
{
    return AmigaSys_GetUserDir();
}

extern "C" int AmigaPlatform_WasSetupCanceled(void)
{
    return 0;
}
