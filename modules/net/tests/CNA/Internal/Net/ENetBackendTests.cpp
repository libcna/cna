// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
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
    // works because CnaTests is linked with -sASYNCIFY=1 (see CMakeLists.txt). Called once per
    // polling-loop iteration below in place of native/Windows's instant, no-delay-needed spin.
    void PollYield() { emscripten_sleep(10); }
#else
    void PollYield() { }
#endif

    // Task 6.4/6.5 (plans/plan_net.md Phase 6): shared handshake helper for the SimulatedLatency/
    // SimulatedPacketLoss tests below - connects fakeClient to host's real bound port, completes a
    // real ClientHello/ServerWelcome round trip, and returns the wire id the host assigned to
    // fakeClient's own gamer (needed as AppDataMessage::SenderWireId for every AppData sent below).
    uint8_t ConnectFakeClientAndCompleteHandshake(ENetHostHandle& fakeClient, NetworkSession* hostSession, ENetPeer** outPeerFromHostSide) {
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
        return welcome.AssignedWireIds.empty() ? 0 : welcome.AssignedWireIds[0];
    }
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
}

// audit_net.md remediation (2026-07-18): replaces the old drop-counting test
// (SendAppDataBeforeHandshakeDropsButIsNowObservable) - that test manufactured its "not yet
// known" target via NetworkGamer::CreateInternal, a gamer that could never actually join (no
// production caller can construct a NetworkGamer* for someone who hasn't joined yet - every real
// remote NetworkGamer* is allocated fresh, internally, by HandleClientHello/HandleServerWelcome/
// HandleGamerJoinBroadcast at the exact moment its wire id is assigned), so it could only ever
// prove the drop, never a real later delivery.
//
// This test instead exercises the one *naturally* reachable pre-handshake gap: AddLocalGamer
// (real public API - e.g. split-screen co-op joining mid-session) assigns only a
// NetworkSession-level placeholder id and does not touch ENetBackend's own wire-id map at all
// (confirmed by reading its implementation) - so a local gamer added between two real client
// joins has a genuine window where SendAppData's *sender* isn't resolved yet, targeting an
// already-real, already-wired remote gamer. Proves the full contract end-to-end over the real
// wire: queued (not dropped, not delivered early) while unresolved, then delivered - with
// payload, target wire-id, and SendDataOptions all intact - the moment a second real ClientHello
// re-runs EnsureLocalWireIds and resolves the sender.
TEST(ENetBackendTest, AppDataQueuedBeforeSecondLocalGamerIsWiredIsDeliveredOnceResolved) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    // NetworkSession::Create's explicit-local-gamers overload (SystemLinkSessionFixture's own
    // choice elsewhere in this file) sizes maxLocalGamers_ exactly to the initial list, leaving
    // no room for AddLocalGamer below - so this test instead uses the maxLocalGamers-int overload
    // with the process-wide signed-in-gamers registry seeded to just one gamer, the same
    // "maxLocalGamers=2, only 1 signed in" pattern NetworkSessionTests.cpp's own
    // DisposeFreesEveryGamerTheSessionEverOwned test uses for the identical reason.
    SignedInGamer hostSignedIn = SignedInGamer::CreateInternal("HostPlayer");
    Gamer::setSignedInGamersProperty(
        new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&hostSignedIn}))
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

    NetworkSession* hostSession = NetworkSession::Create(NetworkSessionType::SystemLink, 2, 8, 0, NetworkSessionProperties{});
    struct DisposeGuard {
        NetworkSession* s;
        ~DisposeGuard() { s->Dispose(); }
    } disposeGuard{hostSession};
    ASSERT_EQ(hostSession->getLocalGamersProperty().getCountProperty(), 1);
    hostSession->Update(); // drain this gamer's own local GamerJoin event, same as SystemLinkSessionFixture

    ENetHostHandle fakeClient1 = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide1 = nullptr;
    uint8_t remoteWireId1 = ConnectFakeClientAndCompleteHandshake(fakeClient1, hostSession, &peerFromHostSide1);

    NetworkGamer* remoteGamer1 = nullptr;
    for (NetworkGamer* g : hostSession->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") remoteGamer1 = g;
    }
    ASSERT_NE(remoteGamer1, nullptr);
    ASSERT_EQ(remoteGamer1->getIdProperty(), remoteWireId1);

    SignedInGamer secondSignedIn = SignedInGamer::CreateInternal("HostPlayer2");
    hostSession->AddLocalGamer(&secondSignedIn);
    ASSERT_EQ(hostSession->getLocalGamersProperty().getCountProperty(), 2);
    LocalNetworkGamer* secondLocal = hostSession->getLocalGamersProperty()[1];

    const std::vector<SharpRuntime::bytecs> payload{9, 8, 7};
    secondLocal->SendData(payload, SendDataOptions::Reliable, remoteGamer1);

    // Genuinely queued: several Update() cycles pass with nothing arriving at fakeClient1 and no
    // drop recorded - not delivered early via some other path, not silently lost either.
    for (int i = 0; i < 5; ++i) {
        hostSession->Update();
        ENetEvent evt{};
        ASSERT_EQ(fakeClient1.Service(0, evt), 0) << "AppData delivered before its sender was ever wired";
    }
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before);

    // A second real client joins - this re-runs EnsureLocalWireIds (inside HandleClientHello),
    // finally assigning secondLocal's wire id, which should flush the queued send.
    ENetHostHandle fakeClient2 = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide2 = nullptr;
    ConnectFakeClientAndCompleteHandshake(fakeClient2, hostSession, &peerFromHostSide2);

    uint8_t secondLocalWireId = secondLocal->getIdProperty();

    // fakeClient1 also receives a GamerJoinBroadcast for fakeClient2's own join over this same
    // connection (real, expected fan-out traffic, unrelated to the queued send) - skip anything
    // that isn't the AppData this test is actually waiting for.
    ENetPacket* received = nullptr;
    std::vector<SharpRuntime::bytecs> data;
    for (int i = 0; i < 200 && !received; ++i, PollYield()) {
        hostSession->Update();
        ENetEvent evt{};
        if (fakeClient1.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<SharpRuntime::bytecs> candidate(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            if (NetPacketCodec::PeekTag(candidate) == MessageTag::AppData) {
                received = evt.packet;
                data = std::move(candidate);
            } else {
                enet_packet_destroy(evt.packet);
            }
        }
    }
    ASSERT_NE(received, nullptr) << "queued AppData was never delivered after its sender resolved";

    AppDataMessage appData = NetPacketCodec::DecodeAppData(data);
    enet_packet_destroy(received);

    EXPECT_EQ(appData.SenderWireId, secondLocalWireId);
    EXPECT_EQ(appData.TargetWireId, remoteWireId1);
    EXPECT_EQ(appData.Options, SendDataOptions::Reliable);
    EXPECT_EQ(appData.Payload, payload);

    // A genuine delayed delivery, not a drop papered over by some other mechanism.
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before);
}

// audit_net.md remediation (2026-07-18, third round): order preservation was only ever implied,
// never actually tested. Queues several ReliableInOrder sends from the same still-unresolved
// sender to the same already-resolved target, then proves they arrive over the real wire in
// exactly their original send order once the sender resolves - not just "all eventually
// delivered," but delivered *in order*.
TEST(ENetBackendTest, MultiplePendingAppDataSendsForSameSenderTargetPairDeliverInOriginalOrder) {
    // SystemLinkSessionFixture's own vector-based Create() sizes maxLocalGamers_ exactly to its
    // initial gamer list, leaving no room for AddLocalGamer below - see
    // AppDataQueuedBeforeSecondLocalGamerIsWiredIsDeliveredOnceResolved's own identical comment.
    SignedInGamer hostSignedIn = SignedInGamer::CreateInternal("HostPlayer");
    Gamer::setSignedInGamersProperty(
        new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&hostSignedIn}))
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

    NetworkSession* hostSession = NetworkSession::Create(NetworkSessionType::SystemLink, 2, 8, 0, NetworkSessionProperties{});
    struct DisposeGuard {
        NetworkSession* s;
        ~DisposeGuard() { s->Dispose(); }
    } disposeGuard{hostSession};
    ASSERT_EQ(hostSession->getLocalGamersProperty().getCountProperty(), 1);
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
    LocalNetworkGamer* secondLocal = hostSession->getLocalGamersProperty()[1];

    constexpr int kSendCount = 5;
    for (int i = 0; i < kSendCount; ++i) {
        secondLocal->SendData(
            std::vector<SharpRuntime::bytecs>{static_cast<SharpRuntime::bytecs>(i)}, SendDataOptions::Reliable, remoteGamer1
        );
    }
    hostSession->Update(); // enqueues all kSendCount, in order (single Update() drains all 5 PacketSend events)

    ENetHostHandle fakeClient2 = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide2 = nullptr;
    ConnectFakeClientAndCompleteHandshake(fakeClient2, hostSession, &peerFromHostSide2);

    std::vector<SharpRuntime::bytecs> receivedOrder;
    for (int i = 0; i < 200 && static_cast<int>(receivedOrder.size()) < kSendCount; ++i, PollYield()) {
        hostSession->Update();
        ENetEvent evt{};
        if (fakeClient1.Service(0, evt) > 0 && evt.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            if (NetPacketCodec::PeekTag(data) == MessageTag::AppData) {
                AppDataMessage appData = NetPacketCodec::DecodeAppData(data);
                ASSERT_EQ(appData.Payload.size(), 1u);
                receivedOrder.push_back(appData.Payload[0]);
            }
            enet_packet_destroy(evt.packet);
        }
    }
    ASSERT_EQ(static_cast<int>(receivedOrder.size()), kSendCount);
    EXPECT_EQ(receivedOrder, (std::vector<SharpRuntime::bytecs>{0, 1, 2, 3, 4}));
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
// queue a send whose sender is a still-unresolved second local gamer (AddLocalGamer, same
// naturally-reachable gap as the delivery test above) targeting an already-resolved remote
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

    // A second local gamer added after the handshake already completed - AddLocalGamer only
    // assigns a NetworkSession-level placeholder id, never an ENetBackend wire-id, so this
    // client has no path to ever resolve it (nothing re-sends ClientHello after AddLocalGamer) -
    // a permanently-unresolved sender is exactly what's needed here, not just a temporary one.
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

// audit_net.md remediation (2026-07-18, third round): the direct-disconnect sibling of the test
// above - HandleDisconnect's own per-gamer removal loop (the host losing one of its clients)
// already called PurgePendingPreHandshakeSendsFor in production code, but had no dedicated test
// proving it. Same shape as the delivery test's own setup, but the remote gamer disconnects
// before the second local gamer's own wire id ever resolves, instead of a third join resolving it.
TEST(ENetBackendTest, ClientDisconnectPurgesPendingAppDataNamingThatGamer) {
    std::size_t before = ENetBackend::GetDroppedAppDataCount();

    SignedInGamer hostSignedIn = SignedInGamer::CreateInternal("HostPlayer");
    Gamer::setSignedInGamersProperty(
        new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&hostSignedIn}))
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

    NetworkSession* hostSession = NetworkSession::Create(NetworkSessionType::SystemLink, 2, 8, 0, NetworkSessionProperties{});
    struct DisposeGuard {
        NetworkSession* s;
        ~DisposeGuard() { s->Dispose(); }
    } disposeGuard{hostSession};
    ASSERT_EQ(hostSession->getLocalGamersProperty().getCountProperty(), 1);
    hostSession->Update();

    ENetHostHandle fakeClient = ENetHostHandle::CreateClient(2);
    ENetPeer* peerFromHostSide = nullptr;
    uint8_t remoteWireId = ConnectFakeClientAndCompleteHandshake(fakeClient, hostSession, &peerFromHostSide);

    NetworkGamer* remoteGamer = nullptr;
    for (NetworkGamer* g : hostSession->getAllGamersProperty()) {
        if (g->getGamertagProperty() == "RemotePlayer") remoteGamer = g;
    }
    ASSERT_NE(remoteGamer, nullptr);
    ASSERT_EQ(remoteGamer->getIdProperty(), remoteWireId);

    SignedInGamer secondSignedIn = SignedInGamer::CreateInternal("HostPlayer2");
    hostSession->AddLocalGamer(&secondSignedIn);
    ASSERT_EQ(hostSession->getLocalGamersProperty().getCountProperty(), 2);
    LocalNetworkGamer* secondLocal = hostSession->getLocalGamersProperty()[1];

    secondLocal->SendData(std::vector<SharpRuntime::bytecs>{1}, SendDataOptions::None, remoteGamer);
    hostSession->Update();
    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before) << "should be queued, not yet dropped";

    int leftCount = 0;
    hostSession->GamerLeft += [&leftCount](System::Object*, const GamerLeftEventArgs&) { ++leftCount; };
    fakeClient.Disconnect(peerFromHostSide, 0);
    fakeClient.Flush();
    for (int i = 0; i < 200 && leftCount == 0; ++i, PollYield()) {
        hostSession->Update();
    }
    ASSERT_EQ(leftCount, 1);

    EXPECT_EQ(ENetBackend::GetDroppedAppDataCount(), before + 1);
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

// --- REMED-NET-001: HandleReceive must verify sender authority on host-only broadcast messages ---
//
// Before this fix, ServerWelcome/GamerJoinBroadcast/GamerLeaveBroadcast/StateChangeBroadcast were
// dispatched to their handlers with no check that the sending peer is this session's actual
// authoritative host (state.HostPeer) - any already-connected peer, speaking this fully-inferable
// wire format, could forge one of these directly. The fix rejects (logs + disconnects the peer)
// any of these four message types received from a peer that isn't state.HostPeer. All four tests
// below prove both halves: the forgery has no effect on authoritative state, AND the offending
// peer gets disconnected rather than silently ignored (a first-class protocol event, not a quiet
// drop). Legitimate broadcasts still working end to end is covered by the many pre-existing
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
