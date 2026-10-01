// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "System/IO/MemoryStream.hpp"

#include <enet/enet.h>
#include <algorithm>
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

        // Per-gamer flags after a message's original payload, in the order the payload lists its
        // gamers: [0x47][u8 count][u8 flags]*count, bit 0 = guest. Written only when a gamer is a
        // guest, so a message without guests is byte for byte what it always was -- which keeps
        // the online relay path, whose strict parser admits no trailing bytes and whose sessions
        // admit no guests, unchanged. The SystemLink decoders stop reading at the end of the
        // original payload, so a peer that predates the block ignores it, and a message from such
        // a peer has none: every gamer reads as no guest. A block of another tag is a later
        // extension and is skipped; one of this tag with the wrong count is malformed.
        constexpr bytecs GamerFlagsBlock = 0x47;
        constexpr bytecs GamerFlagGuest = 0x01;

        void WriteGamerFlags(PacketWriter& writer, const std::vector<bool>& guests)
        {
            if (std::find(guests.begin(), guests.end(), true) == guests.end()) return;
            writer.Write(GamerFlagsBlock);
            writer.Write(EncodeCount(guests.size(), "gamer flags"));
            for (const bool guest : guests)
            {
                writer.Write(static_cast<bytecs>(guest ? GamerFlagGuest : 0));
            }
        }

        std::vector<bool> ReadGamerFlags(PacketReader& reader, std::size_t count)
        {
            std::vector<bool> guests(count, false);
            if (reader.getLengthProperty() - reader.getPositionProperty() < 2 || reader.ReadByte() != GamerFlagsBlock)
            {
                return guests;
            }
            if (reader.ReadByte() != count || reader.getLengthProperty() - reader.getPositionProperty() < static_cast<int>(count))
            {
                throw std::runtime_error("NetPacketCodec: malformed gamer flags");
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                guests[i] = (reader.ReadByte() & GamerFlagGuest) != 0;
            }
            return guests;
        }

        std::vector<bool> GuestsOf(const std::vector<RosterEntry>& entries)
        {
            std::vector<bool> guests;
            for (const auto& entry : entries) guests.push_back(entry.IsGuest);
            return guests;
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
            if (count > 8) throw std::runtime_error("NetPacketCodec: property count exceeds eight slots");
            // Older valid SystemLink frames with fewer slots retain unspecified trailing values.
            for (bytecs i = 0; i < count; ++i)
            {
                if (reader.ReadBoolean()) properties.setItem(i, reader.ReadInt32());
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
        auto guests = message.LocalGuests;
        guests.resize(message.LocalGamertags.size(), false);
        WriteGamerFlags(writer, guests);
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
        message.LocalGuests = ReadGamerFlags(reader, count);
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
        WriteGamerFlags(writer, GuestsOf(message.ExistingRoster));

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
        const auto guests = ReadGamerFlags(reader, message.ExistingRoster.size());
        for (std::size_t i = 0; i < guests.size(); ++i) message.ExistingRoster[i].IsGuest = guests[i];

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
        WriteGamerFlags(writer, GuestsOf(message.NewGamers));
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
        const auto guests = ReadGamerFlags(reader, count);
        for (std::size_t i = 0; i < guests.size(); ++i) message.NewGamers[i].IsGuest = guests[i];
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

    // --- GamerReadyBroadcast ---

    std::vector<bytecs> NetPacketCodec::Encode(const GamerReadyMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::GamerReadyBroadcast));
        writer.Write(EncodeCount(message.Entries.size(), "GamerReadyMessage.Entries"));
        for (const auto& entry : message.Entries)
        {
            writer.Write(static_cast<bytecs>(entry.WireId));
            writer.Write(entry.IsReady);
        }
        return ExtractBytes(writer);
    }

    GamerReadyMessage NetPacketCodec::DecodeGamerReady(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte(); // skip the tag byte (already inspected by the caller via PeekTag)

        GamerReadyMessage message;
        const bytecs count = reader.ReadByte();
        message.Entries.reserve(count);
        for (bytecs i = 0; i < count; ++i)
        {
            GamerReadyEntry entry;
            entry.WireId = reader.ReadByte();
            entry.IsReady = reader.ReadBoolean();
            message.Entries.push_back(entry);
        }
        return message;
    }

    // --- SessionSettingsBroadcast / MachineRosterBroadcast (SystemLink) ---

    std::vector<bytecs> NetPacketCodec::Encode(const SessionSettingsMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::SessionSettingsBroadcast));
        writer.Write(static_cast<bytecs>(message.MaxGamers));
        writer.Write(static_cast<bytecs>(message.PrivateGamerSlots));
        writer.Write(message.AllowJoinInProgress);
        writer.Write(message.AllowHostMigration);
        return ExtractBytes(writer);
    }

    SessionSettingsMessage NetPacketCodec::DecodeSessionSettings(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte();
        SessionSettingsMessage message;
        message.MaxGamers = reader.ReadByte();
        message.PrivateGamerSlots = reader.ReadByte();
        message.AllowJoinInProgress = reader.ReadBoolean();
        message.AllowHostMigration = reader.ReadBoolean();
        return message;
    }

    std::vector<bytecs> NetPacketCodec::Encode(const MachineRosterMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::MachineRosterBroadcast));
        writer.Write(EncodeCount(message.Entries.size(), "MachineRosterMessage.Entries"));
        for (const auto& entry : message.Entries)
        {
            writer.Write(static_cast<bytecs>(entry.WireId));
            writer.Write(static_cast<bytecs>(entry.MachineId));
        }
        return ExtractBytes(writer);
    }

    MachineRosterMessage NetPacketCodec::DecodeMachineRoster(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte();
        MachineRosterMessage message;
        const bytecs count = reader.ReadByte();
        message.Entries.reserve(count);
        for (bytecs i = 0; i < count; ++i)
        {
            MachineRosterEntry entry;
            entry.WireId = reader.ReadByte();
            entry.MachineId = reader.ReadByte();
            message.Entries.push_back(entry);
        }
        return message;
    }

    std::vector<bytecs> NetPacketCodec::Encode(const AddLocalGamerMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::AddLocalGamerRequest));
        writer.Write(message.Gamertag);
        WriteGamerFlags(writer, {message.IsGuest});
        return ExtractBytes(writer);
    }

    AddLocalGamerMessage NetPacketCodec::DecodeAddLocalGamer(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte();
        AddLocalGamerMessage message;
        message.Gamertag = reader.ReadString();
        message.IsGuest = ReadGamerFlags(reader, 1).front();
        return message;
    }

    std::vector<bytecs> NetPacketCodec::Encode(const NetworkStatsMessage& message)
    {
        PacketWriter writer;
        writer.Write(static_cast<bytecs>(MessageTag::NetworkStatsBroadcast));
        writer.Write(EncodeCount(message.Entries.size(), "NetworkStatsMessage.Entries"));
        for (const auto& entry : message.Entries)
        {
            writer.Write(static_cast<bytecs>(entry.WireId));
            writer.Write(static_cast<SharpRuntime::ushortcs>(entry.Milliseconds));
        }
        return ExtractBytes(writer);
    }

    NetworkStatsMessage NetPacketCodec::DecodeNetworkStats(const std::vector<bytecs>& data)
    {
        PacketReader reader;
        FillReader(reader, data);
        (void) reader.ReadByte();
        NetworkStatsMessage message;
        const bytecs count = reader.ReadByte();
        message.Entries.reserve(count);
        for (bytecs i = 0; i < count; ++i)
        {
            RoundtripEntry entry;
            entry.WireId = reader.ReadByte();
            entry.Milliseconds = reader.ReadUInt16();
            message.Entries.push_back(entry);
        }
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

    // --- VoiceData ---

    std::vector<bytecs> NetPacketCodec::Encode(const VoiceDataMessage& message)
    {
        if (message.Payload.size() > MaxVoicePayloadBytes)
            throw std::runtime_error("NetPacketCodec: voice payload too large");
        std::vector<bytecs> bytes;
        bytes.reserve(6 + message.Payload.size());
        bytes.push_back(static_cast<bytecs>(MessageTag::VoiceData));
        bytes.push_back(message.SenderWireId);
        bytes.push_back(message.TargetWireId);
        bytes.push_back(message.Flags);
        bytes.push_back(static_cast<bytecs>(message.Sequence & 0xff));
        bytes.push_back(static_cast<bytecs>(message.Sequence >> 8));
        bytes.insert(bytes.end(), message.Payload.begin(), message.Payload.end());
        return bytes;
    }

    VoiceDataMessage NetPacketCodec::DecodeVoiceData(std::span<const bytecs> data)
    {
        if (data.size() < 6 || data[0] != static_cast<bytecs>(MessageTag::VoiceData) || data.size() > 6 + MaxVoicePayloadBytes)
            throw std::runtime_error("NetPacketCodec: voice frame size");
        VoiceDataMessage message;
        message.SenderWireId = data[1];
        message.TargetWireId = data[2];
        message.Flags = data[3];
        message.Sequence = static_cast<uint16_t>(data[4] | (data[5] << 8));
        message.Payload.assign(data.begin() + 6, data.end());
        const bool talking = (message.Flags & VoiceFlagTalking) != 0;
        if ((message.Flags & ~VoiceFlagTalking) != 0 || talking == message.Payload.empty())
            throw std::runtime_error("NetPacketCodec: voice frame flags");
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
