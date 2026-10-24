/* SPDX-License-Identifier: MIT */
/* Compile the real C translation unit so UBSan observes C signed-shift semantics. */
#include "../../../third_party/enet/protocol.c"

static int checks = 0;
static int failures = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); } } while (0)

/* 33 one-byte fragments: number 31 is bit 31 of the first bitmap word, 32 is bit 0 of the next. */
enum { FragmentCount = 33 };

static enet_uint8 fragment_byte(enet_uint32 number)
{
    return (enet_uint8) (number * 7u + 1u);
}

static void deliver(ENetHost *host, ENetPeer *peer, int reliable, enet_uint32 number)
{
    ENetProtocol command = {0};
    enet_uint8 *wire = (enet_uint8 *) &command;
    enet_uint8 *cursor = wire + sizeof(ENetProtocolSendFragment);
    command.header.command = reliable
        ? (ENET_PROTOCOL_COMMAND_SEND_FRAGMENT | ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE)
        : ENET_PROTOCOL_COMMAND_SEND_UNRELIABLE_FRAGMENT;
    /* The receive loop converts the header's sequence number to host order before dispatching. */
    command.header.channelID = reliable ? 0 : 1;
    command.header.reliableSequenceNumber = reliable ? (enet_uint16) (1u + number) : 0;
    command.sendFragment.startSequenceNumber = ENET_HOST_TO_NET_16(1);
    command.sendFragment.dataLength = ENET_HOST_TO_NET_16(1);
    command.sendFragment.fragmentCount = ENET_HOST_TO_NET_32(FragmentCount);
    command.sendFragment.fragmentNumber = ENET_HOST_TO_NET_32(number);
    command.sendFragment.totalLength = ENET_HOST_TO_NET_32(FragmentCount);
    command.sendFragment.fragmentOffset = ENET_HOST_TO_NET_32(number);
    *cursor = fragment_byte(number);
    host->receivedData = wire;
    host->receivedDataLength = sizeof(ENetProtocolSendFragment) + 1;
    CHECK((reliable ? enet_protocol_handle_send_fragment(host, peer, &command, &cursor)
                    : enet_protocol_handle_send_unreliable_fragment(host, peer, &command, &cursor)) == 0);
    CHECK(cursor == wire + host->receivedDataLength);
}

static void reassemble(ENetHost *host, ENetPeer *peer, int reliable)
{
    ENetChannel *channel = &peer->channels[reliable ? 0 : 1];
    ENetList *queue = reliable ? &channel->incomingReliableCommands : &channel->incomingUnreliableCommands;
    ENetPacket *packet;
    enet_uint32 number;

    /* Bit 31 first, then again: the duplicate must not count as a second arrival. */
    deliver(host, peer, reliable, 31);
    deliver(host, peer, reliable, 31);
    CHECK(!enet_list_empty(queue));
    if (!enet_list_empty(queue))
    {
        const ENetIncomingCommand *start = (const ENetIncomingCommand *) enet_list_begin(queue);
        CHECK(start->fragmentsRemaining == FragmentCount - 1);
        CHECK(start->fragments[0] == 0x80000000u && start->fragments[1] == 0u);
    }
    for (number = 0; number < FragmentCount; ++number)
    {
        if (number == 31)
            continue;
        CHECK(enet_peer_receive(peer, NULL) == NULL);
        deliver(host, peer, reliable, number);
    }

    packet = enet_peer_receive(peer, NULL);
    CHECK(packet != NULL);
    if (packet != NULL)
    {
        CHECK(packet->dataLength == FragmentCount);
        for (number = 0; number < FragmentCount && packet->dataLength == FragmentCount; ++number)
            CHECK(packet->data[number] == fragment_byte(number));
        enet_packet_destroy(packet);
    }
    CHECK(enet_peer_receive(peer, NULL) == NULL);
    CHECK(enet_list_empty(queue));
    CHECK(peer->totalWaitingData == 0);
}

int main(void)
{
    ENetHost host = {0};
    ENetPeer peer = {0};
    ENetChannel channels[2];
    memset(channels, 0, sizeof(channels));
    host.maximumPacketSize = 1024;
    host.maximumWaitingData = 1024;
    enet_list_clear(&host.dispatchQueue);
    peer.host = &host;
    peer.state = ENET_PEER_STATE_CONNECTED;
    peer.channels = channels;
    peer.channelCount = 2;
    enet_list_clear(&peer.dispatchedCommands);
    for (int index = 0; index < 2; ++index)
    {
        enet_list_clear(&channels[index].incomingReliableCommands);
        enet_list_clear(&channels[index].incomingUnreliableCommands);
    }

    reassemble(&host, &peer, 1);
    reassemble(&host, &peer, 0);
    printf("ENET_FRAGMENT_RESULT checks=%d failures=%d\n", checks, failures);
    return failures != 0;
}
