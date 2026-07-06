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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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
    // works because CnaTests is linked with -sASYNCIFY=1 (see CMakeLists.txt). Called once per
    // polling-loop iteration below in place of native/Windows's instant, no-delay-needed spin.
    void PollYield() { emscripten_sleep(10); }
#else
    void PollYield() { }
#endif
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
    // The host's own local gamer, snapshotted via its real SignedInGamer gamertag (not the
    // "Stub Gamer" value NetworkGamer::getGamertagProperty() reports for local gamers).
    EXPECT_EQ(welcome.ExistingRoster[0].Gamertag, "HostPlayer");

    EXPECT_EQ(host.session->getAllGamersProperty().getCountProperty(), 2);
    EXPECT_EQ(joinCount, 1);
    NetworkGamer* remoteGamer = nullptr;
    for (NetworkGamer* g : host.session->getAllGamersProperty()) {
        // Local gamers always report "Stub Gamer" (see WireGamertagFor's own comment); a real
        // gamertag search only ever finds the remote gamer, so the host's own local gamer is
        // looked up directly below instead.
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
    welcome.ExistingRoster = {RosterEntry{0, "HostPlayer"}};
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
    // NOTE: not asserting IsHost here — this fixture's "client" session is itself constructed via
    // NetworkSession::Create() (see SystemLinkSessionFixture above), so its own local gamer
    // legitimately reports IsHost == true; that reflects this test's Create()-based fixture setup,
    // not the real Join()-based client path exercised by NetworkSessionTests.cpp's
    // JoinInvitedMakesLocalGamersReportIsHostFalse. The remote "HostPlayer" gamer's IsHost stays at
    // NetworkGamer's default false — a documented, scoped limitation (RosterEntry carries no host
    // flag; see NetworkGamer::SetIsHost's doc comment), not something this test can prove correct.
}

// Task 2.13: SendAppData silently dropped (bare `return;`, no error/log/queue) whenever sender
// and/or target weren't yet known to ENetBackend's wire-id map - reachable whenever SendData is
// called immediately after ConnectToHost(), before any Update() call has pumped the connect/
// ClientHello/ServerWelcome round-trip. Rather than a bigger queue-and-flush-once-ready redesign,
// this drop is now at least observable via GetDroppedAppDataCount() instead of vanishing with no
// trace; the actual drop behavior itself is unchanged (still a no-op, just now a counted one).
TEST(ENetBackendTest, SendAppDataBeforeHandshakeDropsButIsNowObservable) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    ENetHostHandle fakeHost = ENetHostHandle::CreateHost(kFakeHostTestPort, 4, 2);
    uint16_t fakeHostPort = fakeHost.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    SystemLinkSessionFixture client("ClientPlayer");
    ENetBackend::ConnectToHost(client.session, "127.0.0.1", fakeHostPort);

    // Immediately, before any Update() has pumped the connect/ClientHello/ServerWelcome round
    // trip - client.session's own ENetBackend-side wire-id map is still completely empty, even
    // though the local gamer already has a NetworkSession-level placeholder Id. Targeting a
    // NetworkGamer the session doesn't officially know about yet (rather than "all gamers",
    // which at this early point resolves to just the sender itself and takes NetworkSession::
    // Update()'s purely-local, ENetBackend-bypassing delivery path instead) forces the dispatch
    // through the real, remote-target SendAppData path this task is about.
    LocalNetworkGamer* localGamer = client.session->getLocalGamersProperty()[0];
    NetworkGamer notYetKnownRemote = NetworkGamer::CreateInternal(client.session, "SomeRemotePlayer");
    localGamer->SendData(std::vector<SharpRuntime::bytecs>{1, 2, 3}, SendDataOptions::None, &notYetKnownRemote);

    client.session->Update(); // drains the just-queued PacketSend event -> SendAppData -> drop

    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before + 1);
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
TEST(ENetBackendTest, SimulatedLatencyAndPacketLossHaveNoEffectOnRealTraffic) {
    SystemLinkSessionFixture host("HostPlayer");
    host.session->setSimulatedLatencyProperty(System::TimeSpan::FromSeconds(5.0));
    host.session->setSimulatedPacketLossProperty(1.0f); // "100% simulated loss"
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
    uint8_t hostLocalWireId = 0;

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
    ASSERT_TRUE(hostLocalGamer->getIsDataAvailableProperty())
        << "AppData delivery should complete normally regardless of the simulated settings";

    std::vector<SharpRuntime::bytecs> received(3);
    NetworkGamer* sender = nullptr;
    int len = hostLocalGamer->ReceiveData(received, sender);
    EXPECT_EQ(len, 3);
    EXPECT_EQ(received, (std::vector<SharpRuntime::bytecs>{10, 20, 30}));
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

    // Task 2.6: AllowHostMigration is stored but has no effect on actual behavior (matching FNA's
    // own reference implementation, itself a plain auto-property with no real migration logic
    // anywhere in FNA's stubbed-out networking layer) - HandleDisconnect below unconditionally
    // ends the session regardless of this flag; this test proves that documented behavior rather
    // than letting a future reader assume setting it to true silently, secretly works.
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
