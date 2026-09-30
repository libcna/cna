// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>
#include <optional>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "CNA/Internal/Net/VoiceChat.hpp"
#include "CNA/Internal/GamerServices/VoiceMutes.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

using namespace CNA::Internal::Net;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::GamerJoinedEventArgs;
using Microsoft::Xna::Framework::Net::GamerLeftEventArgs;
using Microsoft::Xna::Framework::Net::HostChangedEventArgs;
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

#ifdef __EMSCRIPTEN__
    // Emscripten's SOCKFS bind()/getsockname() shim never reports back a real OS-assigned
    // ephemeral port (see NEXT.md), so the raw ENetHostHandle "fake host" stand-ins below (playing
    // "the other machine" for Client* tests) need a fixed port too - distinct from ENetBackend's
    // own kEmscriptenHostPort (61191), which the real NetworkSession under test binds to
    // independently via its own constructor in these same tests.
    constexpr uint16_t kFakeHostTestPort = 61192;
#else
    constexpr uint16_t kFakeHostTestPort = 0; // ENET_PORT_ANY - real ephemeral binding works here
#endif

#ifdef __EMSCRIPTEN__
    // Emscripten's default build is fully synchronous/single-threaded: a real WebSocket handshake
    // cannot complete while C++ code holds the call stack, since nothing ever returns control to
    // Node's event loop (confirmed empirically - even a real-time sleep loop never lets it finish
    // without this). emscripten_sleep() genuinely yields back to Node and resumes later, but only
    // works because CnaTests is linked with -sJSPI=1 (see cmake/UnitTests.cmake). Called once per
    // polling-loop iteration below in place of native/Windows's instant, no-delay-needed spin.
    void PollYield() { emscripten_sleep(10); }
#else
    void PollYield() { }
#endif

    // Task 6.4/6.5 (plans/plan_net.md Phase 6): shared handshake helper for the SimulatedLatency/
    // SimulatedPacketLoss tests below - connects fakeClient to host's real bound port, completes a
    // real ClientHello/ServerWelcome round trip, and returns the wire id the host assigned to
    // fakeClient's own gamer (needed as AppDataMessage::SenderWireId for every AppData sent below).
    uint8_t ConnectFakeClientAndCompleteHandshake(
        ENetHostHandle& fakeClient,
        NetworkSession* hostSession,
        ENetPeer** outPeerFromHostSide,
        ServerWelcomeMessage* outWelcome = nullptr
    ) {
        uint16_t hostPort = ENetBackend::GetBoundPort(hostSession);
        EXPECT_GT(hostPort, 0);
        *outPeerFromHostSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
        EXPECT_NE(*outPeerFromHostSide, nullptr);

        bool connected = false;
        for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
            hostSession->Update();
            ENetEvent evt{};
            if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
                connected = true;
            }
        }
        EXPECT_TRUE(connected);

        ClientHelloMessage hello;
        hello.LocalGamertags = {"RemotePlayer"};
        auto helloBytes = NetPacketCodec::Encode(hello);
        fakeClient.Send(*outPeerFromHostSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
        fakeClient.Flush();

        ServerWelcomeMessage welcome;
        bool gotWelcome = false;
        for (int i = 0; i < 200 && !gotWelcome; ++i, PollYield()) {
            hostSession->Update();
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
        EXPECT_TRUE(gotWelcome);
        EXPECT_EQ(welcome.AssignedWireIds.size(), 1u);
        if (outWelcome != nullptr) {
            *outWelcome = welcome;
        }
        return welcome.AssignedWireIds.empty() ? 0 : welcome.AssignedWireIds[0];
    }

    // A real client session joined to a raw ENet host that stands in for the host machine and
    // answers only what a test sends it. One gamer signed in and a local limit of 2 leave room for
    // AddLocalGamer. Welcomed by default with OtherPlayer, the host, at wire id 0 and this machine's
    // gamer numbered 5.
    struct FakeHostedClient {
        SignedInGamer signedIn{SignedInGamer::CreateInternal("ClientPlayer")};
        ENetHostHandle fakeHost{ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2)};
        NetworkSession* session{nullptr};
        ENetPeer* peer{nullptr}; // the client, as the fake host sees it
        NetworkGamer* otherPlayer{nullptr};

        explicit FakeHostedClient(bool welcome = true) {
            Gamer::setSignedInGamersProperty(
                new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&signedIn}))
            );
            session = NetworkSession::Create(NetworkSessionType::SystemLink, 2, 8, 0, NetworkSessionProperties{});
            session->Update();
            ENetBackend::ConnectToHost(session, "127.0.0.1", fakeHost.getBoundPortProperty());
            // Waiting for the hello fixes which gamers it names.
            EXPECT_TRUE(ReceiveAtHost(MessageTag::ClientHello).has_value());
            if (welcome) {
                Welcome();
            }
        }

        ~FakeHostedClient() {
            session->Dispose();
            // setSignedInGamersProperty deletes the collection it replaces, so restore a fresh one.
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }

        void Welcome() {
            ServerWelcomeMessage message;
            message.AssignedWireIds = {5};
            message.ExistingRoster = {RosterEntry{0, "OtherPlayer", true}};
            SendFromHost(NetPacketCodec::Encode(message));
            for (int i = 0; i < 200 && otherPlayer == nullptr; ++i, PollYield()) {
                session->Update();
                for (NetworkGamer* g : session->getAllGamersProperty()) {
                    if (g->getGamertagProperty() == "OtherPlayer") otherPlayer = g;
                }
            }
            EXPECT_NE(otherPlayer, nullptr);
        }

        void SendFromHost(const std::vector<SharpRuntime::bytecs>& bytes) {
            ASSERT_NE(peer, nullptr);
            fakeHost.Send(peer, 0, bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
            fakeHost.Flush();
        }

        // The next message with the given tag to reach the fake host; any other is skipped.
        std::optional<std::vector<SharpRuntime::bytecs>> ReceiveAtHost(MessageTag tag, int attempts = 200) {
            for (int i = 0; i < attempts; ++i, PollYield()) {
                session->Update();
                ENetEvent evt{};
                while (fakeHost.Service(0, evt) > 0) {
                    if (evt.type == ENET_EVENT_TYPE_CONNECT) peer = evt.peer;
                    if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
                    std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                    enet_packet_destroy(evt.packet);
                    if (NetPacketCodec::PeekTag(data) == tag) return data;
                }
            }
            return std::nullopt;
        }

        LocalNetworkGamer* LocalFor(const SignedInGamer& gamer) const {
            for (LocalNetworkGamer* local : session->getLocalGamersProperty()) {
                if (local->getSignedInGamerProperty() == &gamer) return local;
            }
            return nullptr;
        }
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
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    // Task 12.3: subscribing just above already replayed once for the host's own pre-existing
    // local gamer ("HostPlayer") - reset so this test isolates the real, queued join below.
    joinCount = 0;

    ENetPacket* received = nullptr;
    for (int i = 0; i < 200 && !received; ++i, PollYield()) {
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
    // The host's own local gamer, snapshotted with its SignedInGamer's gamertag.
    EXPECT_EQ(welcome.ExistingRoster[0].Gamertag, "HostPlayer");

    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    EXPECT_EQ(joinCount, 1);
    NetworkGamer* remoteGamer = nullptr;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        // Only the remote gamer is looked for by gamertag; the host's own local gamer is taken
        // from LocalGamers below.
        if (g->getGamertagProperty() == "RemotePlayer") remoteGamer = g;
    }
    ASSERT_NE(remoteGamer, nullptr);
    NetworkGamer* localGamer = host.session->getLocalGamersProperty()[0];
    // Task 12.2 (DEFERRED.md item #20): NetworkGamer::Id is now the real, wire-negotiated id
    // (not FNA's hardcoded 0), and only the host's own local gamer reports IsHost == true.
    EXPECT_EQ(remoteGamer->getIdProperty(), welcome.AssignedWireIds[0]);
    EXPECT_EQ(localGamer->getIdProperty(), 0);
    EXPECT_FALSE(remoteGamer->getIsHostProperty());
    EXPECT_TRUE(localGamer->getIsHostProperty());
}

TEST(ENetBackendTest, ClientSendsClientHelloAndProcessesServerWelcome) {
    // A raw ENet host standing in for "the real remote host machine".
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    ClientHelloMessage receivedHello;
    bool gotHello = false;
    for (int i = 0; i < 200 && !gotHello; ++i, PollYield()) {
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
    // Task 4.6: marking this entry IsHost=true - the fake host's own roster entry represents
    // the actual host, so the real client's resulting NetworkGamer should report IsHost==true.
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer", true}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    int joinCount = 0;
    client.session->GamerJoined += [&joinCount](System::Object*, const GamerJoinedEventArgs&) { ++joinCount; };
    // Task 12.3: subscribing just above already replayed once for the client's own pre-existing
    // local gamer ("ClientPlayer") - reset so this test isolates the real, queued join below.
    joinCount = 0;
    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        client.session->Update();
    }

    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);
    EXPECT_EQ(joinCount, 1);
    NetworkGamer* hostPlayer = nullptr;
    for (NetworkGamer* g : client.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "HostPlayer") hostPlayer = g;
    }
    ASSERT_NE(hostPlayer, nullptr);
    // Task 12.2 (DEFERRED.md item #20): the client's own local gamer gets the host-assigned wire
    // id (5, per welcome.AssignedWireIds above); the remote "HostPlayer" gamer gets its roster
    // wire id (0).
    EXPECT_EQ(client.session->getLocalGamersProperty()[0]->getIdProperty(), 5);
    EXPECT_EQ(hostPlayer->getIdProperty(), 0);
    // Task 4.6: RosterEntry now carries a real host flag, so the remote "HostPlayer" gamer
    // correctly reports IsHost == true here (previously a documented, scoped limitation - see
    // cna-samples/DEFERRED.md item #20's own "still-open" note, now resolved). Not asserting
    // anything about this fixture's own local gamer's IsHost here - it's constructed via
    // NetworkSession::Create() (see SystemLinkSessionFixture above), so it legitimately reports
    // IsHost == true regardless, reflecting this test's Create()-based fixture setup rather than
    // the real Join()-based client path exercised by NetworkSessionTests.cpp's
    // JoinInvitedMakesLocalGamersReportIsHostFalse.
    EXPECT_TRUE(hostPlayer->getIsHostProperty());
    // Reference GamerCollection order: by session index, the same on every machine, so the host
    // that was there first precedes this machine's own gamer.
    EXPECT_EQ(client.session->getAllGamersProperty()[0], hostPlayer);
    EXPECT_EQ(client.session->getAllGamersProperty()[1], client.session->getLocalGamersProperty()[0]);
    std::vector<std::string> replayed;
    client.session->GamerJoined += [&replayed](System::Object*, const GamerJoinedEventArgs& args) {
        replayed.push_back(args.getGamerProperty()->getGamertagProperty());
    };
    EXPECT_EQ(replayed, (std::vector<std::string>{"HostPlayer", "ClientPlayer"}));
}

// Reference NetworkSession.AddLocalGamer on the host: the new gamer is numbered at once, after
// everyone already in the session, and every machine already here is told of it, so it can send
// straight away. A machine that joins later finds it in the roster of its welcome.
TEST(ENetBackendTest, HostLocalGamerAddedMidSessionIsAnnouncedAndSendsAtOnce) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    // One gamer signed in and a local limit of 2 leave room for AddLocalGamer.
    SignedInGamer hostSignedIn = SignedInGamer::CreateInternal("HostPlayer");
    Gamer::setSignedInGamersProperty(
        new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&hostSignedIn}))
    );
    // setSignedInGamersProperty deletes the collection it replaces, so restore a fresh one.
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    NetworkSession* hostSession = NetworkSession::Create(NetworkSessionType::SystemLink, 2, 8, 0, NetworkSessionProperties{});
    struct DisposeGuard {
        NetworkSession* s;
        ~DisposeGuard() { s->Dispose(); }
    } disposeGuard{hostSession};
    hostSession->Update();

    ENetHostHandle fakeClient1 = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide1 = nullptr;
    uint8_t remoteWireId1 = ConnectFakeClientAndCompleteHandshake(fakeClient1, hostSession, &peerFromHostSide1);

    NetworkGamer* remoteGamer1 = nullptr;
    for (NetworkGamer* g : hostSession->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") remoteGamer1 = g;
    }
    ASSERT_NE(remoteGamer1, nullptr);

    SignedInGamer secondSignedIn = SignedInGamer::CreateInternal("HostPlayer2");
    hostSession->AddLocalGamer(&secondSignedIn);
    ASSERT_EQ(hostSession->getLocalGamersProperty().getCountProperty(), 2);
    LocalNetworkGamer* firstLocal = hostSession->getLocalGamersProperty()[0];
    LocalNetworkGamer* secondLocal = hostSession->getLocalGamersProperty()[1];
    ASSERT_EQ(secondLocal->getSignedInGamerProperty(), &secondSignedIn);
    const uint8_t secondWireId = secondLocal->getIdProperty();
    EXPECT_NE(secondWireId, firstLocal->getIdProperty());
    EXPECT_NE(secondWireId, remoteWireId1);
    ASSERT_EQ(hostSession->getAllGamersProperty().getCountProperty(), 3);
    EXPECT_EQ(hostSession->getAllGamersProperty()[2], secondLocal);

    const std::vector<SharpRuntime::bytecs> payload{9, 8, 7};
    secondLocal->SendData(payload, SendDataOptions::Reliable, remoteGamer1);

    std::optional<RosterEntry> announced;
    std::optional<AppDataMessage> appData;
    for (int i = 0; i < 200 && !(announced && appData); ++i, PollYield()) {
        hostSession->Update();
        ENetEvent evt{};
        while (fakeClient1.Service(0, evt) > 0) {
            if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            enet_packet_destroy(evt.packet);
            if (NetPacketCodec::PeekTag(data) == MessageTag::GamerJoinBroadcast) {
                for (const RosterEntry& entry : NetPacketCodec::DecodeGamerJoinBroadcast(data).NewGamers) {
                    if (entry.Gamertag == "HostPlayer2") announced = entry;
                }
            } else if (NetPacketCodec::PeekTag(data) == MessageTag::AppData) {
                appData = NetPacketCodec::DecodeAppData(data);
            }
        }
    }
    ASSERT_TRUE(announced.has_value()) << "the client was never told of the host's new gamer";
    EXPECT_EQ(announced->WireId, secondWireId);
    EXPECT_EQ(announced->IsHost, secondLocal->getIsHostProperty());
    ASSERT_TRUE(appData.has_value()) << "the new gamer's send never arrived";
    EXPECT_EQ(appData->SenderWireId, secondWireId);
    EXPECT_EQ(appData->TargetWireId, remoteWireId1);
    EXPECT_EQ(appData->Options, SendDataOptions::Reliable);
    EXPECT_EQ(appData->Payload, payload);
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before);

    ENetHostHandle fakeClient2 = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide2 = nullptr;
    ServerWelcomeMessage welcome2;
    ConnectFakeClientAndCompleteHandshake(fakeClient2, hostSession, &peerFromHostSide2, &welcome2);
    bool listed = false;
    for (const RosterEntry& entry : welcome2.ExistingRoster) {
        listed = listed || (entry.Gamertag == "HostPlayer2" && entry.WireId == secondWireId);
    }
    EXPECT_TRUE(listed) << "a later machine's welcome did not list the host's added gamer";
}

// Reference NetworkSession.AddLocalGamer on a client: the host numbers every gamer, so the client
// asks it to admit the new one. Until the host's join broadcast names it, the gamer has no wire
// id; what it sends meanwhile is held rather than sent under an id nobody gave it, and then
// delivered in its original order.
TEST(ENetBackendTest, ClientLocalGamerIsNumberedByTheHostAndItsEarlierSendsArriveInOrder) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();
    FakeHostedClient client;
    ASSERT_NE(client.otherPlayer, nullptr);

    SignedInGamer secondSignedIn = SignedInGamer::CreateInternal("ClientPlayer2");
    client.session->AddLocalGamer(&secondSignedIn);
    LocalNetworkGamer* secondLocal = client.LocalFor(secondSignedIn);
    ASSERT_NE(secondLocal, nullptr);

    constexpr int kSendCount = 5;
    for (int i = 0; i < kSendCount; ++i) {
        secondLocal->SendData(
            std::vector<SharpRuntime::bytecs>{static_cast<SharpRuntime::bytecs>(i)}, SendDataOptions::Reliable, client.otherPlayer
        );
    }

    auto request = client.ReceiveAtHost(MessageTag::AddLocalGamerRequest);
    ASSERT_TRUE(request.has_value()) << "the client never asked the host to admit its new gamer";
    EXPECT_EQ(NetPacketCodec::DecodeAddLocalGamer(*request).Gamertag, "ClientPlayer2");
    EXPECT_FALSE(client.ReceiveAtHost(MessageTag::AppData, 10).has_value()) << "sent before the host numbered the sender";
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before);

    client.SendFromHost(NetPacketCodec::Encode(GamerJoinBroadcastMessage{{RosterEntry{9, "ClientPlayer2", false}}}));
    std::vector<SharpRuntime::bytecs> receivedOrder;
    for (int i = 0; i < kSendCount; ++i) {
        auto data = client.ReceiveAtHost(MessageTag::AppData);
        ASSERT_TRUE(data.has_value()) << "held send " << i << " was never delivered";
        AppDataMessage appData = NetPacketCodec::DecodeAppData(*data);
        EXPECT_EQ(appData.SenderWireId, 9);
        EXPECT_EQ(appData.TargetWireId, 0);
        ASSERT_EQ(appData.Payload.size(), 1u);
        receivedOrder.push_back(appData.Payload[0]);
    }
    EXPECT_EQ(receivedOrder, (std::vector<SharpRuntime::bytecs>{0, 1, 2, 3, 4}));

    // The broadcast numbered this machine's own gamer; it did not add a remote copy of it.
    EXPECT_EQ(secondLocal->getIdProperty(), 9);
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 3);
    EXPECT_EQ(client.session->getAllGamersProperty()[2], secondLocal);
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before);
}

// A gamer added while the machine is still joining is not in its hello. It is requested once the
// welcome has numbered the others, exactly once, and not before: until then the host has no
// machine to put it on.
TEST(ENetBackendTest, ClientLocalGamerAddedWhileJoiningIsRequestedOnceWelcomed) {
    FakeHostedClient client(false);
    SignedInGamer secondSignedIn = SignedInGamer::CreateInternal("ClientPlayer2");
    client.session->AddLocalGamer(&secondSignedIn);
    EXPECT_FALSE(client.ReceiveAtHost(MessageTag::AddLocalGamerRequest, 10).has_value());

    client.Welcome();
    ASSERT_NE(client.otherPlayer, nullptr);
    auto request = client.ReceiveAtHost(MessageTag::AddLocalGamerRequest);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(NetPacketCodec::DecodeAddLocalGamer(*request).Gamertag, "ClientPlayer2");
    EXPECT_EQ(client.LocalFor(client.signedIn)->getIdProperty(), 5);
    EXPECT_FALSE(client.ReceiveAtHost(MessageTag::AddLocalGamerRequest, 10).has_value()) << "asked twice";
}

// audit_net.md remediation (2026-07-18): the bounded side of the same contract - a caller that
// keeps calling SendData on an unresolved sender/target faster than the queue can ever drain
// (kMaxPendingPreHandshakeAppData reached) must not grow the queue without limit; the oldest
// entry is evicted and counted via GetDroppedAppDataCount(), same observable meaning that counter
// always had.
TEST(ENetBackendTest, PendingAppDataQueueEvictsOldestOnceBoundIsReached) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    // fakeHost never completes the handshake (it's a raw, non-session-aware ENet host - see
    // kFakeHostTestPort's own comment), so client.session's wire-id map never resolves for the
    // remainder of this test - every one of these sends stays queued (or gets evicted), never
    // delivered nor individually drop-counted.
    LocalNetworkGamer* localGamer = client.session->getLocalGamersProperty()[0];
    NetworkGamer notYetKnownRemote = NetworkGamer::CreateInternal(client.session, "SomeRemotePlayer");
    constexpr int kSendsBeyondBound = 5;
    for (int i = 0; i < 64 + kSendsBeyondBound; ++i) {
        localGamer->SendData(
            std::vector<SharpRuntime::bytecs>{static_cast<SharpRuntime::bytecs>(i)}, SendDataOptions::None, &notYetKnownRemote
        );
    }
    // SendData only queues a NetworkSession-level PacketSend event; a single Update() call drains
    // every one of them in order (NetworkSession::Update's own while loop), each in turn calling
    // ENetBackend::SendAppData - exercising the enqueue-or-evict path exactly 69 times.
    client.session->Update();

    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before + kSendsBeyondBound);
}

// audit_net.md remediation (2026-07-18, third round): a real gap the second round's own queue
// missed - HandleGamerLeaveBroadcast (a client learning a *third* peer left, distinct from
// HandleDisconnect's own direct-connection-lost path) never purged PendingPreHandshakeSends,
// so a pending send naming that departed gamer as its target would sit in the queue
// indefinitely (GamerToWireId can never regain an entry for a gamer who left). Proves the fix:
// queue a send whose sender is a still-unresolved second local gamer (a client's AddLocalGamer
// the host has not answered yet) targeting an already-resolved remote
// gamer, then have that remote gamer "leave" via a real GamerLeaveBroadcastMessage - the pending
// entry must be purged (counted, not silently vanish) rather than linger forever.
TEST(ENetBackendTest, GamerLeaveBroadcastPurgesPendingAppDataNamingTheDepartedGamer) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    SignedInGamer clientSignedIn = SignedInGamer::CreateInternal("ClientPlayer");
    Gamer::setSignedInGamersProperty(
        new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&clientSignedIn}))
    );
    // setSignedInGamersProperty() deletes whatever it's currently pointing at before assigning
    // the new value (confirmed by reading Gamer.cpp directly) - restoring to a fresh *empty*
    // collection here, rather than a captured "previous" pointer this test's own
    // setSignedInGamersProperty() call above already deleted, avoids a real double-free.
    // Matches NetworkSessionTests.cpp's own DisposeFreesEveryGamerTheSessionEverOwned pattern.
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    NetworkSession* clientSession =
        NetworkSession::Create(NetworkSessionType::SystemLink, 2, 8, 0, NetworkSessionProperties{});
    struct DisposeGuard {
        NetworkSession* s;
        ~DisposeGuard() { s->Dispose(); }
    } disposeGuard{clientSession};
    ASSERT_EQ(clientSession->getLocalGamersProperty().getCountProperty(), 1);
    clientSession->Update();

    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);
    ENetBackend::ConnectToHost(clientSession, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        clientSession->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "OtherPlayer", true}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();
    for (int i = 0; i < 200 && clientSession->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        clientSession->Update();
    }
    ASSERT_EQ(clientSession->getAllGamersProperty().getCountProperty(), 2);

    NetworkGamer* otherPlayer = nullptr;
    for (NetworkGamer* g : clientSession->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "OtherPlayer") otherPlayer = g;
    }
    ASSERT_NE(otherPlayer, nullptr);

    // A second local gamer added after the handshake already completed has no wire id until the
    // host answers its AddLocalGamerRequest - which this raw fake host never does, so the sender
    // stays unresolved for the rest of the test.
    // AddLocalGamer also adds the new gamer to getAllGamersProperty() itself (not just
    // getLocalGamersProperty()), so the "all gamers" count below is 3 from here on, not 2.
    SignedInGamer secondSignedIn = SignedInGamer::CreateInternal("ClientPlayer2");
    clientSession->AddLocalGamer(&secondSignedIn);
    ASSERT_EQ(clientSession->getLocalGamersProperty().getCountProperty(), 2);
    ASSERT_EQ(clientSession->getAllGamersProperty().getCountProperty(), 3);
    LocalNetworkGamer* secondLocal = clientSession->getLocalGamersProperty()[1];

    secondLocal->SendData(std::vector<SharpRuntime::bytecs>{4, 2}, SendDataOptions::None, otherPlayer);
    clientSession->Update(); // drains the PacketSend event -> SendAppData -> enqueue (sender unresolved)
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before) << "should be queued, not yet dropped";

    GamerLeaveBroadcastMessage leave;
    leave.WireIds = {0}; // OtherPlayer
    auto leaveBytes = NetPacketCodec::Encode(leave);
    fakeHost.Send(clientPeerFromHostSide, 0, leaveBytes.data(), leaveBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();
    for (int i = 0; i < 200 && clientSession->getAllGamersProperty().getCountProperty() > 2; ++i, PollYield()) {
        clientSession->Update();
    }
    // OtherPlayer left: 3 -> 2 (ClientPlayer's own local gamer + the never-resolved ClientPlayer2).
    ASSERT_EQ(clientSession->getAllGamersProperty().getCountProperty(), 2);

    // The pending send named OtherPlayer as its target - now gone, it must have been purged and
    // counted, not left to linger in the queue forever.
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before + 1);
}

// NetworkSession.BytesPerSecondSent/Received and NetworkGamer.RoundtripTime on a SystemLink host:
// the rates come from the host's wire totals over each second, and every client is told, about once
// a second, the host's round trip to each gamer on a client machine.
TEST(ENetBackendTest, HostMeasuresTrafficAndPublishesItsRoundTripsToClientGamers) {
    SystemLinkSessionFixture host("HostPlayer");
    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide = nullptr;
    const uint8_t remoteWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromHostSide);

    std::optional<RoundtripEntry> reported;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline &&
           !(reported && host.session->getBytesPerSecondSentProperty() > 0 && host.session->getBytesPerSecondReceivedProperty() > 0)) {
        host.session->Update();
        ENetEvent evt{};
        while (fakeClient.Service(0, evt) > 0) {
            if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            enet_packet_destroy(evt.packet);
            if (NetPacketCodec::PeekTag(data) != MessageTag::NetworkStatsBroadcast) continue;
            for (const RoundtripEntry& entry : NetPacketCodec::DecodeNetworkStats(data).Entries) {
                if (entry.WireId == remoteWireId) reported = entry;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_TRUE(reported.has_value()) << "the host never published its round trip to the client's gamer";
    NetworkGamer* remote = nullptr;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") remote = g;
    }
    ASSERT_NE(remote, nullptr);
    EXPECT_GT(remote->getRoundtripTimeProperty(), System::TimeSpan::Zero);
    EXPECT_GT(host.session->getBytesPerSecondSentProperty(), 0);
    EXPECT_GT(host.session->getBytesPerSecondReceivedProperty(), 0);
    EXPECT_EQ(host.session->getLocalGamersProperty()[0]->getRoundtripTimeProperty(), System::TimeSpan::Zero);
}

// A SystemLink client reaches the host's gamers in one round trip to the host; a gamer on another
// client is reached through the host, so the host's reported round trip to it is added. Reports
// naming this machine's own gamers are ignored.
TEST(ENetBackendTest, ClientAddsTheHostsRoundTripForAGamerOnAnotherClient) {
    FakeHostedClient client;
    ASSERT_NE(client.otherPlayer, nullptr);
    client.SendFromHost(NetPacketCodec::Encode(GamerJoinBroadcastMessage{{RosterEntry{7, "ThirdPlayer", false}}}));
    NetworkGamer* third = nullptr;
    for (int i = 0; i < 200 && third == nullptr; ++i, PollYield()) {
        client.session->Update();
        for (NetworkGamer* g : client.session->getAllGamersProperty()) {
            if (g->getGamertagProperty() == "ThirdPlayer") third = g;
        }
    }
    ASSERT_NE(third, nullptr);

    client.SendFromHost(NetPacketCodec::Encode(NetworkStatsMessage{{RoundtripEntry{7, 40}, RoundtripEntry{5, 99}}}));
    const auto relayedBy40 = [&] {
        return third->getRoundtripTimeProperty() - client.otherPlayer->getRoundtripTimeProperty() ==
            System::TimeSpan::FromMilliseconds(40);
    };
    for (int i = 0; i < 200 && !relayedBy40(); ++i, PollYield()) {
        client.session->Update();
    }
    EXPECT_TRUE(relayedBy40());
    EXPECT_GT(client.otherPlayer->getRoundtripTimeProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(client.LocalFor(client.signedIn)->getRoundtripTimeProperty(), System::TimeSpan::Zero);
}

// The host's side of a client's AddLocalGamer: the requested gamer joins the requesting machine,
// every machine is told its id, and it leaves when that machine does.
TEST(ENetBackendTest, HostAdmitsAClientsAddedGamerOntoThatClientsMachine) {
    SystemLinkSessionFixture host("HostPlayer");
    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide = nullptr;
    const uint8_t remoteWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromHostSide);

    const auto request = NetPacketCodec::Encode(AddLocalGamerMessage{"RemotePlayer2"});
    fakeClient.Send(peerFromHostSide, 0, request.data(), request.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    std::optional<RosterEntry> announced;
    for (int i = 0; i < 200 && !announced; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        while (fakeClient.Service(0, evt) > 0) {
            if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            enet_packet_destroy(evt.packet);
            if (NetPacketCodec::PeekTag(data) != MessageTag::GamerJoinBroadcast) continue;
            for (const RosterEntry& entry : NetPacketCodec::DecodeGamerJoinBroadcast(data).NewGamers) {
                if (entry.Gamertag == "RemotePlayer2") announced = entry;
            }
        }
    }
    ASSERT_TRUE(announced.has_value()) << "the requesting machine was never told its gamer's id";
    EXPECT_FALSE(announced->IsHost);

    NetworkGamer* first = nullptr;
    NetworkGamer* second = nullptr;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") first = g;
        if (g->getGamertagProperty() == "RemotePlayer2") second = g;
    }
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->getIdProperty(), announced->WireId);
    EXPECT_NE(second->getIdProperty(), remoteWireId);
    EXPECT_EQ(&second->getMachineProperty(), &first->getMachineProperty());
    EXPECT_EQ(first->getMachineProperty().getGamersProperty().getCountProperty(), 2);

    int leftCount = 0;
    host.session->GamerLeft += [&leftCount](System::Object*, const GamerLeftEventArgs&) { ++leftCount; };
    fakeClient.Disconnect(peerFromHostSide, 0);
    fakeClient.Flush();
    for (int i = 0; i < 200 && leftCount < 2; ++i, PollYield()) {
        host.session->Update();
    }
    EXPECT_EQ(leftCount, 2);
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
}

// audit_net.md remediation (2026-07-18, third round): the whole-queue-invalidation sibling -
// this peer's own connection to the host being lost (no migration configured) must discard
// every still-pending send, counted, not just the ones naming a specific departed gamer.
TEST(ENetBackendTest, ClientSessionEndedOnHostDisconnectDropsAndCountsEveryPendingSend) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    // Never completes the handshake (no ServerWelcome sent) - every queued send below stays
    // unresolved right up until the disconnect.
    LocalNetworkGamer* localGamer = client.session->getLocalGamersProperty()[0];
    NetworkGamer notYetKnownRemote = NetworkGamer::CreateInternal(client.session, "SomeRemotePlayer");
    constexpr int kQueuedSends = 3;
    for (int i = 0; i < kQueuedSends; ++i) {
        localGamer->SendData(
            std::vector<SharpRuntime::bytecs>{static_cast<SharpRuntime::bytecs>(i)}, SendDataOptions::None, &notYetKnownRemote
        );
    }
    client.session->Update();
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before) << "should be queued, not yet dropped";

    int sessionEndedCount = 0;
    client.session->SessionEnded +=
        [&sessionEndedCount](System::Object*, const NetworkSessionEndedEventArgs&) { ++sessionEndedCount; };
    fakeHost.Disconnect(clientPeerFromHostSide, 0);
    fakeHost.Flush();
    for (int i = 0; i < 200 && sessionEndedCount == 0; ++i, PollYield()) {
        client.session->Update();
    }
    ASSERT_EQ(sessionEndedCount, 1);

    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before + kQueuedSends);
}

// audit_net.md remediation (2026-07-18, third round): the same whole-queue-invalidation, but via
// the host-migration reset path instead of a plain client-side session-ended path (distinct code
// path in ENetBackend.cpp - see AttemptHostMigration's own shared cleanup block).
TEST(ENetBackendTest, HostMigrationResetDropsAndCountsEveryPendingSend) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    SystemLinkSessionFixture client("ClientPlayer");
    client.session->setAllowHostMigrationProperty(true);

    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "OtherPlayer", true}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();
    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        client.session->Update();
    }
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);

    NetworkGamer* otherPlayer = nullptr;
    for (NetworkGamer* g : client.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "OtherPlayer") otherPlayer = g;
    }
    ASSERT_NE(otherPlayer, nullptr);
    // otherPlayer is the only other known gamer - AttemptHostMigration will promote this client
    // itself once the host connection drops (ClientPromotesItselfWhenItIsTheOnlyKnownSurvivor's
    // own established scenario), which still runs the same shared wire-id-map reset this test
    // is about, regardless of which specific migration outcome is chosen.
    NetworkGamer notYetKnownRemote = NetworkGamer::CreateInternal(client.session, "NeverJoins");
    client.session->getLocalGamersProperty()[0]->SendData(
        std::vector<SharpRuntime::bytecs>{7}, SendDataOptions::None, &notYetKnownRemote
    );
    client.session->Update();
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before) << "should be queued, not yet dropped";

    int hostChangedCount = 0;
    client.session->HostChanged += [&hostChangedCount](System::Object*, const HostChangedEventArgs&) { ++hostChangedCount; };
    fakeHost.Disconnect(clientPeerFromHostSide, 0);
    fakeHost.Flush();
    for (int i = 0; i < 200 && hostChangedCount == 0; ++i, PollYield()) {
        client.session->Update();
    }
    ASSERT_EQ(hostChangedCount, 1);

    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before + 1);
}

// audit_net.md remediation (2026-07-18, third round): Dispose() itself needs no explicit purge -
// TeardownSession's Sessions().erase(session) destroys the whole SessionState (including
// PendingPreHandshakeSends) in one step, and nothing reads it afterward - but that safety claim
// had no test. Proves a pending, permanently-unresolved send survives to Dispose() without
// crashing (would show up as a real ASan use-after-free/leak under CHECKLIST.md's ASan build if
// this were actually broken, not just this plain build).
TEST(ENetBackendTest, DisposeWithPendingAppDataInQueueIsSafe) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    auto* signedIn = new SignedInGamer(SignedInGamer::CreateInternal("ClientPlayer"));
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{signedIn}, 8, 0, NetworkSessionProperties{}
    );
    session->Update();
    ENetBackend::ConnectToHost(session, "127.0.0.1", fakeHostPort);

    LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
    NetworkGamer notYetKnownRemote = NetworkGamer::CreateInternal(session, "SomeRemotePlayer");
    localGamer->SendData(std::vector<SharpRuntime::bytecs>{1, 2, 3}, SendDataOptions::None, &notYetKnownRemote);
    session->Update(); // enqueues - fakeHost never replies, so it's still pending at Dispose() below

    EXPECT_NO_THROW(session->Dispose());
    delete signedIn;
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
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && !gotWelcome; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && !hostLocalGamer->getIsDataAvailableProperty(); ++i, PollYield()) {
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

// Task 5.13: every scenario above is a single host + at most one client. HandleAppData's
// host-relay-between-two-other-peers branch (~ENetBackend.cpp lines 301-310, guarded by
// `state.HostPeer == nullptr` and `target->getIsLocalProperty() == false`) is the single most
// complex routing logic in the file, and was never exercised with a genuine third connected
// party - every prior AppData test always targeted the host's own local gamer. This test adds
// two independent fake clients (PeerA, PeerB) so PeerA can target PeerB directly, forcing the
// real host-relay path instead of the local-delivery or drop paths.
TEST(ENetBackendTest, HostRelaysAppDataBetweenTwoNonLocalPeers) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    struct HandshakeResult {
        ENetPeer* peerFromClientSide;
        uint8_t wireId;
    };

    auto connectAndHandshake = [&](ENetHostHandle& fakeClient, const std::string& gamertag) {
        ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
        EXPECT_NE(peerFromClientSide, nullptr);

        bool connected = false;
        for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
            host.session->Update();
            ENetEvent evt{};
            if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
                connected = true;
            }
        }
        EXPECT_TRUE(connected);

        ClientHelloMessage hello;
        hello.LocalGamertags = {gamertag};
        auto helloBytes = NetPacketCodec::Encode(hello);
        fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
        fakeClient.Flush();

        ServerWelcomeMessage welcome;
        bool gotWelcome = false;
        for (int i = 0; i < 200 && !gotWelcome; ++i, PollYield()) {
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
        EXPECT_TRUE(gotWelcome);
        EXPECT_EQ(welcome.AssignedWireIds.size(), 1u);
        uint8_t assigned = welcome.AssignedWireIds.empty() ? uint8_t{0xFF} : welcome.AssignedWireIds[0];
        return HandshakeResult{peerFromClientSide, assigned};
    };

    ENetHostHandle fakeClientA = ENetHostHandle::CreateClient(2);
    HandshakeResult a = connectAndHandshake(fakeClientA, "PeerA");
    ASSERT_NE(a.wireId, 0xFF);

    ENetHostHandle fakeClientB = ENetHostHandle::CreateClient(2);
    HandshakeResult b = connectAndHandshake(fakeClientB, "PeerB");
    ASSERT_NE(b.wireId, 0xFF);
    ASSERT_NE(a.wireId, b.wireId);

    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 3);

    AppDataMessage appData;
    appData.SenderWireId = a.wireId;
    appData.TargetWireId = b.wireId;
    appData.Options = SendDataOptions::Reliable;
    appData.Payload = {7, 8, 9};
    auto appDataBytes = NetPacketCodec::Encode(appData);
    fakeClientA.Send(a.peerFromClientSide, 0, appDataBytes.data(), appDataBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClientA.Flush();

    AppDataMessage relayed;
    bool gotRelayed = false;
    for (int i = 0; i < 200 && !gotRelayed; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClientB.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            if (NetPacketCodec::PeekTag(data) == MessageTag::AppData) {
                relayed = NetPacketCodec::DecodeAppData(data);
                gotRelayed = true;
            }
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_TRUE(gotRelayed);
    EXPECT_EQ(relayed.SenderWireId, a.wireId);
    EXPECT_EQ(relayed.TargetWireId, b.wireId);
    EXPECT_EQ(relayed.Payload, (std::vector<SharpRuntime::bytecs>{7, 8, 9}));

    // PeerA must not receive an echo of its own relayed packet back - the host only ever
    // forwards to the actual target peer (HandleAppData's `peerIt->second != fromPeer` guard).
    bool sawAppDataAtA = false;
    for (int i = 0; i < 10; ++i) {
        ENetEvent strayEvt{};
        if (fakeClientA.Service(0, strayEvt) > 0 && strayEvt.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<SharpRuntime::bytecs> data(strayEvt.packet->data, strayEvt.packet->data + strayEvt.packet->dataLength);
            if (NetPacketCodec::PeekTag(data) == MessageTag::AppData) {
                sawAppDataAtA = true;
            }
            enet_packet_destroy(strayEvt.packet);
        }
    }
    EXPECT_FALSE(sawAppDataAtA);
}

// Task 4.3: SimulatedLatency/SimulatedPacketLoss are stored but have no effect on actual traffic
// timing or delivery, matching FNA's own reference (a plain get/set auto-property there too, with
// no delay queue or synthetic-drop logic anywhere in FNA's source) - confirmed no such logic
// exists anywhere in ENetBackend/ENetHostHandle either. Locks in the documented (inert) behavior:
// even with extreme simulated values set, a real handshake and AppData delivery complete just as
// promptly and reliably as SendDataOptionsToRecipientTransmitsAppDataToHost/
// HostDeliversAppDataFromRemoteGamerIntoLocalPacketQueue do without them.
// --- Task 6.1-6.5 (plans/plan_net.md Phase 6): real SimulatedLatency/SimulatedPacketLoss ---
//
// All 4 tests below share the same real host + raw-ENetHostHandle-fake-client loopback setup as
// every other AppData test in this file (see ConnectFakeClientAndCompleteHandshake) - real ENet
// traffic end-to-end, not unit-level queue logic, satisfying Task 6.5's own requirement together
// with Task 6.4's determinism requirement in the same tests (SetClockForTesting/
// ResetClockForTesting make the delay case exact; 0.0/1.0 loss need no RNG seeding at all - see
// ShouldDropForSimulatedLoss's own comment in ENetBackend.cpp).

TEST(ENetBackendTest, ZeroSimulatedLatencyAndPacketLossDeliverAppDataImmediately) {
    SystemLinkSessionFixture host("HostPlayer");
    // Explicit defaults - regression guard for Task 6.4's own "behaves exactly as before" case.
    ASSERT_EQ(host.session->getSimulatedLatencyProperty(), System::TimeSpan::Zero);
    ASSERT_EQ(host.session->getSimulatedPacketLossProperty(), 0.0f);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = nullptr;
    uint8_t remoteWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromClientSide);

    AppDataMessage appData;
    appData.SenderWireId = remoteWireId;
    appData.TargetWireId = 0; // the host's own local gamer
    appData.Options = SendDataOptions::Reliable;
    appData.Payload = {10, 20, 30};
    auto appDataBytes = NetPacketCodec::Encode(appData);
    fakeClient.Send(peerFromClientSide, 0, appDataBytes.data(), appDataBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    LocalNetworkGamer* hostLocalGamer = host.session->getLocalGamersProperty()[0];
    for (int i = 0; i < 200 && !hostLocalGamer->getIsDataAvailableProperty(); ++i, PollYield()) {
        host.session->Update();
    }
    ASSERT_TRUE(hostLocalGamer->getIsDataAvailableProperty());

    std::vector<SharpRuntime::bytecs> received(3);
    NetworkGamer* sender = nullptr;
    int len = hostLocalGamer->ReceiveData(received, sender);
    EXPECT_EQ(len, 3);
    EXPECT_EQ(received, (std::vector<SharpRuntime::bytecs>{10, 20, 30}));
}

TEST(ENetBackendTest, SimulatedPacketLossOfOneDropsAllAppDataDeterministically) {
    SystemLinkSessionFixture host("HostPlayer");
    host.session->setSimulatedPacketLossProperty(1.0f);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = nullptr;
    uint8_t remoteWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromClientSide);

    for (SharpRuntime::bytecs i = 0; i < 5; ++i) {
        AppDataMessage appData;
        appData.SenderWireId = remoteWireId;
        appData.TargetWireId = 0;
        appData.Options = SendDataOptions::Reliable;
        appData.Payload = {i};
        auto appDataBytes = NetPacketCodec::Encode(appData);
        fakeClient.Send(peerFromClientSide, 0, appDataBytes.data(), appDataBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    }
    fakeClient.Flush();

    LocalNetworkGamer* hostLocalGamer = host.session->getLocalGamersProperty()[0];
    for (int i = 0; i < 100; ++i, PollYield()) {
        host.session->Update();
    }
    EXPECT_FALSE(hostLocalGamer->getIsDataAvailableProperty())
        << "SimulatedPacketLoss=1.0 should drop every one of the 5 packets sent";
}

TEST(ENetBackendTest, SimulatedPacketLossOfZeroDropsNoAppData) {
    SystemLinkSessionFixture host("HostPlayer");
    host.session->setSimulatedPacketLossProperty(0.0f);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = nullptr;
    uint8_t remoteWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromClientSide);

    constexpr int kSentCount = 5;
    for (SharpRuntime::bytecs i = 0; i < kSentCount; ++i) {
        AppDataMessage appData;
        appData.SenderWireId = remoteWireId;
        appData.TargetWireId = 0;
        appData.Options = SendDataOptions::Reliable;
        appData.Payload = {i};
        auto appDataBytes = NetPacketCodec::Encode(appData);
        fakeClient.Send(peerFromClientSide, 0, appDataBytes.data(), appDataBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    }
    fakeClient.Flush();

    LocalNetworkGamer* hostLocalGamer = host.session->getLocalGamersProperty()[0];
    int receivedCount = 0;
    for (int i = 0; i < 200 && receivedCount < kSentCount; ++i, PollYield()) {
        host.session->Update();
        while (hostLocalGamer->getIsDataAvailableProperty()) {
            std::vector<SharpRuntime::bytecs> received(1);
            NetworkGamer* sender = nullptr;
            hostLocalGamer->ReceiveData(received, sender);
            ++receivedCount;
        }
    }
    EXPECT_EQ(receivedCount, kSentCount) << "SimulatedPacketLoss=0.0 should drop nothing";
}

TEST(ENetBackendTest, SimulatedLatencyDelaysAppDataDeliveryUntilTheClockAdvances) {
    struct ClockGuard {
        ~ClockGuard() { ENetBackend::ResetClockForTesting(); }
    } clockGuard;

    SystemLinkSessionFixture host("HostPlayer");
    constexpr int kLatencyMs = 200;
    host.session->setSimulatedLatencyProperty(System::TimeSpan::FromMilliseconds(kLatencyMs));

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = nullptr;
    uint8_t remoteWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromClientSide);

    auto frozenNow = std::chrono::steady_clock::now();
    ENetBackend::SetClockForTesting(frozenNow);

    AppDataMessage appData;
    appData.SenderWireId = remoteWireId;
    appData.TargetWireId = 0;
    appData.Options = SendDataOptions::Reliable;
    appData.Payload = {42};
    auto appDataBytes = NetPacketCodec::Encode(appData);
    fakeClient.Send(peerFromClientSide, 0, appDataBytes.data(), appDataBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    LocalNetworkGamer* hostLocalGamer = host.session->getLocalGamersProperty()[0];
    // Real ENet delivery of the wire packet itself still needs real polling time - the clock
    // freeze only holds back ReleaseDuePendingDeliveries, not ENet's own transport, so this loop
    // drives real Update() calls (each reading the *frozen* Now()) until the packet has genuinely
    // arrived and been queued internally, then asserts it's deliberately still being held back.
    for (int i = 0; i < 200; ++i, PollYield()) {
        host.session->Update();
    }
    EXPECT_FALSE(hostLocalGamer->getIsDataAvailableProperty())
        << "a packet queued under 200ms of simulated latency must not be delivered while the "
           "clock is still frozen at the moment it was sent";

    ENetBackend::SetClockForTesting(frozenNow + std::chrono::milliseconds(kLatencyMs));
    host.session->Update();
    EXPECT_TRUE(hostLocalGamer->getIsDataAvailableProperty())
        << "advancing the clock by exactly the simulated latency must release the held packet";

    std::vector<SharpRuntime::bytecs> received(1);
    NetworkGamer* sender = nullptr;
    int len = hostLocalGamer->ReceiveData(received, sender);
    EXPECT_EQ(len, 1);
    EXPECT_EQ(received[0], 42);
}

TEST(ENetBackendTest, ClientSendDataTransmitsAppDataToHost) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    bool gotHello = false;
    for (int i = 0; i < 200 && !gotHello; ++i, PollYield()) {
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

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && !received; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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

    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
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

    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() > 1; ++i, PollYield()) {
        host.session->Update();
    }

    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(host.session->getRemoteGamersProperty().getCountProperty(), 0);
    ASSERT_EQ(host.session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(host.session->getPreviousGamersProperty()[0]->getGamertagProperty(), "RemotePlayer");
    EXPECT_EQ(leftCount, 1);
}

TEST(ENetBackendTest, ClientRaisesSessionEndedOnHostDisconnect) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    // Task 5.2/5.3 (plans/plan_net.md Phase 5): AllowHostMigration now has a real effect in general (see
    // the tests below), but this specific scenario never completes a real ServerWelcome handshake
    // - client.session's own WireIdToGamer stays empty, so AttemptHostMigration has no roster to
    // find a survivor in and correctly falls back to the exact same immediate-end behavior as
    // AllowHostMigration=false. Setting it true here (rather than testing the disabled default)
    // proves that fallback explicitly, instead of only ever exercising it by accident.
    client.session->setAllowHostMigrationProperty(true);

    int endedCount = 0;
    NetworkSessionEndReason observedReason = NetworkSessionEndReason::ClientSignedOut;
    client.session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs& e) {
        ++endedCount;
        observedReason = e.getEndReasonProperty();
    };

    fakeHost.Disconnect(clientPeerFromHostSide, 0);
    fakeHost.Flush();

    for (int i = 0; i < 200 && endedCount == 0; ++i, PollYield()) {
        client.session->Update();
    }

    EXPECT_EQ(endedCount, 1);
    EXPECT_EQ(observedReason, NetworkSessionEndReason::HostEndedSession);
    EXPECT_EQ(client.session->getSessionStateProperty(), NetworkSessionState::Ended);
}

// --- Task 5.2/5.3/5.4: real host migration (plans/plan_net.md Phase 5) ---

// Task 5.4: the regression case ClientRaisesSessionEndedOnHostDisconnect above can't actually
// cover, since its own handshake never completes far enough to give AttemptHostMigration a real
// survivor to consider - this one does (a real ServerWelcome roster with another known gamer), so
// AllowHostMigration=false is proven to keep ending the session immediately even when migration
// would otherwise have a real decision to make.
TEST(ENetBackendTest, AllowHostMigrationFalseStillEndsSessionImmediatelyWithARealSurvivorKnown) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    // Explicit default - AllowHostMigration is false unless a caller opts in.
    ASSERT_FALSE(client.session->getAllowHostMigrationProperty());
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer", true}, RosterEntry{2, "OtherSurvivor", false}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 3; ++i, PollYield()) {
        client.session->Update();
    }
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 3);

    int endedCount = 0;
    NetworkSessionEndReason observedReason = NetworkSessionEndReason::ClientSignedOut;
    client.session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs& e) {
        ++endedCount;
        observedReason = e.getEndReasonProperty();
    };
    int hostChangedCount = 0;
    client.session->HostChanged += [&](System::Object*, const HostChangedEventArgs&) { ++hostChangedCount; };

    fakeHost.Disconnect(clientPeerFromHostSide, 0);
    fakeHost.Flush();

    for (int i = 0; i < 200 && endedCount == 0; ++i, PollYield()) {
        client.session->Update();
    }

    EXPECT_EQ(endedCount, 1);
    EXPECT_EQ(observedReason, NetworkSessionEndReason::HostEndedSession);
    EXPECT_EQ(hostChangedCount, 0);
    EXPECT_EQ(client.session->getSessionStateProperty(), NetworkSessionState::Ended);
}

TEST(ENetBackendTest, ClientPromotesItselfWhenItIsTheOnlyKnownSurvivor) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    client.session->setAllowHostMigrationProperty(true);
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    // ConnectToHost's own StartHosting call always registers this session for discovery too, even
    // while it's purely playing "client" - unregister first so the "discoverable after promotion"
    // check below genuinely proves the *new* RegisterHost call AttemptHostMigration makes, not
    // this pre-existing side effect.
    ENetDiscoveryService::UnregisterHost(client.session);
    for (const auto& candidate : ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)) {
        ASSERT_NE(candidate.GetConnectPort(), ENetBackend::GetBoundPort(client.session));
    }

    // Only the host is known - once it dies, the client's own local gamer is the sole survivor and
    // must deterministically promote itself (trivially the "lowest remaining wire id").
    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer", true}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        client.session->Update();
    }
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);

    int hostChangedCount = 0;
    NetworkGamer* observedNewHost = nullptr;
    client.session->HostChanged += [&](System::Object*, const HostChangedEventArgs& e) {
        ++hostChangedCount;
        observedNewHost = e.getNewHostProperty();
    };
    int endedCount = 0;
    client.session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs&) { ++endedCount; };

    fakeHost.Disconnect(clientPeerFromHostSide, 0);
    fakeHost.Flush();

    for (int i = 0; i < 200 && hostChangedCount == 0; ++i, PollYield()) {
        client.session->Update();
    }

    EXPECT_EQ(hostChangedCount, 1);
    EXPECT_EQ(endedCount, 0);
    EXPECT_EQ(observedNewHost, client.session->getLocalGamersProperty()[0]);
    // Task 5.1's "rebuilt from scratch, not preserved" scope note: the stale remote "HostPlayer"
    // gamer is really gone (a real GamerLeave, not just superseded bookkeeping).
    EXPECT_EQ(client.session->getAllGamersProperty().getCountProperty(), 1);

    // The real proof this is a genuine promotion, not just local flag-flipping: this session is
    // now really discoverable at its own bound port, exactly like any other real SystemLink host.
    bool foundSelf = false;
    for (const auto& candidate : ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)) {
        if (candidate.GetConnectPort() == ENetBackend::GetBoundPort(client.session)) {
            foundSelf = true;
        }
    }
    EXPECT_TRUE(foundSelf);
}

// Task 5.3: proves the tie-break math itself (excluding the dead host, picking the true minimum
// remaining wire id) rather than a full cross-process reconnect, which needs a second real
// NetworkSession to reconnect to - see plans/plan_net.md Task 5.1's own note on why that's covered by
// the multi-process harness instead (Task 5.5), not here.
TEST(ENetBackendTest, ClientTargetsTheLowestSurvivingWireIdInsteadOfPromotingItself) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    client.session->setAllowHostMigrationProperty(true);
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    // The client's own local gamer gets wire id 5; "OtherSurvivor" (id 2) has a lower id than the
    // client but is not the dying host (id 0) - it, not the client, must be the deterministic
    // choice.
    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer", true}, RosterEntry{2, "OtherSurvivor", false}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 3; ++i, PollYield()) {
        client.session->Update();
    }
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 3);

    fakeHost.Disconnect(clientPeerFromHostSide, 0);
    fakeHost.Flush();

    // No real "OtherSurvivor" session is discoverable in this single-process test (see the test's
    // own top comment) - AttemptHostMigration correctly gives up and falls back to ending the
    // session, but only *after* recording the gamertag it actually targeted, proving the tie-break
    // math itself picked "OtherSurvivor" (id 2), not the dead host (id 0) or itself (id 5).
    int endedCount = 0;
    client.session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs&) { ++endedCount; };
    for (int i = 0; i < 200 && endedCount == 0; ++i, PollYield()) {
        client.session->Update();
    }

    EXPECT_EQ(endedCount, 1);
    EXPECT_EQ(ENetBackend::GetLastMigrationReconnectAttemptGamertagForTesting(client.session), "OtherSurvivor");
}

// Task 2.7: incoming ClientHello was previously accepted unconditionally regardless of
// sessionState_/AllowJoinInProgress - a host already Playing with AllowJoinInProgress == false
// (the default) still silently accepted a new player's join mid-game.
TEST(ENetBackendTest, HostRejectsClientHelloWhenPlayingAndJoinInProgressDisallowed) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);
    ASSERT_FALSE(host.session->getAllowJoinInProgressProperty());

    host.session->StartGame();
    for (int i = 0; i < 5; ++i, PollYield()) {
        host.session->Update();
    }
    ASSERT_EQ(host.session->getSessionStateProperty(), NetworkSessionState::Playing);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    ClientHelloMessage hello;
    hello.LocalGamertags = {"LateJoiner"};
    auto helloBytes = NetPacketCodec::Encode(hello);
    fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    bool disconnected = false;
    for (int i = 0; i < 200 && !disconnected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
        }
    }
    EXPECT_TRUE(disconnected) << "host should have disconnected the peer instead of accepting its join";
    // The host's own local gamer only - no new gamer was ever added.
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
}

TEST(ENetBackendTest, ClientProcessesGamerLeaveBroadcast) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
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

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
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

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() > 1; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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

    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    // The host's gamer count and the fake client's own view of the ServerWelcome reply are
    // updated by two separate steps (host-side AddRemoteGamer vs. the reply actually arriving in
    // the fake client's receive queue) that aren't perfectly synchronized under a genuinely-async
    // transport (unlike the always-instant native ENet loopback) - so the loop above can exit
    // right as gamer count hits 2 but before the ServerWelcome has actually arrived here. Drain
    // any such straggler now so it isn't mistaken for the StateChangeBroadcast sent by
    // StartGame() below.
    for (int i = 0; i < 20; ++i, PollYield()) {
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }

    host.session->StartGame();

    ENetPacket* received = nullptr;
    for (int i = 0; i < 200 && !received; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && !received; ++i, PollYield()) {
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

namespace {
    // Waits for the next GamerReadyBroadcast the fake machine receives, draining anything else.
    std::optional<GamerReadyMessage> NextGamerReady(ENetHostHandle& fake, NetworkSession* session) {
        for (int i = 0; i < 200; ++i, PollYield()) {
            session->Update();
            ENetEvent evt{};
            if (fake.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
                std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                enet_packet_destroy(evt.packet);
                if (NetPacketCodec::PeekTag(data) == MessageTag::GamerReadyBroadcast) return NetPacketCodec::DecodeGamerReady(data);
            }
        }
        return std::nullopt;
    }
}

TEST(ENetBackendTest, LobbyReadinessTravelsBetweenHostAndClient) {
    SystemLinkSessionFixture host("HostPlayer");
    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = nullptr;
    const uint8_t clientWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromClientSide);
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) host.session->Update();
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    auto* local = host.session->getLocalGamersProperty()[0];
    NetworkGamer* remote = host.session->getRemoteGamersProperty()[0];

    // The host's own gamer: every client hears it.
    local->setIsReadyProperty(true);
    auto heard = NextGamerReady(fakeClient, host.session);
    ASSERT_TRUE(heard.has_value());
    ASSERT_EQ(heard->Entries.size(), 1u);
    EXPECT_TRUE(heard->Entries[0].IsReady);
    EXPECT_NE(heard->Entries[0].WireId, clientWireId);
    EXPECT_FALSE(host.session->getIsEveryoneReadyProperty());

    // A client may not speak for the host's gamer, only for its own.
    auto forged = NetPacketCodec::Encode(GamerReadyMessage{{GamerReadyEntry{heard->Entries[0].WireId, false}}});
    fakeClient.Send(peerFromClientSide, 0, forged.data(), forged.size(), ENET_PACKET_FLAG_RELIABLE);
    auto own = NetPacketCodec::Encode(GamerReadyMessage{{GamerReadyEntry{clientWireId, true}}});
    fakeClient.Send(peerFromClientSide, 0, own.data(), own.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();
    for (int i = 0; i < 200 && !remote->getIsReadyProperty(); ++i, PollYield()) host.session->Update();
    EXPECT_TRUE(remote->getIsReadyProperty());
    EXPECT_TRUE(local->getIsReadyProperty());
    EXPECT_TRUE(host.session->getIsEveryoneReadyProperty());

    // ResetReady speaks for everyone, the client's gamer included.
    host.session->ResetReady();
    EXPECT_FALSE(host.session->getIsEveryoneReadyProperty());
    heard = NextGamerReady(fakeClient, host.session);
    ASSERT_TRUE(heard.has_value());
    EXPECT_EQ(heard->Entries.size(), 2u);
    for (const auto& entry : heard->Entries) EXPECT_FALSE(entry.IsReady);
}

// Reference NetworkMachine.RemoveFromSession on a SystemLink host: the local machine is refused;
// a client machine's gamers leave at once and the client is disconnected with the removal reason.
TEST(ENetBackendTest, HostRemovesAClientMachineWhichIsToldWhy) {
    SystemLinkSessionFixture host("HostPlayer");
    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* hostFromClientSide = nullptr;
    (void)ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &hostFromClientSide);
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) host.session->Update();
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    std::vector<std::string> left;
    host.session->GamerLeft += [&left](System::Object*, const GamerLeftEventArgs& e) {
        left.push_back(e.getGamerProperty()->getGamertagProperty());
    };
    EXPECT_THROW(host.session->getLocalGamersProperty()[0]->getMachineProperty().RemoveFromSession(),
                 System::InvalidOperationException);

    host.session->getRemoteGamersProperty()[0]->getMachineProperty().RemoveFromSession();
    std::optional<uint32_t> reason;
    for (int i = 0; i < 200 && !reason; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0) {
            if (evt.type == ENET_EVENT_TYPE_DISCONNECT) reason = evt.data;
            else if (evt.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(*reason, DisconnectRemovedByHost);
    for (int i = 0; i < 50 && left.empty(); ++i, PollYield()) host.session->Update();
    EXPECT_EQ(left, std::vector<std::string>{"RemotePlayer"});
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(host.session->getRemoteGamersProperty().getCountProperty(), 0);
}

TEST(ENetBackendTest, AClientTheHostRemovedEndsWithRemovedByHost) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHost.getBoundPortProperty());
    ENetPeer* clientFromHostSide = nullptr;
    bool gotHello = false;
    for (int i = 0; i < 200 && !gotHello; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0) {
            if (evt.type == ENET_EVENT_TYPE_CONNECT) clientFromHostSide = evt.peer;
            else if (evt.type == ENET_EVENT_TYPE_RECEIVE) {
                std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                gotHello = NetPacketCodec::PeekTag(data) == MessageTag::ClientHello;
                enet_packet_destroy(evt.packet);
            }
        }
    }
    ASSERT_TRUE(gotHello);
    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {1};
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer", true}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();
    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) client.session->Update();
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);

    std::optional<NetworkSessionEndReason> ended;
    client.session->SessionEnded += [&ended](System::Object*, const NetworkSessionEndedEventArgs& e) { ended = e.getEndReasonProperty(); };
    fakeHost.Disconnect(clientFromHostSide, DisconnectRemovedByHost);
    fakeHost.Flush();
    for (int i = 0; i < 200 && !ended; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        (void)fakeHost.Service(0, evt);
    }
    ASSERT_TRUE(ended.has_value());
    EXPECT_EQ(*ended, NetworkSessionEndReason::RemovedByHost);
}

// SystemLink: every machine reports the host's settings and machine grouping. The host sends both
// with the welcome and again whenever they change.
TEST(ENetBackendTest, HostSendsItsSettingsAndMachineGroupingAndRepublishesChanges) {
    SystemLinkSessionFixture host("HostPlayer");
    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* hostFromClientSide = nullptr;
    const uint8_t clientWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &hostFromClientSide);
    std::optional<SessionSettingsMessage> settings;
    std::optional<MachineRosterMessage> machines;
    const auto drain = [&] {
        for (int i = 0; i < 100; ++i, PollYield()) {
            host.session->Update();
            ENetEvent evt{};
            while (fakeClient.Service(0, evt) > 0) {
                if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
                std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                enet_packet_destroy(evt.packet);
                if (NetPacketCodec::PeekTag(data) == MessageTag::SessionSettingsBroadcast) settings = NetPacketCodec::DecodeSessionSettings(data);
                if (NetPacketCodec::PeekTag(data) == MessageTag::MachineRosterBroadcast) machines = NetPacketCodec::DecodeMachineRoster(data);
            }
        }
    };
    drain();
    ASSERT_TRUE(settings.has_value());
    EXPECT_EQ(settings->MaxGamers, 8);
    EXPECT_FALSE(settings->AllowHostMigration);
    ASSERT_TRUE(machines.has_value());
    ASSERT_EQ(machines->Entries.size(), 2u);
    const uint8_t hostWireId = host.session->getLocalGamersProperty()[0]->getIdProperty();
    for (const auto& entry : machines->Entries) {
        EXPECT_EQ(entry.MachineId, entry.WireId == hostWireId ? 0 : 1) << static_cast<int>(entry.WireId);
        EXPECT_TRUE(entry.WireId == hostWireId || entry.WireId == clientWireId);
    }
    settings.reset();
    host.session->setAllowHostMigrationProperty(true);
    host.session->setMaxGamersProperty(6);
    drain();
    ASSERT_TRUE(settings.has_value());
    EXPECT_TRUE(settings->AllowHostMigration);
    EXPECT_EQ(settings->MaxGamers, 6);
}

TEST(ENetBackendTest, AClientReportsTheHostsSettingsAndMachines) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHost.getBoundPortProperty());
    ENetPeer* clientFromHostSide = nullptr;
    bool gotHello = false;
    for (int i = 0; i < 200 && !gotHello; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0) {
            if (evt.type == ENET_EVENT_TYPE_CONNECT) clientFromHostSide = evt.peer;
            else if (evt.type == ENET_EVENT_TYPE_RECEIVE) {
                std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                gotHello = NetPacketCodec::PeekTag(data) == MessageTag::ClientHello;
                enet_packet_destroy(evt.packet);
            }
        }
    }
    ASSERT_TRUE(gotHello);
    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer", true}, RosterEntry{1, "HostPlayer2", false}, RosterEntry{2, "Other", false}};
    for (const auto& bytes : {NetPacketCodec::Encode(welcome),
                              NetPacketCodec::Encode(SessionSettingsMessage{12, 2, true, true}),
                              NetPacketCodec::Encode(MachineRosterMessage{{{0, 0}, {1, 0}, {2, 7}, {5, 9}}})}) {
        fakeHost.Send(clientFromHostSide, 0, bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    }
    fakeHost.Flush();
    NetworkGamer* hostPlayer = nullptr;
    for (int i = 0; i < 200 && !(client.session->getMaxGamersProperty() == 12 &&
                                 hostPlayer && hostPlayer->getMachineProperty().getGamersProperty().getCountProperty() == 2); ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        (void)fakeHost.Service(0, evt);
        hostPlayer = client.session->FindGamerById(0);
    }
    ASSERT_NE(hostPlayer, nullptr);
    EXPECT_EQ(client.session->getMaxGamersProperty(), 12);
    EXPECT_EQ(client.session->getPrivateGamerSlotsProperty(), 2);
    EXPECT_TRUE(client.session->getAllowJoinInProgressProperty());
    EXPECT_TRUE(client.session->getAllowHostMigrationProperty());
    // The host's two gamers share its machine; the third gamer is on another; the client's own
    // gamer stays on the local machine.
    EXPECT_EQ(&hostPlayer->getMachineProperty(), &client.session->FindGamerById(1)->getMachineProperty());
    EXPECT_NE(&hostPlayer->getMachineProperty(), &client.session->FindGamerById(2)->getMachineProperty());
    EXPECT_EQ(client.session->getLocalGamersProperty()[0]->getMachineProperty().getGamersProperty().getCountProperty(), 1);
}

TEST(ENetBackendTest, AJoiningMachineLearnsWhoIsAlreadyReady) {
    SystemLinkSessionFixture host("HostPlayer");
    host.session->getLocalGamersProperty()[0]->setIsReadyProperty(true);
    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = nullptr;
    const uint8_t clientWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, host.session, &peerFromClientSide);
    auto heard = NextGamerReady(fakeClient, host.session);
    ASSERT_TRUE(heard.has_value());
    ASSERT_EQ(heard->Entries.size(), 1u);
    EXPECT_NE(heard->Entries[0].WireId, clientWireId);
    EXPECT_TRUE(heard->Entries[0].IsReady);
}

TEST(ENetBackendTest, ClientAppliesTheHostsReadinessReportsOnly) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHost.getBoundPortProperty());
    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) clientPeerFromHostSide = evt.peer;
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    // Welcome the client (wire id 2) with the host's gamer (wire id 1) in the roster.
    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {2};
    welcome.ExistingRoster = {RosterEntry{1, "FakeHost", true}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();
    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) client.session->Update();
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);

    // The client's own change goes to the host.
    auto* local = client.session->getLocalGamersProperty()[0];
    local->setIsReadyProperty(true);
    std::optional<GamerReadyMessage> reported;
    for (int i = 0; i < 200 && !reported; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            enet_packet_destroy(evt.packet);
            if (NetPacketCodec::PeekTag(data) == MessageTag::GamerReadyBroadcast) reported = NetPacketCodec::DecodeGamerReady(data);
        }
    }
    ASSERT_TRUE(reported.has_value());
    ASSERT_EQ(reported->Entries.size(), 1u);
    EXPECT_EQ(reported->Entries[0].WireId, 2);
    EXPECT_TRUE(reported->Entries[0].IsReady);

    // The host's reports apply to anyone: its own gamer, and the client's (ResetReady).
    auto hostReport = NetPacketCodec::Encode(GamerReadyMessage{{GamerReadyEntry{1, true}, GamerReadyEntry{2, false}}});
    fakeHost.Send(clientPeerFromHostSide, 0, hostReport.data(), hostReport.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();
    NetworkGamer* remote = client.session->getRemoteGamersProperty()[0];
    for (int i = 0; i < 200 && !remote->getIsReadyProperty(); ++i, PollYield()) client.session->Update();
    EXPECT_TRUE(remote->getIsReadyProperty());
    EXPECT_FALSE(local->getIsReadyProperty());
}

TEST(ENetBackendTest, ClientProcessesStateChangeBroadcast) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
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

    for (int i = 0; i < 200 && client.session->getSessionStateProperty() != NetworkSessionState::Playing; ++i, PollYield()) {
        client.session->Update();
    }

    EXPECT_EQ(client.session->getSessionStateProperty(), NetworkSessionState::Playing);
    EXPECT_EQ(startedCount, 1);
}

TEST(ENetBackendTest, HostWelcomeAndLaterBroadcastPreserveCurrentSessionProperties) {
    SystemLinkSessionFixture host("HostPlayer");
    auto& properties = host.session->getSessionPropertiesProperty();
    properties[0] = 1;
    properties[1] = std::nullopt;
    properties[2] = -7;
    properties[3] = 1;

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = nullptr;
    ServerWelcomeMessage welcome;
    ConnectFakeClientAndCompleteHandshake(
        fakeClient, host.session, &peerFromClientSide, &welcome
    );

    ASSERT_EQ(welcome.SessionProperties.getCountProperty(), 8);
    EXPECT_EQ(welcome.SessionProperties.getItem(0), 1);
    EXPECT_EQ(welcome.SessionProperties.getItem(1), std::nullopt);
    EXPECT_EQ(welcome.SessionProperties.getItem(2), -7);
    EXPECT_EQ(welcome.SessionProperties.getItem(3), 1);

    properties.setItem(0, 2);
    properties.setItem(1, 6);
    properties.setItem(2, std::numeric_limits<int>::min());
    properties.setItem(3, 0);

    SessionPropertiesBroadcastMessage broadcast;
    bool receivedBroadcast = false;
    for (int i = 0; i < 200 && !receivedBroadcast; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<SharpRuntime::bytecs> data(
                evt.packet->data, evt.packet->data + evt.packet->dataLength
            );
            if (NetPacketCodec::PeekTag(data) == MessageTag::SessionPropertiesBroadcast) {
                broadcast = NetPacketCodec::DecodeSessionPropertiesBroadcast(data);
                receivedBroadcast = true;
            }
            enet_packet_destroy(evt.packet);
        }
    }

    ASSERT_TRUE(receivedBroadcast);
    ASSERT_EQ(broadcast.SessionProperties.getCountProperty(), 8);
    EXPECT_EQ(broadcast.SessionProperties.getItem(0), 2);
    EXPECT_EQ(broadcast.SessionProperties.getItem(1), 6);
    EXPECT_EQ(broadcast.SessionProperties.getItem(2), std::numeric_limits<int>::min());
    EXPECT_EQ(broadcast.SessionProperties.getItem(3), 0);
}

TEST(ENetBackendTest, ClientAppliesWelcomeAndLaterAuthoritativeSessionPropertiesBroadcast) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    const uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    // This Create-based transport fixture adopts the local role of a real Join constructor.
    client.session->getLocalGamersProperty()[0]->SetIsHost(false);
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    bool gotHello = false;
    for (int i = 0; i < 200 && !gotHello; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0) {
            if (evt.type == ENET_EVENT_TYPE_CONNECT) {
                clientPeerFromHostSide = evt.peer;
            } else if (evt.type == ENET_EVENT_TYPE_RECEIVE) {
                std::vector<SharpRuntime::bytecs> data(
                    evt.packet->data, evt.packet->data + evt.packet->dataLength
                );
                gotHello = NetPacketCodec::PeekTag(data) == MessageTag::ClientHello;
                enet_packet_destroy(evt.packet);
            }
        }
    }
    ASSERT_TRUE(gotHello);
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {{0, "HostPlayer", true}};
    welcome.SessionProperties.setItem(0, 1);
    welcome.SessionProperties.setItem(1, std::nullopt);
    welcome.SessionProperties.setItem(2, 3);
    const auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(
        clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE
    );
    fakeHost.Flush();

    for (int i = 0;
         i < 200 && client.session->getSessionPropertiesProperty().getItem(0) != 1;
         ++i, PollYield()) {
        client.session->Update();
    }
    const NetworkSession& constClientAfterWelcome = *client.session;
    const auto& welcomed = constClientAfterWelcome.getSessionPropertiesProperty();
    ASSERT_EQ(welcomed.getCountProperty(), 8);
    EXPECT_EQ(welcomed.getItem(0), 1);
    EXPECT_EQ(welcomed.getItem(1), std::nullopt);
    EXPECT_EQ(welcomed.getItem(2), 3);
    auto& mutableClientProperties = client.session->getSessionPropertiesProperty();
    auto retained = mutableClientProperties[0];
    EXPECT_FALSE(client.session->getIsHostProperty());
    EXPECT_THROW(mutableClientProperties[0] = 1, System::InvalidOperationException);
    EXPECT_THROW(mutableClientProperties.setItem(1, 9), System::InvalidOperationException);
    EXPECT_THROW(retained = 8, System::InvalidOperationException);
    EXPECT_THROW(mutableClientProperties = NetworkSessionProperties{}, System::InvalidOperationException);
    EXPECT_EQ(mutableClientProperties.getItem(0), 1);
    EXPECT_EQ(mutableClientProperties.getItem(1), std::nullopt);

    SessionPropertiesBroadcastMessage update;
    update.SessionProperties.setItem(0, 2);
    update.SessionProperties.setItem(1, 6);
    update.SessionProperties.setItem(2, std::numeric_limits<int>::max());
    update.SessionProperties.setItem(3, 0);
    const auto updateBytes = NetPacketCodec::Encode(update);
    fakeHost.Send(
        clientPeerFromHostSide, 0, updateBytes.data(), updateBytes.size(), ENET_PACKET_FLAG_RELIABLE
    );
    fakeHost.Flush();

    for (int i = 0;
         i < 200 && client.session->getSessionPropertiesProperty().getItem(0) != 2;
         ++i, PollYield()) {
        client.session->Update();
    }
    const NetworkSession& constClientAfterUpdate = *client.session;
    const auto& updated = constClientAfterUpdate.getSessionPropertiesProperty();
    ASSERT_EQ(updated.getCountProperty(), 8);
    EXPECT_EQ(updated.getItem(0), 2);
    EXPECT_EQ(updated.getItem(1), 6);
    EXPECT_EQ(updated.getItem(2), std::numeric_limits<int>::max());
    EXPECT_EQ(updated.getItem(3), 0);
}

TEST(ENetBackendTest, HostRejectsForgedSessionPropertiesBroadcastFromClient) {
    SystemLinkSessionFixture host("HostPlayer");
    host.session->getSessionPropertiesProperty()[0] = 7;

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect(
        "127.0.0.1", ENetBackend::GetBoundPort(host.session), 2
    );
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    SessionPropertiesBroadcastMessage forged;
    forged.SessionProperties.setItem(0, 999);
    const auto bytes = NetPacketCodec::Encode(forged);
    fakeClient.Send(peerFromClientSide, 0, bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    bool disconnected = false;
    for (int i = 0; i < 200 && !disconnected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
        }
    }
    EXPECT_TRUE(disconnected);
    const NetworkSession& constHost = *host.session;
    ASSERT_EQ(constHost.getSessionPropertiesProperty().getCountProperty(), 8);
    EXPECT_EQ(constHost.getSessionPropertiesProperty().getItem(0), 7);
}

// --- Task 1.4: HandleReceive must not let a decode exception escape Update() ---

// Task 1.4: any Decode* call in HandleReceive throws std::runtime_error on a truncated/malformed
// payload (BinaryReader::ReadBytes/ReadString throw on underflow) - and this arrives over an
// already-open ENet channel from a connected peer, with no further payload validation. Confirms
// the host survives a truncated ClientHello (tag byte with no further data at all) without
// crashing or throwing out of Update(), and keeps functioning normally afterward for a real,
// well-formed ClientHello from a second connection.
TEST(ENetBackendTest, HostSurvivesTruncatedClientHelloAndContinuesFunctioningAfterward) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle badClient = ENetHostHandle::CreateClient(2);
    ENetPeer* badPeerFromClientSide = badClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(badPeerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (badClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    // A well-formed Encode(ClientHelloMessage) always has at least a tag byte plus a gamertag-count
    // byte; sending just the tag byte simulates a corrupted/truncated packet - DecodeClientHello's
    // very first read after the tag (the count byte) hits end-of-stream and throws.
    Microsoft::Xna::Framework::Net::PacketWriter writer;
    writer.Write(static_cast<SharpRuntime::bytecs>(MessageTag::ClientHello));
    auto truncatedBytes = NetPacketCodec::ExtractBytes(writer);
    ASSERT_EQ(truncatedBytes.size(), 1u);
    badClient.Send(badPeerFromClientSide, 0, truncatedBytes.data(), truncatedBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    badClient.Flush();

    // Pump enough Update() calls for the truncated packet to actually be received and processed;
    // none of them may throw or crash the process.
    for (int i = 0; i < 50; ++i, PollYield()) {
        EXPECT_NO_THROW(host.session->Update());
    }

    // The host must still be fully functional afterward: a second, real client connecting and
    // sending a well-formed ClientHello must still be processed normally.
    ENetHostHandle goodClient = ENetHostHandle::CreateClient(2);
    ENetPeer* goodPeerFromClientSide = goodClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(goodPeerFromClientSide, nullptr);

    connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (goodClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    ClientHelloMessage hello;
    hello.LocalGamertags = {"RemotePlayer"};
    auto helloBytes = NetPacketCodec::Encode(hello);
    goodClient.Send(goodPeerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    goodClient.Flush();

    int joinCount = 0;
    host.session->GamerJoined += [&joinCount](System::Object*, const GamerJoinedEventArgs&) { ++joinCount; };
    joinCount = 0; // reset past the replay for the host's own pre-existing local gamer

    for (int i = 0; i < 200 && joinCount == 0; ++i, PollYield()) {
        host.session->Update();
    }

    EXPECT_EQ(joinCount, 1);
    NetworkGamer* remoteGamer = nullptr;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") remoteGamer = g;
    }
    EXPECT_NE(remoteGamer, nullptr);
}

// Task 2.11: NextWireId (a uint8_t) was only ever incremented, never reclaimed on a gamer leaving
// - not 256 *simultaneous* gamers, just 256 *cumulative* joins over the session's life, would
// silently wrap around and reassign an id still owned by another gamer, corrupting HandleAppData's
// wire-id-based routing. Rather than spinning literally 256+ real ENet connect/disconnect cycles
// (slow, and it only demonstrates the wraparound at the very end), this directly proves the actual
// fix mechanism: a disconnected peer's wire id is reclaimed and handed back out to the *next*
// connecting peer, rather than the counter marching forever upward - the property that prevents
// wraparound regardless of how many cumulative join/leave cycles occur.
TEST(ENetBackendTest, DisconnectedPeerWireIdIsReclaimedAndReusedByTheNextJoiner) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    std::vector<uint8_t> assignedIds;
    for (int cycle = 0; cycle < 3; ++cycle) {
        ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
        ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
        ASSERT_NE(peerFromClientSide, nullptr);

        bool connected = false;
        for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
            host.session->Update();
            ENetEvent evt{};
            if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
                connected = true;
            }
        }
        ASSERT_TRUE(connected);

        ClientHelloMessage hello;
        hello.LocalGamertags = {"Churner"};
        auto helloBytes = NetPacketCodec::Encode(hello);
        fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
        fakeClient.Flush();

        ENetPacket* received = nullptr;
        for (int i = 0; i < 200 && !received; ++i, PollYield()) {
            host.session->Update();
            ENetEvent evt{};
            if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
                received = evt.packet;
            }
        }
        ASSERT_NE(received, nullptr);
        std::vector<SharpRuntime::bytecs> data(received->data, received->data + received->dataLength);
        ServerWelcomeMessage welcome = NetPacketCodec::DecodeServerWelcome(data);
        enet_packet_destroy(received);
        ASSERT_EQ(welcome.AssignedWireIds.size(), 1u);
        assignedIds.push_back(welcome.AssignedWireIds[0]);

        fakeClient.Disconnect(peerFromClientSide, 0);
        fakeClient.Flush();
        for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() > 1; ++i, PollYield()) {
            host.session->Update();
        }
        ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1)
            << "cycle " << cycle << " did not clean up on disconnect";
    }

    // Every cycle reused the same reclaimed id instead of a fresh, ever-incrementing one.
    ASSERT_EQ(assignedIds.size(), 3u);
    EXPECT_EQ(assignedIds[0], assignedIds[1]);
    EXPECT_EQ(assignedIds[1], assignedIds[2]);
}

// Task 2.14: TeardownSession used to just erase from Sessions(), destroying the ENetHostHandle
// (-> enet_host_destroy()) with no prior enet_peer_disconnect for still-connected peers - they'd
// only learn the connection is gone once ENet's own internal timeout eventually elapses, instead
// of receiving an immediate, clean DISCONNECT event. Confirms a connected peer sees a prompt
// DISCONNECT (within a normal, short polling window - not by waiting out a real timeout) when the
// local session is disposed.
TEST(ENetBackendTest, DisposeDisconnectsConnectedPeersPromptlyInsteadOfWaitingForTimeout) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    host.session->Dispose();

    bool disconnected = false;
    for (int i = 0; i < 200 && !disconnected; ++i, PollYield()) {
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
        }
    }
    EXPECT_TRUE(disconnected) << "peer should have seen a prompt DISCONNECT, not a timeout";
}

// Task 3.1: every remote NetworkGamer created by HandleClientHello/HandleServerWelcome/
// HandleGamerJoinBroadcast used to be permanently leaked - NetworkSession::AddRemoteGamer
// deliberately never takes ownership (see its own doc comment), so ownership belongs to
// ENetBackend's own per-session SessionState instead. Confirms a remote gamer created via a real
// ClientHello handshake is tracked, and freed once the host's session is disposed
// (TeardownSession erasing its SessionState).
TEST(ENetBackendTest, HostFreesOwnedRemoteGamerOnDispose) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);
    EXPECT_EQ(ENetBackend::GetOwnedRemoteGamerCountForTesting(host.session), 0u);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    EXPECT_EQ(ENetBackend::GetOwnedRemoteGamerCountForTesting(host.session), 1u);

    host.session->Dispose();
    EXPECT_EQ(ENetBackend::GetOwnedRemoteGamerCountForTesting(host.session), 0u);
}

// Task 4.1: NetworkGamer::RoundtripTime was permanently dead - roundtripTime_ default-constructs
// to System::TimeSpan::Zero and nothing anywhere ever assigned it. Confirms the host's view of a
// real, directly-connected remote gamer's RTT becomes non-zero (ENet's own native per-peer RTT
// tracking, now actually surfaced) over a real two-peer ENet connection.
TEST(ENetBackendTest, HostMeasuresRealRoundtripTimeForRemoteGamer) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    NetworkGamer* remoteGamer = nullptr;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") remoteGamer = g;
    }
    ASSERT_NE(remoteGamer, nullptr);

    EXPECT_GT(remoteGamer->getRoundtripTimeProperty(), System::TimeSpan::Zero);
}

// --- REMED-NET-001: HandleReceive must verify sender authority on host-only broadcast messages ---
//
// Before this fix, ServerWelcome/GamerJoinBroadcast/GamerLeaveBroadcast/StateChangeBroadcast were
// dispatched to their handlers with no check that the sending peer is this session's actual
// authoritative host (state.HostPeer) - any already-connected peer, speaking this fully-inferable
// wire format, could forge one of these directly. The fix rejects (logs + disconnects the peer)
// any host-only message received from a peer that isn't state.HostPeer. The four original tests
// below and HostRejectsForgedSessionPropertiesBroadcastFromClient above prove both halves: the
// forgery has no effect on authoritative state, AND the offending peer gets disconnected rather
// than silently ignored (a first-class protocol event, not a quiet drop). Legitimate broadcasts
// still working end to end is covered by the many pre-existing
// ClientProcessesGamerLeaveBroadcast/ClientProcessesStateChangeBroadcast/
// ClientSendsClientHelloAndProcessesServerWelcome/HostRespondsToClientHelloWithServerWelcome.../
// HostBroadcastsStateChangeOnStartAndEndGame tests above (unchanged by this fix, still exercising
// the exact same handlers via the one legitimate sender, state.HostPeer / the host itself).

TEST(ENetBackendTest, HostRejectsForgedServerWelcomeFromNonHostPeer) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    uint8_t hostGamerWireIdBefore = host.session->getLocalGamersProperty()[0]->getIdProperty();

    // Forged directly by the already-connected client peer, not the host - a plain connected
    // client should never originate a ServerWelcome (it's only ever a host's reply).
    ServerWelcomeMessage forged;
    forged.AssignedWireIds = {77}; // attempts to corrupt the host's own local gamer's wire id
    forged.ExistingRoster = {RosterEntry{0, "HostPlayer", true}, RosterEntry{55, "InjectedFakeGamer", false}};
    auto forgedBytes = NetPacketCodec::Encode(forged);
    fakeClient.Send(peerFromClientSide, 0, forgedBytes.data(), forgedBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    bool disconnected = false;
    for (int i = 0; i < 200 && !disconnected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
        }
    }
    EXPECT_TRUE(disconnected) << "host should have disconnected the peer that forged a ServerWelcome";

    for (int i = 0; i < 20; ++i, PollYield()) {
        host.session->Update();
    }
    // The forging peer (and its one real gamer) is gone via the disconnect's own normal cleanup;
    // critically, no injected fake gamer was ever added, and the host's own wire id is untouched.
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(host.session->getLocalGamersProperty()[0]->getIdProperty(), hostGamerWireIdBefore);
}

TEST(ENetBackendTest, HostRejectsForgedGamerJoinBroadcastFromNonHostPeer) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    GamerJoinBroadcastMessage forged;
    forged.NewGamers = {RosterEntry{99, "InjectedFakeGamer", false}};
    auto forgedBytes = NetPacketCodec::Encode(forged);
    fakeClient.Send(peerFromClientSide, 0, forgedBytes.data(), forgedBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    bool disconnected = false;
    for (int i = 0; i < 200 && !disconnected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
        }
    }
    EXPECT_TRUE(disconnected) << "host should have disconnected the peer that forged a GamerJoinBroadcast";

    for (int i = 0; i < 20; ++i, PollYield()) {
        host.session->Update();
    }
    // Only HostPlayer remains - RemotePlayer is gone via the kick's own normal cleanup, and
    // InjectedFakeGamer was never added at all.
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        EXPECT_NE(g->getGamertagProperty(), "InjectedFakeGamer");
    }
}

TEST(ENetBackendTest, HostRejectsForgedGamerLeaveBroadcastFromNonHostPeer) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    auto connectAndHandshake = [&](ENetHostHandle& fakeClient, const std::string& gamertag) {
        ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
        EXPECT_NE(peerFromClientSide, nullptr);

        bool connected = false;
        for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
            host.session->Update();
            ENetEvent evt{};
            if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
                connected = true;
            }
        }
        EXPECT_TRUE(connected);

        ClientHelloMessage hello;
        hello.LocalGamertags = {gamertag};
        auto helloBytes = NetPacketCodec::Encode(hello);
        fakeClient.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
        fakeClient.Flush();

        ServerWelcomeMessage welcome;
        bool gotWelcome = false;
        for (int i = 0; i < 200 && !gotWelcome; ++i, PollYield()) {
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
        EXPECT_TRUE(gotWelcome);
        EXPECT_EQ(welcome.AssignedWireIds.size(), 1u);
        return peerFromClientSide;
    };

    ENetHostHandle fakeClientA = ENetHostHandle::CreateClient(2);
    ENetPeer* peerAFromHostSide = connectAndHandshake(fakeClientA, "PeerA");
    ENetHostHandle fakeClientB = ENetHostHandle::CreateClient(2);
    connectAndHandshake(fakeClientB, "PeerB");
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 3);

    uint8_t wireIdB = 0;
    bool foundB = false;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "PeerB") {
            wireIdB = static_cast<uint8_t>(g->getIdProperty());
            foundB = true;
        }
    }
    ASSERT_TRUE(foundB);

    // PeerA forges a GamerLeaveBroadcast naming PeerB's wire id - a gamer it does not own,
    // belonging to an entirely different connected client.
    GamerLeaveBroadcastMessage forged;
    forged.WireIds = {wireIdB};
    auto forgedBytes = NetPacketCodec::Encode(forged);
    fakeClientA.Send(peerAFromHostSide, 0, forgedBytes.data(), forgedBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClientA.Flush();

    bool aDisconnected = false;
    for (int i = 0; i < 200 && !aDisconnected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evtA{};
        if (fakeClientA.Service(0, evtA) > 0 && evtA.type == ENET_EVENT_TYPE_DISCONNECT) {
            aDisconnected = true;
        }
        ENetEvent evtB{};
        if (fakeClientB.Service(0, evtB) > 0 && evtB.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evtB.packet);
        }
    }
    EXPECT_TRUE(aDisconnected) << "host should have disconnected the peer that forged a GamerLeaveBroadcast";

    for (int i = 0; i < 20; ++i, PollYield()) {
        host.session->Update();
    }
    // Only the forger (PeerA) was removed via the kick's own normal cleanup - PeerB, the intended
    // forged-kick target, is still a full member.
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    bool bStillPresent = false;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        if (g->getIdProperty() == wireIdB) bStillPresent = true;
    }
    EXPECT_TRUE(bStillPresent) << "the forged-kick target must not actually be removed";
}

TEST(ENetBackendTest, HostRejectsForgedStateChangeBroadcastFromNonHostPeer) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);
    ASSERT_EQ(host.session->getSessionStateProperty(), NetworkSessionState::Lobby);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    int gameStartedCount = 0;
    host.session->GameStarted += [&gameStartedCount](System::Object*, const GameStartedEventArgs&) { ++gameStartedCount; };

    StateChangeBroadcastMessage forged;
    forged.NewState = NetworkSessionState::Playing;
    auto forgedBytes = NetPacketCodec::Encode(forged);
    fakeClient.Send(peerFromClientSide, 0, forgedBytes.data(), forgedBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    bool disconnected = false;
    for (int i = 0; i < 200 && !disconnected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
        }
    }
    EXPECT_TRUE(disconnected) << "host should have disconnected the peer that forged a StateChangeBroadcast";

    for (int i = 0; i < 20; ++i, PollYield()) {
        host.session->Update();
    }
    EXPECT_EQ(host.session->getSessionStateProperty(), NetworkSessionState::Lobby);
    EXPECT_EQ(gameStartedCount, 0);
}

// REMED-NET-001: a "client" role session still owns a real, accepting ENetHost of its own (see
// ConnectToHost's own doc comment - even a peer that only ever intended to join someone else can
// be promoted to host later via migration, so its socket accepts connections from the moment it's
// created). A rogue third party - not the real, trusted host - can connect directly to it and
// forge a host-only broadcast straight into this peer's own view of the session, with no MITM or
// address spoofing needed. Exercises the other half of IsFromAuthoritativeHost's check
// (state.HostPeer != nullptr but peer != state.HostPeer), distinct from the host-side tests above
// (where state.HostPeer is always null).
TEST(ENetBackendTest, ClientRejectsForgedGamerLeaveBroadcastFromRogueThirdPartyPeer) {
    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    ENetPeer* clientPeerFromHostSide = nullptr;
    for (int i = 0; i < 200 && !clientPeerFromHostSide; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (fakeHost.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            clientPeerFromHostSide = evt.peer;
        }
    }
    ASSERT_NE(clientPeerFromHostSide, nullptr);

    ServerWelcomeMessage welcome;
    welcome.AssignedWireIds = {5};
    welcome.ExistingRoster = {RosterEntry{0, "OtherPlayer", true}};
    auto welcomeBytes = NetPacketCodec::Encode(welcome);
    fakeHost.Send(clientPeerFromHostSide, 0, welcomeBytes.data(), welcomeBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeHost.Flush();

    for (int i = 0; i < 200 && client.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        client.session->Update();
    }
    ASSERT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);

    uint16_t clientOwnPort = ENetBackend::GetBoundPort(client.session);
    ASSERT_GT(clientOwnPort, 0);
    ASSERT_NE(clientOwnPort, fakeHostPort);

    // A rogue peer, entirely unrelated to the real fakeHost above, connects straight to this
    // client's own incidental listening socket.
    ENetHostHandle rogue = ENetHostHandle::CreateClient(2);
    ENetPeer* roguePeerFromClientSide = rogue.Connect("127.0.0.1", clientOwnPort, 2);
    ASSERT_NE(roguePeerFromClientSide, nullptr);

    bool rogueConnected = false;
    for (int i = 0; i < 200 && !rogueConnected; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (rogue.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            rogueConnected = true;
        }
    }
    ASSERT_TRUE(rogueConnected);

    // Forges a GamerLeaveBroadcast naming the real host's own wire id (0), attempting to make this
    // peer believe the real host just left.
    GamerLeaveBroadcastMessage forged;
    forged.WireIds = {0};
    auto forgedBytes = NetPacketCodec::Encode(forged);
    rogue.Send(roguePeerFromClientSide, 0, forgedBytes.data(), forgedBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    rogue.Flush();

    bool rogueDisconnected = false;
    for (int i = 0; i < 200 && !rogueDisconnected; ++i, PollYield()) {
        client.session->Update();
        ENetEvent evt{};
        if (rogue.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            rogueDisconnected = true;
        }
    }
    EXPECT_TRUE(rogueDisconnected)
        << "the client should have rejected+disconnected a forged broadcast from a peer that is not its real host";

    // The real host ("OtherPlayer") is still a full member - the forged kick from the rogue peer
    // had no effect.
    EXPECT_EQ(client.session->getAllGamersProperty().getCountProperty(), 2);
    bool hostStillPresent = false;
    for (NetworkGamer* g : client.session->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "OtherPlayer") hostStillPresent = true;
    }
    EXPECT_TRUE(hostStillPresent);
}

// --- REMED-NET-003: HandleClientHello per-peer resend guard ---

TEST(ENetBackendTest, HostRejectsDuplicateClientHelloFromAlreadyHandshakedPeer) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = fakeClient.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
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
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    ASSERT_EQ(ENetBackend::GetOwnedRemoteGamerCountForTesting(host.session), 1u);

    // Same already-handshaked peer resends ClientHello, attempting to inject two more gamers.
    ClientHelloMessage resend;
    resend.LocalGamertags = {"InjectedGamerA", "InjectedGamerB"};
    auto resendBytes = NetPacketCodec::Encode(resend);
    fakeClient.Send(peerFromClientSide, 0, resendBytes.data(), resendBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    fakeClient.Flush();

    bool disconnected = false;
    for (int i = 0; i < 200 && !disconnected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (fakeClient.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
        }
    }
    EXPECT_TRUE(disconnected) << "host should have disconnected the peer that resent ClientHello";

    for (int i = 0; i < 20; ++i, PollYield()) {
        host.session->Update();
    }
    // Only HostPlayer remains active: RemotePlayer's real gamer is gone via the kick's own normal
    // cleanup (removed from the active roster, though - like any other disconnect - its
    // NetworkGamer object stays owned/allocated until the session itself tears down; see Task 3.1's
    // own doc comment on OwnedRemoteGamers). Critically, neither InjectedGamerA nor InjectedGamerB
    // was ever allocated at all - the resend was rejected before HandleClientHello's own
    // gamer-creation loop ever ran, so the owned count stays at exactly the one real gamer from the
    // original, legitimate handshake.
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(ENetBackend::GetOwnedRemoteGamerCountForTesting(host.session), 1u);
}

TEST(ENetBackendTest, HostAcceptsFreshClientHelloAfterALegitimateDisconnectAndReconnect) {
    // Regression for REMED-NET-003: the per-peer resend guard is keyed on live ENetPeer identity
    // (state.PeerWireIds, erased on disconnect - see HandleClientHello's own comment) - a genuine
    // reconnect always arrives as a brand-new CONNECT event on a fresh peer object and must not be
    // mistaken for a resend on the old, already-torn-down connection.
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

    ENetHostHandle firstConnection = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromClientSide = firstConnection.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(peerFromClientSide, nullptr);

    bool connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (firstConnection.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    ClientHelloMessage hello;
    hello.LocalGamertags = {"ReconnectingPlayer"};
    auto helloBytes = NetPacketCodec::Encode(hello);
    firstConnection.Send(peerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    firstConnection.Flush();
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (firstConnection.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);

    firstConnection.Disconnect(peerFromClientSide, 0);
    firstConnection.Flush();
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() > 1; ++i, PollYield()) {
        host.session->Update();
    }
    ASSERT_EQ(host.session->getAllGamersProperty().getCountProperty(), 1);

    // The same physical client reconnecting - a fresh ENetHostHandle/peer, matching how every
    // other reconnect test in this file (DisconnectedPeerWireIdIsReclaimedAndReusedByTheNextJoiner)
    // models "the same machine, a new connection".
    ENetHostHandle reconnected = ENetHostHandle::CreateClient(2);
    ENetPeer* newPeerFromClientSide = reconnected.Connect("127.0.0.1", hostPort, 2);
    ASSERT_NE(newPeerFromClientSide, nullptr);

    connected = false;
    for (int i = 0; i < 200 && !connected; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (reconnected.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
        }
    }
    ASSERT_TRUE(connected);

    reconnected.Send(newPeerFromClientSide, 0, helloBytes.data(), helloBytes.size(), ENET_PACKET_FLAG_RELIABLE);
    reconnected.Flush();
    for (int i = 0; i < 200 && host.session->getAllGamersProperty().getCountProperty() < 2; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        if (reconnected.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(evt.packet);
        }
    }
    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2)
        << "a genuine reconnect must still be accepted, not mistaken for a ClientHello resend";
    // 2, not 1: the first (now-disconnected) "ReconnectingPlayer" NetworkGamer object is still
    // owned (removed from the active roster, but not freed - see Task 3.1's own doc comment on
    // OwnedRemoteGamers) alongside the second, freshly-joined one - this is pre-existing,
    // unrelated-to-this-fix lifetime behavior, not a leak this test is asserting against.
    EXPECT_EQ(ENetBackend::GetOwnedRemoteGamerCountForTesting(host.session), 2u);
}

// ---- Network voice (GSX-E1) ---------------------------------------------------------------------
namespace {
    // The devices voice uses in these tests: a microphone that hears a 440 Hz tone while `talking`,
    // and a speaker that records what it was given.
    struct VoiceProbe {
        bool talking = true;
        bool open = false;
        double phase = 0.0;
        std::vector<std::pair<std::uint8_t, std::size_t>> played;
        double playedEnergy = 0.0;
    };
    class ToneCapture final : public IVoiceCapture {
    public:
        explicit ToneCapture(std::shared_ptr<VoiceProbe> probe) : probe_(std::move(probe)) {}
        bool present() override { return true; }
        void setOpen(bool open) override { probe_->open = open; }
        void read(std::vector<std::int16_t>& out) override {
            if (!probe_->open) return;
            for (int i = 0; i < 2 * VoiceFrameSamples; ++i) {
                out.push_back(probe_->talking ? static_cast<std::int16_t>(8000.0 * std::sin(probe_->phase)) : 0);
                probe_->phase += 2.0 * 3.14159265358979 * 440.0 / VoiceSampleRate;
            }
        }
    private:
        std::shared_ptr<VoiceProbe> probe_;
    };
    class RecordingPlayback final : public IVoicePlayback {
    public:
        explicit RecordingPlayback(std::shared_ptr<VoiceProbe> probe) : probe_(std::move(probe)) {}
        void play(std::uint8_t talker, std::span<const std::int16_t> pcm) override {
            probe_->played.emplace_back(talker, pcm.size());
            for (const auto sample : pcm) probe_->playedEnergy += static_cast<double>(sample) * sample;
        }
        void release(std::uint8_t) override {}
    private:
        std::shared_ptr<VoiceProbe> probe_;
    };
    // Installs the probe's devices for one test (before its session's first Update) and restores
    // the defaults and the mute list after it.
    struct VoiceDevices {
        std::shared_ptr<VoiceProbe> probe = std::make_shared<VoiceProbe>();
        VoiceDevices() {
            auto shared = probe;
            setVoiceDevicesForTesting([shared] { return std::make_unique<ToneCapture>(shared); },
                                      [shared] { return std::make_unique<RecordingPlayback>(shared); });
        }
        ~VoiceDevices() {
            setVoiceDevicesForTesting({}, {});
            CNA::Internal::GamerServices::resetVoiceMutesForTesting();
        }
    };

    // Voice frames reaching the fake host over a stretch of real time.
    std::vector<VoiceDataMessage> CollectVoice(FakeHostedClient& client, std::chrono::milliseconds span) {
        std::vector<VoiceDataMessage> frames;
        const auto until = std::chrono::steady_clock::now() + span;
        while (std::chrono::steady_clock::now() < until) {
            client.session->Update();
            ENetEvent evt{};
            while (client.fakeHost.Service(0, evt) > 0) {
                if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
                std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
                enet_packet_destroy(evt.packet);
                if (NetPacketCodec::PeekTag(data) == MessageTag::VoiceData) frames.push_back(NetPacketCodec::DecodeVoiceData(data));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return frames;
    }
    std::optional<VoiceDataMessage> FirstSpeech(FakeHostedClient& client) {
        for (int round = 0; round < 20; ++round)
            for (const auto& frame : CollectVoice(client, std::chrono::milliseconds(50)))
                if (frame.Flags & VoiceFlagTalking) return frame;
        return std::nullopt;
    }
    std::size_t Speech(const std::vector<VoiceDataMessage>& frames) {
        return static_cast<std::size_t>(std::count_if(frames.begin(), frames.end(), [](const auto& frame) { return (frame.Flags & VoiceFlagTalking) != 0; }));
    }
}

// XNA: voice is routed automatically between every gamer of a session. The microphone's owner
// sends Opus speech to each remote machine and reads as talking; silence ends the talking.
TEST(ENetBackendTest, VoiceFromTheMicrophoneOwnerReachesTheRemoteMachine) {
    if (!voiceAvailable()) GTEST_SKIP() << "built without libopus";
    VoiceDevices devices;
    FakeHostedClient client;
    ASSERT_NE(client.otherPlayer, nullptr);
    const auto speech = FirstSpeech(client);
    ASSERT_TRUE(speech.has_value()) << "no speech reached the other machine";
    EXPECT_EQ(speech->SenderWireId, 5);
    EXPECT_EQ(speech->TargetWireId, 0);
    EXPECT_FALSE(speech->Payload.empty());
    EXPECT_LE(speech->Payload.size(), MaxVoicePayloadBytes);
    LocalNetworkGamer* local = client.LocalFor(client.signedIn);
    ASSERT_NE(local, nullptr);
    EXPECT_TRUE(local->getHasVoiceProperty());
    EXPECT_TRUE(local->getIsTalkingProperty());
    EXPECT_TRUE(devices.probe->open);
    // Quiet: talking ends once the hangover has passed; the gamer still has voice.
    devices.probe->talking = false;
    for (int i = 0; i < 30; ++i) client.session->Update();
    EXPECT_FALSE(local->getIsTalkingProperty());
    EXPECT_TRUE(local->getHasVoiceProperty());
}

// A remote gamer's speech makes it talk and is played, once; a lost frame is concealed and a late
// one dropped. Its HasVoice comes from any frame, speech or not.
TEST(ENetBackendTest, RemoteVoiceMakesItsGamerTalkAndIsPlayed) {
    if (!voiceAvailable()) GTEST_SKIP() << "built without libopus";
    VoiceDevices devices;
    FakeHostedClient client;
    ASSERT_NE(client.otherPlayer, nullptr);
    EXPECT_FALSE(client.otherPlayer->getHasVoiceProperty());
    // Real speech: this client's own, sent back as OtherPlayer's.
    auto speech = FirstSpeech(client);
    ASSERT_TRUE(speech.has_value());
    VoiceDataMessage frame = *speech;
    frame.SenderWireId = 0;
    frame.TargetWireId = 5;
    frame.Sequence = 40;
    client.SendFromHost(NetPacketCodec::Encode(frame));
    for (int i = 0; i < 200 && devices.probe->played.empty(); ++i, PollYield()) client.session->Update();
    ASSERT_EQ(devices.probe->played.size(), 1u);
    EXPECT_EQ(devices.probe->played[0].first, 0);
    EXPECT_EQ(devices.probe->played[0].second, static_cast<std::size_t>(VoiceFrameSamples));
    EXPECT_GT(devices.probe->playedEnergy, 0.0);
    EXPECT_TRUE(client.otherPlayer->getHasVoiceProperty());
    EXPECT_TRUE(client.otherPlayer->getIsTalkingProperty());
    EXPECT_FALSE(client.otherPlayer->getIsMutedByLocalUserProperty());
    // Frame 41 lost: 42 plays after one concealed frame; then the late 41 is dropped.
    frame.Sequence = 42;
    client.SendFromHost(NetPacketCodec::Encode(frame));
    for (int i = 0; i < 200 && devices.probe->played.size() < 3; ++i, PollYield()) client.session->Update();
    EXPECT_EQ(devices.probe->played.size(), 3u);
    frame.Sequence = 41;
    client.SendFromHost(NetPacketCodec::Encode(frame));
    for (int i = 0; i < 20; ++i, PollYield()) client.session->Update();
    EXPECT_EQ(devices.probe->played.size(), 3u);
    // A quarter second without speech: no longer talking, still has voice.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    client.session->Update();
    EXPECT_FALSE(client.otherPlayer->getIsTalkingProperty());
    EXPECT_TRUE(client.otherPlayer->getHasVoiceProperty());
}

// EnableSendVoice(remote, false) stops this gamer's speech to that gamer (the microphone closes
// when nobody could hear it) but not the once-a-second presence; the Guide's Mute stops both
// directions and shows as IsMutedByLocalUser.
TEST(ENetBackendTest, EnableSendVoiceAndMuteStopSpeechButNotPresence) {
    if (!voiceAvailable()) GTEST_SKIP() << "built without libopus";
    VoiceDevices devices;
    FakeHostedClient client;
    ASSERT_NE(client.otherPlayer, nullptr);
    auto speech = FirstSpeech(client);
    ASSERT_TRUE(speech.has_value());
    LocalNetworkGamer* local = client.LocalFor(client.signedIn);
    local->EnableSendVoice(client.otherPlayer, false);
    (void)CollectVoice(client, std::chrono::milliseconds(100));
    auto frames = CollectVoice(client, std::chrono::milliseconds(1200));
    EXPECT_EQ(Speech(frames), 0u);
    EXPECT_GE(frames.size(), 1u) << "presence stopped with the speech";
    EXPECT_FALSE(devices.probe->open);
    local->EnableSendVoice(client.otherPlayer, true);
    EXPECT_GT(Speech(CollectVoice(client, std::chrono::milliseconds(300))), 0u);

    CNA::Internal::GamerServices::setVoiceMuted("ClientPlayer", "OtherPlayer", true);
    (void)CollectVoice(client, std::chrono::milliseconds(100));
    EXPECT_TRUE(client.otherPlayer->getIsMutedByLocalUserProperty());
    frames = CollectVoice(client, std::chrono::milliseconds(1200));
    EXPECT_EQ(Speech(frames), 0u);
    EXPECT_GE(frames.size(), 1u);
    VoiceDataMessage incoming = *speech;
    incoming.SenderWireId = 0;
    incoming.TargetWireId = 5;
    client.SendFromHost(NetPacketCodec::Encode(incoming));
    (void)CollectVoice(client, std::chrono::milliseconds(200));
    EXPECT_TRUE(devices.probe->played.empty()) << "a muted gamer was heard";
    EXPECT_TRUE(client.otherPlayer->getIsTalkingProperty()) << "a muted gamer still shows talking";
}

// The host takes a voice frame only from the machine that owns its sender, and relays one between
// two other machines unchanged.
TEST(ENetBackendTest, TheHostTakesVoiceOnlyFromItsSendersMachineAndRelaysIt) {
    if (!voiceAvailable()) GTEST_SKIP() << "built without libopus";
    VoiceDevices devices;
    SystemLinkSessionFixture host("HostPlayer");
    ENetHostHandle fakeClient1 = ENetHostHandle::CreateClient(2);
    ENetHostHandle fakeClient2 = ENetHostHandle::CreateClient(2);
    ENetPeer* peer1 = nullptr;
    ENetPeer* peer2 = nullptr;
    const uint8_t remote1 = ConnectFakeClientAndCompleteHandshake(fakeClient1, host.session, &peer1);
    const uint8_t remote2 = ConnectFakeClientAndCompleteHandshake(fakeClient2, host.session, &peer2);
    NetworkGamer* first = nullptr;
    for (NetworkGamer* gamer : host.session->getAllGamersProperty())
        if (gamer->getIdProperty() == remote1) first = gamer;
    ASSERT_NE(first, nullptr);
    const uint8_t hostId = host.session->getLocalGamersProperty()[0]->getIdProperty();
    // Speech from the host, for a real payload.
    std::optional<VoiceDataMessage> speech;
    for (int i = 0; i < 400 && !speech; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        while (fakeClient1.Service(0, evt) > 0) {
            if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            enet_packet_destroy(evt.packet);
            if (NetPacketCodec::PeekTag(data) == MessageTag::VoiceData && (data[3] & VoiceFlagTalking)) speech = NetPacketCodec::DecodeVoiceData(data);
        }
    }
    ASSERT_TRUE(speech.has_value());
    auto sendFrom = [&](ENetHostHandle& from, ENetPeer* to, VoiceDataMessage frame) {
        const auto bytes = NetPacketCodec::Encode(frame);
        from.Send(to, 1, bytes.data(), bytes.size(), ENET_PACKET_FLAG_UNSEQUENCED);
        from.Flush();
    };
    // Client 2 claiming client 1's gamer is ignored.
    VoiceDataMessage spoof = *speech;
    spoof.SenderWireId = remote1;
    spoof.TargetWireId = hostId;
    sendFrom(fakeClient2, peer2, spoof);
    for (int i = 0; i < 30; ++i, PollYield()) host.session->Update();
    EXPECT_TRUE(devices.probe->played.empty());
    EXPECT_FALSE(first->getHasVoiceProperty());
    // Client 1 as itself is heard.
    sendFrom(fakeClient1, peer1, spoof);
    for (int i = 0; i < 200 && devices.probe->played.empty(); ++i, PollYield()) host.session->Update();
    EXPECT_EQ(devices.probe->played.size(), 1u);
    EXPECT_TRUE(first->getHasVoiceProperty());
    EXPECT_TRUE(first->getIsTalkingProperty());
    // Client 1 to client 2 goes through the host unchanged.
    VoiceDataMessage relayed = *speech;
    relayed.SenderWireId = remote1;
    relayed.TargetWireId = remote2;
    relayed.Sequence = 7;
    sendFrom(fakeClient1, peer1, relayed);
    std::optional<VoiceDataMessage> arrived;
    for (int i = 0; i < 200 && !arrived; ++i, PollYield()) {
        host.session->Update();
        ENetEvent evt{};
        while (fakeClient2.Service(0, evt) > 0) {
            if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            enet_packet_destroy(evt.packet);
            if (NetPacketCodec::PeekTag(data) == MessageTag::VoiceData) {
                auto frame = NetPacketCodec::DecodeVoiceData(data);
                if (frame.SenderWireId == remote1) arrived = frame;
            }
        }
    }
    ASSERT_TRUE(arrived.has_value());
    EXPECT_EQ(arrived->TargetWireId, remote2);
    EXPECT_EQ(arrived->Sequence, 7);
    EXPECT_EQ(arrived->Payload, relayed.Payload);
}
