// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
//
// Task 6.1: a small standalone (non-GTest) executable that plays either the "host" or "client"
// role in a real two-process ENet loopback test. Every Phase 5 test proved the ENet backend works
// within a single process (one real NetworkSession plus a raw ENetHostHandle/socket standing in
// for "the other machine" — see NEXT.md section 4/5 for why only one real NetworkSession can
// exist per process). This harness is spawned twice, as two genuinely independent OS processes,
// by tests/CNA/Internal/Net/TwoProcessLoopbackTest.cpp, to prove the same real transport works
// across separate address spaces too.
//
// The host prints "PORT=<n>\n" to stdout as soon as it has a real bound ENet port, then waits for
// the client to join and exchange one AppData round trip. The client receives the host's port via
// --port (handed to it out-of-band by the orchestrating test, not through network discovery — see
// the approved plan for why cross-process ENetDiscoveryService port sharing was deliberately not
// used here). Exit codes: 0 success, 1 internal timeout, 2 unexpected exception or protocol
// mismatch, 64 bad usage.
//
// Task 5.5 (plans/plan_net.md Phase 5): two more roles, migration-host/migration-survivor, prove real
// host migration across 3 genuinely independent processes (a real NetworkSession per process is
// still exactly one each - see above). Unlike host/client, migration-survivor's own reconnect to
// the newly-promoted peer *does* go through the real ENetDiscoveryService LAN rediscovery path
// (there is no other way for a surviving peer to learn the new host's address for real - see
// AttemptHostMigration's own doc comment in ENetBackend.cpp), deliberately exercising the exact
// cross-process discovery-port sharing this file's own host/client roles avoid.
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/VoiceChat.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "System/InvalidOperationException.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <sys/resource.h>

using namespace CNA::Internal::Net;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

namespace {
    constexpr SharpRuntime::bytecs kMagicPayload[] = {0x42, 0x13, 0x37, 0x99};
    constexpr SharpRuntime::bytecs kMigrationReady[] = {0x72, 0x65, 0x61, 0x64, 0x79};
    constexpr auto kPollInterval = std::chrono::milliseconds(2);
    constexpr auto kSendFlushWindow = std::chrono::milliseconds(500);

    NetworkGamer* FindRemoteGamer(NetworkSession* session) {
        for (NetworkGamer* gamer : session->getAllGamersProperty()) {
            if (!gamer->getIsLocalProperty()) {
                return gamer;
            }
        }
        return nullptr;
    }

    // Polls session->Update() until predicate is true or deadline elapses. Returns false on
    // timeout (caller decides how to fail).
    template <typename Predicate>
    bool PumpUntil(NetworkSession* session, std::chrono::steady_clock::time_point deadline, Predicate predicate) {
        while (!predicate()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            session->Update();
            std::this_thread::sleep_for(kPollInterval);
        }
        return true;
    }

    int RunHost(int timeoutSeconds) {
        auto gamer = SignedInGamer::CreateInternal("HostPlayer");
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );

        uint16_t port = ENetBackend::GetBoundPort(session);
        if (port == 0) {
            std::fprintf(stderr, "host: never bound a real ENet port\n");
            session->Dispose();
            return 2;
        }
        std::printf("PORT=%u\n", static_cast<unsigned>(port));
        std::fflush(stdout);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "host: timed out waiting for client to join\n");
            session->Dispose();
            return 1;
        }

        NetworkGamer* remote = FindRemoteGamer(session);
        if (remote == nullptr) {
            std::fprintf(stderr, "host: joined roster has no remote gamer\n");
            session->Dispose();
            return 2;
        }

        LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
        std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
        NetworkGamer* sender = nullptr;
        bool gotPayload = false;

        if (!PumpUntil(session, deadline, [&] {
                if (!localGamer->getIsDataAvailableProperty()) {
                    return false;
                }
                int len = localGamer->ReceiveData(received, sender);
                gotPayload = (len == static_cast<int>(sizeof(kMagicPayload)));
                return true;
            })) {
            std::fprintf(stderr, "host: timed out waiting for client's payload\n");
            session->Dispose();
            return 1;
        }
        if (!gotPayload) {
            std::fprintf(stderr, "host: received payload had unexpected length\n");
            session->Dispose();
            return 2;
        }

        localGamer->SendData(received, SendDataOptions::Reliable, remote);

        // Give ENet a real window to actually flush the echo out before the process exits.
        auto flushDeadline = std::chrono::steady_clock::now() + kSendFlushWindow;
        while (std::chrono::steady_clock::now() < flushDeadline) {
            session->Update();
            std::this_thread::sleep_for(kPollInterval);
        }

        session->Dispose();
        return 0;
    }

    int RunClient(uint16_t port, int timeoutSeconds) {
        if (port == 0) {
            std::fprintf(stderr, "client: --port is required and must be nonzero\n");
            return 64;
        }

        auto gamer = SignedInGamer::CreateInternal("ClientPlayer");
        using Microsoft::Xna::Framework::GamerServices::Gamer;
        using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&gamer})));
        struct RestoreSignedIn {
            ~RestoreSignedIn() { Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}))); }
        } restore;
        // The parent supplies discovery metadata; client construction/handshake uses public Join.
        auto available = AvailableNetworkSession::CreateInternal(1, "HostPlayer", 0, 7,
            NetworkSessionProperties{}, QualityOfService::CreateInternal(), "127.0.0.1", port,
            NetworkSessionType::SystemLink);
        NetworkSession* session = NetworkSession::Join(&available);
        auto& properties = session->getSessionPropertiesProperty();
        bool rejected = false;
        try { properties[0] = 7; } catch(const System::InvalidOperationException&) { rejected = true; }
        if (session->getIsHostProperty() || !rejected || properties.getItem(0).has_value()) {
            std::fprintf(stderr, "client: public Join did not enforce host-only property writes\n");
            session->Dispose();
            return 70;
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "client: timed out waiting to join host\n");
            session->Dispose();
            return 1;
        }

        NetworkGamer* remote = FindRemoteGamer(session);
        if (remote == nullptr) {
            std::fprintf(stderr, "client: joined roster has no remote gamer\n");
            session->Dispose();
            return 2;
        }

        LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
        std::vector<SharpRuntime::bytecs> payload(kMagicPayload, kMagicPayload + sizeof(kMagicPayload));
        localGamer->SendData(payload, SendDataOptions::Reliable, remote);

        std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
        NetworkGamer* sender = nullptr;
        bool gotReply = false;

        if (!PumpUntil(session, deadline, [&] {
                if (!localGamer->getIsDataAvailableProperty()) {
                    return false;
                }
                int len = localGamer->ReceiveData(received, sender);
                gotReply = (len == static_cast<int>(sizeof(kMagicPayload)));
                return true;
            })) {
            std::fprintf(stderr, "client: timed out waiting for host's echo\n");
            session->Dispose();
            return 1;
        }

        session->Dispose();

        if (!gotReply || received != payload) {
            std::fprintf(stderr, "client: echoed payload did not match what was sent\n");
            return 2;
        }
        return 0;
    }

    // Join after a SystemLink Find keeps the search's local gamers: reference Join inherits the
    // search's local-gamer limit, and a search given a list joins exactly that group. Two gamers are
    // signed in; --find=limit searches with a limit of one, --find=list with only the second gamer.
    // Either way exactly one local gamer joins, then exchanges the usual payload with a host role.
    int RunFindJoinClient(uint16_t port, const std::string& mode, int timeoutSeconds) {
        if (port == 0) {
            std::fprintf(stderr, "find-join-client: --port is required and must be nonzero\n");
            return 64;
        }
        auto first = SignedInGamer::CreateInternal("ClientPlayer");
        auto second = SignedInGamer::CreateInternal("ClientPlayer2", false, false, Microsoft::Xna::Framework::PlayerIndex::Two);
        using Microsoft::Xna::Framework::GamerServices::Gamer;
        using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&first, &second})));
        struct RestoreSignedIn {
            ~RestoreSignedIn() { Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}))); }
        } restore;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        NetworkSession* session = nullptr;
        while (session == nullptr) {
            if (std::chrono::steady_clock::now() >= deadline) {
                std::fprintf(stderr, "find-join-client: timed out finding the host\n");
                return 1;
            }
            AvailableNetworkSessionCollection found = mode == "list"
                ? NetworkSession::Find(NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&second}, NetworkSessionProperties{})
                : NetworkSession::Find(NetworkSessionType::SystemLink, 1, NetworkSessionProperties{});
            // Discovery also reaches any other SystemLink host on this machine; join the one started
            // for this test.
            for (int i = 0; i < found.getCountProperty() && session == nullptr; ++i) {
                AvailableNetworkSession listing = found.getItem(i);
                if (listing.GetConnectPort() == port) session = NetworkSession::Join(&listing);
            }
        }

        SignedInGamer* expected = mode == "list" ? &second : &first;
        const auto& locals = session->getLocalGamersProperty();
        if (locals.getCountProperty() != 1 || locals[0]->getSignedInGamerProperty() != expected) {
            std::fprintf(stderr, "find-join-client: joined with %d local gamers, not just %s\n",
                         locals.getCountProperty(), expected->getGamertagProperty().c_str());
            session->Dispose();
            return 3;
        }
        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "find-join-client: timed out waiting to join host\n");
            session->Dispose();
            return 1;
        }
        NetworkGamer* remote = FindRemoteGamer(session);
        LocalNetworkGamer* localGamer = locals[0];
        std::vector<SharpRuntime::bytecs> payload(kMagicPayload, kMagicPayload + sizeof(kMagicPayload));
        localGamer->SendData(payload, SendDataOptions::Reliable, remote);
        std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
        NetworkGamer* sender = nullptr;
        if (!PumpUntil(session, deadline, [&] {
                if (!localGamer->getIsDataAvailableProperty()) return false;
                localGamer->ReceiveData(received, sender);
                return true;
            })) {
            std::fprintf(stderr, "find-join-client: timed out waiting for host's echo\n");
            session->Dispose();
            return 1;
        }
        session->Dispose();
        if (received != payload) {
            std::fprintf(stderr, "find-join-client: echoed payload did not match what was sent\n");
            return 2;
        }
        return 0;
    }

    // A client adds a second local gamer after joining (split-screen co-op joining mid-session).
    // The host must place it on that client's machine, receive its send from it, and reach it with
    // a reply addressed to it.
    int RunAddedGamerHost(int timeoutSeconds) {
        auto gamer = SignedInGamer::CreateInternal("HostPlayer");
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );
        uint16_t port = ENetBackend::GetBoundPort(session);
        if (port == 0) {
            std::fprintf(stderr, "added-gamer-host: never bound a real ENet port\n");
            session->Dispose();
            return 2;
        }
        std::printf("PORT=%u\n", static_cast<unsigned>(port));
        std::fflush(stdout);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 3; })) {
            std::fprintf(stderr, "added-gamer-host: timed out waiting for the client's added gamer\n");
            session->Dispose();
            return 1;
        }

        LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
        NetworkGamer* first = nullptr;
        NetworkGamer* added = nullptr;
        for (NetworkGamer* g : session->getAllGamersProperty()) {
            if (g->getGamertagProperty() == "ClientPlayer") first = g;
            if (g->getGamertagProperty() == "ClientPlayer2") added = g;
        }
        if (first == nullptr || added == nullptr || added->getIsLocalProperty() ||
            &added->getMachineProperty() != &first->getMachineProperty() ||
            &added->getMachineProperty() == &localGamer->getMachineProperty()) {
            std::fprintf(stderr, "added-gamer-host: the added gamer is not on the client's machine\n");
            session->Dispose();
            return 2;
        }

        std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
        NetworkGamer* sender = nullptr;
        int length = 0;
        if (!PumpUntil(session, deadline, [&] {
                if (!localGamer->getIsDataAvailableProperty()) return false;
                length = localGamer->ReceiveData(received, sender);
                return true;
            })) {
            std::fprintf(stderr, "added-gamer-host: timed out waiting for the added gamer's payload\n");
            session->Dispose();
            return 1;
        }
        if (sender != added || length != static_cast<int>(sizeof(kMagicPayload))) {
            std::fprintf(stderr, "added-gamer-host: the payload did not come from the added gamer\n");
            session->Dispose();
            return 2;
        }

        localGamer->SendData(received, SendDataOptions::Reliable, added);
        auto flushDeadline = std::chrono::steady_clock::now() + kSendFlushWindow;
        while (std::chrono::steady_clock::now() < flushDeadline) {
            session->Update();
            std::this_thread::sleep_for(kPollInterval);
        }
        session->Dispose();
        return 0;
    }

    int RunAddedGamerClient(uint16_t port, int timeoutSeconds) {
        if (port == 0) {
            std::fprintf(stderr, "added-gamer-client: --port is required and must be nonzero\n");
            return 64;
        }
        auto gamer = SignedInGamer::CreateInternal("ClientPlayer");
        auto second = SignedInGamer::CreateInternal("ClientPlayer2");
        using Microsoft::Xna::Framework::GamerServices::Gamer;
        using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&gamer})));
        struct RestoreSignedIn {
            ~RestoreSignedIn() { Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}))); }
        } restore;
        auto available = AvailableNetworkSession::CreateInternal(1, "HostPlayer", 0, 7,
            NetworkSessionProperties{}, QualityOfService::CreateInternal(), "127.0.0.1", port,
            NetworkSessionType::SystemLink);
        NetworkSession* session = NetworkSession::Join(&available);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "added-gamer-client: timed out waiting to join host\n");
            session->Dispose();
            return 1;
        }
        NetworkGamer* host = FindRemoteGamer(session);

        session->AddLocalGamer(&second);
        LocalNetworkGamer* added = nullptr;
        for (LocalNetworkGamer* local : session->getLocalGamersProperty()) {
            if (local->getSignedInGamerProperty() == &second) added = local;
        }
        if (host == nullptr || added == nullptr) {
            std::fprintf(stderr, "added-gamer-client: AddLocalGamer did not add the gamer\n");
            session->Dispose();
            return 2;
        }

        // Sent at once: the transport holds it until the host has numbered the gamer.
        std::vector<SharpRuntime::bytecs> payload(kMagicPayload, kMagicPayload + sizeof(kMagicPayload));
        added->SendData(payload, SendDataOptions::Reliable, host);

        std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
        NetworkGamer* sender = nullptr;
        if (!PumpUntil(session, deadline, [&] {
                if (!added->getIsDataAvailableProperty()) return false;
                added->ReceiveData(received, sender);
                return true;
            })) {
            std::fprintf(stderr, "added-gamer-client: timed out waiting for the host's reply to the added gamer\n");
            session->Dispose();
            return 1;
        }
        const bool ok = sender == host && received == payload;
        session->Dispose();
        if (!ok) {
            std::fprintf(stderr, "added-gamer-client: the reply did not match what was sent\n");
            return 2;
        }
        return 0;
    }

    // Task 5.5 (plans/plan_net.md Phase 5): the host role for a genuine 3-process host migration test.
    // Waits for both migration-survivor roles below to join (3 total gamers: this host + 2
    // survivors), then Dispose()s - a graceful Dispose() sends real ENet DISCONNECT notifications
    // to both peers immediately, the fastest, most deterministic way to trigger their own real
    // HandleDisconnect/AttemptHostMigration paths, instead of waiting out ENet's connection
    // timeout for an unplugged-cable-style abrupt loss.
    int RunMigrationHost(int timeoutSeconds) {
        auto gamer = SignedInGamer::CreateInternal("HostPlayer");
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );
        // As in XNA, the host allows migration and every machine learns it from the host.
        session->setAllowHostMigrationProperty(true);

        uint16_t port = ENetBackend::GetBoundPort(session);
        if (port == 0) {
            std::fprintf(stderr, "migration-host: never bound a real ENet port\n");
            session->Dispose();
            return 2;
        }
        std::printf("PORT=%u\n", static_cast<unsigned>(port));
        std::fflush(stdout);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 3; })) {
            std::fprintf(stderr, "migration-host: timed out waiting for both survivors to join\n");
            session->Dispose();
            return 1;
        }

        // A host-side join can precede delivery of the roster broadcast. Wait for each
        // survivor to acknowledge its full roster and installed migration handlers.
        auto* local=session->getLocalGamersProperty()[0];std::set<NetworkGamer*> ready;
        std::vector<SharpRuntime::bytecs> received(sizeof(kMigrationReady));
        if(!PumpUntil(session,deadline,[&] {
            while(local->getIsDataAvailableProperty()) {
                NetworkGamer* sender=nullptr;const auto length=local->ReceiveData(received,sender);
                if(length!=static_cast<int>(sizeof(kMigrationReady))||!sender||sender->getIsLocalProperty()
                    ||!std::equal(received.begin(),received.end(),std::begin(kMigrationReady)))
                    throw std::runtime_error("Invalid migration readiness acknowledgement");
                ready.insert(sender);
            }
            return ready.size()==2;
        })) {
            std::fprintf(stderr,"migration-host: timed out waiting for survivor roster acknowledgements\n");
            session->Dispose();return 1;
        }
        std::printf("ROSTER_READY=2\n");std::fflush(stdout);
        session->Dispose();
        return 0;
    }

    // Task 5.5: one of the two surviving-peer roles. The orchestrating test spawns this role for
    // "SurvivorA" first, waits for its own JOINED line, then spawns it again for "SurvivorB" -
    // wire-ids are assigned host-side in ClientHello arrival order (see EnsureLocalWireIds's own
    // doc comment), so this ordering deterministically gives SurvivorA the lower wire id and
    // therefore the promotion (Task 5.1's "lowest remaining wire id" rule) - not a race the test
    // has to tolerate.
    //
    // After the host dies, exactly one of the two real processes running this function gets
    // promoted (HostChanged fires with newHost == its own local gamer) and the other reconnects
    // (HostChanged fires with newHost != its own local gamer) - both real outcomes are proven by
    // one real AppData round trip over the post-migration connection, mirroring RunHost/RunClient's
    // own echo halves above.
    int RunMigrationSurvivor(uint16_t hostPort, const std::string& gamertag, int timeoutSeconds) {
        if (hostPort == 0) {
            std::fprintf(stderr, "%s: --port is required and must be nonzero\n", gamertag.c_str());
            return 64;
        }

        auto gamer = SignedInGamer::CreateInternal(gamertag);
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );
        session->setAllowHostMigrationProperty(true);
        ENetBackend::ConnectToHost(session, "127.0.0.1", hostPort);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

        // Signals JOINED as soon as this process itself has a wire id (roster >= 2: itself + the
        // host) - not the full 3-gamer roster. The orchestrating test spawns SurvivorB only after
        // reading SurvivorA's own JOINED line, specifically *to* guarantee SurvivorA's wire id is
        // assigned first (see this function's own top comment) - gating JOINED on the full roster
        // instead would deadlock, since SurvivorB can never join until it's spawned.
        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "%s: timed out waiting to join the host\n", gamertag.c_str());
            session->Dispose();
            return 1;
        }
        std::printf("JOINED wireid=%d\n", static_cast<int>(session->getLocalGamersProperty()[0]->getIdProperty()));
        std::fflush(stdout);

        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 3; })) {
            std::fprintf(stderr, "%s: timed out waiting for the full 3-gamer roster to join\n", gamertag.c_str());
            session->Dispose();
            return 1;
        }

        bool hostChanged = false;
        bool sessionEnded = false;
        NetworkGamer* newHost = nullptr;
        session->HostChanged += [&](System::Object*, const HostChangedEventArgs& e) {
            hostChanged = true;
            newHost = e.getNewHostProperty();
        };
        session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs&) {
            sessionEnded = true;
        };
        session->getLocalGamersProperty()[0]->SendData(
            std::vector<SharpRuntime::bytecs>(std::begin(kMigrationReady),std::end(kMigrationReady)),
            SendDataOptions::ReliableInOrder,session->getHostProperty());

        if (!PumpUntil(session, deadline, [&] { return hostChanged || sessionEnded; })) {
            std::fprintf(stderr, "%s: timed out waiting for host migration to complete\n", gamertag.c_str());
            session->Dispose();
            return 1;
        }
        if (sessionEnded) {
            std::fprintf(stderr, "%s: session ended instead of migrating\n", gamertag.c_str());
            session->Dispose();
            return 2;
        }

        bool amINewHost = (newHost == session->getLocalGamersProperty()[0]);
        std::printf("%s\n", amINewHost ? "PROMOTED" : "RECONNECTED");
        std::fflush(stdout);

        // Real end-to-end proof: wait for the other survivor to (re)join this new topology, then
        // exchange one real AppData round trip over the fresh connection.
        if (!PumpUntil(session, deadline, [&] { return session->getAllGamersProperty().getCountProperty() >= 2; })) {
            std::fprintf(stderr, "%s: timed out waiting for the other survivor after migration\n", gamertag.c_str());
            session->Dispose();
            return 1;
        }

        NetworkGamer* remote = FindRemoteGamer(session);
        if (remote == nullptr) {
            std::fprintf(stderr, "%s: post-migration roster has no remote gamer\n", gamertag.c_str());
            session->Dispose();
            return 2;
        }
        LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];

        if (amINewHost) {
            std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
            NetworkGamer* sender = nullptr;
            bool gotPayload = false;
            if (!PumpUntil(session, deadline, [&] {
                    if (!localGamer->getIsDataAvailableProperty()) {
                        return false;
                    }
                    int len = localGamer->ReceiveData(received, sender);
                    gotPayload = (len == static_cast<int>(sizeof(kMagicPayload)));
                    return true;
                })) {
                std::fprintf(stderr, "%s: timed out waiting for the other survivor's payload\n", gamertag.c_str());
                session->Dispose();
                return 1;
            }
            if (!gotPayload) {
                std::fprintf(stderr, "%s: received payload had unexpected length\n", gamertag.c_str());
                session->Dispose();
                return 2;
            }
            localGamer->SendData(received, SendDataOptions::Reliable, remote);
            auto flushDeadline = std::chrono::steady_clock::now() + kSendFlushWindow;
            while (std::chrono::steady_clock::now() < flushDeadline) {
                session->Update();
                std::this_thread::sleep_for(kPollInterval);
            }
        } else {
            std::vector<SharpRuntime::bytecs> payload(kMagicPayload, kMagicPayload + sizeof(kMagicPayload));
            localGamer->SendData(payload, SendDataOptions::Reliable, remote);

            std::vector<SharpRuntime::bytecs> received(sizeof(kMagicPayload));
            NetworkGamer* sender = nullptr;
            bool gotReply = false;
            if (!PumpUntil(session, deadline, [&] {
                    if (!localGamer->getIsDataAvailableProperty()) {
                        return false;
                    }
                    int len = localGamer->ReceiveData(received, sender);
                    gotReply = (len == static_cast<int>(sizeof(kMagicPayload)));
                    return true;
                })) {
                std::fprintf(stderr, "%s: timed out waiting for the echo\n", gamertag.c_str());
                session->Dispose();
                return 1;
            }
            if (!gotReply || received != payload) {
                std::fprintf(stderr, "%s: echoed payload did not match what was sent\n", gamertag.c_str());
                session->Dispose();
                return 2;
            }
        }

        session->Dispose();
        return 0;
    }

    // Task 6.3: ENetBackend::StartHosting used to emplace() the new session into its Sessions()
    // map *before* calling ENetDiscoveryService::RegisterHost(), which can throw (EnsureSocket's
    // enet_socket_create()/enet_socket_bind() failure). A throw there used to leave a permanent,
    // leaked Sessions() entry - a real, live, still-bound ENet host that's never discoverable via
    // Find() and never torn down (TeardownSession only ever runs from NetworkSession::Dispose(),
    // and the failed NetworkSession was never fully constructed, so no caller ever holds a
    // pointer to Dispose()).
    //
    // Forcing this deterministically needs an isolated process for two independent reasons: (1)
    // ENetDiscoveryService's own discovery socket is a process-wide singleton with no reset hook
    // (see ENetLibrary's own precedent) - once successfully bound, EnsureSocket() never attempts
    // to bind again, so this must run somewhere that has never touched it before, not inside
    // CnaTests, where countless earlier tests already have; and (2) forcing an actual bind
    // failure by occupying the discovery port with another socket doesn't work at all - this port
    // is deliberately designed so any two REUSEADDR-set UDP sockets can coexist on it (see
    // ENetDiscoveryService.cpp's own EnsureSocket, Task 6.5's note, and NEXT.md), so a
    // port-conflict approach is inherently racy/unreliable (confirmed empirically: it failed for
    // the wrong reason under real CnaTests suite ordering). Instead, this lowers RLIMIT_NOFILE to
    // exactly one more than the number of file descriptors already open (stdin/stdout/stderr, the
    // last two dup2'd onto the orchestrating test's pipe) - a portable, deterministic way to make
    // the *second* socket()/open() call from this point on fail with EMFILE. The "+1" headroom
    // matters: StartHosting's own ENetHostHandle::CreateHost() call (its real game-hosting ENet
    // host) opens one socket *before* ever reaching RegisterHost()'s discovery socket, and must be
    // allowed to succeed normally so the failure actually lands where this test needs it to -
    // inside RegisterHost() itself, exercising the exact reordering this task fixed - rather than
    // failing a step earlier (which the original, buggier limit of "already-open count, no
    // headroom" was confirmed to do: it made CreateHost() itself throw first, meaning neither the
    // buggy nor the fixed emplace()/RegisterHost() ordering was ever actually exercised, and the
    // test passed either way for the wrong reason).
    //
    // Confirms NetworkSession::Create(SystemLink, ...) throws cleanly (not a crash/hang) under
    // that condition, then asserts via ENetBackend::GetSessionCountForTesting() that the failed
    // attempt left *zero* sessions registered - the real, deterministic proof (not dependent on
    // whether a later allocation happens to reuse the failed session's freed address, which a
    // naive "does a retry work" check would be). Restores the original file descriptor limit and
    // retries with the same session type as a secondary sanity check that real hosting still
    // works normally afterward.
    // Voice between two real processes: each machine's microphone hears a tone, each speaker
    // counts what it played; both wait until the other gamer talks and was heard.
    std::atomic<int> voicePlayed{0};
    class ToneMicrophone final : public IVoiceCapture {
    public:
        bool present() override { return true; }
        void setOpen(bool) override {}
        void read(std::vector<std::int16_t>& out) override {
            const auto now = std::chrono::steady_clock::now();
            const auto samples = std::chrono::duration_cast<std::chrono::microseconds>(now - last_).count() * VoiceSampleRate / 1000000;
            last_ = now;
            for (long long i = 0; i < std::min<long long>(samples, VoiceSampleRate / 10); ++i) {
                out.push_back(static_cast<std::int16_t>(8000.0 * std::sin(phase_)));
                phase_ += 0.17;
            }
        }
    private:
        std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
        double phase_ = 0.0;
    };
    class CountingSpeaker final : public IVoicePlayback {
    public:
        void play(std::uint8_t, std::span<const std::int16_t>) override { ++voicePlayed; }
        void release(std::uint8_t) override {}
    };
    int RunVoice(bool hosting, uint16_t port, int timeoutSeconds) {
        if (!voiceAvailable()) {
            std::printf("PORT=0\nVOICE_UNAVAILABLE\n");
            std::fflush(stdout);
            return 0;
        }
        setVoiceDevicesForTesting([] { return std::make_unique<ToneMicrophone>(); }, [] { return std::make_unique<CountingSpeaker>(); });
        auto gamer = SignedInGamer::CreateInternal(hosting ? "HostPlayer" : "ClientPlayer");
        using Microsoft::Xna::Framework::GamerServices::Gamer;
        using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;
        Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&gamer})));
        struct RestoreSignedIn {
            ~RestoreSignedIn() { Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}))); }
        } restore;
        NetworkSession* session = nullptr;
        if (hosting) {
            session = NetworkSession::Create(NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{});
            std::printf("PORT=%u\n", static_cast<unsigned>(ENetBackend::GetBoundPort(session)));
            std::fflush(stdout);
        } else {
            auto available = AvailableNetworkSession::CreateInternal(1, "HostPlayer", 0, 7, NetworkSessionProperties{},
                QualityOfService::CreateInternal(), "127.0.0.1", port, NetworkSessionType::SystemLink);
            session = NetworkSession::Join(&available);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        NetworkGamer* remote = nullptr;
        bool talking = false;
        const bool heard = PumpUntil(session, deadline, [&] {
            remote = FindRemoteGamer(session);
            talking = talking || session->getLocalGamersProperty()[0]->getIsTalkingProperty();
            return remote != nullptr && remote->getHasVoiceProperty() && remote->getIsTalkingProperty() && voicePlayed > 0;
        });
        // Keep speaking a while, so the other process hears this one too. (Once it has left, nobody
        // can hear this one and its microphone closes.)
        const auto linger = std::chrono::steady_clock::now() + std::chrono::milliseconds(800);
        while (std::chrono::steady_clock::now() < linger) {
            session->Update();
            if (FindRemoteGamer(session) != nullptr) talking = talking || session->getLocalGamersProperty()[0]->getIsTalkingProperty();
            std::this_thread::sleep_for(kPollInterval);
        }
        session->Dispose();
        setVoiceDevicesForTesting({}, {});
        if (!heard || !talking) {
            std::fprintf(stderr, "voice: heard=%d talking=%d played=%d\n", heard, talking, voicePlayed.load());
            return 1;
        }
        std::printf("VOICE_OK played=%d\n", voicePlayed.load());
        return 0;
    }

    int RunStartHostingPartialFailure() {
        rlimit originalLimit{};
        if (getrlimit(RLIMIT_NOFILE, &originalLimit) != 0) {
            std::fprintf(stderr, "partial-failure: getrlimit(RLIMIT_NOFILE) failed\n");
            return 2;
        }

        // 3 covers stdin/stdout/stderr, the only descriptors this minimal harness has open at
        // this point; +1 is the headroom for ENetHostHandle::CreateHost()'s own socket (see the
        // function's own top comment for why this exact number matters). Lowering rlim_cur below
        // the current open-descriptor count is POSIX-legal and doesn't need closing anything
        // first - it only blocks descriptors opened *after* this point.
        rlimit exhaustedLimit = originalLimit;
        exhaustedLimit.rlim_cur = 4;
        if (setrlimit(RLIMIT_NOFILE, &exhaustedLimit) != 0) {
            std::fprintf(stderr, "partial-failure: setrlimit(RLIMIT_NOFILE) failed\n");
            return 2;
        }

        if (ENetBackend::GetSessionCountForTesting() != 0) {
            std::fprintf(stderr, "partial-failure: expected a freshly-started process with zero registered "
                                  "sessions before this test even runs\n");
            setrlimit(RLIMIT_NOFILE, &originalLimit);
            return 2;
        }

        auto gamer = SignedInGamer::CreateInternal("HostPlayer");

        bool threwAsExpected = false;
        try {
            NetworkSession* neverConstructed = NetworkSession::Create(
                NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
            );
            (void) neverConstructed;
        } catch (const std::exception&) {
            threwAsExpected = true;
        }

        // Restore the real limit; the retry below needs to actually open a socket successfully.
        setrlimit(RLIMIT_NOFILE, &originalLimit);

        if (!threwAsExpected) {
            std::fprintf(stderr, "partial-failure: expected NetworkSession::Create to throw while the "
                                  "file descriptor limit was exhausted, but it did not\n");
            return 2;
        }

        // The real proof: StartHosting must be all-or-nothing. A correct fix never commits the
        // failed session into Sessions() at all, regardless of where the retry below happens to
        // land on the heap.
        std::size_t countAfterFailure = ENetBackend::GetSessionCountForTesting();
        if (countAfterFailure != 0) {
            std::fprintf(stderr, "partial-failure: %zu session(s) left registered after the failed "
                                  "StartHosting attempt - expected exactly 0\n", countAfterFailure);
            return 2;
        }

        NetworkSession* retry = NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
        );
        uint16_t port = ENetBackend::GetBoundPort(retry);
        if (port == 0) {
            std::fprintf(stderr, "partial-failure: retry never bound a real ENet port\n");
            retry->Dispose();
            return 2;
        }

        retry->Dispose();
        return 0;
    }
}

int main(int argc, char** argv) {
    std::string role;
    uint16_t port = 0;
    int timeoutSeconds = 8;
    std::string gamertag;
    std::string findMode = "limit";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--role=", 0) == 0) {
            role = arg.substr(7);
        } else if (arg.rfind("--port=", 0) == 0) {
            port = static_cast<uint16_t>(std::stoi(arg.substr(7)));
        } else if (arg.rfind("--timeout=", 0) == 0) {
            timeoutSeconds = std::stoi(arg.substr(10));
        } else if (arg.rfind("--gamertag=", 0) == 0) {
            gamertag = arg.substr(11);
        } else if (arg.rfind("--find=", 0) == 0) {
            findMode = arg.substr(7);
        }
    }

    try {
        if (role == "host") {
            return RunHost(timeoutSeconds);
        }
        if (role == "client") {
            return RunClient(port, timeoutSeconds);
        }
        if (role == "find-join-client") {
            return RunFindJoinClient(port, findMode, timeoutSeconds);
        }
        if (role == "added-gamer-host") {
            return RunAddedGamerHost(timeoutSeconds);
        }
        if (role == "added-gamer-client") {
            return RunAddedGamerClient(port, timeoutSeconds);
        }
        if (role == "voice-host") {
            return RunVoice(true, 0, timeoutSeconds);
        }
        if (role == "voice-client") {
            return RunVoice(false, port, timeoutSeconds);
        }
        if (role == "start-hosting-partial-failure") {
            return RunStartHostingPartialFailure();
        }
        if (role == "migration-host") {
            return RunMigrationHost(timeoutSeconds);
        }
        if (role == "migration-survivor") {
            if (gamertag.empty()) {
                std::fprintf(stderr, "migration-survivor: --gamertag is required\n");
                return 64;
            }
            return RunMigrationSurvivor(port, gamertag, timeoutSeconds);
        }
        std::fprintf(stderr,
                      "Usage: %s --role=host|client|find-join-client|added-gamer-host|added-gamer-client|"
                      "start-hosting-partial-failure|migration-host|migration-survivor|voice-host|voice-client "
                      "[--port=<n>] [--gamertag=<name>] [--find=limit|list] [--timeout=<seconds>]\n",
                      argv[0]);
        return 64;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "Unhandled exception: %s\n", ex.what());
        return 2;
    } catch (...) {
        std::fprintf(stderr, "Unhandled unknown exception\n");
        return 2;
    }
}
