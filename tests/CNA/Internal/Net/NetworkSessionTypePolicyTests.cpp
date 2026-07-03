// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
//
// Task 5.9: final NetworkSessionType policy regression pass. Systematically sweeps every
// NetworkSessionType other than SystemLink, proving each is provably unaffected by all of Phase
// 5's real-networking machinery (Tasks 5.1-5.8) — no real ENet host, no discovery, no wire
// traffic, and no wall-clock cost paid searching a network that only SystemLink ever talks to.
// This is a verification pass, not new functionality: no production code changes are expected
// from this task, only test coverage that was previously scattered across individual tasks'
// spot-checks (e.g. ENetBackendTests.cpp's *IsNoOpForNonSystemLinkTypes tests, each covering only
// NetworkSessionType::Local) becoming a single systematic sweep over all four synthetic types.
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include <chrono>
#include <string>
#include <vector>

using namespace CNA::Internal::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::Net::GameStartedEventArgs;
using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
using Microsoft::Xna::Framework::Net::NetworkSessionState;
using Microsoft::Xna::Framework::Net::NetworkSessionType;
using Microsoft::Xna::Framework::Net::SendDataOptions;

namespace {
    // Every NetworkSessionType this project treats as fully synthetic (no real ENet networking).
    // SystemLink is deliberately excluded — that's the one type Phase 5 makes real.
    constexpr NetworkSessionType kSyntheticTypes[] = {
        NetworkSessionType::Local,
        NetworkSessionType::LocalWithLeaderboards,
        NetworkSessionType::PlayerMatch,
        NetworkSessionType::Ranked,
    };

    // Matches ENetBackendTests.cpp's SystemLinkSessionFixture: only one real NetworkSession can
    // exist per process (activeSession_ gate — see NEXT.md section 4/5), so each loop iteration
    // below constructs and disposes exactly one before moving to the next type.
    struct SyntheticSessionFixture {
        SignedInGamer signedIn;
        NetworkSession* session;

        SyntheticSessionFixture(NetworkSessionType type, const std::string& gamertag)
            : signedIn(SignedInGamer::CreateInternal(gamertag))
            , session(NetworkSession::Create(
                  type, std::vector<SignedInGamer*>{&signedIn}, 8, 0, NetworkSessionProperties{}
              ))
        {
        }

        ~SyntheticSessionFixture() { session->Dispose(); }
    };
}

TEST(NetworkSessionTypePolicyTest, RealNetworkingEnabledIsFalseForEverySyntheticType) {
    for (NetworkSessionType type : kSyntheticTypes) {
        EXPECT_FALSE(ENetBackend::RealNetworkingEnabled(type)) << "type=" << static_cast<int>(type);
    }
    EXPECT_TRUE(ENetBackend::RealNetworkingEnabled(NetworkSessionType::SystemLink));
}

TEST(NetworkSessionTypePolicyTest, CreatingASessionNeverBindsARealPortForSyntheticTypes) {
    for (NetworkSessionType type : kSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");
        EXPECT_EQ(ENetBackend::GetBoundPort(fixture.session), 0) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, UpdateNeverBindsAPortOrThrowsForSyntheticTypes) {
    for (NetworkSessionType type : kSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");
        EXPECT_NO_THROW(fixture.session->Update());
        EXPECT_EQ(ENetBackend::GetBoundPort(fixture.session), 0) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, ConnectToHostIsNoOpForEverySyntheticType) {
    for (NetworkSessionType type : kSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");
        EXPECT_NO_THROW(ENetBackend::ConnectToHost(fixture.session, "127.0.0.1", 12345));
        EXPECT_EQ(fixture.session->getAllGamersProperty().getCountProperty(), 1) << "type=" << static_cast<int>(type);
        EXPECT_EQ(ENetBackend::GetBoundPort(fixture.session), 0) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, SendDataStaysFullySyntheticForEverySyntheticType) {
    for (NetworkSessionType type : kSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");
        LocalNetworkGamer* gamer = fixture.session->getLocalGamersProperty()[0];

        std::vector<SharpRuntime::bytecs> payload{1, 2, 3};
        gamer->SendData(payload, SendDataOptions::Reliable);
        fixture.session->Update();

        // PacketSend stays a complete no-op for every synthetic type: nothing ever lands in
        // packetQueue_, matching the pre-Phase-5 stub behavior (see
        // LocalNetworkGamerTest.SendDataThenReceiveDataRoundtrip for the Local-only precedent).
        EXPECT_FALSE(gamer->getIsDataAvailableProperty()) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, StartGameEndGameWorkLocallyButNeverBindAPortForSyntheticTypes) {
    for (NetworkSessionType type : kSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");

        int startedCount = 0;
        fixture.session->GameStarted += [&startedCount](System::Object*, const GameStartedEventArgs&) { ++startedCount; };

        fixture.session->StartGame();
        fixture.session->Update();
        EXPECT_EQ(fixture.session->getSessionStateProperty(), NetworkSessionState::Playing) << "type=" << static_cast<int>(type);
        EXPECT_EQ(startedCount, 1) << "type=" << static_cast<int>(type);
        // No ENet host was ever created to broadcast a StateChangeBroadcastMessage from.
        EXPECT_EQ(ENetBackend::GetBoundPort(fixture.session), 0) << "type=" << static_cast<int>(type);

        fixture.session->EndGame();
        fixture.session->Update();
        EXPECT_EQ(fixture.session->getSessionStateProperty(), NetworkSessionState::Lobby) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, FindReturnsEmptyImmediatelyForEverySyntheticType) {
    for (NetworkSessionType type : kSyntheticTypes) {
        auto start = std::chrono::steady_clock::now();
        std::vector<AvailableNetworkSession> found = ENetDiscoveryService::FindSessions(type);
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();

        EXPECT_TRUE(found.empty()) << "type=" << static_cast<int>(type);
        // SystemLink's real search blocks for a fixed ~150ms window (see
        // ENetDiscoveryServiceTests.cpp); every synthetic type must return long before that,
        // proving the "not SystemLink" fast path is actually taken rather than merely finding
        // nothing after paying the full window's cost.
        EXPECT_LT(elapsedMs, 50) << "type=" << static_cast<int>(type);
    }
}
