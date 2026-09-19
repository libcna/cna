// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Diagnostics/Diagnostics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Inspector
{
    /** @brief Major version of the CNA Inspector wire protocol. */
    inline constexpr std::uint16_t ProtocolMajorVersion = 1;
    /** @brief Minor version of the CNA Inspector wire protocol. */
    inline constexpr std::uint16_t ProtocolMinorVersion = 0;
    /** @brief Maximum accepted or emitted wire payload. */
    inline constexpr std::size_t MaximumPayloadBytes = 8U * 1024U * 1024U;
    /** @brief Maximum event records returned by one request. */
    inline constexpr std::size_t MaximumEventsPerResponse = 4096;
    /** @brief Maximum encoded image data returned by one preview. */
    inline constexpr std::size_t MaximumPreviewBytes = 4U * 1024U * 1024U;

    /** @brief Inspector protocol message type. */
    enum class MessageType : std::uint16_t
    {
        /** @brief First authenticated client negotiation message. */
        ClientHello = 1,
        /** @brief Successful server negotiation response. */
        ServerHello = 2,
        /** @brief Structured request failure. */
        Error = 3,
        /** @brief Demand-driven diagnostics snapshot request. */
        SnapshotRequest = 10,
        /** @brief Diagnostics snapshot response. */
        SnapshotResponse = 11,
        /** @brief Cursor-based diagnostics event request. */
        EventsRequest = 12,
        /** @brief Cursor-based diagnostics event response. */
        EventsResponse = 13,
        /** @brief Explicit resource preview request. */
        PreviewRequest = 14,
        /** @brief Poll of an asynchronous preview ticket. */
        PreviewPollRequest = 15,
        /** @brief Preview request or poll response. */
        PreviewResponse = 16,
        /** @brief Connection liveness request. */
        Ping = 17,
        /** @brief Connection liveness response. */
        Pong = 18
    };

    /** @brief Negotiated Inspector capability bit. */
    enum class Capability : std::uint64_t
    {
        /** @brief Point-in-time snapshots are available. */
        Snapshots = 1ULL << 0U,
        /** @brief Cursor-based FULL event reads are available. */
        Events = 1ULL << 1U,
        /** @brief Resource metadata can be requested. */
        ResourceMetadata = 1ULL << 2U,
        /** @brief CPU zone events can be displayed. */
        CpuZones = 1ULL << 3U,
        /** @brief Accuracy classifications are carried end to end. */
        Accuracy = 1ULL << 4U,
        /** @brief Event loss and discontinuity counters are carried end to end. */
        Discontinuities = 1ULL << 5U,
        /** @brief An explicit, bounded preview provider is available. */
        ResourcePreviews = 1ULL << 6U
    };

    /** @brief Selective parts of a diagnostics snapshot response. */
    enum class SnapshotPart : std::uint32_t
    {
        /** @brief Current metric samples. */
        Metrics = 1U << 0U,
        /** @brief Bounded recent frame history. */
        Frames = 1U << 1U,
        /** @brief Current resource metadata. */
        Resources = 1U << 2U
    };

    /** @brief Structured protocol error code. */
    enum class ErrorCode : std::uint16_t
    {
        /** @brief The request was malformed or invalid for the connection state. */
        InvalidRequest,
        /** @brief No mutually supported protocol version exists. */
        UnsupportedVersion,
        /** @brief Authentication failed. */
        Unauthorized,
        /** @brief The requested optional feature is unavailable. */
        Unavailable,
        /** @brief A configured protocol bound was exceeded. */
        LimitExceeded,
        /** @brief Request rate or pending-work bounds were reached. */
        Busy,
        /** @brief The provider or optional source failed safely. */
        InternalError
    };

    /** @brief Status of a manually requested resource preview. */
    enum class PreviewStatus : std::uint8_t
    {
        /** @brief The source does not support a preview for this resource. */
        Unavailable,
        /** @brief Asynchronous readback is still in progress. */
        Pending,
        /** @brief Bounded encoded image bytes are ready. */
        Ready,
        /** @brief Readback failed without affecting the application. */
        Failed
    };

    /** @brief Fixed 24-byte header preceding every Inspector payload. */
    struct PacketHeader
    {
        std::uint16_t majorVersion = ProtocolMajorVersion;
        std::uint16_t minorVersion = ProtocolMinorVersion;
        MessageType type = MessageType::Error;
        std::uint16_t flags = 0;
        std::uint32_t payloadBytes = 0;
        std::uint64_t requestId = 0;
    };

    /** @brief Owned Inspector wire packet. */
    struct Packet
    {
        PacketHeader header;
        std::vector<std::uint8_t> payload;
    };

    /** @brief One bounded session metadata key/value pair. */
    struct MetadataEntry
    {
        std::string key;
        std::string value;
    };

    /** @brief Authentication and capability negotiation request. */
    struct HelloRequest
    {
        std::uint16_t minimumMajorVersion = ProtocolMajorVersion;
        std::uint16_t maximumMajorVersion = ProtocolMajorVersion;
        std::uint64_t requestedCapabilities = ~0ULL;
        std::string clientName;
        std::string authenticationToken;
    };

    /** @brief Negotiated session and application identity. */
    struct HelloResponse
    {
        std::uint16_t selectedMajorVersion = ProtocolMajorVersion;
        std::uint16_t selectedMinorVersion = ProtocolMinorVersion;
        std::uint16_t providerInterfaceVersion = 0;
        std::uint64_t capabilities = 0;
        std::uint32_t maximumPayloadBytes = 0;
        std::uint32_t maximumEventsPerResponse = 0;
        std::uint32_t maximumPreviewBytes = 0;
        bool localOnly = true;
        std::string applicationName;
        std::string cnaVersion;
        std::string targetPlatform;
        std::string platformBackend;
        std::string graphicsRenderer;
        std::string buildConfiguration;
        std::vector<MetadataEntry> metadata;
    };

    /** @brief Selective point-in-time snapshot request. */
    struct SnapshotRequest
    {
        std::uint32_t parts = static_cast<std::uint32_t>(SnapshotPart::Metrics)
            | static_cast<std::uint32_t>(SnapshotPart::Frames);
    };

    /** @brief Selective snapshot plus total collection counts. */
    struct SnapshotResponse
    {
        std::uint32_t includedParts = 0;
        std::uint32_t metricCount = 0;
        std::uint32_t frameCount = 0;
        std::uint32_t resourceCount = 0;
        Diagnostics::Snapshot snapshot;
    };

    /** @brief Cursor and bound for an event history request. */
    struct EventsRequest
    {
        std::uint64_t afterSequence = 0;
        std::uint32_t maximumEvents = 512;
    };

    /** @brief One event with its name resolved by the application-side provider. */
    struct ResolvedEvent
    {
        Diagnostics::EventRecord event;
        std::string name;
    };

    /** @brief Event history response retaining all provider loss metadata. */
    struct EventsResponse
    {
        std::vector<ResolvedEvent> events;
        std::uint64_t oldestAvailableSequence = 0;
        std::uint64_t newestAvailableSequence = 0;
        std::uint64_t eventsDroppedBeforeStart = 0;
        std::uint64_t producerEventsDropped = 0;
    };

    /** @brief Explicit bounded resource-preview request. */
    struct PreviewRequest
    {
        Diagnostics::ResourceId resourceId = 0;
        std::uint32_t maximumWidth = 1024;
        std::uint32_t maximumHeight = 1024;
        std::uint32_t maximumBytes = static_cast<std::uint32_t>(MaximumPreviewBytes);
    };

    /** @brief Poll request for a previously returned preview ticket. */
    struct PreviewPollRequest
    {
        std::uint64_t ticket = 0;
    };

    /** @brief Bounded preview state and optional locally displayable image. */
    struct PreviewResponse
    {
        PreviewStatus status = PreviewStatus::Unavailable;
        std::uint64_t ticket = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::string mimeType;
        std::vector<std::uint8_t> encodedImage;
        std::string message;
    };

    /** @brief Structured error payload. */
    struct ErrorResponse
    {
        ErrorCode code = ErrorCode::InvalidRequest;
        std::string message;
    };

    /** @brief Stateless strict encoder and decoder for protocol version 1. */
    class ProtocolCodec
    {
    public:
        /**
         * @brief Encodes a fixed packet header.
         * @param header Header to encode.
         * @return Exactly 24 little-endian bytes.
         */
        [[nodiscard]] static std::vector<std::uint8_t> EncodeHeader(const PacketHeader& header);

        /**
         * @brief Decodes and validates a fixed packet header.
         * @param bytes Candidate 24-byte header.
         * @param header Receives the decoded header.
         * @param error Receives a validation error.
         * @return True when the header is valid and within protocol limits.
         */
        [[nodiscard]] static bool DecodeHeader(std::span<const std::uint8_t> bytes,
                                               PacketHeader& header,
                                               std::string& error);

        /**
         * @brief Encodes a hello request.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const HelloRequest& value, std::uint64_t requestId);
        /**
         * @brief Encodes a hello response.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const HelloResponse& value, std::uint64_t requestId);
        /**
         * @brief Encodes a snapshot request.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const SnapshotRequest& value, std::uint64_t requestId);
        /**
         * @brief Encodes a snapshot response.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const SnapshotResponse& value, std::uint64_t requestId);
        /**
         * @brief Encodes an events request.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const EventsRequest& value, std::uint64_t requestId);
        /**
         * @brief Encodes an events response.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const EventsResponse& value, std::uint64_t requestId);
        /**
         * @brief Encodes a preview request.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const PreviewRequest& value, std::uint64_t requestId);
        /**
         * @brief Encodes a preview poll request.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const PreviewPollRequest& value, std::uint64_t requestId);
        /**
         * @brief Encodes a preview response.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const PreviewResponse& value, std::uint64_t requestId);
        /**
         * @brief Encodes a structured error.
         * @param value Value to encode.
         * @param requestId Correlation identifier.
         * @return Encoded packet.
         */
        [[nodiscard]] static Packet Encode(const ErrorResponse& value, std::uint64_t requestId);

        /**
         * @brief Decodes a hello request.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, HelloRequest& value, std::string& error);
        /**
         * @brief Decodes a hello response.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, HelloResponse& value, std::string& error);
        /**
         * @brief Decodes a snapshot request.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, SnapshotRequest& value, std::string& error);
        /**
         * @brief Decodes a snapshot response.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, SnapshotResponse& value, std::string& error);
        /**
         * @brief Decodes an events request.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, EventsRequest& value, std::string& error);
        /**
         * @brief Decodes an events response.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, EventsResponse& value, std::string& error);
        /**
         * @brief Decodes a preview request.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, PreviewRequest& value, std::string& error);
        /**
         * @brief Decodes a preview poll request.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, PreviewPollRequest& value, std::string& error);
        /**
         * @brief Decodes a preview response.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, PreviewResponse& value, std::string& error);
        /**
         * @brief Decodes a structured error.
         * @param packet Packet to decode.
         * @param value Receives the value.
         * @param error Receives an error.
         * @return True on success.
         */
        [[nodiscard]] static bool Decode(const Packet& packet, ErrorResponse& value, std::string& error);
    };

    /**
     * @brief Tests whether a bit is present in a capability mask.
     * @param mask Capability mask.
     * @param capability Capability to test.
     * @return True when the capability is present.
     */
    [[nodiscard]] constexpr bool HasCapability(std::uint64_t mask, Capability capability) noexcept
    {
        return (mask & static_cast<std::uint64_t>(capability)) != 0;
    }

    /**
     * @brief Tests whether a bit is present in a snapshot-parts mask.
     * @param mask Snapshot-parts mask.
     * @param part Part to test.
     * @return True when the part is requested.
     */
    [[nodiscard]] constexpr bool HasSnapshotPart(std::uint32_t mask, SnapshotPart part) noexcept
    {
        return (mask & static_cast<std::uint32_t>(part)) != 0;
    }
}
