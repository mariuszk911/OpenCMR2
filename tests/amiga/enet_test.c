/*
 * ENet over bsdsocket.library on AmigaOS (src/net/enet_bsdsocket.c). Needs a
 * running TCP/IP stack (Roadshow); loopback only, no other machine. Not
 * staged for users; run from a Shell:
 *
 *     amiga_enet_test > enet_test.log
 *
 * It opens the library, resolves addresses, measures a WaitSelect timeout,
 * and connects two ENet hosts through 127.0.0.1 to exchange reliable packets.
 * An emulator run is emulator evidence, not PiStorm evidence.
 */
#include "enet/enet.h"
#include "platform/amiga/amiga_sys.h"
#include "port/sys.h"

#include <stdio.h>
#include <string.h>

#define PORT 28631
#define PACKETS 200

static int failures;

static void Check(int ok, const char *what)
{
    if (!ok) {
        failures++;
        Sys_Log("enet_test: FAIL %s", what);
        printf("enet_test: FAIL %s\n", what);
    }
}

int main(void)
{
    ENetAddress address, to;
    ENetHost *server, *client;
    ENetPeer *peer;
    ENetEvent event;
    enet_uint32 condition, start, elapsed;
    unsigned sent = 0, received = 0, connected = 0, accepted = 0, order = 0;
    char text[32];

    AmigaSys_SetCommandLine("amiga_enet_test");
    if (enet_initialize() != 0) {
        printf("enet_test: no TCP/IP stack (bsdsocket.library v4)\n");
        return 20;
    }
    Check(enet_initialize() == 0, "second initialise is counted");
    enet_deinitialize();

    /* Addresses: literals never reach the resolver. */
    Check(enet_address_set_host(&to, "127.0.0.1") == 0 && to.host == ENET_HOST_TO_NET_32(0x7f000001), "127.0.0.1");
    Check(enet_address_set_host_ip(&to, "255.255.255.255") == 0 && to.host == ENET_HOST_BROADCAST, "broadcast literal");
    Check(enet_address_set_host_ip(&to, "300.1.1.1") != 0, "bad literal refused");
    Check(enet_address_get_host_ip(&to, text, sizeof(text)) == 0 && strcmp(text, "255.255.255.255") == 0, "address text");

    /* Time. */
    start = enet_time_get();
    {
        ENetSocket idle = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        Check(idle != ENET_SOCKET_NULL, "create a datagram socket");
        Check(enet_socket_set_option(idle, ENET_SOCKOPT_NONBLOCK, 1) == 0, "non-blocking");
        Check(enet_socket_set_option(idle, ENET_SOCKOPT_BROADCAST, 1) == 0, "broadcast option");
        Check(enet_socket_set_option(idle, ENET_SOCKOPT_REUSEADDR, 1) == 0, "reuse address");
        Check(enet_socket_set_option(idle, ENET_SOCKOPT_RCVBUF, 262144) == 0, "receive buffer");
        Check(enet_socket_set_option(idle, ENET_SOCKOPT_SNDBUF, 262144) == 0, "send buffer");
        address.host = ENET_HOST_ANY;
        address.port = PORT + 1;
        Check(enet_socket_bind(idle, &address) == 0, "bind");
        Check(enet_socket_get_address(idle, &address) == 0 && address.port == PORT + 1, "bound port");
        condition = ENET_SOCKET_WAIT_RECEIVE;
        start = enet_time_get();
        Check(enet_socket_wait(idle, &condition, 100) == 0 && condition == ENET_SOCKET_WAIT_NONE, "wait times out");
        elapsed = enet_time_get() - start;
        Sys_Log("enet_test: WaitSelect(100 ms) took %lu ms", (unsigned long)elapsed);
        printf("enet_test: WaitSelect(100 ms) took %lu ms\n", (unsigned long)elapsed);
        Check(elapsed >= 90 && elapsed <= 400, "wait timeout length");
        {
            ENetBuffer none;
            char data[8];
            none.data = data;
            none.dataLength = sizeof(data);
            Check(enet_socket_receive(idle, NULL, &none, 1) == 0, "empty non-blocking receive is 0, not an error");
        }
        /* A datagram to ourselves, sent from a second socket. */
        {
            ENetSocket other = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
            ENetBuffer out[2], in;
            char head[4] = { 'A', 'M', 'I', 'G' }, tail[4] = { 'A', '!', '!', '!' }, data[16];
            ENetAddress from;
            int got;
            out[0].data = head;
            out[0].dataLength = 4;
            out[1].data = tail;
            out[1].dataLength = 4;
            to.host = ENET_HOST_TO_NET_32(0x7f000001);
            to.port = PORT + 1;
            Check(other != ENET_SOCKET_NULL, "second socket");
            Check(enet_socket_send(other, &to, out, 2) == 8, "send two buffers");
            condition = ENET_SOCKET_WAIT_RECEIVE;
            Check(enet_socket_wait(idle, &condition, 500) == 0 && (condition & ENET_SOCKET_WAIT_RECEIVE), "wait wakes");
            in.data = data;
            in.dataLength = sizeof(data);
            got = enet_socket_receive(idle, &from, &in, 1);
            Check(got == 8 && memcmp(data, "AMIGA!!!", 8) == 0, "gathered datagram arrives whole");
            Check(from.host == ENET_HOST_TO_NET_32(0x7f000001), "sender address");
            in.dataLength = 4;      /* a datagram longer than the buffer is reported, not cut silently */
            Check(enet_socket_send(other, &to, out, 2) == 8, "send again");
            condition = ENET_SOCKET_WAIT_RECEIVE;
            enet_socket_wait(idle, &condition, 500);
            got = enet_socket_receive(idle, &from, &in, 1);
            Sys_Log("enet_test: short buffer receive returned %d", got);
            printf("enet_test: short buffer receive returned %d (-2 means truncated)\n", got);
            enet_socket_destroy(other);
        }
        enet_socket_destroy(idle);
    }

    /* Two ENet hosts through the loopback. */
    address.host = ENET_HOST_ANY;
    address.port = PORT;
    server = enet_host_create(&address, 4, 2, 0, 0);
    client = enet_host_create(NULL, 1, 2, 0, 0);
    Check(server != NULL && client != NULL, "create hosts");
    if (server == NULL || client == NULL) {
        enet_deinitialize();
        return 1;
    }
    to.host = ENET_HOST_TO_NET_32(0x7f000001);
    to.port = PORT;
    peer = enet_host_connect(client, &to, 2, 0);
    Check(peer != NULL, "connect");
    start = enet_time_get();
    while (peer != NULL && enet_time_get() - start < 5000 && !(connected && accepted && received >= PACKETS)) {
        while (enet_host_service(client, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT)
                connected = 1;
            else if (event.type == ENET_EVENT_TYPE_RECEIVE)
                enet_packet_destroy(event.packet);
        }
        while (enet_host_service(server, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                accepted = 1;
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                unsigned n = 0;
                if (event.packet->dataLength == 12 && memcmp(event.packet->data, "pkt ", 4) == 0) {
                    memcpy(&n, event.packet->data + 4, 4);
                    if (n == order)
                        order++;
                }
                received++;
                enet_packet_destroy(event.packet);
            }
        }
        while (connected && sent < PACKETS) {
            unsigned char data[12];
            ENetPacket *packet;
            memcpy(data, "pkt ", 4);
            memcpy(data + 4, &sent, 4);
            memset(data + 8, 0x5a, 4);
            packet = enet_packet_create(data, sizeof(data), ENET_PACKET_FLAG_RELIABLE);
            if (packet == NULL || enet_peer_send(peer, 0, packet) < 0)
                break;
            sent++;
        }
        {
            enet_uint32 wait = ENET_SOCKET_WAIT_RECEIVE;
            enet_socket_wait(server->socket, &wait, 2);
        }
    }
    elapsed = enet_time_get() - start;
    Check(connected && accepted, "the host accepted the connection");
    Check(sent == PACKETS && received == PACKETS && order == PACKETS, "reliable packets arrive complete and in order");
    Sys_Log("enet_test: %u/%u packets, in order %u, %lu ms", received, sent, order, (unsigned long)elapsed);
    printf("enet_test: %u/%u packets, in order %u, %lu ms\n", received, sent, order, (unsigned long)elapsed);

    /* Leave properly. */
    enet_peer_disconnect(peer, 0);
    start = enet_time_get();
    while (enet_time_get() - start < 2000) {
        int more = enet_host_service(client, &event, 0) > 0;
        more |= enet_host_service(server, &event, 0) > 0;
        if (!more && server->connectedPeers == 0)
            break;
    }
    Check(server->connectedPeers == 0, "the host saw the disconnect");
    enet_host_destroy(client);
    enet_host_destroy(server);
    enet_deinitialize();

    if (failures != 0) {
        printf("enet_test: %d failures\n", failures);
        return 1;
    }
    printf("enet_test: OK\n");
    Sys_Log("enet_test: OK");
    return 0;
}
