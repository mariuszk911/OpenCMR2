/*
 * OpenCMR2 entry point on AmigaOS.
 *
 * Domain A: it may include the game headers, but no NDK or MiniGL headers.
 * The OS work is behind AmigaPlatform_* and the port/Sys calls.
 */
#include "platform/amiga/amiga_platform.h"
#include "platform/amiga/amiga_save_notice.h"
#include "port/save_image_disk.h"
#include "port/sys.h"
#include "diagnostics/timing.h"
#include "diagnostics/replay_trace.h"
#include "main.h"

// AmigaOS version string, read by the Version command and installers.
// "used" keeps it in the executable although nothing references it.
__attribute__((used)) static const char s_versionTag[] = "$VER: OpenCMR2 0.9.2 (27.11.2026)";

// A refused saved game (a PC save, a damaged or newer file): the game logs the
// name of every ignored file; the player is told once per kind of refusal.
static void ReportRefusedSave(SavedImageStatus status)
{
    static unsigned announced;

    Sys_Log("save: refused a saved game (status %d)", (int)status);
    if (AmigaSaveNotice_ShouldShow(status, &announced))
        Sys_MessageBox("OpenCMR2", AmigaSaveNotice_Text(status), SYS_MESSAGEBOX_OK);
}

int main(int argc, char **argv)
{
    Sys_Log("opencmr2: main entry");
    if (!AmigaPlatform_Init(argc, argv)) {
        Sys_Log("platform: initialization failed");
        Sys_Exit(1);
    }
    SavedImageDisk_SetRejectHandler(ReportRefusedSave);
    Diagnostics::StartProfileFromEnvironment();
    Diagnostics::ReplayTrace_StartFromOptions();
    const unsigned int code = CMain::Initialize(Sys_GetCommandLine());
    Diagnostics::StopProfile();
    Diagnostics::ReplayTrace_Stop();
    AmigaPlatform_Shutdown();
    return (int)code;
}
