// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"

using namespace CNA::Internal::Net;
using Microsoft::Xna::Framework::Net::NetworkSessionType;

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
