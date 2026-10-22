/* SPDX-License-Identifier: MIT */
/* Compile the real C translation unit so UBSan observes C signed-shift semantics. */
#include "../../../third_party/enet/protocol.c"

static int checks = 0;
static int failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

static void deliver(ENetHost *host, ENetPeer *peer, enet_uint16 group, int expected)
{
    ENetProtocol command = {0};
    enet_uint8 *wire = (enet_uint8 *) &command;
    enet_uint8 *cursor = wire + sizeof(ENetProtocolSendUnsequenced);
    ENetPacket *packet;
    command.header.command = ENET_PROTOCOL_COMMAND_SEND_UNSEQUENCED;
    command.header.channelID = 0;
    command.sendUnsequenced.unsequencedGroup = ENET_HOST_TO_NET_16(group);
    command.sendUnsequenced.dataLength = ENET_HOST_TO_NET_16(1);
    *cursor = (enet_uint8) group;
    host->receivedData = wire;
    host->receivedDataLength = sizeof(ENetProtocolSendUnsequenced) + 1;
    CHECK(enet_protocol_handle_send_unsequenced(host, peer, &command, &cursor) == 0);
    CHECK(cursor == wire + host->receivedDataLength);
    packet = enet_peer_receive(peer, NULL);
    CHECK((packet != NULL) == expected);
    if (packet != NULL)
    {
        CHECK(packet->dataLength == 1 && packet->data[0] == (enet_uint8) group);
        CHECK((packet->flags & ENET_PACKET_FLAG_UNSEQUENCED) != 0);
        enet_packet_destroy(packet);
    }
    CHECK(enet_list_empty(&peer->dispatchedCommands));
    CHECK(peer->totalWaitingData == 0);
}

int main(void)
{
    ENetHost host = {0};
    ENetPeer peer = {0};
    ENetChannel channel = {0};
    enet_uint32 group;
    host.maximumPacketSize = 1024;
    host.maximumWaitingData = 1024;
    enet_list_clear(&host.dispatchQueue);
    peer.host = &host;
    peer.state = ENET_PEER_STATE_CONNECTED;
    peer.channels = &channel;
    peer.channelCount = 1;
    enet_list_clear(&peer.dispatchedCommands);
    enet_list_clear(&channel.incomingReliableCommands);
    enet_list_clear(&channel.incomingUnreliableCommands);

    /* Every word's bit31, its neighbours, duplicate suppression and window rollover. */
    for (group = 0; group < ENET_PEER_UNSEQUENCED_WINDOW_SIZE * 2; ++group)
    {
        const enet_uint32 index = group % ENET_PEER_UNSEQUENCED_WINDOW_SIZE;
        deliver(&host, &peer, (enet_uint16)group, 1);
        CHECK((peer.unsequencedWindow[index / 32] & (1u << (index % 32))) != 0);
        deliver(&host, &peer, (enet_uint16)group, 0);
    }
    /* Prior window packets must remain stale after rollover, not re-enter the queue. */
    deliver(&host, &peer, 31, 0);
    CHECK(enet_list_empty(&channel.incomingUnreliableCommands));
    printf("ENET_UNSEQUENCED_RESULT checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
