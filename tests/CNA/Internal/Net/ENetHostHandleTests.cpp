// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetHostHandle.hpp"

using CNA::Internal::Net::ENetHostHandle;

TEST(ENetHostHandleTest, CreateHostBindsToEphemeralPort) {
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
    ENetHostHandle server = ENetHostHandle::CreateHost(0, 1, 2);
    uint16_t serverPort = server.getBoundPortProperty();
    ASSERT_GT(serverPort, 0);

    ENetHostHandle client = ENetHostHandle::CreateClient(2);
    ENetPeer* clientSidePeer = client.Connect("127.0.0.1", serverPort, 2);
    ASSERT_NE(clientSidePeer, nullptr);

    ENetPeer* serverSidePeer = nullptr;
    bool clientSideConnected = false;
    for (int i = 0; i < 200 && (!serverSidePeer || !clientSideConnected); ++i) {
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
    for (int i = 0; i < 200 && !received; ++i) {
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
