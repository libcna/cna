// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"

namespace CNA::Internal::Net
{
    using SharpRuntime::bytecs;

    namespace
    {
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

        NetworkSessionProperties ReadProperties(Microsoft::Xna::Framework::Net::PacketReader& reader)
        {
            NetworkSessionProperties properties;
            int32_t presentCount = reader.ReadInt32();
            for (int32_t i = 0; i < presentCount; ++i)
            {
                int32_t index = reader.ReadInt32();
                int32_t value = reader.ReadInt32();
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
