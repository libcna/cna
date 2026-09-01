// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "System/IO/MemoryStream.hpp"

#include <enet/enet.h>
#include <limits>
#include <stdexcept>

namespace CNA::Internal::Net
{
    using SharpRuntime::bytecs;

    namespace
    {
        bytecs EncodeCount(std::size_t size, const char* fieldName);

        RosterEntry ReadRosterEntry(PacketReader& reader)
        {
            RosterEntry entry;
            entry.WireId = reader.ReadByte();
            entry.Gamertag = reader.ReadString();
            entry.IsHost = reader.ReadBoolean();
            return entry;
        }

        void WriteRosterEntry(PacketWriter& writer, const RosterEntry& entry)
        {
            writer.Write(entry.WireId);
            writer.Write(entry.Gamertag);
            writer.Write(entry.IsHost);
        }

        void WriteSessionProperties(PacketWriter& writer, const NetworkSessionProperties& properties)
        {
            writer.Write(EncodeCount(
                static_cast<std::size_t>(properties.getCountProperty()),
                "NetworkSessionProperties"
            ));
            for (const auto& value : properties)
            {
                writer.Write(value.has_value());
                if (value.has_value())
                {
                    writer.Write(*value);
                }
            }
        }

        NetworkSessionProperties ReadSessionProperties(PacketReader& reader)
        {
            NetworkSessionProperties properties;
            const bytecs count = reader.ReadByte();
            for (bytecs i = 0; i < count; ++i)
            {
                if (reader.ReadBoolean())
                {
                    properties.Add(reader.ReadInt32());
                }
                else
                {
                    properties.Add(std::nullopt);
                }
            }
            return properties;
        }

        // Task 2.12: every list-length count field in this wire format is a single byte; naively
        // casting a collection's size() down to bytecs would silently wrap (e.g. 256 -> 0) while
        // the full, untruncated collection is still serialized right after it, desynchronizing
        // the decoder. Currently unreachable via any real join/leave flow
        // (NetworkSession::MaxSupportedGamers == 31), but nothing in the wire format itself
        // enforces that invariant - guard explicitly here rather than relying on it forever.
        bytecs EncodeCount(std::size_t size, const char* fieldName)
        {
            if (size > static_cast<std::size_t>(std::numeric_limits<bytecs>::max()))
            {
                throw std::runtime_error(
                    std::string("NetPacketCodec: ") + fieldName + " exceeds the wire format's 255-entry capacity"
                );
            }
            return static_cast<bytecs>(size);
        }
    }

    std::vector<bytecs> NetPacketCodec::ExtractBytes(PacketWriter& writer)
    {
        auto* mem = dynamic_cast<System::IO::MemoryStream*>(writer.getBaseStreamProperty());
        return mem ? mem->ToArray() : std::vector<bytecs>{};
    }

    void NetPacketCodec::FillReader(PacketReader& reader, const std::vector<bytecs>& data)
    {
        if (!data.empty())
        {
            reader.getBaseStreamProperty()->Write(data.data(), 0, static_cast<int>(data.size()));
        }
        reader.setPositionProperty(0);
    }

    MessageTag NetPacketCodec::PeekTag(const std::vector<bytecs>& data)
    {
        if (data.empty())
        {
            throw std::out_of_range("Cannot peek a MessageTag from an empty buffer.");
        }
        return static_cast<MessageTag>(data[0]);
    }

    // --- ClientHello ---

    std::vector<bytecs> NetPacketCodec::Encode(const ClientHelloMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::ClientHello));
        writer.Write(EncodeCount(message.LocalGamertags.size(), "ClientHelloMessage.LocalGamertags"));
        for (const auto& gamertag : message.LocalGamertags)
        {
            writer.Write(gamertag);
        }
        return ExtractBytes(writer);
    }

    ClientHelloMessage NetPacketCodec::DecodeClientHello(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        ClientHelloMessage message;
        bytecs count = reader.ReadByte();
        message.LocalGamertags.reserve(count);
        for (bytecs i = 0; i < count; ++i)
        {
            message.LocalGamertags.push_back(reader.ReadString());
        }
        return message;
    }

    // --- ServerWelcome ---

    std::vector<bytecs> NetPacketCodec::Encode(const ServerWelcomeMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::ServerWelcome));

        writer.Write(EncodeCount(message.AssignedWireIds.size(), "ServerWelcomeMessage.AssignedWireIds"));
        for (bytecs wireId : message.AssignedWireIds)
        {
            writer.Write(wireId);
        }

        writer.Write(EncodeCount(message.ExistingRoster.size(), "ServerWelcomeMessage.ExistingRoster"));
        for (const auto& entry : message.ExistingRoster)
        {
            WriteRosterEntry(writer, entry);
        }

        WriteSessionProperties(writer, message.SessionProperties);

        return ExtractBytes(writer);
    }

    ServerWelcomeMessage NetPacketCodec::DecodeServerWelcome(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        ServerWelcomeMessage message;

        bytecs assignedCount = reader.ReadByte();
        message.AssignedWireIds.reserve(assignedCount);
        for (bytecs i = 0; i < assignedCount; ++i)
        {
            message.AssignedWireIds.push_back(reader.ReadByte());
        }

        bytecs rosterCount = reader.ReadByte();
        message.ExistingRoster.reserve(rosterCount);
        for (bytecs i = 0; i < rosterCount; ++i)
        {
            message.ExistingRoster.push_back(ReadRosterEntry(reader));
        }

        message.SessionProperties = ReadSessionProperties(reader);

        return message;
    }

    // --- GamerJoinBroadcast ---

    std::vector<bytecs> NetPacketCodec::Encode(const GamerJoinBroadcastMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::GamerJoinBroadcast));
        writer.Write(EncodeCount(message.NewGamers.size(), "GamerJoinBroadcastMessage.NewGamers"));
        for (const auto& entry : message.NewGamers)
        {
            WriteRosterEntry(writer, entry);
        }
        return ExtractBytes(writer);
    }

    GamerJoinBroadcastMessage NetPacketCodec::DecodeGamerJoinBroadcast(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        GamerJoinBroadcastMessage message;
        bytecs count = reader.ReadByte();
        message.NewGamers.reserve(count);
        for (bytecs i = 0; i < count; ++i)
        {
            message.NewGamers.push_back(ReadRosterEntry(reader));
        }
        return message;
    }

    // --- GamerLeaveBroadcast ---

    std::vector<bytecs> NetPacketCodec::Encode(const GamerLeaveBroadcastMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::GamerLeaveBroadcast));
        writer.Write(EncodeCount(message.WireIds.size(), "GamerLeaveBroadcastMessage.WireIds"));
        for (bytecs wireId : message.WireIds)
        {
            writer.Write(wireId);
        }
        return ExtractBytes(writer);
    }

    GamerLeaveBroadcastMessage NetPacketCodec::DecodeGamerLeaveBroadcast(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        GamerLeaveBroadcastMessage message;
        bytecs count = reader.ReadByte();
        message.WireIds.reserve(count);
        for (bytecs i = 0; i < count; ++i)
        {
            message.WireIds.push_back(reader.ReadByte());
        }
        return message;
    }

    // --- StateChangeBroadcast ---

    std::vector<bytecs> NetPacketCodec::Encode(const StateChangeBroadcastMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::StateChangeBroadcast));
        writer.Write(static_cast<bytecs>(message.NewState));
        return ExtractBytes(writer);
    }

    StateChangeBroadcastMessage NetPacketCodec::DecodeStateChangeBroadcast(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        StateChangeBroadcastMessage message;
        message.NewState = static_cast<NetworkSessionState>(reader.ReadByte());
        return message;
    }

    // --- SessionPropertiesBroadcast ---

    std::vector<bytecs> NetPacketCodec::Encode(const SessionPropertiesBroadcastMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::SessionPropertiesBroadcast));
        WriteSessionProperties(writer, message.SessionProperties);
        return ExtractBytes(writer);
    }

    SessionPropertiesBroadcastMessage NetPacketCodec::DecodeSessionPropertiesBroadcast(
        const std::vector<bytecs>& data
    )
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        SessionPropertiesBroadcastMessage message;
        message.SessionProperties = ReadSessionProperties(reader);
        return message;
    }

    // --- AppData ---

    std::vector<bytecs> NetPacketCodec::Encode(const AppDataMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::AppData));
        writer.Write(message.SenderWireId);
        writer.Write(message.TargetWireId);
        writer.Write(static_cast<bytecs>(message.Options));
        if (!message.Payload.empty())
        {
            writer.Write(message.Payload.data(), 0, static_cast<int>(message.Payload.size()));
        }
        return ExtractBytes(writer);
    }

    AppDataMessage NetPacketCodec::DecodeAppData(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        AppDataMessage message;
        message.SenderWireId = reader.ReadByte();
        message.TargetWireId = reader.ReadByte();
        message.Options = static_cast<SendDataOptions>(reader.ReadByte());

        int remaining = reader.getLengthProperty() - reader.getPositionProperty();
        message.Payload.resize(static_cast<size_t>(remaining > 0 ? remaining : 0));
        if (remaining > 0)
        {
            reader.Read(message.Payload.data(), 0, remaining);
        }
        return message;
    }

    // --- SendDataOptions -> ENet flags ---

    uint32_t NetPacketCodec::SendDataOptionsToEnetFlags(SendDataOptions options)
    {
        switch (options)
        {
            case SendDataOptions::None:
                return ENET_PACKET_FLAG_UNSEQUENCED;
            case SendDataOptions::InOrder:
                return 0;
            case SendDataOptions::Reliable:
            case SendDataOptions::ReliableInOrder:
            case SendDataOptions::Chat:
                return ENET_PACKET_FLAG_RELIABLE;
        }
        return ENET_PACKET_FLAG_RELIABLE;
    }
}
