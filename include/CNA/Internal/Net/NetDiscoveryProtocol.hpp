// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
    using Microsoft::Xna::Framework::Net::NetworkSessionType;

    /** @brief Wire format version for the discovery protocol; bump on incompatible changes. */
    constexpr uint8_t kDiscoveryProtocolVersion = 1;

    /** @brief Broadcast (and sent to localhost as a fallback) by a client searching for LAN sessions. */
    struct DiscoveryQueryMessage
    {
        uint8_t ProtocolVersion{kDiscoveryProtocolVersion};
        NetworkSessionType SessionTypeFilter{NetworkSessionType::SystemLink};
    };

    /** @brief Sent by a host directly (unicast) back to a querier, describing its session. */
    struct DiscoveryAnnounceMessage
    {
        uint8_t ProtocolVersion{kDiscoveryProtocolVersion};
        uint16_t ConnectPort{0};
        int32_t CurrentGamerCount{0};
        int32_t MaxGamers{0};
        int32_t OpenPrivateSlots{0};
        int32_t OpenPublicSlots{0};
        std::string HostGamertag;
        NetworkSessionProperties Properties;
    };

    /**
     * @brief Encodes and decodes the raw-socket LAN discovery query/announce messages.
     *
     * These travel over a plain UDP socket, not an ENet connected channel — ENet itself has no
     * built-in discovery mechanism. Uses the same PacketWriter/PacketReader-based wire format as
     * NetPacketCodec for consistency, even though the transport differs.
     */
    class NetDiscoveryProtocol
    {
    public:
        static std::vector<SharpRuntime::bytecs> Encode(const DiscoveryQueryMessage& message);
        static std::vector<SharpRuntime::bytecs> Encode(const DiscoveryAnnounceMessage& message);

        static DiscoveryQueryMessage DecodeQuery(const std::vector<SharpRuntime::bytecs>& data);
        static DiscoveryAnnounceMessage DecodeAnnounce(const std::vector<SharpRuntime::bytecs>& data);
    };
}
