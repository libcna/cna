// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "Microsoft/Xna/Framework/Net/NetworkSessionState.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Net
{
    /** @brief ENet disconnect data a host sends to a machine it removes (XNA NetworkMachine.RemoveFromSession). */
    inline constexpr std::uint32_t DisconnectRemovedByHost = 0x524D5648u;

    using Microsoft::Xna::Framework::Net::NetworkSessionState;
    using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
    using Microsoft::Xna::Framework::Net::PacketReader;
    using Microsoft::Xna::Framework::Net::PacketWriter;
    using Microsoft::Xna::Framework::Net::SendDataOptions;

    /**
     * @brief Identifies the kind of message at the front of a connected-channel packet.
     */
    enum class MessageTag : uint8_t
    {
        ClientHello = 0x01,
        ServerWelcome = 0x02,
        GamerJoinBroadcast = 0x03,
        GamerLeaveBroadcast = 0x04,
        // 0x05 is reserved for a future HostChangeBroadcast (host migration); not implemented.
        StateChangeBroadcast = 0x06,
        SessionPropertiesBroadcast = 0x07,
        GamerReadyBroadcast = 0x08,
        // SystemLink only: the host's session settings and machine grouping. The online path takes
        // both from the session directory and refuses these tags.
        SessionSettingsBroadcast = 0x09,
        MachineRosterBroadcast = 0x0A,
        AppData = 0x10,
    };

    /** @brief A wire-id/gamertag/host-flag triple describing one gamer in a roster snapshot. */
    struct RosterEntry
    {
        uint8_t WireId{0};
        std::string Gamertag;
        /**
         * @brief Task 4.6: whether this entry represents the session's host.
         *
         * Lets a receiving client correctly set NetworkGamer::IsHost on the remote gamer
         * representing the actual host machine (see ENetBackend.cpp's HandleServerWelcome/
         * HandleGamerJoinBroadcast) - previously this struct carried no host information at all,
         * so a client's own view of the host's gamer always reported IsHost == false.
         */
        bool IsHost{false};
    };

    /** @brief Sent by a client immediately after connecting, announcing its local gamers. */
    struct ClientHelloMessage
    {
        std::vector<std::string> LocalGamertags;
    };

    /** @brief Sent by the host in reply to ClientHello: assigned wire-ids and the current roster. */
    struct ServerWelcomeMessage
    {
        std::vector<uint8_t> AssignedWireIds;
        std::vector<RosterEntry> ExistingRoster;
        /** @brief Complete host-authoritative session-property snapshot at join time. */
        NetworkSessionProperties SessionProperties;
    };

    /** @brief Sent by the host to already-connected clients when new gamers join. */
    struct GamerJoinBroadcastMessage
    {
        std::vector<RosterEntry> NewGamers;
    };

    /** @brief Sent by the host to remaining clients when gamers leave. */
    struct GamerLeaveBroadcastMessage
    {
        std::vector<uint8_t> WireIds;
    };

    /** @brief Sent by the host to propagate a StartGame/EndGame session-state transition. */
    struct StateChangeBroadcastMessage
    {
        NetworkSessionState NewState{NetworkSessionState::Lobby};
    };

    /** @brief Sent by the host when its mutable session properties change. */
    struct SessionPropertiesBroadcastMessage
    {
        /** @brief Complete replacement snapshot, including null entries and list length. */
        NetworkSessionProperties SessionProperties;
    };

    /** @brief One gamer's lobby readiness. */
    struct GamerReadyEntry
    {
        uint8_t WireId{0};
        bool IsReady{false};
    };
    /**
     * @brief Lobby readiness of gamers: a client reports its own gamers to the host, and the host
     * reports every gamer's changes (and a snapshot to a machine that just joined) to the others.
     */
    struct GamerReadyMessage
    {
        std::vector<GamerReadyEntry> Entries;
    };
    /** @brief The host's session settings, which every machine reports (SystemLink). */
    struct SessionSettingsMessage
    {
        uint8_t MaxGamers{0};
        uint8_t PrivateGamerSlots{0};
        bool AllowJoinInProgress{false};
        bool AllowHostMigration{false};
    };
    /** @brief One gamer's machine, numbered by the host (its own machine is 0). */
    struct MachineRosterEntry
    {
        uint8_t WireId{0};
        uint8_t MachineId{0};
    };
    /** @brief Every gamer's machine as the host groups them (SystemLink). */
    struct MachineRosterMessage
    {
        std::vector<MachineRosterEntry> Entries;
    };
    /** @brief Carries application SendData/ReceiveData payloads between peers, relayed by the host. */
    struct AppDataMessage
    {
        uint8_t SenderWireId{0};
        uint8_t TargetWireId{0};
        SendDataOptions Options{SendDataOptions::None};
        std::vector<SharpRuntime::bytecs> Payload;
    };

    /**
     * @brief Encodes and decodes the connected-channel wire messages exchanged between
     * NetworkSession peers, and maps SendDataOptions to ENet packet flags.
     *
     * Built on the already-shipped PacketWriter/PacketReader (themselves thin
     * System::IO::BinaryWriter/BinaryReader wrappers over a MemoryStream) rather than
     * hand-rolled byte packing, for consistency with how the rest of the Net namespace
     * already serializes binary data.
     */
    class NetPacketCodec
    {
    public:
        static std::vector<SharpRuntime::bytecs> Encode(const ClientHelloMessage& message);
        static std::vector<SharpRuntime::bytecs> Encode(const ServerWelcomeMessage& message);
        static std::vector<SharpRuntime::bytecs> Encode(const GamerJoinBroadcastMessage& message);
        static std::vector<SharpRuntime::bytecs> Encode(const GamerLeaveBroadcastMessage& message);
        static std::vector<SharpRuntime::bytecs> Encode(const StateChangeBroadcastMessage& message);
        /**
         * @brief Encodes a complete host-authoritative session-property snapshot.
         *
         * @param message The snapshot message to encode.
         * @return Connected-channel packet bytes.
         */
        static std::vector<SharpRuntime::bytecs> Encode(const SessionPropertiesBroadcastMessage& message);
        /**
         * @brief Encodes gamer readiness entries.
         *
         * @param message The entries to encode (at most 255).
         * @return Connected-channel packet bytes.
         */
        static std::vector<SharpRuntime::bytecs> Encode(const GamerReadyMessage& message);
        /**
         * @brief Encodes the host's session settings.
         *
         * @param message The settings.
         * @return Connected-channel packet bytes.
         */
        static std::vector<SharpRuntime::bytecs> Encode(const SessionSettingsMessage& message);
        /**
         * @brief Encodes the host's machine grouping.
         *
         * @param message The entries (at most 255).
         * @return Connected-channel packet bytes.
         */
        static std::vector<SharpRuntime::bytecs> Encode(const MachineRosterMessage& message);
        static std::vector<SharpRuntime::bytecs> Encode(const AppDataMessage& message);

        /** @brief Reads the leading MessageTag byte without needing a full decode. */
        static MessageTag PeekTag(const std::vector<SharpRuntime::bytecs>& data);

        static ClientHelloMessage DecodeClientHello(const std::vector<SharpRuntime::bytecs>& data);
        static ServerWelcomeMessage DecodeServerWelcome(const std::vector<SharpRuntime::bytecs>& data);
        static GamerJoinBroadcastMessage DecodeGamerJoinBroadcast(const std::vector<SharpRuntime::bytecs>& data);
        static GamerLeaveBroadcastMessage DecodeGamerLeaveBroadcast(const std::vector<SharpRuntime::bytecs>& data);
        static StateChangeBroadcastMessage DecodeStateChangeBroadcast(const std::vector<SharpRuntime::bytecs>& data);
        /**
         * @brief Decodes a complete host-authoritative session-property snapshot.
         *
         * @param data Connected-channel packet bytes.
         * @return The decoded snapshot message.
         */
        static SessionPropertiesBroadcastMessage DecodeSessionPropertiesBroadcast(
            const std::vector<SharpRuntime::bytecs>& data
        );
        /**
         * @brief Decodes gamer readiness entries.
         *
         * @param data Connected-channel packet bytes.
         * @return The decoded entries.
         */
        static GamerReadyMessage DecodeGamerReady(const std::vector<SharpRuntime::bytecs>& data);
        /**
         * @brief Decodes the host's session settings.
         *
         * @param data Connected-channel packet bytes.
         * @return The decoded settings.
         */
        static SessionSettingsMessage DecodeSessionSettings(const std::vector<SharpRuntime::bytecs>& data);
        /**
         * @brief Decodes the host's machine grouping.
         *
         * @param data Connected-channel packet bytes.
         * @return The decoded entries.
         */
        static MachineRosterMessage DecodeMachineRoster(const std::vector<SharpRuntime::bytecs>& data);
        static AppDataMessage DecodeAppData(const std::vector<SharpRuntime::bytecs>& data);

        /**
         * @brief Maps a SendDataOptions value to the ENet packet flags used to send it.
         *
         * `None` -> `ENET_PACKET_FLAG_UNSEQUENCED` (true best-effort: may drop or reorder).
         * `InOrder` -> `0` (ENet's plain unreliable send is already sequenced per-channel and
         * never delivered out of order — an exact match for "in order" without "reliable").
         * `Reliable`/`ReliableInOrder` -> `ENET_PACKET_FLAG_RELIABLE` (ENet's reliable delivery
         * is inherently ordered too, so both collapse onto the same flag — ENet has no
         * "reliable but unordered" primitive). `Chat` -> `ENET_PACKET_FLAG_RELIABLE` (a judgment
         * call: FNA never implements real delivery for this value, so there is no behavior to
         * match; text should not silently drop).
         *
         * @param options The delivery option to map.
         * @return The corresponding ENet packet flags (an `enet_uint32` bitmask).
         */
        static uint32_t SendDataOptionsToEnetFlags(SendDataOptions options);

        /** @brief Extracts the bytes written so far from a PacketWriter's backing buffer. */
        static std::vector<SharpRuntime::bytecs> ExtractBytes(PacketWriter& writer);

        /** @brief Fills a PacketReader's backing buffer from data and rewinds it to the start. */
        static void FillReader(PacketReader& reader, const std::vector<SharpRuntime::bytecs>& data);
    };
}
