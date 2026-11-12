/*
 * ENet's platform layer for AmigaOS over bsdsocket.library (Roadshow, Miami,
 * Genesis...). It replaces third_party/enet/unix.c, which stays unmodified:
 * this file provides exactly the functions unix.c does (enet/enet.h lists
 * them as the system specific API).
 *
 * Rules that shape it:
 *
 * - bsdsocket.library belongs to the task that opens it: OpenLibrary gives
 *   that task its own SocketBase, and socket descriptors belong to it. Every
 *   call here must come from the task that called enet_initialize, which is
 *   the game's main task (net::Session pumps from there; there is no service
 *   thread, see net/net_service.h).
 * - Nothing here blocks except enet_socket_wait (WaitSelect, with the timeout
 *   it is given), enet_address_set_host and enet_address_get_host (the
 *   stack's resolver; an address literal never reaches it).
 * - The break mask is cleared so that Ctrl-C does not abort a socket call
 *   in the middle of a race.
 * - errno is not libnix's: the stack writes its own code through
 *   SBTC_ERRNOPTR into socketErrno, and the numbers are the BSD ones the
 *   stack uses (they differ from libnix's <errno.h>).
 * - One owner, one cleanup path: enet_deinitialize closes the library, after
 *   the sockets are closed (enet_host_destroy runs first). enet_initialize
 *   is counted, so a second owner does not close the first one's base.
 *
 * Domain B (OS glue): never compiled with -malign-int.
 */

#define ENET_BUILDING_LIB 1
#include "enet/enet.h"

#include <string.h>

#include "port/sys.h"

/* AmigaOS headers last. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <netinet/tcp.h>
#include <sys/filio.h>
#include <sys/uio.h>

/* The stack's error numbers (BSD): not the ones in libnix's <errno.h>. */
#define STACK_EINTR 4
#define STACK_EWOULDBLOCK 35
#define STACK_EINPROGRESS 36

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct Library *SocketBase = NULL;

static int initCount = 0;
static LONG socketErrno = 0;
static enet_uint32 timeBase = 0;

static void
LogFailure (const char * what)
{
    Sys_Log ("net: %s failed (stack error %ld)", what, (long) socketErrno);
}

int
enet_initialize (void)
{
    STRPTR release = NULL;

    if (initCount > 0)
    {
        initCount ++;
        return 0;
    }

    SocketBase = OpenLibrary ((CONST_STRPTR) "bsdsocket.library", 4);
    if (SocketBase == NULL)
    {
        Sys_Log ("net: bsdsocket.library v4 is not available (no TCP/IP stack running?)");
        return -1;
    }

    /* Own errno, and no Ctrl-C aborting a call. Zero means every tag was accepted. */
    socketErrno = 0;
    if (SocketBaseTags (SBTM_SETVAL (SBTC_ERRNOPTR (sizeof (socketErrno))), (ULONG) & socketErrno,
                        SBTM_SETVAL (SBTC_BREAKMASK), 0,
                        TAG_DONE) != 0)
    {
        Sys_Log ("net: bsdsocket.library refused the errno pointer or the break mask");
        CloseLibrary (SocketBase);
        SocketBase = NULL;
        return -1;
    }

    if (SocketBaseTags (SBTM_GETREF (SBTC_RELEASESTRPTR), (ULONG) & release, TAG_DONE) == 0 && release != NULL)
        Sys_Log ("net: bsdsocket.library %u.%u, %s", (unsigned) SocketBase -> lib_Version,
                 (unsigned) SocketBase -> lib_Revision, (const char *) release);
    else
        Sys_Log ("net: bsdsocket.library %u.%u", (unsigned) SocketBase -> lib_Version,
                 (unsigned) SocketBase -> lib_Revision);

    initCount = 1;
    return 0;
}

void
enet_deinitialize (void)
{
    if (initCount == 0)
        return;
    if (-- initCount > 0)
        return;
    CloseLibrary (SocketBase);
    SocketBase = NULL;
}

enet_uint32
enet_host_random_seed (void)
{
    return (enet_uint32) Sys_GetUnixTime () ^ (Sys_GetTicks () << 10);
}

enet_uint32
enet_time_get (void)
{
    return Sys_GetTicks () - timeBase;
}

void
enet_time_set (enet_uint32 newTimeBase)
{
    timeBase = Sys_GetTicks () - newTimeBase;
}

int
enet_address_set_host_ip (ENetAddress * address, const char * name)
{
    in_addr_t ip = inet_addr ((STRPTR) name);

    /* The stack returns INADDR_NONE for a name that is not an address, which
       is also the broadcast address's value: accept that one spelled out. */
    if (ip == (in_addr_t) INADDR_NONE && strcmp (name, "255.255.255.255") != 0)
        return -1;

    address -> host = (enet_uint32) ip;
    return 0;
}

/* The stack's resolver blocks the calling task until it answers or gives up. */
int
enet_address_set_host (ENetAddress * address, const char * name)
{
    struct hostent * hostEntry;

    if (enet_address_set_host_ip (address, name) == 0)
        return 0;

    hostEntry = gethostbyname ((STRPTR) name);
    if (hostEntry != NULL && hostEntry -> h_addrtype == AF_INET && hostEntry -> h_length == 4 &&
        hostEntry -> h_addr_list != NULL && hostEntry -> h_addr_list [0] != NULL)
    {
        memcpy (& address -> host, hostEntry -> h_addr_list [0], 4);
        return 0;
    }

    return -1;
}

int
enet_address_get_host_ip (const ENetAddress * address, char * name, size_t nameLength)
{
    STRPTR text = Inet_NtoA ((in_addr_t) address -> host);
    size_t length;

    if (text == NULL)
        return -1;
    length = strlen ((const char *) text);
    if (length >= nameLength)
        return -1;
    memcpy (name, text, length + 1);
    return 0;
}

int
enet_address_get_host (const ENetAddress * address, char * name, size_t nameLength)
{
    struct hostent * hostEntry;
    enet_uint32 host = address -> host;

    hostEntry = gethostbyaddr ((STRPTR) & host, sizeof (host), AF_INET);
    if (hostEntry != NULL && hostEntry -> h_name != NULL)
    {
        size_t length = strlen (hostEntry -> h_name);

        if (length >= nameLength)
            return -1;
        memcpy (name, hostEntry -> h_name, length + 1);
        return 0;
    }

    return enet_address_get_host_ip (address, name, nameLength);
}

static void
FillAddress (struct sockaddr_in * sin, const ENetAddress * address)
{
    memset (sin, 0, sizeof (struct sockaddr_in));
    sin -> sin_len = sizeof (struct sockaddr_in);
    sin -> sin_family = AF_INET;
    if (address != NULL)
    {
        sin -> sin_port = ENET_HOST_TO_NET_16 (address -> port);
        sin -> sin_addr.s_addr = address -> host;
    }
    else
    {
        sin -> sin_port = 0;
        sin -> sin_addr.s_addr = INADDR_ANY;
    }
}

int
enet_socket_bind (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;
    int result;

    FillAddress (& sin, address);
    result = bind (socket, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
    if (result == -1)
        LogFailure ("bind");
    return result;
}

int
enet_socket_get_address (ENetSocket socket, ENetAddress * address)
{
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);

    if (getsockname (socket, (struct sockaddr *) & sin, & sinLength) == -1)
        return -1;

    address -> host = (enet_uint32) sin.sin_addr.s_addr;
    address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    return 0;
}

int
enet_socket_listen (ENetSocket socket, int backlog)
{
    return listen (socket, backlog < 0 ? SOMAXCONN : backlog);
}

ENetSocket
enet_socket_create (ENetSocketType type)
{
    ENetSocket result;

    if (SocketBase == NULL)
        return ENET_SOCKET_NULL;
    result = socket (AF_INET, type == ENET_SOCKET_TYPE_DATAGRAM ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (result == -1)
    {
        LogFailure ("socket");
        return ENET_SOCKET_NULL;
    }
    return result;
}

int
enet_socket_set_option (ENetSocket socket, ENetSocketOption option, int value)
{
    LONG result = -1;
    LONG flag = value;

    switch (option)
    {
        case ENET_SOCKOPT_NONBLOCK:
            flag = value ? 1 : 0;
            result = IoctlSocket (socket, FIONBIO, & flag);
            break;

        case ENET_SOCKOPT_BROADCAST:
            result = setsockopt (socket, SOL_SOCKET, SO_BROADCAST, & flag, sizeof (flag));
            break;

        case ENET_SOCKOPT_REUSEADDR:
            result = setsockopt (socket, SOL_SOCKET, SO_REUSEADDR, & flag, sizeof (flag));
            break;

        case ENET_SOCKOPT_RCVBUF:
            result = setsockopt (socket, SOL_SOCKET, SO_RCVBUF, & flag, sizeof (flag));
            break;

        case ENET_SOCKOPT_SNDBUF:
            result = setsockopt (socket, SOL_SOCKET, SO_SNDBUF, & flag, sizeof (flag));
            break;

        case ENET_SOCKOPT_NODELAY:
            result = setsockopt (socket, IPPROTO_TCP, TCP_NODELAY, & flag, sizeof (flag));
            break;

        case ENET_SOCKOPT_TTL:
            result = setsockopt (socket, IPPROTO_IP, IP_TTL, & flag, sizeof (flag));
            break;

        /* The receive and send timeouts apply to blocking sockets, which
           this backend never uses. */
        default:
            break;
    }
    return result == -1 ? -1 : 0;
}

int
enet_socket_get_option (ENetSocket socket, ENetSocketOption option, int * value)
{
    LONG result = -1;
    LONG flag = 0;
    socklen_t length = sizeof (flag);

    switch (option)
    {
        case ENET_SOCKOPT_ERROR:
            result = getsockopt (socket, SOL_SOCKET, SO_ERROR, & flag, & length);
            break;

        case ENET_SOCKOPT_TTL:
            result = getsockopt (socket, IPPROTO_IP, IP_TTL, & flag, & length);
            break;

        default:
            break;
    }
    if (result == -1)
        return -1;
    * value = (int) flag;
    return 0;
}

int
enet_socket_connect (ENetSocket socket, const ENetAddress * address)
{
    struct sockaddr_in sin;
    LONG result;

    FillAddress (& sin, address);
    result = connect (socket, (struct sockaddr *) & sin, sizeof (struct sockaddr_in));
    if (result == -1 && socketErrno == STACK_EINPROGRESS)
        return 0;

    return (int) result;
}

ENetSocket
enet_socket_accept (ENetSocket socket, ENetAddress * address)
{
    LONG result;
    struct sockaddr_in sin;
    socklen_t sinLength = sizeof (struct sockaddr_in);

    result = accept (socket,
                     address != NULL ? (struct sockaddr *) & sin : NULL,
                     address != NULL ? & sinLength : NULL);
    if (result == -1)
        return ENET_SOCKET_NULL;

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return result;
}

int
enet_socket_shutdown (ENetSocket socket, ENetSocketShutdown how)
{
    return shutdown (socket, (int) how);
}

void
enet_socket_destroy (ENetSocket socket)
{
    if (socket != ENET_SOCKET_NULL && SocketBase != NULL)
        CloseSocket (socket);
}

/* ENetBuffer has the layout of struct iovec (pointer, length), as unix.c relies on. */
int
enet_socket_send (ENetSocket socket,
                  const ENetAddress * address,
                  const ENetBuffer * buffers,
                  size_t bufferCount)
{
    struct msghdr msgHdr;
    struct sockaddr_in sin;
    LONG sentLength;

    memset (& msgHdr, 0, sizeof (struct msghdr));

    if (address != NULL)
    {
        FillAddress (& sin, address);
        msgHdr.msg_name = & sin;
        msgHdr.msg_namelen = sizeof (struct sockaddr_in);
    }

    msgHdr.msg_iov = (struct iovec *) buffers;
    msgHdr.msg_iovlen = bufferCount;

    sentLength = sendmsg (socket, & msgHdr, MSG_NOSIGNAL);
    if (sentLength == -1)
    {
        if (socketErrno == STACK_EWOULDBLOCK)
            return 0;

        return -1;
    }

    return (int) sentLength;
}

int
enet_socket_receive (ENetSocket socket,
                     ENetAddress * address,
                     ENetBuffer * buffers,
                     size_t bufferCount)
{
    struct msghdr msgHdr;
    struct sockaddr_in sin;
    LONG recvLength;

    memset (& msgHdr, 0, sizeof (struct msghdr));

    if (address != NULL)
    {
        msgHdr.msg_name = & sin;
        msgHdr.msg_namelen = sizeof (struct sockaddr_in);
    }

    msgHdr.msg_iov = (struct iovec *) buffers;
    msgHdr.msg_iovlen = bufferCount;

    recvLength = recvmsg (socket, & msgHdr, MSG_NOSIGNAL);
    if (recvLength == -1)
    {
        if (socketErrno == STACK_EWOULDBLOCK)
            return 0;

        return -1;
    }

    if (msgHdr.msg_flags & MSG_TRUNC)
        return -2;

    if (address != NULL)
    {
        address -> host = (enet_uint32) sin.sin_addr.s_addr;
        address -> port = ENET_NET_TO_HOST_16 (sin.sin_port);
    }

    return (int) recvLength;
}

/* The timeout is {seconds, microseconds}: the layout of both the stack's and
   the POSIX struct timeval, whatever the headers call them. */
struct SelectTimeout
{
    LONG seconds;
    LONG microseconds;
};

int
enet_socketset_select (ENetSocket maxSocket, ENetSocketSet * readSet, ENetSocketSet * writeSet, enet_uint32 timeout)
{
    struct SelectTimeout timeVal;

    timeVal.seconds = (LONG) (timeout / 1000);
    timeVal.microseconds = (LONG) ((timeout % 1000) * 1000);

    return (int) WaitSelect (maxSocket + 1, readSet, writeSet, NULL, (void *) & timeVal, NULL);
}

int
enet_socket_wait (ENetSocket socket, enet_uint32 * condition, enet_uint32 timeout)
{
    fd_set readSet, writeSet;
    struct SelectTimeout timeVal;
    LONG selectCount;

    timeVal.seconds = (LONG) (timeout / 1000);
    timeVal.microseconds = (LONG) ((timeout % 1000) * 1000);

    FD_ZERO (& readSet);
    FD_ZERO (& writeSet);

    if (* condition & ENET_SOCKET_WAIT_SEND)
        FD_SET (socket, & writeSet);

    if (* condition & ENET_SOCKET_WAIT_RECEIVE)
        FD_SET (socket, & readSet);

    selectCount = WaitSelect (socket + 1, & readSet, & writeSet, NULL, (void *) & timeVal, NULL);

    if (selectCount < 0)
    {
        if (socketErrno == STACK_EINTR && * condition & ENET_SOCKET_WAIT_INTERRUPT)
        {
            * condition = ENET_SOCKET_WAIT_INTERRUPT;
            return 0;
        }

        return -1;
    }

    * condition = ENET_SOCKET_WAIT_NONE;

    if (selectCount == 0)
        return 0;

    if (FD_ISSET (socket, & writeSet))
        * condition |= ENET_SOCKET_WAIT_SEND;

    if (FD_ISSET (socket, & readSet))
        * condition |= ENET_SOCKET_WAIT_RECEIVE;

    return 0;
}
