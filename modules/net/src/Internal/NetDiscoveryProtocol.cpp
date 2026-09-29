// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include <stdexcept>
#include <array>

namespace CNA::Internal::Net
{
    using SharpRuntime::bytecs;

    namespace
    {
        // Only present values are sent as indexed pairs within the fixed eight-slot contract.
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
            const int32_t presentCount = reader.ReadInt32();
            if (presentCount < 0 || presentCount > 8)
                throw std::runtime_error("NetDiscoveryProtocol: property count out of range");
            std::array<bool,8> seen{};
            for (int32_t i = 0; i < presentCount; ++i)
            {
                const int32_t index = reader.ReadInt32();
                const int32_t value = reader.ReadInt32();
                if (index < 0 || index >= 8)
                    throw std::runtime_error("NetDiscoveryProtocol: property index out of range");
                if (seen[static_cast<std::size_t>(index)])
                    throw std::runtime_error("NetDiscoveryProtocol: duplicate property index");
                seen[static_cast<std::size_t>(index)] = true;
                properties.setItem(index, value);
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

    std::vector<bytecs> NetDiscoveryProtocol::Encode(const DiscoveryQosProbeMessage& message)
    {
        std::vector<bytecs> bytes(kQosProbeBytes, 0);
        bytes[0] = static_cast<bytecs>(DiscoveryMessageTag::QosProbe);
        bytes[1] = message.ProtocolVersion;
        bytes[2] = static_cast<bytecs>(message.ConnectPort & 0xff);
        bytes[3] = static_cast<bytecs>(message.ConnectPort >> 8);
        bytes[4] = message.Index;
        bytes[5] = message.Count;
        return bytes;
    }

    std::vector<bytecs> NetDiscoveryProtocol::EncodeUpstreamProbe(const DiscoveryQosProbeMessage& message)
    {
        auto bytes = Encode(message);
        bytes[0] = static_cast<bytecs>(DiscoveryMessageTag::UpstreamProbe);
        return bytes;
    }

    std::vector<bytecs> NetDiscoveryProtocol::Encode(const DiscoveryUpstreamInviteMessage& message)
    {
        Microsoft::Xna::Framework::Net::PacketWriter writer;
        writer.Write(static_cast<bytecs>(DiscoveryMessageTag::UpstreamInvite));
        writer.Write(message.ProtocolVersion);
        writer.Write(message.ConnectPort);
        writer.Write(message.ResponderPort);
        return NetPacketCodec::ExtractBytes(writer);
    }

    DiscoveryUpstreamInviteMessage NetDiscoveryProtocol::DecodeUpstreamInvite(const std::vector<bytecs>& data)
    {
        Microsoft::Xna::Framework::Net::PacketReader reader;
        NetPacketCodec::FillReader(reader, data);
        (void) reader.ReadByte();
        DiscoveryUpstreamInviteMessage message;
        message.ProtocolVersion = reader.ReadByte();
        ValidateProtocolVersion(message.ProtocolVersion);
        message.ConnectPort = reader.ReadUInt16();
        message.ResponderPort = reader.ReadUInt16();
        return message;
    }

    std::vector<bytecs> NetDiscoveryProtocol::Encode(const DiscoveryUpstreamReportMessage& message)
    {
        Microsoft::Xna::Framework::Net::PacketWriter writer;
        writer.Write(static_cast<bytecs>(DiscoveryMessageTag::UpstreamReport));
        writer.Write(message.ProtocolVersion);
        writer.Write(message.ConnectPort);
        writer.Write(message.BytesPerSecond);
        return NetPacketCodec::ExtractBytes(writer);
    }

    DiscoveryUpstreamReportMessage NetDiscoveryProtocol::DecodeUpstreamReport(const std::vector<bytecs>& data)
    {
        Microsoft::Xna::Framework::Net::PacketReader reader;
        NetPacketCodec::FillReader(reader, data);
        (void) reader.ReadByte();
        DiscoveryUpstreamReportMessage message;
        message.ProtocolVersion = reader.ReadByte();
        ValidateProtocolVersion(message.ProtocolVersion);
        message.ConnectPort = reader.ReadUInt16();
        message.BytesPerSecond = reader.ReadInt32();
        if (message.BytesPerSecond < 0)
            throw std::runtime_error("NetDiscoveryProtocol: upstream rate");
        return message;
    }

    DiscoveryQosProbeMessage NetDiscoveryProtocol::DecodeQosProbe(const std::vector<bytecs>& data)
    {
        if (data.size() != kQosProbeBytes)
            throw std::runtime_error("NetDiscoveryProtocol: QoS probe size");
        DiscoveryQosProbeMessage message;
        message.ProtocolVersion = data[1];
        ValidateProtocolVersion(message.ProtocolVersion);
        message.ConnectPort = static_cast<uint16_t>(data[2] | (data[3] << 8));
        message.Index = data[4];
        message.Count = data[5];
        if (message.Count < 2 || message.Count > 32 || message.Index >= message.Count)
            throw std::runtime_error("NetDiscoveryProtocol: QoS probe index");
        return message;
    }
}
