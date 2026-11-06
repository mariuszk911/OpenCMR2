/*
 * input.device adapter check. This is not the game: it opens the same
 * activated game window AmigaSys_CreateGameWindow uses, installs the
 * input.device handler, and waits for a bridge-injected Return down. The
 * release is injected through IDCMP, which is the fallback path
 * Sys_PollEvent uses when a handler release is missed. The result is written
 * to RAM:input_probe.out so an async bridge launch can inject the key while
 * the test is running.
 *
 * No user directory is created, so Sys_Log cannot open its log file and the
 * test does not leave a locked opencmr2.log behind.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <exec/memory.h>
#include <exec/ports.h>
#include <intuition/intuition.h>
#include <proto/exec.h>

#include <stdio.h>

#define INPUT_PROBE "RAM:input_probe.out"
#define DIK_RETURN 0x1c

static void Line(FILE *out, const char *text)
{
    fprintf(out, "%s\n", text);
    fflush(out);
}

/* The bridge's synthetic key-up does not reach input.device on this target,
 * so inject the release through IDCMP. This is the fallback path the game's
 * Sys_PollEvent uses when the handler misses a release. */
static void SendRelease(struct Window *window, struct MsgPort *reply)
{
    struct IntuiMessage *message =
        (struct IntuiMessage *)AllocMem(sizeof(struct IntuiMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (message == NULL)
        return;
    message->ExecMessage.mn_ReplyPort = reply;
    message->Class = IDCMP_RAWKEY;
    message->Code = 0x80 | 0x44;
    message->Qualifier = 0;
    PutMsg(window->UserPort, (struct Message *)message);
}

int main(void)
{
    FILE *out = fopen(INPUT_PROBE, "w");
    struct Window *window;
    struct MsgPort *reply;
    SysEvent event;
    int sawDown = 0;
    int sawUp = 0;

    if (out == NULL)
        return 1;
    reply = CreateMsgPort();
    if (reply == NULL) {
        fclose(out);
        return 1;
    }
    Line(out, "input_probe: start");
    AmigaSys_SetUserDir("RAM:input-probe-no-log");
    AmigaSys_SetDataDir("RAM:input-probe-no-data");

    window = (struct Window *)AmigaSys_CreateGameWindow(320, 200, "OpenCMR2 input probe");
    if (window == NULL) {
        Line(out, "input_probe: FAIL window");
        DeleteMsgPort(reply);
        fclose(out);
        return 1;
    }
    if ((window->Flags & WFLG_WINDOWACTIVE) == 0) {
        Line(out, "input_probe: FAIL inactive");
        AmigaSys_SetWindow(NULL);
        DeleteMsgPort(reply);
        fclose(out);
        return 1;
    }
    if (!AmigaSys_InputInit()) {
        Line(out, "input_probe: FAIL init");
        AmigaSys_SetWindow(NULL);
        AmigaSys_Shutdown();
        DeleteMsgPort(reply);
        fclose(out);
        return 1;
    }
    Line(out, "input_probe: ready");

    for (int i = 0; i < 200 && !(sawDown && sawUp); ++i) {
        unsigned char keys[256];
        // The game drains IDCMP every frame; the release fallback lives there.
        while (Sys_PollEvent(&event)) {
        }
        AmigaSys_GetKeyState(keys);
        if (!sawDown && (keys[DIK_RETURN] & 0x80) != 0) {
            sawDown = 1;
            Line(out, "input_probe: down");
            SendRelease(window, reply);
        }
        if (sawDown && !sawUp && (keys[DIK_RETURN] & 0x80) == 0) {
            sawUp = 1;
            Line(out, "input_probe: up");
        }
        Sys_Sleep(50);
    }

    {
        struct Message *message;
        while ((message = GetMsg(reply)) != NULL)
            FreeMem(message, sizeof(struct IntuiMessage));
    }
    DeleteMsgPort(reply);
    AmigaSys_InputShutdown();
    AmigaSys_SetWindow(NULL);
    AmigaSys_Shutdown();

    if (sawDown && sawUp)
        Line(out, "input_probe: OK");
    else
        Line(out, "input_probe: FAIL timeout");
    fclose(out);
    return sawDown && sawUp ? 0 : 1;
}
