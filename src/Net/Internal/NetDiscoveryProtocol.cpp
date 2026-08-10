// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include <stdexcept>

namespace CNA::Internal::Net
{
    using SharpRuntime::bytecs;

    namespace
    {
        // Task 1.2: no real game session plausibly has anywhere near this many custom int?
        // properties (FNA's own NetworkSessionProperties is an unbounded List<int?> with no
        // documented cap, but this wire format is CNA's own invention for the ENet transport, not
        // part of FNA's design). A generous-but-safe ceiling that still rejects a maliciously huge
        // wire-supplied index (see ReadProperties) before it can drive an unbounded number of
        // Add() calls.
        constexpr int32_t kMaxPropertyIndex = 256;

        // NetworkSessionProperties is a sparse list of std::optional<int>; only present
        // (non-nullopt) entries are written, as (index, value) pairs, to keep the packet small.
        void WriteProperties(Microsoft::Xna::Framework::Net::PacketWriter& writer, const NetworkSessionProperties& properties)
        {
            int32_t presentCount = 0;
            for (const auto& value : properties)
            {
                if (value.has_value())
                {
                    ++presentCount;
                }
            }
            writer.Write(presentCount);

            int32_t index = 0;
            for (const auto& value : properties)
            {
                if (value.has_value())
                {
                    writer.Write(index);
                    writer.Write(*value);
                }
                ++index;
            }
        }

        // Task 1.6: the wire's ProtocolVersion byte was read but never compared against
        // kDiscoveryProtocolVersion - purely decorative. Rejects a mismatched version up front
        // instead of parsing the rest of a possibly-incompatible payload as if it were
        // current-format (which, for any future format change, could misinterpret unrelated bytes
        // as ConnectPort/property indices/etc. rather than failing cleanly).
        void ValidateProtocolVersion(uint8_t version)
        {
            if (version != kDiscoveryProtocolVersion)
            {
                throw std::runtime_error("NetDiscoveryProtocol: unsupported protocol version");
            }
        }

        NetworkSessionProperties ReadProperties(Microsoft::Xna::Framework::Net::PacketReader& reader)
        {
            NetworkSessionProperties properties;
            int32_t presentCount = reader.ReadInt32();
            for (int32_t i = 0; i < presentCount; ++i)
            {
                int32_t index = reader.ReadInt32();
                int32_t value = reader.ReadInt32();
                // Task 1.1: index comes straight off the wire (this message is parsed from
                // unauthenticated broadcast UDP - see ENetDiscoveryService). A negative index
                // would make this pre-extend loop's guard (count <= index) false immediately (0
                // iterations), then fall through to NetworkSessionProperties::operator[](index)'s
                // own "index >= size()" guard, also false for a negative index - reaching
                // properties_[static_cast<std::size_t>(index)] with a huge, out-of-bounds
                // std::size_t. Reject malformed input before it ever reaches operator[].
                if (index < 0)
                {
                    throw std::runtime_error("NetDiscoveryProtocol: negative property index");
                }
                // Task 1.2: an unbounded positive index (e.g. near INT32_MAX) would otherwise
                // make the pre-extend loop below call Add() up to ~2 billion times - a
                // multi-second hang/OOM from a single crafted packet, decoupled from how many
                // bytes are actually left in the buffer (unlike presentCount above, which is
                // naturally bounded by PacketReader eventually throwing on underflow).
                if (index >= kMaxPropertyIndex)
                {
                    throw std::runtime_error("NetDiscoveryProtocol: property index out of range");
                }
                // operator[](index) only targets an arbitrary index once the list is already at
                // least that long — past the end, it appends instead (a documented
                // NetworkSessionProperties quirk). Pre-extend with Add() so the assignment below
                // always lands on the intended slot.
                while (properties.getCountProperty() <= index)
                {
                    properties.Add(std::nullopt);
                }
                properties[index] = value;
            }
            return properties;
        }
    }

    std::vector<bytecs> NetDiscoveryProtocol::Encode(const DiscoveryQueryMessage& message)
    {
        Microsoft::Xna::Framework::Net::PacketWriter writer;
        writer.Write(static_cast<bytecs>(DiscoveryMessageTag::Query));
        writer.Write(message.ProtocolVersion);
        writer.Write(static_cast<bytecs>(message.SessionTypeFilter));
        return NetPacketCodec::ExtractBytes(writer);
    }

    DiscoveryMessageTag NetDiscoveryProtocol::PeekTag(const std::vector<bytecs>& data)
    {
        return static_cast<DiscoveryMessageTag>(data.at(0));
    }

    DiscoveryQueryMessage NetDiscoveryProtocol::DecodeQuery(const std::vector<bytecs>& data)
    {
        Microsoft::Xna::Framework::Net::PacketReader reader;
        NetPacketCodec::FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        DiscoveryQueryMessage message;
        message.ProtocolVersion = reader.ReadByte();
        ValidateProtocolVersion(message.ProtocolVersion);
        message.SessionTypeFilter = static_cast<NetworkSessionType>(reader.ReadByte());
        return message;
    }

    std::vector<bytecs> NetDiscoveryProtocol::Encode(const DiscoveryAnnounceMessage& message)
    {
        Microsoft::Xna::Framework::Net::PacketWriter writer;
        writer.Write(static_cast<bytecs>(DiscoveryMessageTag::Announce));
        writer.Write(message.ProtocolVersion);
        writer.Write(message.ConnectPort);
        writer.Write(message.CurrentGamerCount);
        writer.Write(message.MaxGamers);
        writer.Write(message.OpenPrivateSlots);
        writer.Write(message.OpenPublicSlots);
        writer.Write(message.HostGamertag);
        WriteProperties(writer, message.Properties);
        return NetPacketCodec::ExtractBytes(writer);
    }

    DiscoveryAnnounceMessage NetDiscoveryProtocol::DecodeAnnounce(const std::vector<bytecs>& data)
    {
        Microsoft::Xna::Framework::Net::PacketReader reader;
        NetPacketCodec::FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte

        DiscoveryAnnounceMessage message;
        message.ProtocolVersion = reader.ReadByte();
        ValidateProtocolVersion(message.ProtocolVersion);
        message.ConnectPort = reader.ReadUInt16();
        message.CurrentGamerCount = reader.ReadInt32();
        message.MaxGamers = reader.ReadInt32();
        message.OpenPrivateSlots = reader.ReadInt32();
        message.OpenPublicSlots = reader.ReadInt32();
        message.HostGamertag = reader.ReadString();
        message.Properties = ReadProperties(reader);
        return message;
    }
}
