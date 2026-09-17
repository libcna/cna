// SPDX-License-Identifier: MS-PL

#include "CNA/Inspector/Protocol.hpp"

#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace CNA::Inspector
{
    namespace
    {
        constexpr std::uint32_t PacketMagic = 0x49414E43U; // "CNAI" in little-endian bytes.
        constexpr std::size_t HeaderBytes = 24;
        constexpr std::size_t MaximumStringBytes = 4096;
        constexpr std::size_t MaximumAuthenticationTokenBytes = 256;
        constexpr std::size_t MaximumMetadataEntries = 64;
        constexpr std::size_t MaximumMetricSamples = 512;
        constexpr std::size_t MaximumFrameSamples = Diagnostics::FrameHistoryCapacity;
        constexpr std::size_t MaximumFrameMetrics = 64;
        constexpr std::size_t MaximumResourceRecords = 65536;

        bool IsKnownMessageType(std::uint16_t type)
        {
            switch (static_cast<MessageType>(type))
            {
                case MessageType::ClientHello:
                case MessageType::ServerHello:
                case MessageType::Error:
                case MessageType::SnapshotRequest:
                case MessageType::SnapshotResponse:
                case MessageType::EventsRequest:
                case MessageType::EventsResponse:
                case MessageType::PreviewRequest:
                case MessageType::PreviewPollRequest:
                case MessageType::PreviewResponse:
                case MessageType::Ping:
                case MessageType::Pong:
                    return true;
            }
            return false;
        }

        bool IsValidUtf8(std::span<const std::uint8_t> bytes)
        {
            for (std::size_t i = 0; i < bytes.size();)
            {
                const auto first = bytes[i];
                if (first <= 0x7F)
                {
                    ++i;
                    continue;
                }

                std::size_t trailing = 0;
                std::uint32_t codePoint = 0;
                if (first >= 0xC2 && first <= 0xDF)
                {
                    trailing = 1;
                    codePoint = first & 0x1FU;
                }
                else if (first >= 0xE0 && first <= 0xEF)
                {
                    trailing = 2;
                    codePoint = first & 0x0FU;
                }
                else if (first >= 0xF0 && first <= 0xF4)
                {
                    trailing = 3;
                    codePoint = first & 0x07U;
                }
                else
                {
                    return false;
                }
                if (i + trailing >= bytes.size())
                {
                    return false;
                }
                for (std::size_t j = 1; j <= trailing; ++j)
                {
                    const auto next = bytes[i + j];
                    if ((next & 0xC0U) != 0x80U)
                    {
                        return false;
                    }
                    codePoint = (codePoint << 6U) | (next & 0x3FU);
                }
                if ((trailing == 2 && codePoint < 0x800U)
                    || (trailing == 3 && codePoint < 0x10000U)
                    || codePoint > 0x10FFFFU
                    || (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
                {
                    return false;
                }
                i += trailing + 1;
            }
            return true;
        }

        class Writer
        {
        public:
            template<typename T>
            void Integer(T value)
            {
                using Unsigned = std::make_unsigned_t<T>;
                auto bits = static_cast<Unsigned>(value);
                for (std::size_t i = 0; i < sizeof(T); ++i)
                {
                    bytes_.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
                    bits >>= 8U;
                }
                CheckLimit();
            }

            template<typename T>
            void Enum(T value)
            {
                Integer(static_cast<std::underlying_type_t<T>>(value));
            }

            void Boolean(bool value)
            {
                Integer<std::uint8_t>(value ? 1U : 0U);
            }

            void Double(double value)
            {
                Integer(std::bit_cast<std::uint64_t>(value));
            }

            void String(std::string_view value, std::size_t maximum = MaximumStringBytes)
            {
                if (value.size() > maximum || value.size() > std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::length_error("Inspector string exceeds its protocol bound");
                }
                const auto bytes = std::span(
                    reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
                if (!IsValidUtf8(bytes))
                {
                    throw std::invalid_argument("Inspector string is not valid UTF-8");
                }
                Integer(static_cast<std::uint32_t>(value.size()));
                bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
                CheckLimit();
            }

            void Bytes(std::span<const std::uint8_t> value, std::size_t maximum)
            {
                if (value.size() > maximum || value.size() > std::numeric_limits<std::uint32_t>::max())
                {
                    throw std::length_error("Inspector byte field exceeds its protocol bound");
                }
                Integer(static_cast<std::uint32_t>(value.size()));
                bytes_.insert(bytes_.end(), value.begin(), value.end());
                CheckLimit();
            }

            [[nodiscard]] std::vector<std::uint8_t> Take()
            {
                return std::move(bytes_);
            }

        private:
            void CheckLimit() const
            {
                if (bytes_.size() > MaximumPayloadBytes)
                {
                    throw std::length_error("Inspector payload exceeds the maximum packet size");
                }
            }

            std::vector<std::uint8_t> bytes_;
        };

        class Reader
        {
        public:
            explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

            template<typename T>
            bool Integer(T& value)
            {
                if (Remaining() < sizeof(T))
                {
                    return Fail("truncated integer");
                }
                using Unsigned = std::make_unsigned_t<T>;
                Unsigned bits = 0;
                for (std::size_t i = 0; i < sizeof(T); ++i)
                {
                    bits |= static_cast<Unsigned>(bytes_[offset_ + i]) << (i * 8U);
                }
                offset_ += sizeof(T);
                value = static_cast<T>(bits);
                return true;
            }

            template<typename T>
            bool Enum(T& value, std::underlying_type_t<T> maximum)
            {
                std::underlying_type_t<T> raw{};
                if (!Integer(raw) || raw > maximum)
                {
                    return Fail("invalid enumeration value");
                }
                value = static_cast<T>(raw);
                return true;
            }

            bool Boolean(bool& value)
            {
                std::uint8_t raw = 0;
                if (!Integer(raw) || raw > 1)
                {
                    return Fail("invalid Boolean value");
                }
                value = raw != 0;
                return true;
            }

            bool Double(double& value)
            {
                std::uint64_t bits = 0;
                if (!Integer(bits))
                {
                    return false;
                }
                value = std::bit_cast<double>(bits);
                return true;
            }

            bool String(std::string& value, std::size_t maximum = MaximumStringBytes)
            {
                std::uint32_t size = 0;
                if (!Integer(size) || size > maximum || Remaining() < size)
                {
                    return Fail("invalid or truncated string");
                }
                const auto bytes = bytes_.subspan(offset_, size);
                if (!IsValidUtf8(bytes))
                {
                    return Fail("string is not valid UTF-8");
                }
                value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                offset_ += size;
                return true;
            }

            bool Bytes(std::vector<std::uint8_t>& value, std::size_t maximum)
            {
                std::uint32_t size = 0;
                if (!Integer(size) || size > maximum || Remaining() < size)
                {
                    return Fail("invalid or truncated byte field");
                }
                value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                             bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
                offset_ += size;
                return true;
            }

            bool Count(std::uint32_t& value, std::size_t maximum)
            {
                if (!Integer(value) || value > maximum)
                {
                    return Fail("collection count exceeds its protocol bound");
                }
                return true;
            }

            [[nodiscard]] bool Finished()
            {
                return Remaining() == 0 || Fail("trailing payload bytes");
            }

            [[nodiscard]] const std::string& Error() const
            {
                return error_;
            }

        private:
            [[nodiscard]] std::size_t Remaining() const
            {
                return bytes_.size() - offset_;
            }

            bool Fail(std::string_view message)
            {
                if (error_.empty())
                {
                    error_ = message;
                }
                return false;
            }

            std::span<const std::uint8_t> bytes_;
            std::size_t offset_ = 0;
            std::string error_;
        };

        Packet MakePacket(MessageType type, std::uint64_t requestId, Writer&& writer)
        {
            Packet packet;
            packet.header.type = type;
            packet.header.requestId = requestId;
            packet.payload = writer.Take();
            packet.header.payloadBytes = static_cast<std::uint32_t>(packet.payload.size());
            return packet;
        }

        bool CheckPacket(const Packet& packet, MessageType type, Reader& reader, std::string& error)
        {
            if (packet.header.type != type)
            {
                error = "unexpected Inspector message type";
                return false;
            }
            if (packet.payload.size() != packet.header.payloadBytes
                || packet.payload.size() > MaximumPayloadBytes)
            {
                error = "Inspector packet payload length mismatch";
                return false;
            }
            (void) reader;
            return true;
        }

        bool Finish(Reader& reader, std::string& error)
        {
            const auto result = reader.Finished();
            if (!result)
            {
                error = reader.Error();
            }
            return result;
        }

        void WriteMetric(Writer& writer, const Diagnostics::MetricSample& metric)
        {
            writer.Integer(metric.id);
            writer.String(metric.name);
            writer.Integer(metric.value);
            writer.Enum(metric.kind);
            writer.Enum(metric.unit);
            writer.Enum(metric.accuracy);
        }

        bool ReadMetric(Reader& reader, Diagnostics::MetricSample& metric)
        {
            return reader.Integer(metric.id)
                && reader.String(metric.name)
                && reader.Integer(metric.value)
                && reader.Enum(metric.kind, static_cast<std::uint8_t>(Diagnostics::MetricKind::FrameCounter))
                && reader.Enum(metric.unit, static_cast<std::uint8_t>(Diagnostics::MetricUnit::BasisPoints))
                && reader.Enum(metric.accuracy, static_cast<std::uint8_t>(Diagnostics::Accuracy::Unavailable));
        }

        void WriteEvent(Writer& writer, const Diagnostics::EventRecord& event)
        {
            writer.Integer(event.sequence);
            writer.Integer(event.timestampNs);
            writer.Integer(event.durationNs);
            writer.Integer(event.frameNumber);
            writer.Integer(event.threadId);
            writer.Integer(event.correlationId);
            writer.Integer(event.parentCorrelationId);
            writer.Integer(event.value);
            writer.Integer(event.name);
            writer.Enum(event.kind);
            writer.Enum(event.category);
        }

        bool ReadEvent(Reader& reader, Diagnostics::EventRecord& event)
        {
            return reader.Integer(event.sequence)
                && reader.Integer(event.timestampNs)
                && reader.Integer(event.durationNs)
                && reader.Integer(event.frameNumber)
                && reader.Integer(event.threadId)
                && reader.Integer(event.correlationId)
                && reader.Integer(event.parentCorrelationId)
                && reader.Integer(event.value)
                && reader.Integer(event.name)
                && reader.Enum(event.kind, static_cast<std::uint8_t>(Diagnostics::EventKind::Malformed))
                && reader.Enum(event.category, static_cast<std::uint8_t>(Diagnostics::Category::Gpu));
        }

        void WriteResource(Writer& writer, const Diagnostics::ResourceRecord& resource)
        {
            writer.Integer(resource.id);
            writer.Enum(resource.kind);
            writer.String(resource.label);
            writer.String(resource.format);
            writer.Integer(resource.width);
            writer.Integer(resource.height);
            writer.Integer(resource.depth);
            writer.Integer(resource.mipCount);
            writer.Integer(resource.estimatedBytes);
            writer.Enum(resource.byteAccuracy);
        }

        bool ReadResource(Reader& reader, Diagnostics::ResourceRecord& resource)
        {
            return reader.Integer(resource.id)
                && reader.Enum(resource.kind,
                    static_cast<std::uint8_t>(Diagnostics::ResourceKind::Custom))
                && reader.String(resource.label)
                && reader.String(resource.format)
                && reader.Integer(resource.width)
                && reader.Integer(resource.height)
                && reader.Integer(resource.depth)
                && reader.Integer(resource.mipCount)
                && reader.Integer(resource.estimatedBytes)
                && reader.Enum(resource.byteAccuracy,
                    static_cast<std::uint8_t>(Diagnostics::Accuracy::Unavailable));
        }
    }

    std::vector<std::uint8_t> ProtocolCodec::EncodeHeader(const PacketHeader& header)
    {
        Writer writer;
        writer.Integer(PacketMagic);
        writer.Integer(header.majorVersion);
        writer.Integer(header.minorVersion);
        writer.Enum(header.type);
        writer.Integer(header.flags);
        writer.Integer(header.payloadBytes);
        writer.Integer(header.requestId);
        return writer.Take();
    }

    bool ProtocolCodec::DecodeHeader(std::span<const std::uint8_t> bytes,
                                     PacketHeader& header,
                                     std::string& error)
    {
        if (bytes.size() != HeaderBytes)
        {
            error = "Inspector packet header must contain exactly 24 bytes";
            return false;
        }
        Reader reader(bytes);
        std::uint32_t magic = 0;
        std::uint16_t rawType = 0;
        if (!reader.Integer(magic) || magic != PacketMagic
            || !reader.Integer(header.majorVersion)
            || !reader.Integer(header.minorVersion)
            || !reader.Integer(rawType)
            || !IsKnownMessageType(rawType)
            || !reader.Integer(header.flags)
            || header.flags != 0
            || !reader.Integer(header.payloadBytes)
            || header.payloadBytes > MaximumPayloadBytes
            || !reader.Integer(header.requestId)
            || !reader.Finished())
        {
            error = reader.Error().empty() ? "invalid Inspector packet header" : reader.Error();
            return false;
        }
        header.type = static_cast<MessageType>(rawType);
        return true;
    }

    Packet ProtocolCodec::Encode(const HelloRequest& value, std::uint64_t requestId)
    {
        Writer writer;
        writer.Integer(value.minimumMajorVersion);
        writer.Integer(value.maximumMajorVersion);
        writer.Integer(value.requestedCapabilities);
        writer.String(value.clientName, 128);
        writer.String(value.authenticationToken, MaximumAuthenticationTokenBytes);
        return MakePacket(MessageType::ClientHello, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const HelloResponse& value, std::uint64_t requestId)
    {
        if (value.metadata.size() > MaximumMetadataEntries)
        {
            throw std::length_error("too many Inspector session metadata entries");
        }
        Writer writer;
        writer.Integer(value.selectedMajorVersion);
        writer.Integer(value.selectedMinorVersion);
        writer.Integer(value.providerInterfaceVersion);
        writer.Integer(value.capabilities);
        writer.Integer(value.maximumPayloadBytes);
        writer.Integer(value.maximumEventsPerResponse);
        writer.Integer(value.maximumPreviewBytes);
        writer.Boolean(value.localOnly);
        writer.String(value.applicationName);
        writer.String(value.cnaVersion);
        writer.String(value.targetPlatform);
        writer.String(value.platformBackend);
        writer.String(value.graphicsRenderer);
        writer.String(value.buildConfiguration);
        writer.Integer(static_cast<std::uint32_t>(value.metadata.size()));
        for (const auto& entry : value.metadata)
        {
            writer.String(entry.key, 128);
            writer.String(entry.value, 1024);
        }
        return MakePacket(MessageType::ServerHello, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const SnapshotRequest& value, std::uint64_t requestId)
    {
        Writer writer;
        writer.Integer(value.parts);
        return MakePacket(MessageType::SnapshotRequest, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const SnapshotResponse& value, std::uint64_t requestId)
    {
        const auto& snapshot = value.snapshot;
        if (snapshot.metrics.size() > MaximumMetricSamples
            || snapshot.recentFrames.size() > MaximumFrameSamples
            || snapshot.resources.size() > MaximumResourceRecords)
        {
            throw std::length_error("Inspector snapshot collection exceeds its protocol bound");
        }

        Writer writer;
        writer.Integer(value.includedParts);
        writer.Integer(value.metricCount);
        writer.Integer(value.frameCount);
        writer.Integer(value.resourceCount);
        writer.Enum(snapshot.buildMode);
        writer.Enum(snapshot.runtimeMode);
        writer.Integer<std::uint16_t>(0);
        writer.Integer(snapshot.currentFrameNumber);
        writer.Integer(snapshot.malformedZoneCount);
        writer.Integer(snapshot.malformedFrameCount);
        writer.Integer(snapshot.producerEventsDropped);
        writer.Integer(snapshot.eventHistoryOverwrites);
        writer.Integer(snapshot.sourceCollectionFailures);
        writer.Integer(snapshot.profilerOwnedBytes);
        writer.Integer(snapshot.registeredResourceBytes);

        writer.Integer(static_cast<std::uint32_t>(snapshot.metrics.size()));
        for (const auto& metric : snapshot.metrics)
        {
            WriteMetric(writer, metric);
        }
        writer.Integer(static_cast<std::uint32_t>(snapshot.recentFrames.size()));
        for (const auto& frame : snapshot.recentFrames)
        {
            if (frame.metrics.size() > MaximumFrameMetrics)
            {
                throw std::length_error("Inspector frame metric count exceeds its protocol bound");
            }
            writer.Integer(frame.frameNumber);
            writer.Integer(frame.startTimestampNs);
            writer.Integer(frame.durationNs);
            writer.Double(frame.framesPerSecond);
            writer.Integer(static_cast<std::uint32_t>(frame.metrics.size()));
            for (const auto& metric : frame.metrics)
            {
                WriteMetric(writer, metric);
            }
        }
        writer.Integer(static_cast<std::uint32_t>(snapshot.resources.size()));
        for (const auto& resource : snapshot.resources)
        {
            WriteResource(writer, resource);
        }
        return MakePacket(MessageType::SnapshotResponse, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const EventsRequest& value, std::uint64_t requestId)
    {
        Writer writer;
        writer.Integer(value.afterSequence);
        writer.Integer(value.maximumEvents);
        return MakePacket(MessageType::EventsRequest, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const EventsResponse& value, std::uint64_t requestId)
    {
        if (value.events.size() > MaximumEventsPerResponse)
        {
            throw std::length_error("Inspector event response exceeds its protocol bound");
        }
        Writer writer;
        writer.Integer(value.oldestAvailableSequence);
        writer.Integer(value.newestAvailableSequence);
        writer.Integer(value.eventsDroppedBeforeStart);
        writer.Integer(value.producerEventsDropped);
        writer.Integer(static_cast<std::uint32_t>(value.events.size()));
        for (const auto& resolved : value.events)
        {
            WriteEvent(writer, resolved.event);
            writer.String(resolved.name);
        }
        return MakePacket(MessageType::EventsResponse, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const PreviewRequest& value, std::uint64_t requestId)
    {
        Writer writer;
        writer.Integer(value.resourceId);
        writer.Integer(value.maximumWidth);
        writer.Integer(value.maximumHeight);
        writer.Integer(value.maximumBytes);
        return MakePacket(MessageType::PreviewRequest, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const PreviewPollRequest& value, std::uint64_t requestId)
    {
        Writer writer;
        writer.Integer(value.ticket);
        return MakePacket(MessageType::PreviewPollRequest, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const PreviewResponse& value, std::uint64_t requestId)
    {
        Writer writer;
        writer.Enum(value.status);
        writer.Integer(value.ticket);
        writer.Integer(value.width);
        writer.Integer(value.height);
        writer.String(value.mimeType, 64);
        writer.Bytes(value.encodedImage, MaximumPreviewBytes);
        writer.String(value.message, 1024);
        return MakePacket(MessageType::PreviewResponse, requestId, std::move(writer));
    }

    Packet ProtocolCodec::Encode(const ErrorResponse& value, std::uint64_t requestId)
    {
        Writer writer;
        writer.Enum(value.code);
        writer.String(value.message, 1024);
        return MakePacket(MessageType::Error, requestId, std::move(writer));
    }

    bool ProtocolCodec::Decode(const Packet& packet, HelloRequest& value, std::string& error)
    {
        Reader reader(packet.payload);
        if (!CheckPacket(packet, MessageType::ClientHello, reader, error)
            || !reader.Integer(value.minimumMajorVersion)
            || !reader.Integer(value.maximumMajorVersion)
            || value.minimumMajorVersion > value.maximumMajorVersion
            || !reader.Integer(value.requestedCapabilities)
            || !reader.String(value.clientName, 128)
            || !reader.String(value.authenticationToken, MaximumAuthenticationTokenBytes))
        {
            error = error.empty() ? reader.Error() : error;
            return false;
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, HelloResponse& value, std::string& error)
    {
        Reader reader(packet.payload);
        std::uint32_t count = 0;
        if (!CheckPacket(packet, MessageType::ServerHello, reader, error)
            || !reader.Integer(value.selectedMajorVersion)
            || !reader.Integer(value.selectedMinorVersion)
            || !reader.Integer(value.providerInterfaceVersion)
            || !reader.Integer(value.capabilities)
            || !reader.Integer(value.maximumPayloadBytes)
            || value.maximumPayloadBytes == 0
            || value.maximumPayloadBytes > MaximumPayloadBytes
            || !reader.Integer(value.maximumEventsPerResponse)
            || value.maximumEventsPerResponse == 0
            || value.maximumEventsPerResponse > MaximumEventsPerResponse
            || !reader.Integer(value.maximumPreviewBytes)
            || value.maximumPreviewBytes == 0
            || value.maximumPreviewBytes > MaximumPreviewBytes
            || !reader.Boolean(value.localOnly)
            || !reader.String(value.applicationName)
            || !reader.String(value.cnaVersion)
            || !reader.String(value.targetPlatform)
            || !reader.String(value.platformBackend)
            || !reader.String(value.graphicsRenderer)
            || !reader.String(value.buildConfiguration)
            || !reader.Count(count, MaximumMetadataEntries))
        {
            error = error.empty() ? reader.Error() : error;
            return false;
        }
        value.metadata.resize(count);
        for (auto& entry : value.metadata)
        {
            if (!reader.String(entry.key, 128) || !reader.String(entry.value, 1024))
            {
                error = reader.Error();
                return false;
            }
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, SnapshotRequest& value, std::string& error)
    {
        Reader reader(packet.payload);
        constexpr auto allowed = static_cast<std::uint32_t>(SnapshotPart::Metrics)
            | static_cast<std::uint32_t>(SnapshotPart::Frames)
            | static_cast<std::uint32_t>(SnapshotPart::Resources);
        if (!CheckPacket(packet, MessageType::SnapshotRequest, reader, error)
            || !reader.Integer(value.parts) || (value.parts & ~allowed) != 0)
        {
            error = error.empty() ? reader.Error() : error;
            if (error.empty()) error = "snapshot request contains unknown part bits";
            return false;
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, SnapshotResponse& value, std::string& error)
    {
        Reader reader(packet.payload);
        auto& snapshot = value.snapshot;
        constexpr auto allowed = static_cast<std::uint32_t>(SnapshotPart::Metrics)
            | static_cast<std::uint32_t>(SnapshotPart::Frames)
            | static_cast<std::uint32_t>(SnapshotPart::Resources);
        std::uint16_t reserved = 0;
        std::uint32_t count = 0;
        if (!CheckPacket(packet, MessageType::SnapshotResponse, reader, error)
            || !reader.Integer(value.includedParts)
            || (value.includedParts & ~allowed) != 0
            || !reader.Integer(value.metricCount)
            || !reader.Integer(value.frameCount)
            || !reader.Integer(value.resourceCount)
            || !reader.Enum(snapshot.buildMode, static_cast<std::uint8_t>(Diagnostics::Mode::Full))
            || !reader.Enum(snapshot.runtimeMode, static_cast<std::uint8_t>(Diagnostics::Mode::Full))
            || !reader.Integer(reserved) || reserved != 0
            || !reader.Integer(snapshot.currentFrameNumber)
            || !reader.Integer(snapshot.malformedZoneCount)
            || !reader.Integer(snapshot.malformedFrameCount)
            || !reader.Integer(snapshot.producerEventsDropped)
            || !reader.Integer(snapshot.eventHistoryOverwrites)
            || !reader.Integer(snapshot.sourceCollectionFailures)
            || !reader.Integer(snapshot.profilerOwnedBytes)
            || !reader.Integer(snapshot.registeredResourceBytes)
            || !reader.Count(count, MaximumMetricSamples))
        {
            error = error.empty() ? reader.Error() : error;
            if (error.empty()) error = "snapshot response contains unknown part bits";
            return false;
        }
        snapshot.metrics.resize(count);
        for (auto& metric : snapshot.metrics)
        {
            if (!ReadMetric(reader, metric))
            {
                error = reader.Error();
                return false;
            }
        }
        if (!reader.Count(count, MaximumFrameSamples))
        {
            error = reader.Error();
            return false;
        }
        snapshot.recentFrames.resize(count);
        for (auto& frame : snapshot.recentFrames)
        {
            std::uint32_t metricCount = 0;
            if (!reader.Integer(frame.frameNumber)
                || !reader.Integer(frame.startTimestampNs)
                || !reader.Integer(frame.durationNs)
                || !reader.Double(frame.framesPerSecond)
                || !reader.Count(metricCount, MaximumFrameMetrics))
            {
                error = reader.Error();
                return false;
            }
            frame.metrics.resize(metricCount);
            for (auto& metric : frame.metrics)
            {
                if (!ReadMetric(reader, metric))
                {
                    error = reader.Error();
                    return false;
                }
            }
        }
        if (!reader.Count(count, MaximumResourceRecords))
        {
            error = reader.Error();
            return false;
        }
        snapshot.resources.resize(count);
        for (auto& resource : snapshot.resources)
        {
            if (!ReadResource(reader, resource))
            {
                error = reader.Error();
                return false;
            }
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, EventsRequest& value, std::string& error)
    {
        Reader reader(packet.payload);
        if (!CheckPacket(packet, MessageType::EventsRequest, reader, error)
            || !reader.Integer(value.afterSequence)
            || !reader.Integer(value.maximumEvents)
            || value.maximumEvents == 0
            || value.maximumEvents > MaximumEventsPerResponse)
        {
            error = error.empty() ? reader.Error() : error;
            if (error.empty()) error = "event request bound is invalid";
            return false;
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, EventsResponse& value, std::string& error)
    {
        Reader reader(packet.payload);
        std::uint32_t count = 0;
        if (!CheckPacket(packet, MessageType::EventsResponse, reader, error)
            || !reader.Integer(value.oldestAvailableSequence)
            || !reader.Integer(value.newestAvailableSequence)
            || !reader.Integer(value.eventsDroppedBeforeStart)
            || !reader.Integer(value.producerEventsDropped)
            || !reader.Count(count, MaximumEventsPerResponse))
        {
            error = error.empty() ? reader.Error() : error;
            return false;
        }
        value.events.resize(count);
        for (auto& resolved : value.events)
        {
            if (!ReadEvent(reader, resolved.event) || !reader.String(resolved.name))
            {
                error = reader.Error();
                return false;
            }
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, PreviewRequest& value, std::string& error)
    {
        Reader reader(packet.payload);
        if (!CheckPacket(packet, MessageType::PreviewRequest, reader, error)
            || !reader.Integer(value.resourceId)
            || !reader.Integer(value.maximumWidth)
            || !reader.Integer(value.maximumHeight)
            || !reader.Integer(value.maximumBytes)
            || value.resourceId == 0 || value.maximumWidth == 0 || value.maximumWidth > 4096
            || value.maximumHeight == 0 || value.maximumHeight > 4096
            || value.maximumBytes == 0 || value.maximumBytes > MaximumPreviewBytes)
        {
            error = error.empty() ? reader.Error() : error;
            if (error.empty()) error = "preview request exceeds a protocol bound";
            return false;
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, PreviewPollRequest& value, std::string& error)
    {
        Reader reader(packet.payload);
        if (!CheckPacket(packet, MessageType::PreviewPollRequest, reader, error)
            || !reader.Integer(value.ticket) || value.ticket == 0)
        {
            error = error.empty() ? reader.Error() : error;
            if (error.empty()) error = "preview poll ticket is invalid";
            return false;
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, PreviewResponse& value, std::string& error)
    {
        Reader reader(packet.payload);
        if (!CheckPacket(packet, MessageType::PreviewResponse, reader, error)
            || !reader.Enum(value.status, static_cast<std::uint8_t>(PreviewStatus::Failed))
            || !reader.Integer(value.ticket)
            || !reader.Integer(value.width)
            || !reader.Integer(value.height)
            || value.width > 4096 || value.height > 4096
            || !reader.String(value.mimeType, 64)
            || !reader.Bytes(value.encodedImage, MaximumPreviewBytes)
            || !reader.String(value.message, 1024))
        {
            error = error.empty() ? reader.Error() : error;
            return false;
        }
        if (value.status == PreviewStatus::Ready
            && (value.width == 0 || value.height == 0 || value.encodedImage.empty()
                || (value.mimeType != "image/png" && value.mimeType != "image/jpeg"
                    && value.mimeType != "image/webp")))
        {
            error = "ready preview does not contain a supported bounded image";
            return false;
        }
        if (value.status == PreviewStatus::Pending && value.ticket == 0)
        {
            error = "pending preview does not contain a ticket";
            return false;
        }
        if (value.status != PreviewStatus::Ready
            && (value.width != 0 || value.height != 0
                || !value.mimeType.empty() || !value.encodedImage.empty()))
        {
            error = "non-ready preview contains image data";
            return false;
        }
        return Finish(reader, error);
    }

    bool ProtocolCodec::Decode(const Packet& packet, ErrorResponse& value, std::string& error)
    {
        Reader reader(packet.payload);
        if (!CheckPacket(packet, MessageType::Error, reader, error)
            || !reader.Enum(value.code, static_cast<std::uint16_t>(ErrorCode::InternalError))
            || !reader.String(value.message, 1024))
        {
            error = error.empty() ? reader.Error() : error;
            return false;
        }
        return Finish(reader, error);
    }
}
