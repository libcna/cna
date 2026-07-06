// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

using namespace CNA::Internal::Net;
using Microsoft::Xna::Framework::Net::NetworkSessionType;
using Microsoft::Xna::Framework::Net::PacketWriter;

TEST(NetDiscoveryProtocolTest, PeekTagOnEmptyBufferThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(NetDiscoveryProtocol::PeekTag(empty), std::out_of_range);
}

TEST(NetDiscoveryProtocolTest, QueryRoundtrip) {
    DiscoveryQueryMessage message;
    message.ProtocolVersion = kDiscoveryProtocolVersion;
    message.SessionTypeFilter = NetworkSessionType::SystemLink;

    auto bytes = NetDiscoveryProtocol::Encode(message);
    EXPECT_EQ(NetDiscoveryProtocol::PeekTag(bytes), DiscoveryMessageTag::Query);

    auto decoded = NetDiscoveryProtocol::DecodeQuery(bytes);

    EXPECT_EQ(decoded.ProtocolVersion, kDiscoveryProtocolVersion);
    EXPECT_EQ(decoded.SessionTypeFilter, NetworkSessionType::SystemLink);
}

TEST(NetDiscoveryProtocolTest, AnnounceRoundtripWithNoProperties) {
    DiscoveryAnnounceMessage message;
    message.ConnectPort = 12345;
    message.CurrentGamerCount = 2;
    message.MaxGamers = 8;
    message.OpenPrivateSlots = 1;
    message.OpenPublicSlots = 5;
    message.HostGamertag = "hostplayer";

    auto bytes = NetDiscoveryProtocol::Encode(message);
    EXPECT_EQ(NetDiscoveryProtocol::PeekTag(bytes), DiscoveryMessageTag::Announce);

    auto decoded = NetDiscoveryProtocol::DecodeAnnounce(bytes);

    EXPECT_EQ(decoded.ProtocolVersion, kDiscoveryProtocolVersion);
    EXPECT_EQ(decoded.ConnectPort, 12345);
    EXPECT_EQ(decoded.CurrentGamerCount, 2);
    EXPECT_EQ(decoded.MaxGamers, 8);
    EXPECT_EQ(decoded.OpenPrivateSlots, 1);
    EXPECT_EQ(decoded.OpenPublicSlots, 5);
    EXPECT_EQ(decoded.HostGamertag, "hostplayer");
    EXPECT_EQ(decoded.Properties.getCountProperty(), 0);
}

TEST(NetDiscoveryProtocolTest, AnnounceRoundtripWithSparseProperties) {
    DiscoveryAnnounceMessage message;
    message.ConnectPort = 999;
    message.HostGamertag = "sparse";
    // Sparse: only indices 0 and 3 have values; 1 and 2 stay nullopt.
    message.Properties.Add(42);
    message.Properties.Add(std::nullopt);
    message.Properties.Add(std::nullopt);
    message.Properties.Add(7);

    auto bytes = NetDiscoveryProtocol::Encode(message);
    auto decoded = NetDiscoveryProtocol::DecodeAnnounce(bytes);

    ASSERT_EQ(decoded.Properties.getCountProperty(), 4);
    EXPECT_EQ(decoded.Properties[0], std::optional<int>(42));
    EXPECT_EQ(decoded.Properties[1], std::nullopt);
    EXPECT_EQ(decoded.Properties[2], std::nullopt);
    EXPECT_EQ(decoded.Properties[3], std::optional<int>(7));
}

namespace {
    // Hand-crafts a well-formed Announce message up through the properties section, then injects
    // a malformed properties section that DiscoveryAnnounceMessage::Encode() (via WriteProperties)
    // could never itself produce - simulating a crafted/corrupted packet from an untrusted source
    // (LAN discovery is unauthenticated broadcast UDP). presentCount/index/value are written
    // exactly as WriteProperties would for a well-formed entry, except the index itself is
    // adversarial.
    std::vector<SharpRuntime::bytecs> BuildAnnounceWithRawPropertyIndex(int32_t index, int32_t value) {
        PacketWriter writer;
        writer.Write(static_cast<SharpRuntime::bytecs>(DiscoveryMessageTag::Announce));
        writer.Write(kDiscoveryProtocolVersion);
        writer.Write(static_cast<uint16_t>(1)); // ConnectPort
        writer.Write(static_cast<int32_t>(0));  // CurrentGamerCount
        writer.Write(static_cast<int32_t>(8));  // MaxGamers
        writer.Write(static_cast<int32_t>(0));  // OpenPrivateSlots
        writer.Write(static_cast<int32_t>(8));  // OpenPublicSlots
        writer.Write(std::string("malformed")); // HostGamertag
        writer.Write(static_cast<int32_t>(1));  // presentCount
        writer.Write(index);
        writer.Write(value);
        return NetPacketCodec::ExtractBytes(writer);
    }
}

// Task 1.1: a crafted/corrupted DiscoveryAnnounceMessage with a negative property index used to
// reach NetworkSessionProperties::operator[](index)'s "index >= size()" guard (also false for a
// negative index), falling through to an out-of-bounds std::vector access via
// static_cast<std::size_t>(negative) - undefined behavior. Confirmed fixed: rejected cleanly with
// a catchable exception instead.
TEST(NetDiscoveryProtocolTest, DecodeAnnounceRejectsNegativePropertyIndex) {
    auto bytes = BuildAnnounceWithRawPropertyIndex(-1, 123);
    EXPECT_THROW(NetDiscoveryProtocol::DecodeAnnounce(bytes), std::runtime_error);
}

// Task 1.2: an unbounded positive property index (e.g. near INT32_MAX) would otherwise make
// ReadProperties' pre-extend loop call Add() up to ~2 billion times - a real hang/OOM from one
// crafted packet, entirely decoupled from how many bytes are actually left in the buffer. This
// test itself has an implicit timeout (the whole suite run) that would catch a hang if the fix
// regressed; asserting a prompt, clean exception is the direct proof.
TEST(NetDiscoveryProtocolTest, DecodeAnnounceRejectsHugePropertyIndex) {
    auto bytes = BuildAnnounceWithRawPropertyIndex(INT32_MAX - 1, 123);
    EXPECT_THROW(NetDiscoveryProtocol::DecodeAnnounce(bytes), std::runtime_error);
}

// Task 1.6: ProtocolVersion was written on the wire by Encode() and read back by Decode*, but
// never actually compared against kDiscoveryProtocolVersion - purely decorative. Simulates a peer
// running a genuinely different protocol version (e.g. an older/newer build with a different
// kDiscoveryProtocolVersion constant) faithfully encoding its own real message, rather than a
// malformed/adversarial payload — Encode() itself never validates ProtocolVersion, so setting it
// directly on the message before encoding is sufficient.
TEST(NetDiscoveryProtocolTest, DecodeQueryRejectsMismatchedProtocolVersion) {
    DiscoveryQueryMessage message;
    message.ProtocolVersion = kDiscoveryProtocolVersion + 1;
    message.SessionTypeFilter = NetworkSessionType::SystemLink;

    auto bytes = NetDiscoveryProtocol::Encode(message);
    EXPECT_THROW(NetDiscoveryProtocol::DecodeQuery(bytes), std::runtime_error);
}

TEST(NetDiscoveryProtocolTest, DecodeAnnounceRejectsMismatchedProtocolVersion) {
    DiscoveryAnnounceMessage message;
    message.ProtocolVersion = kDiscoveryProtocolVersion + 1;
    message.ConnectPort = 12345;
    message.HostGamertag = "hostplayer";

    auto bytes = NetDiscoveryProtocol::Encode(message);
    EXPECT_THROW(NetDiscoveryProtocol::DecodeAnnounce(bytes), std::runtime_error);
}

// Task 5.14: adversarial/truncated-buffer coverage at the codec-unit level, complementing
// ENetDiscoveryServiceTest's own PollIgnoresMalformedAnnounceWhileIdlingAndDiscoveryKeepsWorking
// (which proves the *system* survives a truncated packet via HandleReceived's try/catch) with a
// direct assertion that DecodeQuery/DecodeAnnounce themselves throw cleanly - rather than reading
// out of bounds - when handed a buffer truncated down to just its tag byte.

TEST(NetDiscoveryProtocolTest, DecodeQueryThrowsOnTruncatedBuffer) {
    DiscoveryQueryMessage message;
    message.ProtocolVersion = kDiscoveryProtocolVersion;
    message.SessionTypeFilter = NetworkSessionType::SystemLink;

    auto bytes = NetDiscoveryProtocol::Encode(message);
    bytes.resize(1); // keep only the tag byte
    EXPECT_THROW(NetDiscoveryProtocol::DecodeQuery(bytes), std::runtime_error);
}

TEST(NetDiscoveryProtocolTest, DecodeAnnounceThrowsOnTruncatedBuffer) {
    DiscoveryAnnounceMessage message;
    message.ConnectPort = 12345;
    message.HostGamertag = "hostplayer";

    auto bytes = NetDiscoveryProtocol::Encode(message);
    bytes.resize(1);
    EXPECT_THROW(NetDiscoveryProtocol::DecodeAnnounce(bytes), std::runtime_error);
}
