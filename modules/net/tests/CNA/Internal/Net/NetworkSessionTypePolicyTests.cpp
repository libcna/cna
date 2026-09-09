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
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include <any>
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

    // The two synthetic types a game can still CREATE. Both are offline session types in XNA too,
    // so a single machine really is the whole session and there is nothing missing about them.
    constexpr NetworkSessionType kOfflineSyntheticTypes[] = {
        NetworkSessionType::Local,
        NetworkSessionType::LocalWithLeaderboards,
    };

    // The two that are refused (SAMPLE-096, misc/known_gaps.md). They are Xbox LIVE matchmaking
    // types: the service finds the peers. With no service, a created session is a room no peer can
    // reach, so create/find refuse instead of handing one back.
    constexpr NetworkSessionType kMatchmakingTypes[] = {
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
    for (NetworkSessionType type : kOfflineSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");
        EXPECT_EQ(ENetBackend::GetBoundPort(fixture.session), 0) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, UpdateNeverBindsAPortOrThrowsForSyntheticTypes) {
    for (NetworkSessionType type : kOfflineSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");
        EXPECT_NO_THROW(fixture.session->Update());
        EXPECT_EQ(ENetBackend::GetBoundPort(fixture.session), 0) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, ConnectToHostIsNoOpForEverySyntheticType) {
    for (NetworkSessionType type : kOfflineSyntheticTypes) {
        SyntheticSessionFixture fixture(type, "Player");
        EXPECT_NO_THROW(ENetBackend::ConnectToHost(fixture.session, "127.0.0.1", 12345));
        EXPECT_EQ(fixture.session->getAllGamersProperty().getCountProperty(), 1) << "type=" << static_cast<int>(type);
        EXPECT_EQ(ENetBackend::GetBoundPort(fixture.session), 0) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, SendDataStaysFullySyntheticForEverySyntheticType) {
    for (NetworkSessionType type : kOfflineSyntheticTypes) {
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
    for (NetworkSessionType type : kOfflineSyntheticTypes) {
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

// SAMPLE-096 (Invites) measured the gap this closes: real XNA refuses
// NetworkSession.Create(PlayerMatch, ...) for a signed-in profile that is not LIVE-eligible and the
// sample prints the reason on screen, while CNA used to succeed and hand back a session with no
// port, no discovery and no peer that could ever arrive. The synthetic no-op policy above is
// deliberate; having no refusal was not.
TEST(NetworkSessionTypePolicyTest, CreateRefusesEveryMatchmakingType) {
    SignedInGamer gamer = SignedInGamer::CreateInternal("Player");
    for (NetworkSessionType type : kMatchmakingTypes) {
        EXPECT_THROW(
            NetworkSession::Create(type, std::vector<SignedInGamer*>{&gamer}, 8, 0,
                                   NetworkSessionProperties{}),
            Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
        ) << "type=" << static_cast<int>(type);
        EXPECT_THROW(
            NetworkSession::Create(type, 1, 8),
            Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
        ) << "type=" << static_cast<int>(type);
    }
}

TEST(NetworkSessionTypePolicyTest, FindRefusesEveryMatchmakingType) {
    SignedInGamer gamer = SignedInGamer::CreateInternal("Player");
    for (NetworkSessionType type : kMatchmakingTypes) {
        EXPECT_THROW(
            NetworkSession::Find(type, 1, NetworkSessionProperties{}),
            Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
        ) << "type=" << static_cast<int>(type);
        EXPECT_THROW(
            NetworkSession::Find(type, std::vector<SignedInGamer*>{&gamer},
                                 NetworkSessionProperties{}),
            Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
        ) << "type=" << static_cast<int>(type);
    }
}

// A refusal that leaves activeAction_/activeSession_ set would brick every later Begin* call for
// the rest of the process (the failure mode Task 6.1 already fixed for a throwing constructor), so
// prove the refusing path is reachable twice and that an ordinary SystemLink-shaped call still
// works afterwards.
TEST(NetworkSessionTypePolicyTest, ARefusedTypeLeavesNoPendingActionBehind) {
    EXPECT_THROW(
        NetworkSession::Create(NetworkSessionType::PlayerMatch, 1, 8),
        Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
    );
    EXPECT_THROW(
        NetworkSession::Create(NetworkSessionType::PlayerMatch, 1, 8),
        Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
    );

    SyntheticSessionFixture fixture(NetworkSessionType::Local, "Player");
    EXPECT_EQ(fixture.session->getSessionTypeProperty(), NetworkSessionType::Local);
}

// XNA's contract calls JoinInvited from an InviteAccepted handler. Nothing raises that event here,
// so no invitation can be pending; the old behaviour built a PlayerMatch session out of nothing.
TEST(NetworkSessionTypePolicyTest, JoinInvitedRefusesBecauseNoInvitationCanBePending) {
    SignedInGamer gamer = SignedInGamer::CreateInternal("Player");
    EXPECT_THROW(
        NetworkSession::JoinInvited(1),
        Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
    );
    EXPECT_THROW(
        NetworkSession::JoinInvited(std::vector<SignedInGamer*>{&gamer}),
        Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
    );
    EXPECT_THROW(
        NetworkSession::BeginJoinInvited(1, System::AsyncCallback{}, std::any{}),
        Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException
    );
    // Argument validation still wins over the refusal, as it does everywhere else here.
    EXPECT_THROW(
        NetworkSession::BeginJoinInvited(0, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
}
