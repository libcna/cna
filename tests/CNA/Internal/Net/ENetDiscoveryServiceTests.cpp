// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include <string>
#include <vector>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#endif

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

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
namespace {
    // Mirrors ENetDiscoveryService.cpp's private kDiscoveryPort (61190) - not exposed publicly,
    // duplicated here deliberately since this test simulates an external, untrusted sender that
    // wouldn't have access to CNA's own internals either.
    constexpr uint16_t kTestDiscoveryPort = 61190;

    // Hand-crafts a raw Announce datagram byte-for-byte matching NetDiscoveryProtocol::Encode's
    // wire format for DiscoveryAnnounceMessage, with a single raw (index, value) property pair -
    // adversarial when index is negative/huge, well-formed when index is small and non-negative.
    // This bypasses the normal Encode() path entirely, simulating a crafted/external packet.
    std::vector<SharpRuntime::bytecs> BuildRawAnnounceWithPropertyIndex(int32_t index, int32_t value) {
        Microsoft::Xna::Framework::Net::PacketWriter writer;
        writer.Write(static_cast<SharpRuntime::bytecs>(DiscoveryMessageTag::Announce));
        writer.Write(kDiscoveryProtocolVersion);
        writer.Write(static_cast<uint16_t>(4242)); // ConnectPort
        writer.Write(static_cast<int32_t>(0));     // CurrentGamerCount
        writer.Write(static_cast<int32_t>(8));     // MaxGamers
        writer.Write(static_cast<int32_t>(0));     // OpenPrivateSlots
        writer.Write(static_cast<int32_t>(8));     // OpenPublicSlots
        writer.Write(std::string("SpoofedHost"));  // HostGamertag
        writer.Write(static_cast<int32_t>(1));     // presentCount
        writer.Write(index);
        writer.Write(value);
        return NetPacketCodec::ExtractBytes(writer);
    }

    // Sends bytes to 127.0.0.1:kTestDiscoveryPort over a plain POSIX UDP socket - deliberately not
    // using ENet at all, since a real attacker on the LAN needs no ENet dependency either; this is
    // the same unauthenticated raw-socket surface ENetDiscoveryService itself listens on.
    void SendRawUdpDatagram(uint16_t port, const std::vector<SharpRuntime::bytecs>& bytes) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        ASSERT_GE(sock, 0) << "failed to create a raw UDP socket for this test: " << strerror(errno);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr), 1);

        ssize_t sent = sendto(sock, bytes.data(), bytes.size(), 0,
                               reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        EXPECT_EQ(sent, static_cast<ssize_t>(bytes.size()));
        close(sock);
    }
}

// Task 1.3: HandleReceived/DecodeAnnounce throwing mid-poll (here, via Task 1.1's negative-index
// guard) used to leave FindSessions()'s currentResults_ pointing at its own about-to-be-destroyed
// stack-local `results` vector, since the plain `currentResults_ = nullptr;` reset at the end of
// FindSessions() was skipped by the exception unwinding straight past it. Confirmed fixed via
// CurrentResultsGuard's RAII reset - retained as defense-in-depth even though Task 1.4 below
// separately stopped HandleReceived from letting DecodeAnnounce's exception escape in the first
// place (so, unlike when this test was originally written against Task 1.3 alone, FindSessions()
// no longer throws here at all; it silently drops the malformed packet and keeps searching).
TEST(ENetDiscoveryServiceTest, MalformedAnnounceDuringSearchIsIgnoredAndDoesNotLeaveADanglingResultsPointer) {
    SystemLinkSessionFixture host("HostPlayer");

    // Already sitting in the OS socket receive buffer before FindSessions() below even sends its
    // own query, so it's the first datagram FindSessions()'s poll loop picks up.
    SendRawUdpDatagram(kTestDiscoveryPort, BuildRawAnnounceWithPropertyIndex(-1, 999));

    // Task 1.4: the malformed packet must not crash the process, must not throw out of
    // FindSessions(), and must not corrupt state so badly that the real host can't still be found
    // within this very same call.
    std::vector<AvailableNetworkSession> found;
    EXPECT_NO_THROW(found = ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].getHostGamertagProperty(), "HostPlayer");
    EXPECT_EQ(found[0].GetConnectPort(), ENetBackend::GetBoundPort(host.session));

    // A second, completely fresh search must also still work correctly - proof that no lingering
    // dangling pointer or corrupted static state survived the first call either (the scenario
    // Task 1.3's CurrentResultsGuard fix specifically targets).
    std::vector<AvailableNetworkSession> foundAgain = ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink);
    ASSERT_EQ(foundAgain.size(), 1u);
    EXPECT_EQ(foundAgain[0].getHostGamertagProperty(), "HostPlayer");
}

// Task 1.4: a malformed announce arriving while no search is in flight (currentResults_ == nullptr)
// takes the passive-responder path — ENetDiscoveryService::Poll(), driven from every
// NetworkSession::Update() regardless of whether anyone is calling FindSessions() at all. Confirms
// that path also survives a malformed datagram without crashing or wedging discovery for
// subsequent, legitimate searches.
TEST(ENetDiscoveryServiceTest, PollIgnoresMalformedAnnounceWhileIdlingAndDiscoveryKeepsWorking) {
    SystemLinkSessionFixture host("HostPlayer");

    SendRawUdpDatagram(kTestDiscoveryPort, BuildRawAnnounceWithPropertyIndex(-1, 999));

    // Pump real Update() calls (-> Poll(), with currentResults_ == nullptr the whole time - no
    // FindSessions() is in flight) so the malformed packet is processed via the idle path.
    auto pumpDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < pumpDeadline)
    {
        EXPECT_NO_THROW(host.session->Update());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Discovery must still work normally afterward.
    std::vector<AvailableNetworkSession> found = ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].getHostGamertagProperty(), "HostPlayer");
}
#endif // !defined(__EMSCRIPTEN__) && !defined(_WIN32)
