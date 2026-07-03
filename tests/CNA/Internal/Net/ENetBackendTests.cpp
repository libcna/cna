// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include <string>
#include <vector>

using namespace CNA::Internal::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::GamerJoinedEventArgs;
using Microsoft::Xna::Framework::Net::GamerLeftEventArgs;
using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionEndedEventArgs;
using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
using Microsoft::Xna::Framework::Net::NetworkSessionState;
using Microsoft::Xna::Framework::Net::NetworkSessionType;

namespace {
    // NetworkSession::Create()/BeginCreate() gates on a single process-wide activeSession_ (a
    // real, preserved FNA constraint — see NEXT.md section 4/5): only one real NetworkSession can
    // be alive in this test binary at a time. So Task 5.4's loopback tests below each use exactly
    // one real NetworkSession, paired with a raw ENetHostHandle (from Task 5.1) standing in for
    // "the other machine" — never two real NetworkSession instances at once.
    //
    // RAII-guards Dispose() (matching NetworkSessionTests.cpp's own LocalGamerFixture precedent)
    // so an ASSERT_* failure mid-test can't strand activeSession_ for every later test.
    struct SystemLinkSessionFixture {
        SignedInGamer signedIn;
        NetworkSession* session;

        explicit SystemLinkSessionFixture(const std::string& gamertag)
            : signedIn(SignedInGamer::CreateInternal(gamertag))
            , session(NetworkSession::Create(
                  NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&signedIn}, 8, 0, NetworkSessionProperties{}
              ))
        {
            session->Update(); // drain this gamer's own local GamerJoin event
        }

        ~SystemLinkSessionFixture() { session->Dispose(); }
    };
}

TEST(ENetBackendTest, RealNetworkingEnabledOnlyForSystemLink) {
    EXPECT_FALSE(ENetBackend::RealNetworkingEnabled(NetworkSessionType::Local));
    EXPECT_TRUE(ENetBackend::RealNetworkingEnabled(NetworkSessionType::SystemLink));
    EXPECT_FALSE(ENetBackend::RealNetworkingEnabled(NetworkSessionType::PlayerMatch));
    EXPECT_FALSE(ENetBackend::RealNetworkingEnabled(NetworkSessionType::Ranked));
    EXPECT_FALSE(ENetBackend::RealNetworkingEnabled(NetworkSessionType::LocalWithLeaderboards));
}

TEST(ENetBackendTest, TeardownAndPumpOnUnregisteredSessionAreSafeNoOps) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

    // Local sessions never get registered with ENetBackend at all.
    EXPECT_NO_THROW(ENetBackend::TeardownSession(session));
    EXPECT_NO_THROW(ENetBackend::PumpSession(session));
    EXPECT_EQ(ENetBackend::GetBoundPort(session), 0);

    session->Dispose();
}

TEST(ENetBackendTest, StartHostingIsIdempotent) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

    uint16_t port = ENetBackend::GetBoundPort(session);
    ASSERT_GT(port, 0);

    // Calling StartHosting again (the constructor already called it once) must not rebind or
    // otherwise change the already-registered host.
    ENetBackend::StartHosting(session);
    EXPECT_EQ(ENetBackend::GetBoundPort(session), port);

    session->Dispose();
}

TEST(ENetBackendTest, StartHostingIsNoOpForNonSystemLinkTypes) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

    ENetBackend::StartHosting(session);
    EXPECT_EQ(ENetBackend::GetBoundPort(session), 0);

    session->Dispose();
}

// --- Task 5.4: ConnectToHost handshake + roster sync ---

TEST(ENetBackendTest, ConnectToHostIsNoOpForNonSystemLinkTypes) {
    auto gamer = SignedInGamer::CreateInternal("tag1");
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

    EXPECT_NO_THROW(ENetBackend::ConnectToHost(session, "127.0.0.1", 12345));
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 1);

    session->Dispose();
}

TEST(ENetBackendTest, HostRespondsToClientHelloWithServerWelcomeAndAddsRemoteGamer) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    // A raw ENet client standing in for "the other machine" (see the fixture's own comment for
    // why this isn't a second real NetworkSession).
    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i) {
        host.session->Update(); // pumps the host's real ENet transport
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    ClientHelloMessage hello;
    hello.LocalGamertags = {"RemotePlayer"};
    auto helloBytes = NetPacketCodec::Encode(hello);
    fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    int joinCount = 0;
    host.session->GamerJoined += [&joinCount](System::Object*, const GamerJoinedEventArgs&) { ++joinCount; };

    ENetPacket* received = nullptr;
    for (int i = 0; i < 200 && !received; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            received = evt.packet;
        }
    }
    ASSERT_NE(received, nullptr);

    std::vector<SharpRuntime::bytecs> data(received->data, received->data + received->dataLength);
    EXPECT_EQ(NetPacketCodec::PeekTag(data), MessageTag::ServerWelcome);
    ServerWelcomeMessage welcome = NetPacketCodec::DecodeServerWelcome(data);
    enet_packet_destroy(received);

    ASSERT_EQ(welcome.AssignedWireIds.size(), 1u);
    ASSERT_EQ(welcome.ExistingRoster.size(), 1u);
    // The host's own local gamer, snapshotted via its real SignedInGamer gamertag (not the
    // "Stub Gamer" value NetworkGamer::getGamertagProperty() reports for local gamers).
    EXPECT_EQ(welcome.ExistingRoster[0].Gamertag, "HostPlayer");

    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    EXPECT_EQ(joinCount, 1);
    bool foundRemote = false;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") foundRemote = true;
    }
    EXPECT_TRUE(foundRemote);
}

TEST(ENetBackendTest, ClientSendsClientHelloAndProcessesServerWelcome) {
    // A raw ENet host standing in for "the real remote host machine".
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(0, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    ClientHelloMessage receivedHello;
    bool gotHello = false;
    for (int i = 0; i < 200 && !gotHello; ++i) {
        client.session->Update(); // pumps the client's transport; sends ClientHello on CONNECT
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0) {
            if (evt.type == ENET_EVENT_TYPE_CONNECT) {
                clientPeerFromHostSide = evt.peer;
            } else if (evt.type == ENET_EVENT_TYPE_RECEIVE) {
                std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                if (NetPacketCodec::PeekTag(data) == MessageTag::ClientHello) {
                    receivedHello = NetPacketCodec::DecodeClientHello(data);
                    gotHello = true;
                }
                enet_packet_destroy(evt.packet);
            }
        }
    }
    ASSERT_TRUE(gotHello);
    ASSERT_NE(clientPeerFromHostSide, nullptr);
    ASSERT_EQ(receivedHello.LocalGamertags.size(), 1u);
    EXPECT_EQ(receivedHello.LocalGamertags[0], "ClientPlayer");

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer"}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    int joinCount = 0;
    client.session->GamerJoined += [&joinCount](System::Object*, const GamerJoinedEventArgs&) { ++joinCount; };
    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i) {
        client.session->Update();
    }

    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);
    EXPECT_EQ(joinCount, 1);
    bool foundHostPlayer = false;
    for (NetworkGamer* g : client.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "HostPlayer") foundHostPlayer = true;
    }
    EXPECT_TRUE(foundHostPlayer);
}

// --- Task 5.5: AppData relay (real SendData/ReceiveData) ---

TEST(ENetBackendTest, HostDeliversAppDataFromRemoteGamerIntoLocalPacketQueue) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    ClientHelloMessage hello;
    hello.LocalGamertags = {"RemotePlayer"};
    auto helloBytes = NetPacketCodec::Encode(hello);
    fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    ServerWelcomeMessage welcome;
    bool gotWelcome = false;
    for (int i = 0; i < 200 && !gotWelcome; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            if (NetPacketCodec::PeekTag(data) == MessageTag::ServerWelcome) {
                welcome = NetPacketCodec::DecodeServerWelcome(data);
                gotWelcome = true;
            }
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_TRUE(gotWelcome);
    ASSERT_EQ(welcome.AssignedWireIds.size(), 1u);
    uint8_t remoteWireId = welcome.AssignedWireIds[0];
    uint8_t hostLocalWireId = 0; // the host's own local gamer is always assigned wire-id 0 first

    AppDataMessage appData;
    appData.SenderWireId = remoteWireId;
    appData.TargetWireId = hostLocalWireId;
    appData.Options = SendDataOptions::Reliable;
    appData.Payload = {10, 20, 30};
    auto appDataBytes = NetPacketCodec::Encode(appData);
    fakeClient.Send(peerFromClientSide, 0, appDataBytes.data(), appDataBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    LocalNetworkGamer* hostLocalGamer = host.session->getLocalGamersProperty()[0];
    for (int i = 0; i < 200 && !hostLocalGamer->getIsDataAvailableProperty(); ++i) {
        host.session->Update();
    }
    ASSERT_TRUE(hostLocalGamer->getIsDataAvailableProperty());

    std::vector<SharpRuntime::bytecs> received(3);
    NetworkGamer* sender = nullptr;
    int len = hostLocalGamer->ReceiveData(received, sender);
    EXPECT_EQ(len, 3);
    EXPECT_EQ(received, (std::vector<SharpRuntime::bytecs>{10, 20, 30}));
    ASSERT_NE(sender, nullptr);
    EXPECT_EQ(sender->getGamertagProperty(), "RemotePlayer");
}

TEST(ENetBackendTest, ClientSendDataTransmitsAppDataToHost) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(0, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    bool gotHello = false;
    for (int i = 0; i < 200 && !gotHello; ++i) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0) {
            if (evt.type == ENET_EVENT_TYPE_CONNECT) {
                clientPeerFromHostSide = evt.peer;
            } else if (evt.type == ENET_EVENT_TYPE_RECEIVE) {
                std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                if (NetPacketCodec::PeekTag(data) == MessageTag::ClientHello) {
                    gotHello = true;
                }
                enet_packet_destroy(evt.packet);
            }
        }
    }
    ASSERT_TRUE(gotHello);
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer"}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i) {
        client.session->Update();
    }
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);

    NetworkGamer* hostPlayerProxy = nullptr;
    for (NetworkGamer* g : client.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "HostPlayer") hostPlayerProxy = g;
    }
    ASSERT_NE(hostPlayerProxy, nullptr);

    LocalNetworkGamer* clientLocalGamer = client.session->getLocalGamersProperty()[0];
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4};
    clientLocalGamer->SendData(payload, SendDataOptions::Reliable, hostPlayerProxy);
    client.session->Update(); // dequeues the PacketSend event and transmits it via ENet

    ENetPacket* received = nullptr;
    for (int i = 0; i < 200 && !received; ++i) {
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            received = evt.packet;
        }
    }
    ASSERT_NE(received, nullptr);

    std::vector<SharpRuntime::bytecs> data(received->data, received->data + received->dataLength);
    EXPECT_EQ(NetPacketCodec::PeekTag(data), MessageTag::AppData);
    AppDataMessage decoded = NetPacketCodec::DecodeAppData(data);
    enet_packet_destroy(received);

    EXPECT_EQ(decoded.SenderWireId, 5);
    EXPECT_EQ(decoded.TargetWireId, 0);
    EXPECT_EQ(decoded.Options, SendDataOptions::Reliable);
    EXPECT_EQ(decoded.Payload, (std::vector<SharpRuntime::bytecs>{1, 2, 3, 4}));
}

// --- Task 5.6: Disconnect/leave handling ---

TEST(ENetBackendTest, HostRemovesGamerAndFiresGamerLeftOnClientDisconnect) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    ClientHelloMessage hello;
    hello.LocalGamertags = {"RemotePlayer"};
    auto helloBytes = NetPacketCodec::Encode(hello);
    fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    int leftCount = 0;
    host.session->GamerLeft += [&leftCount](System::Object*, const GamerLeftEventArgs&) { ++leftCount; };

    fakeClient.Disconnect(peerFromClientSide, 0);
    fakeClient.Flush();

    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() > 1; ++i) {
        host.session->Update();
    }

    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(host.session->getRemoteGamersProperty().getCountProperty(), 0);
    ASSERT_EQ(host.session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(host.session->getPreviousGamersProperty()[0]->getGamertagProperty(), "RemotePlayer");
    EXPECT_EQ(leftCount, 1);
}

TEST(ENetBackendTest, ClientRaisesSessionEndedOnHostDisconnect) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(0, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    int endedCount = 0;
    NetworkSessionEndReason observedReason = NetworkSessionEndReason::ClientSignedOut;
    client.session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs& e) {
        ++endedCount;
        observedReason = e.getEndReasonProperty();
    };

    fakeHost.Disconnect(clientPeerFromHostSide, 0);
    fakeHost.Flush();

    for (int i = 0; i < 200 && endedCount == 0; ++i) {
        client.session->Update();
    }

    EXPECT_EQ(endedCount, 1);
    EXPECT_EQ(observedReason, NetworkSessionEndReason::HostEndedSession);
    EXPECT_EQ(client.session->getSessionStateProperty(), NetworkSessionState::Ended);
}

TEST(ENetBackendTest, ClientProcessesGamerLeaveBroadcast) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(0, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "OtherPlayer"}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i) {
        client.session->Update();
    }
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);

    int leftCount = 0;
    client.session->GamerLeft += [&leftCount](System::Object*, const GamerLeftEventArgs&) { ++leftCount; };

    GamerLeaveBroadcastMessage leave;
    leave.WireIds = {0};
    auto leaveBytes = NetPacketCodec::Encode(leave);
    fakeHost.Send(clientPeerFromHostSide, 0, leaveBytes.data(), leaveBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() > 1; ++i) {
        client.session->Update();
    }

    EXPECT_EQ(client.session->getAllGamersProperty().getCountProperty(), 1);
    ASSERT_EQ(client.session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(client.session->getPreviousGamersProperty()[0]->getGamertagProperty(), "OtherPlayer");
    EXPECT_EQ(leftCount, 1);
}

// --- Task 5.7: StartGame/EndGame state broadcast ---

TEST(ENetBackendTest, HostBroadcastsStateChangeOnStartAndEndGame) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    ClientHelloMessage hello;
    hello.LocalGamertags = {"RemotePlayer"};
    auto helloBytes = NetPacketCodec::Encode(hello);
    fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    host.session->StartGame();

    ENetPacket* received = nullptr;
    for (int i = 0; i < 200 && !received; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            received = evt.packet;
        }
    }
    ASSERT_NE(received, nullptr);
    {
        std::vector<SharpRuntime::bytecs> data(received->data, received->data + received->dataLength);
        EXPECT_EQ(NetPacketCodec::PeekTag(data), MessageTag::StateChangeBroadcast);
        EXPECT_EQ(NetPacketCodec::DecodeStateChangeBroadcast(data).NewState, NetworkSessionState::Playing);
        enet_packet_destroy(received);
    }
    EXPECT_EQ(host.session->getSessionStateProperty(), NetworkSessionState::Playing);

    host.session->EndGame();

    received = nullptr;
    for (int i = 0; i < 200 && !received; ++i) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            received = evt.packet;
        }
    }
    ASSERT_NE(received, nullptr);
    std::vector<SharpRuntime::bytecs> data(received->data, received->data + received->dataLength);
    EXPECT_EQ(NetPacketCodec::PeekTag(data), MessageTag::StateChangeBroadcast);
    EXPECT_EQ(NetPacketCodec::DecodeStateChangeBroadcast(data).NewState, NetworkSessionState::Lobby);
    enet_packet_destroy(received);
    EXPECT_EQ(host.session->getSessionStateProperty(), NetworkSessionState::Lobby);
}

TEST(ENetBackendTest, ClientProcessesStateChangeBroadcast) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(0, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    int startedCount = 0;
    client.session->GameStarted += [&startedCount](System::Object*, const GameStartedEventArgs&) { ++startedCount; };

    StateChangeBroadcastMessage msg;
    msg.NewState = NetworkSessionState::Playing;
    auto bytes = NetPacketCodec::Encode(msg);
    fakeHost.Send(clientPeerFromHostSide, 0, bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getSessionStateProperty() != NetworkSessionState::Playing; ++i) {
        client.session->Update();
    }

    EXPECT_EQ(client.session->getSessionStateProperty(), NetworkSessionState::Playing);
    EXPECT_EQ(startedCount, 1);
}
