// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include <enet/enet.h>
#include <limits>

using namespace CNA::Internal::Net;

TEST(NetPacketCodecTest, PeekTagOnEmptyBufferThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(NetPacketCodec::PeekTag(empty), std::out_of_range);
}

TEST(NetPacketCodecTest, ClientHelloRoundtrip) {
    ClientHelloMessage message;
    message.LocalGamertags = {"alice", "bob"};

    auto bytes = NetPacketCodec::Encode(message);
    EXPECT_EQ(NetPacketCodec::PeekTag(bytes), MessageTag::ClientHello);

    auto decoded = NetPacketCodec::DecodeClientHello(bytes);
    ASSERT_EQ(decoded.LocalGamertags.size(), 2u);
    EXPECT_EQ(decoded.LocalGamertags[0], "alice");
    EXPECT_EQ(decoded.LocalGamertags[1], "bob");
}

TEST(NetPacketCodecTest, ClientHelloRoundtripEmpty) {
    ClientHelloMessage message;
    auto bytes = NetPacketCodec::Encode(message);
    auto decoded = NetPacketCodec::DecodeClientHello(bytes);
    EXPECT_TRUE(decoded.LocalGamertags.empty());
}

// Task 2.12: every list-length count field in this wire format is a single byte; naively casting
// size() down to bytecs would silently wrap (e.g. 256 -> 0) while the full, untruncated collection
// is still serialized right after it, desynchronizing the decoder. Currently unreachable via any
// real join/leave flow (NetworkSession::MaxSupportedGamers == 31), but nothing in the wire format
// itself enforced that invariant.
TEST(NetPacketCodecTest, ClientHelloEncodeThrowsWhenLocalGamertagsExceeds255) {
    ClientHelloMessage message;
    message.LocalGamertags.assign(256, "x");
    EXPECT_THROW(NetPacketCodec::Encode(message), std::runtime_error);
}

TEST(NetPacketCodecTest, ServerWelcomeRoundtrip) {
    ServerWelcomeMessage message;
    message.AssignedWireIds = {3, 4};
    // Task 4.6: RosterEntry's 3rd field (IsHost) - "host" is the real host, "guest" is not.
    message.ExistingRoster = {{1, "host", true}, {2, "guest", false}};
    message.SessionProperties.Add(17);
    message.SessionProperties.Add(std::nullopt);
    message.SessionProperties.Add(-42);

    auto bytes = NetPacketCodec::Encode(message);
    EXPECT_EQ(NetPacketCodec::PeekTag(bytes), MessageTag::ServerWelcome);

    auto decoded = NetPacketCodec::DecodeServerWelcome(bytes);
    ASSERT_EQ(decoded.AssignedWireIds.size(), 2u);
    EXPECT_EQ(decoded.AssignedWireIds[0], 3);
    EXPECT_EQ(decoded.AssignedWireIds[1], 4);
    ASSERT_EQ(decoded.ExistingRoster.size(), 2u);
    EXPECT_EQ(decoded.ExistingRoster[0].WireId, 1);
    EXPECT_EQ(decoded.ExistingRoster[0].Gamertag, "host");
    EXPECT_TRUE(decoded.ExistingRoster[0].IsHost);
    EXPECT_EQ(decoded.ExistingRoster[1].WireId, 2);
    EXPECT_EQ(decoded.ExistingRoster[1].Gamertag, "guest");
    EXPECT_FALSE(decoded.ExistingRoster[1].IsHost);
    ASSERT_EQ(decoded.SessionProperties.getCountProperty(), 3);
    EXPECT_EQ(decoded.SessionProperties.getItem(0), 17);
    EXPECT_EQ(decoded.SessionProperties.getItem(1), std::nullopt);
    EXPECT_EQ(decoded.SessionProperties.getItem(2), -42);
}

TEST(NetPacketCodecTest, ServerWelcomeEncodeThrowsWhenAssignedWireIdsExceeds255) {
    ServerWelcomeMessage message;
    message.AssignedWireIds.assign(256, 1);
    EXPECT_THROW(NetPacketCodec::Encode(message), std::runtime_error);
}

TEST(NetPacketCodecTest, ServerWelcomeEncodeThrowsWhenExistingRosterExceeds255) {
    ServerWelcomeMessage message;
    message.ExistingRoster.assign(256, RosterEntry{1, "x"});
    EXPECT_THROW(NetPacketCodec::Encode(message), std::runtime_error);
}

TEST(NetPacketCodecTest, GamerJoinBroadcastRoundtrip) {
    GamerJoinBroadcastMessage message;
    message.NewGamers = {{5, "newgamer"}};

    auto bytes = NetPacketCodec::Encode(message);
    EXPECT_EQ(NetPacketCodec::PeekTag(bytes), MessageTag::GamerJoinBroadcast);

    auto decoded = NetPacketCodec::DecodeGamerJoinBroadcast(bytes);
    ASSERT_EQ(decoded.NewGamers.size(), 1u);
    EXPECT_EQ(decoded.NewGamers[0].WireId, 5);
    EXPECT_EQ(decoded.NewGamers[0].Gamertag, "newgamer");
    // Task 4.6: a newly-joined gamer is never the host - IsHost round-trips as false by default.
    EXPECT_FALSE(decoded.NewGamers[0].IsHost);
}

TEST(NetPacketCodecTest, GamerJoinBroadcastEncodeThrowsWhenNewGamersExceeds255) {
    GamerJoinBroadcastMessage message;
    message.NewGamers.assign(256, RosterEntry{1, "x"});
    EXPECT_THROW(NetPacketCodec::Encode(message), std::runtime_error);
}

TEST(NetPacketCodecTest, GamerLeaveBroadcastRoundtrip) {
    GamerLeaveBroadcastMessage message;
    message.WireIds = {7, 8, 9};

    auto bytes = NetPacketCodec::Encode(message);
    EXPECT_EQ(NetPacketCodec::PeekTag(bytes), MessageTag::GamerLeaveBroadcast);

    auto decoded = NetPacketCodec::DecodeGamerLeaveBroadcast(bytes);
    ASSERT_EQ(decoded.WireIds.size(), 3u);
    EXPECT_EQ(decoded.WireIds[0], 7);
    EXPECT_EQ(decoded.WireIds[1], 8);
    EXPECT_EQ(decoded.WireIds[2], 9);
}

TEST(NetPacketCodecTest, GamerLeaveBroadcastEncodeThrowsWhenWireIdsExceeds255) {
    GamerLeaveBroadcastMessage message;
    message.WireIds.assign(256, 1);
    EXPECT_THROW(NetPacketCodec::Encode(message), std::runtime_error);
}

TEST(NetPacketCodecTest, StateChangeBroadcastRoundtrip) {
    StateChangeBroadcastMessage message;
    message.NewState = Microsoft::Xna::Framework::Net::NetworkSessionState::Playing;

    auto bytes = NetPacketCodec::Encode(message);
    EXPECT_EQ(NetPacketCodec::PeekTag(bytes), MessageTag::StateChangeBroadcast);

    auto decoded = NetPacketCodec::DecodeStateChangeBroadcast(bytes);
    EXPECT_EQ(decoded.NewState, Microsoft::Xna::Framework::Net::NetworkSessionState::Playing);
}

TEST(NetPacketCodecTest, SessionPropertiesBroadcastRoundtripPreservesSparseValues) {
    SessionPropertiesBroadcastMessage message;
    message.SessionProperties.Add(std::numeric_limits<int>::min());
    message.SessionProperties.Add(std::nullopt);
    message.SessionProperties.Add(0);
    message.SessionProperties.Add(std::numeric_limits<int>::max());

    auto bytes = NetPacketCodec::Encode(message);
    EXPECT_EQ(NetPacketCodec::PeekTag(bytes), MessageTag::SessionPropertiesBroadcast);

    auto decoded = NetPacketCodec::DecodeSessionPropertiesBroadcast(bytes);
    ASSERT_EQ(decoded.SessionProperties.getCountProperty(), 4);
    EXPECT_EQ(decoded.SessionProperties.getItem(0), std::numeric_limits<int>::min());
    EXPECT_EQ(decoded.SessionProperties.getItem(1), std::nullopt);
    EXPECT_EQ(decoded.SessionProperties.getItem(2), 0);
    EXPECT_EQ(decoded.SessionProperties.getItem(3), std::numeric_limits<int>::max());
}

TEST(NetPacketCodecTest, SessionPropertiesBroadcastRoundtripPreservesEmptyCollection) {
    SessionPropertiesBroadcastMessage message;
    auto decoded = NetPacketCodec::DecodeSessionPropertiesBroadcast(NetPacketCodec::Encode(message));
    EXPECT_EQ(decoded.SessionProperties.getCountProperty(), 0);
}

TEST(NetPacketCodecTest, SessionPropertiesBroadcastEncodeRejectsMoreThan255Values) {
    SessionPropertiesBroadcastMessage message;
    for (int i = 0; i < 256; ++i) {
        message.SessionProperties.Add(i);
    }
    EXPECT_THROW(NetPacketCodec::Encode(message), std::runtime_error);
}

TEST(NetPacketCodecTest, AppDataRoundtrip) {
    AppDataMessage message;
    message.SenderWireId = 1;
    message.TargetWireId = 2;
    message.Options = SendDataOptions::Reliable;
    message.Payload = {10, 20, 30, 40, 50};

    auto bytes = NetPacketCodec::Encode(message);
    EXPECT_EQ(NetPacketCodec::PeekTag(bytes), MessageTag::AppData);

    auto decoded = NetPacketCodec::DecodeAppData(bytes);
    EXPECT_EQ(decoded.SenderWireId, 1);
    EXPECT_EQ(decoded.TargetWireId, 2);
    EXPECT_EQ(decoded.Options, SendDataOptions::Reliable);
    ASSERT_EQ(decoded.Payload.size(), 5u);
    EXPECT_EQ(decoded.Payload[0], 10);
    EXPECT_EQ(decoded.Payload[4], 50);
}

TEST(NetPacketCodecTest, AppDataRoundtripEmptyPayload) {
    AppDataMessage message;
    message.SenderWireId = 0;
    message.TargetWireId = 1;
    message.Options = SendDataOptions::None;

    auto bytes = NetPacketCodec::Encode(message);
    auto decoded = NetPacketCodec::DecodeAppData(bytes);
    EXPECT_TRUE(decoded.Payload.empty());
}

// Task 5.14: adversarial/truncated-buffer coverage at the codec-unit level. Task 1.4 already
// proved the *system* survives a truncated packet arriving over the wire (ENetBackendTest's
// HostSurvivesTruncatedClientHelloAndContinuesFunctioningAfterward, via HandleReceive's own
// try/catch), but that only exercises the outer resilience layer - nothing directly asserted
// that each Decode* function itself throws a clean, catchable exception (rather than reading out
// of bounds) when hand it a buffer truncated mid-field. These truncate a well-formed encoded
// message down to just its tag byte, forcing every subsequent field read to hit
// BinaryReader::ReadBytes' own underflow guard.

TEST(NetPacketCodecTest, DecodeClientHelloThrowsOnTruncatedBuffer) {
    ClientHelloMessage message;
    message.LocalGamertags = {"alice"};
    auto bytes = NetPacketCodec::Encode(message);
    bytes.resize(1); // keep only the tag byte
    EXPECT_THROW(NetPacketCodec::DecodeClientHello(bytes), System::IO::EndOfStreamException);
}

TEST(NetPacketCodecTest, DecodeServerWelcomeThrowsOnTruncatedBuffer) {
    ServerWelcomeMessage message;
    message.AssignedWireIds = {3, 4};
    message.ExistingRoster = {{1, "host"}};
    auto bytes = NetPacketCodec::Encode(message);
    bytes.resize(1);
    EXPECT_THROW(NetPacketCodec::DecodeServerWelcome(bytes), System::IO::EndOfStreamException);
}

TEST(NetPacketCodecTest, DecodeGamerJoinBroadcastThrowsOnTruncatedBuffer) {
    GamerJoinBroadcastMessage message;
    message.NewGamers = {{5, "newgamer"}};
    auto bytes = NetPacketCodec::Encode(message);
    bytes.resize(1);
    EXPECT_THROW(NetPacketCodec::DecodeGamerJoinBroadcast(bytes), System::IO::EndOfStreamException);
}

TEST(NetPacketCodecTest, DecodeGamerLeaveBroadcastThrowsOnTruncatedBuffer) {
    GamerLeaveBroadcastMessage message;
    message.WireIds = {7, 8, 9};
    auto bytes = NetPacketCodec::Encode(message);
    bytes.resize(1);
    EXPECT_THROW(NetPacketCodec::DecodeGamerLeaveBroadcast(bytes), System::IO::EndOfStreamException);
}

TEST(NetPacketCodecTest, DecodeStateChangeBroadcastThrowsOnTruncatedBuffer) {
    StateChangeBroadcastMessage message;
    message.NewState = Microsoft::Xna::Framework::Net::NetworkSessionState::Playing;
    auto bytes = NetPacketCodec::Encode(message);
    bytes.resize(1);
    EXPECT_THROW(NetPacketCodec::DecodeStateChangeBroadcast(bytes), System::IO::EndOfStreamException);
}

TEST(NetPacketCodecTest, DecodeSessionPropertiesBroadcastThrowsOnTruncatedBuffer) {
    SessionPropertiesBroadcastMessage message;
    message.SessionProperties.Add(42);
    auto bytes = NetPacketCodec::Encode(message);
    bytes.resize(1);
    EXPECT_THROW(
        NetPacketCodec::DecodeSessionPropertiesBroadcast(bytes),
        System::IO::EndOfStreamException
    );
}

TEST(NetPacketCodecTest, DecodeAppDataThrowsOnTruncatedBuffer) {
    AppDataMessage message;
    message.SenderWireId = 1;
    message.TargetWireId = 2;
    message.Options = SendDataOptions::Reliable;
    message.Payload = {10, 20, 30};
    auto bytes = NetPacketCodec::Encode(message);
    bytes.resize(1);
    EXPECT_THROW(NetPacketCodec::DecodeAppData(bytes), System::IO::EndOfStreamException);
}

TEST(NetPacketCodecTest, SendDataOptionsToEnetFlagsMapping) {
    EXPECT_EQ(NetPacketCodec::SendDataOptionsToEnetFlags(SendDataOptions::None), static_cast<uint32_t>(ENET_PACKET_FLAG_UNSEQUENCED));
    EXPECT_EQ(NetPacketCodec::SendDataOptionsToEnetFlags(SendDataOptions::InOrder), 0u);
    EXPECT_EQ(NetPacketCodec::SendDataOptionsToEnetFlags(SendDataOptions::Reliable), static_cast<uint32_t>(ENET_PACKET_FLAG_RELIABLE));
    EXPECT_EQ(NetPacketCodec::SendDataOptionsToEnetFlags(SendDataOptions::ReliableInOrder), static_cast<uint32_t>(ENET_PACKET_FLAG_RELIABLE));
    EXPECT_EQ(NetPacketCodec::SendDataOptionsToEnetFlags(SendDataOptions::Chat), static_cast<uint32_t>(ENET_PACKET_FLAG_RELIABLE));
}
