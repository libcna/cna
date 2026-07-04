// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include <string>
#include <vector>

using namespace CNA::Internal::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
using Microsoft::Xna::Framework::Net::NetworkSessionType;

namespace {
    // Matches ENetBackendTests.cpp's SystemLinkSessionFixture: only one real NetworkSession can
    // exist per process (activeSession_ gate in BeginCreate/BeginFind — see NEXT.md section 4/5),
    // so every test here builds exactly one, RAII-disposed on scope exit.
    struct SystemLinkSessionFixture {
        SignedInGamer signedIn;
        NetworkSession* session;

        explicit SystemLinkSessionFixture(const std::string& gamertag)
            : signedIn(SignedInGamer::CreateInternal(gamertag))
            , session(NetworkSession::Create(
                  NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&signedIn}, 8, 0, NetworkSessionProperties{}
              ))
        {
            session->Update();
        }

        ~SystemLinkSessionFixture() { session->Dispose(); }
    };
}

TEST(ENetDiscoveryServiceTest, FindSessionsReturnsEmptyImmediatelyForNonSystemLinkTypes) {
    // No socket I/O at all for non-SystemLink types — matches the NetworkSessionType policy that
    // only SystemLink is backed by real networking (see ENetBackend::RealNetworkingEnabled).
    EXPECT_TRUE(ENetDiscoveryService::FindSessions(NetworkSessionType::Local).empty());
    EXPECT_TRUE(ENetDiscoveryService::FindSessions(NetworkSessionType::PlayerMatch).empty());
    EXPECT_TRUE(ENetDiscoveryService::FindSessions(NetworkSessionType::Ranked).empty());
    EXPECT_TRUE(ENetDiscoveryService::FindSessions(NetworkSessionType::LocalWithLeaderboards).empty());
}

TEST(ENetDiscoveryServiceTest, FindSessionsReturnsEmptyWhenNoHostIsRegistered) {
    // No SystemLinkSessionFixture in this test, so ENetBackend::StartHosting never registered
    // anyone — the search should complete its window and report nothing, not hang or throw.
    EXPECT_TRUE(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink).empty());
}

TEST(ENetDiscoveryServiceTest, FindSessionsDiscoversRegisteredHost) {
    SystemLinkSessionFixture host("HostPlayer");
    uint16_t hostPort = ENetBackend::GetBoundPort(host.session);
    ASSERT_GT(hostPort, 0);

#ifdef __EMSCRIPTEN__
    // ENetDiscoveryService is permanently disabled on Emscripten — raw UDP broadcast/unicast has
    // no equivalent on the Web platform (see NEXT.md and the class's own header doc comment).
    EXPECT_TRUE(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink).empty());
#else
    // ENetBackend::StartHosting (called from the fixture's own NetworkSession constructor)
    // already registered host.session with ENetDiscoveryService. FindSessions is a standalone
    // static call with no activeSession_ involvement, so — unlike NetworkSession::Find() itself,
    // which would throw InvalidOperationException here since host.session already occupies
    // activeSession_ — it can be exercised directly in the same process as a live host. See
    // NEXT.md for why the full public Find() path can't be end-to-end tested this way.
    std::vector<AvailableNetworkSession> found = ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink);

    // Deduped to exactly one entry even though the query reaches this host via two paths
    // (broadcast and an explicit loopback unicast) — see ENetDiscoveryService.cpp's dedup-by-
    // connect-port comment. Which path's reply "wins" the dedup race is non-deterministic (could
    // surface as "127.0.0.1" or this machine's real LAN address), so GetConnectAddress() is only
    // checked for being non-empty, not an exact value.
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].getHostGamertagProperty(), "HostPlayer");
    EXPECT_EQ(found[0].GetConnectPort(), hostPort);
    EXPECT_FALSE(found[0].GetConnectAddress().empty());
    EXPECT_EQ(found[0].getCurrentGamerCountProperty(), 1);
    // NetworkSession::Create()'s EndCreate hardcodes MaxGamers to 69 rather than forwarding the
    // caller's argument (a real, preserved FNA quirk — see NetworkSessionTests.cpp), so open
    // public slots is 69 - privateSlots(0) - currentGamerCount(1), not derived from the 8 passed
    // to Create() here.
    EXPECT_EQ(found[0].getOpenPublicGamerSlotsProperty(), 68);
#endif
}

TEST(ENetDiscoveryServiceTest, UnregisterHostStopsAnsweringQueries) {
    SystemLinkSessionFixture host("HostPlayer");
    ASSERT_GT(ENetBackend::GetBoundPort(host.session), 0);

    ENetDiscoveryService::UnregisterHost(host.session);

    EXPECT_TRUE(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink).empty());
}

TEST(ENetDiscoveryServiceTest, UnregisterHostIsSafeForAnUnregisteredOrMismatchedSession) {
    SystemLinkSessionFixture host("HostPlayer");

    // Neither call should affect host's own registration, nor throw.
    EXPECT_NO_THROW(ENetDiscoveryService::UnregisterHost(nullptr));

    std::vector<AvailableNetworkSession> found = ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink);
#ifdef __EMSCRIPTEN__
    // ENetDiscoveryService is permanently disabled on Emscripten (see NEXT.md) — always empty.
    EXPECT_TRUE(found.empty());
#else
    EXPECT_EQ(found.size(), 1u);
#endif
}
