// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetHostHandle.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using CNA::Internal::Net::ENetHostHandle;

namespace {
#ifdef __EMSCRIPTEN__
    // Emscripten's default build is fully synchronous/single-threaded: a real WebSocket handshake
    // cannot complete while C++ code holds the call stack, since nothing ever returns control to
    // Node's event loop. emscripten_sleep() genuinely yields back to Node and resumes later, but
    // only works because CnaTests is linked with -sJSPI=1 (see cmake/UnitTests.cmake).
    void PollYield() { emscripten_sleep(10); }
#else
    void PollYield() { }
#endif
}

TEST(ENetHostHandleTest, CreateHostBindsToEphemeralPort) {
#ifdef __EMSCRIPTEN__
    GTEST_SKIP() << "Emscripten's SOCKFS bind()/getsockname() shim never reports back a real "
                    "OS-assigned ephemeral port (always echoes back the literal port requested, "
                    "0 in this case) - a permanent platform limitation, not a CNA bug. See NEXT.md.";
#endif
    ENetHostHandle server = ENetHostHandle::CreateHost(0, 4, 2);
    EXPECT_TRUE(server.IsValid());
    EXPECT_GT(server.getBoundPortProperty(), 0);
}

TEST(ENetHostHandleTest, CreateClientHasNoBoundPort) {
    ENetHostHandle client = ENetHostHandle::CreateClient(2);
    EXPECT_TRUE(client.IsValid());
    EXPECT_EQ(client.getBoundPortProperty(), 0);
}

TEST(ENetHostHandleTest, MoveConstructionTransfersOwnership) {
    ENetHostHandle server = ENetHostHandle::CreateHost(0, 4, 2);
    uint16_t port = server.getBoundPortProperty();

    ENetHostHandle moved(std::move(server));
    EXPECT_TRUE(moved.IsValid());
    EXPECT_EQ(moved.getBoundPortProperty(), port);
    EXPECT_FALSE(server.IsValid());
}

TEST(ENetHostHandleTest, MoveAssignmentTransfersOwnership) {
    ENetHostHandle server = ENetHostHandle::CreateHost(0, 4, 2);
    ENetHostHandle other = ENetHostHandle::CreateClient(2);

    other = std::move(server);
    EXPECT_TRUE(other.IsValid());
    EXPECT_FALSE(server.IsValid());
}

// Smoke test: proves this sandboxed environment can actually bind a loopback UDP socket and
// exchange a real packet end-to-end, before any wire protocol/relay logic is built on top of
// that assumption.
TEST(ENetHostHandleTest, LoopbackConnectAndExchangeOnePacket) {
#ifdef __EMSCRIPTEN__
    // Emscripten's SOCKFS bind()/getsockname() shim never reports back a real ephemeral port (see
    // CreateHostBindsToEphemeralPort above), so this smoke test's actual point - a real bound host
    // exchanging a real packet - uses a fixed port here instead of ENET_PORT_ANY (0).
    constexpr uint16_t kLoopbackTestPort = 61193;
    ENetHostHandle server = ENetHostHandle::CreateHost(kLoopbackTestPort, 1, 2);
#else
    ENetHostHandle server = ENetHostHandle::CreateHost(0, 1, 2);
#endif
    uint16_t serverPort = server.getBoundPortProperty();
    ASSERT_GT(serverPort, 0);

    ENetHostHandle client = ENetHostHandle::CreateClient(2);
    ENetPeer* clientSidePeer = client.Connect("127.0.0.1", serverPort, 2);
    ASSERT_NE(clientSidePeer, nullptr);

    ENetPeer* serverSidePeer = nullptr;
    bool clientSideConnected = false;
    for (int i = 0; i < 200 && (!serverSidePeer || !clientSideConnected); ++i, PollYield()) {
        ENetEvent serverEvt{};
        if (server.Service(0, serverEvt) > 0 && serverEvt.type == ENET_EVENT_TYPE_CONNECT) {
            serverSidePeer = serverEvt.peer;
        }
        ENetEvent clientEvt{};
        if (client.Service(0, clientEvt) > 0 && clientEvt.type == ENET_EVENT_TYPE_CONNECT) {
            clientSideConnected = true;
        }
    }
    ASSERT_NE(serverSidePeer, nullptr) << "Server never observed the incoming connection";
    ASSERT_TRUE(clientSideConnected) << "Client never observed its own connection completing";

    const char payload[] = "hello";
    client.Send(clientSidePeer, 0, payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE);
    client.Flush();

    ENetPacket* received = nullptr;
    for (int i = 0; i < 200 && !received; ++i, PollYield()) {
        ENetEvent serverEvt{};
        if (server.Service(0, serverEvt) > 0 && serverEvt.type == ENET_EVENT_TYPE_RECEIVE) {
            received = serverEvt.packet;
        }
        ENetEvent clientEvt{};
        client.Service(0, clientEvt);
    }
    ASSERT_NE(received, nullptr) << "Server never received the packet";
    ASSERT_EQ(received->dataLength, sizeof(payload));
    EXPECT_STREQ(reinterpret_cast<const char*>(received->data), payload);
    enet_packet_destroy(received);
}

// Task 5.15: error-path coverage. Connect()'s address-resolution failure branch
// (enet_address_set_host_ip and enet_address_set_host both failing) was previously untested -
// ".invalid" is an RFC 2606-reserved TLD guaranteed to never resolve, so this is deterministic
// and fast (no real network round-trip needed to observe the failure).
TEST(ENetHostHandleTest, ConnectWithUnresolvableHostnameThrows) {
    ENetHostHandle client = ENetHostHandle::CreateClient(2);
    EXPECT_THROW(
        (void) client.Connect("this-hostname-should-not-resolve.invalid", 12345, 2),
        std::runtime_error
    );
}

// Task 5.15: Send()'s `if (enet_peer_send(peer, channel, packet) < 0) enet_packet_destroy(packet);`
// cleanup branch was previously untested. enet_peer_send() itself rejects (returns < 0) unless the
// peer is in ENET_PEER_STATE_CONNECTED - true immediately after Connect() returns (the peer starts
// in ENET_PEER_STATE_CONNECTING and only transitions once a real handshake completes via
// Service()), so sending before ever calling Service() deterministically exercises this path
// without needing a real connection or a timeout.
TEST(ENetHostHandleTest, SendToNotYetConnectedPeerDoesNotThrowOrLeak) {
    ENetHostHandle client = ENetHostHandle::CreateClient(2);
    ENetPeer* peer = client.Connect("127.0.0.1", 61199, 2);
    ASSERT_NE(peer, nullptr);

    const char payload[] = "hello";
    EXPECT_NO_THROW(client.Send(peer, 0, payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE));
}

// Task 5.15: Broadcast() on a host with zero connected peers - enet_host_broadcast() has nothing
// to iterate over and must be a safe no-op, not a crash.
TEST(ENetHostHandleTest, BroadcastWithZeroConnectedPeersDoesNotThrow) {
    ENetHostHandle server = ENetHostHandle::CreateHost(0, 4, 2);
    const char payload[] = "hello";
    EXPECT_NO_THROW(server.Broadcast(0, payload, sizeof(payload), ENET_PACKET_FLAG_RELIABLE));
}

// Task 5.15: the `if (!packet) return;` guard in both Send() and Broadcast(), for when
// enet_packet_create() itself returns null, is intentionally left untested - real ENet only
// returns null there on a malloc() failure (see third_party/enet/packet.c), which cannot be
// triggered deterministically without replacing the global allocator. Documented here rather
// than skipped silently; see plans/plan_net.md Task 5.15 for the same note.
