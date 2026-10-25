/*
 * M1 Sys event translation smoke for AmigaOS. Run from a Shell:
 *
 *     DH20:amiga_event_test
 *
 * The test uses a fake Window/UserPort, so no Intuition window is opened; the
 * desktop query does use the default public screen.
 */
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <intuition/intuition.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

static int Fail(const char *what)
{
    Sys_Log("event: FAIL %s", what);
    printf("event: FAIL %s\n", what);
    Sys_Exit(1);
    return 1;
}

static void Send(struct MsgPort *port, struct MsgPort *reply, ULONG messageClass,
                 UWORD code, UWORD qualifier)
{
    struct IntuiMessage *message =
        (struct IntuiMessage *)AllocMem(sizeof(struct IntuiMessage), MEMF_CLEAR | MEMF_PUBLIC);
    if (message == NULL)
        Fail("AllocMem");
    message->ExecMessage.mn_ReplyPort = reply;
    message->Class = messageClass;
    message->Code = code;
    message->Qualifier = qualifier;
    PutMsg(port, (struct Message *)message);
}

static void DrainReplies(struct MsgPort *reply)
{
    struct Message *message;
    while ((message = GetMsg(reply)) != NULL)
        FreeMem(message, sizeof(struct IntuiMessage));
}

int main(void)
{
    struct MsgPort *port = CreateMsgPort();
    struct MsgPort *reply = CreateMsgPort();
    struct Window fake;
    SysEvent event;

    if (port == NULL || reply == NULL)
        return Fail("CreateMsgPort");
    memset(&fake, 0, sizeof(fake));
    fake.UserPort = port;
    AmigaSys_SetWindow(&fake);

    if (Sys_PollEvent(&event))
        return Fail("empty port");

    Send(port, reply, IDCMP_CLOSEWINDOW, 0, 0);
    if (!Sys_PollEvent(&event) || event.type != SYS_EVENT_QUIT)
        return Fail("close window");
    DrainReplies(reply);

    Send(port, reply, IDCMP_ACTIVEWINDOW, 0, 0);
    if (!Sys_PollEvent(&event) || event.type != SYS_EVENT_FOCUS_GAINED)
        return Fail("focus gained");
    DrainReplies(reply);

    Send(port, reply, IDCMP_INACTIVEWINDOW, 0, 0);
    if (!Sys_PollEvent(&event) || event.type != SYS_EVENT_FOCUS_LOST)
        return Fail("focus lost");
    DrainReplies(reply);

    Send(port, reply, IDCMP_RAWKEY, 0x45, 0);
    if (!Sys_PollEvent(&event) || event.type != SYS_EVENT_KEY_DOWN || event.key != 0x01)
        return Fail("escape");
    DrainReplies(reply);

    Send(port, reply, IDCMP_RAWKEY, 0xc5, 0);
    if (Sys_PollEvent(&event))
        return Fail("raw key release");
    DrainReplies(reply);

    Send(port, reply, IDCMP_RAWKEY, 0x44, IEQUALIFIER_REPEAT);
    if (!Sys_PollEvent(&event) || event.key != 0x1c || !event.repeat)
        return Fail("repeat return");
    DrainReplies(reply);

    Send(port, reply, IDCMP_VANILLAKEY, 'A', 0);
    if (!Sys_PollEvent(&event) || event.type != SYS_EVENT_TEXT || event.character != 'A')
        return Fail("vanilla key");
    DrainReplies(reply);

    Send(port, reply, IDCMP_RAWKEY, 0x7f, 0);
    if (Sys_PollEvent(&event))
        return Fail("unmapped raw key");
    DrainReplies(reply);

    int width = 0;
    int height = 0;
    Sys_GetDesktopSize(&width, &height);
    if (width <= 0 || height <= 0)
        return Fail("desktop size");

    if (!Sys_CreateWindow("test", 0))
        return Fail("Sys_CreateWindow");
    Sys_DestroyWindow();
    AmigaSys_SetWindow(NULL);
    if (Sys_PollEvent(&event))
        return Fail("unregistered window");

    DeleteMsgPort(port);
    DeleteMsgPort(reply);
    Sys_Log("event: OK (desktop %ldx%ld)", (long)width, (long)height);
    printf("event: OK\n");
    Sys_Exit(0);
    return 1;
}
