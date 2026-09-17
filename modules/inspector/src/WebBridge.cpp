// SPDX-License-Identifier: MS-PL

#include "WebBridge.hpp"

#include "InternalSocket.hpp"
#include "WebAssets.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace CNA::Inspector::Detail
{
    namespace
    {
        constexpr std::size_t MaximumHttpRequestBytes = 16U * 1024U;
        constexpr std::size_t MaximumHttpResponseBytes = 12U * 1024U * 1024U;
        constexpr std::uint32_t HttpTimeoutMilliseconds = 5000;

        class JsonWriter
        {
        public:
            void Raw(std::string_view value)
            {
                output_.append(value);
                Check();
            }

            void String(std::string_view value)
            {
                output_.push_back('"');
                for (const auto character : value)
                {
                    switch (character)
                    {
                        case '"': output_ += "\\\""; break;
                        case '\\': output_ += "\\\\"; break;
                        case '\b': output_ += "\\b"; break;
                        case '\f': output_ += "\\f"; break;
                        case '\n': output_ += "\\n"; break;
                        case '\r': output_ += "\\r"; break;
                        case '\t': output_ += "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(character) < 0x20U)
                            {
                                std::ostringstream escape;
                                escape << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                                       << static_cast<unsigned>(static_cast<unsigned char>(character));
                                output_ += escape.str();
                            }
                            else
                            {
                                output_.push_back(character);
                            }
                            break;
                    }
                }
                output_.push_back('"');
                Check();
            }

            template<typename T>
            void Number(T value)
            {
                output_ += std::to_string(value);
                Check();
            }

            void Double(double value)
            {
                if (!std::isfinite(value))
                {
                    Raw("null");
                    return;
                }
                std::ostringstream stream;
                stream << std::setprecision(12) << value;
                Raw(stream.str());
            }

            void Unsigned64(std::uint64_t value)
            {
                String(std::to_string(value));
            }

            void Signed64(std::int64_t value)
            {
                String(std::to_string(value));
            }

            [[nodiscard]] std::string Take()
            {
                return std::move(output_);
            }

        private:
            void Check() const
            {
                if (output_.size() > MaximumHttpResponseBytes)
                {
                    throw std::length_error("Inspector JSON response exceeds its bound");
                }
            }

            std::string output_;
        };

        std::string_view AccuracyName(Diagnostics::Accuracy value)
        {
            switch (value)
            {
                case Diagnostics::Accuracy::Exact: return "exact";
                case Diagnostics::Accuracy::Estimated: return "estimated";
                case Diagnostics::Accuracy::Unavailable: return "unavailable";
            }
            return "unavailable";
        }

        std::string_view ModeName(Diagnostics::Mode value)
        {
            switch (value)
            {
                case Diagnostics::Mode::Off: return "off";
                case Diagnostics::Mode::Stats: return "stats";
                case Diagnostics::Mode::Full: return "full";
            }
            return "off";
        }

        std::string_view MetricKindName(Diagnostics::MetricKind value)
        {
            switch (value)
            {
                case Diagnostics::MetricKind::Counter: return "counter";
                case Diagnostics::MetricKind::Gauge: return "gauge";
                case Diagnostics::MetricKind::FrameCounter: return "frame-counter";
            }
            return "gauge";
        }

        std::string_view MetricUnitName(Diagnostics::MetricUnit value)
        {
            switch (value)
            {
                case Diagnostics::MetricUnit::Count: return "count";
                case Diagnostics::MetricUnit::Bytes: return "bytes";
                case Diagnostics::MetricUnit::Nanoseconds: return "nanoseconds";
                case Diagnostics::MetricUnit::PerSecond: return "per-second";
                case Diagnostics::MetricUnit::BasisPoints: return "basis-points";
            }
            return "count";
        }

        std::string_view ResourceKindName(Diagnostics::ResourceKind value)
        {
            switch (value)
            {
                case Diagnostics::ResourceKind::Unknown: return "unknown";
                case Diagnostics::ResourceKind::Texture2D: return "texture-2d";
                case Diagnostics::ResourceKind::Texture3D: return "texture-3d";
                case Diagnostics::ResourceKind::TextureCube: return "texture-cube";
                case Diagnostics::ResourceKind::VertexBuffer: return "vertex-buffer";
                case Diagnostics::ResourceKind::IndexBuffer: return "index-buffer";
                case Diagnostics::ResourceKind::RenderTarget2D: return "render-target-2d";
                case Diagnostics::ResourceKind::RenderTargetCube: return "render-target-cube";
                case Diagnostics::ResourceKind::AudioVoice: return "audio-voice";
                case Diagnostics::ResourceKind::Custom: return "custom";
            }
            return "unknown";
        }

        std::string_view EventKindName(Diagnostics::EventKind value)
        {
            switch (value)
            {
                case Diagnostics::EventKind::Zone: return "zone";
                case Diagnostics::EventKind::Marker: return "marker";
                case Diagnostics::EventKind::Frame: return "frame";
                case Diagnostics::EventKind::ResourceCreated: return "resource-created";
                case Diagnostics::EventKind::ResourceDestroyed: return "resource-destroyed";
                case Diagnostics::EventKind::Malformed: return "malformed";
            }
            return "marker";
        }

        std::string_view CategoryName(Diagnostics::Category value)
        {
            switch (value)
            {
                case Diagnostics::Category::Core: return "core";
                case Diagnostics::Category::Update: return "update";
                case Diagnostics::Category::Draw: return "draw";
                case Diagnostics::Category::Graphics: return "graphics";
                case Diagnostics::Category::Audio: return "audio";
                case Diagnostics::Category::Content: return "content";
                case Diagnostics::Category::Application: return "application";
                case Diagnostics::Category::Gpu: return "gpu";
            }
            return "core";
        }

        std::string_view PreviewStatusName(PreviewStatus value)
        {
            switch (value)
            {
                case PreviewStatus::Unavailable: return "unavailable";
                case PreviewStatus::Pending: return "pending";
                case PreviewStatus::Ready: return "ready";
                case PreviewStatus::Failed: return "failed";
            }
            return "failed";
        }

        void WriteMetric(JsonWriter& output, const Diagnostics::MetricSample& metric)
        {
            output.Raw("{\"id\":"); output.Number(metric.id);
            output.Raw(",\"name\":"); output.String(metric.name);
            output.Raw(",\"value\":"); output.Signed64(metric.value);
            output.Raw(",\"kind\":"); output.String(MetricKindName(metric.kind));
            output.Raw(",\"unit\":"); output.String(MetricUnitName(metric.unit));
            output.Raw(",\"accuracy\":"); output.String(AccuracyName(metric.accuracy));
            output.Raw("}");
        }

        std::string SessionJson(const HelloResponse& session)
        {
            JsonWriter output;
            output.Raw("{\"applicationName\":"); output.String(session.applicationName);
            output.Raw(",\"cnaVersion\":"); output.String(session.cnaVersion);
            output.Raw(",\"targetPlatform\":"); output.String(session.targetPlatform);
            output.Raw(",\"platformBackend\":"); output.String(session.platformBackend);
            output.Raw(",\"graphicsRenderer\":"); output.String(session.graphicsRenderer);
            output.Raw(",\"buildConfiguration\":"); output.String(session.buildConfiguration);
            output.Raw(",\"localOnly\":"); output.Raw(session.localOnly ? "true" : "false");
            output.Raw(",\"protocolMajor\":"); output.Number(session.selectedMajorVersion);
            output.Raw(",\"protocolMinor\":"); output.Number(session.selectedMinorVersion);
            output.Raw(",\"providerInterfaceVersion\":"); output.Number(session.providerInterfaceVersion);
            output.Raw(",\"limits\":{\"payloadBytes\":"); output.Number(session.maximumPayloadBytes);
            output.Raw(",\"events\":"); output.Number(session.maximumEventsPerResponse);
            output.Raw(",\"previewBytes\":"); output.Number(session.maximumPreviewBytes);
            output.Raw("},\"capabilities\":{");
            constexpr std::array pairs{
                std::pair{"snapshots", Capability::Snapshots},
                std::pair{"events", Capability::Events},
                std::pair{"resourceMetadata", Capability::ResourceMetadata},
                std::pair{"cpuZones", Capability::CpuZones},
                std::pair{"accuracy", Capability::Accuracy},
                std::pair{"discontinuities", Capability::Discontinuities},
                std::pair{"resourcePreviews", Capability::ResourcePreviews}};
            for (std::size_t i = 0; i < pairs.size(); ++i)
            {
                if (i != 0) output.Raw(",");
                output.String(pairs[i].first); output.Raw(":");
                output.Raw(HasCapability(session.capabilities, pairs[i].second) ? "true" : "false");
            }
            output.Raw("},\"metadata\":[");
            for (std::size_t i = 0; i < session.metadata.size(); ++i)
            {
                if (i != 0) output.Raw(",");
                output.Raw("{\"key\":"); output.String(session.metadata[i].key);
                output.Raw(",\"value\":"); output.String(session.metadata[i].value); output.Raw("}");
            }
            output.Raw("]}");
            return output.Take();
        }

        std::string SnapshotJson(const SnapshotResponse& response)
        {
            const auto& snapshot = response.snapshot;
            JsonWriter output;
            output.Raw("{\"buildMode\":"); output.String(ModeName(snapshot.buildMode));
            output.Raw(",\"runtimeMode\":"); output.String(ModeName(snapshot.runtimeMode));
            output.Raw(",\"currentFrameNumber\":"); output.Unsigned64(snapshot.currentFrameNumber);
            output.Raw(",\"malformedZoneCount\":"); output.Unsigned64(snapshot.malformedZoneCount);
            output.Raw(",\"malformedFrameCount\":"); output.Unsigned64(snapshot.malformedFrameCount);
            output.Raw(",\"producerEventsDropped\":"); output.Unsigned64(snapshot.producerEventsDropped);
            output.Raw(",\"eventHistoryOverwrites\":"); output.Unsigned64(snapshot.eventHistoryOverwrites);
            output.Raw(",\"sourceCollectionFailures\":"); output.Unsigned64(snapshot.sourceCollectionFailures);
            output.Raw(",\"profilerOwnedBytes\":"); output.Unsigned64(snapshot.profilerOwnedBytes);
            output.Raw(",\"registeredResourceBytes\":"); output.Unsigned64(snapshot.registeredResourceBytes);
            output.Raw(",\"metricCount\":"); output.Number(response.metricCount);
            output.Raw(",\"frameCount\":"); output.Number(response.frameCount);
            output.Raw(",\"resourceCount\":"); output.Number(response.resourceCount);
            output.Raw(",\"metrics\":[");
            for (std::size_t i = 0; i < snapshot.metrics.size(); ++i)
            {
                if (i != 0) output.Raw(",");
                WriteMetric(output, snapshot.metrics[i]);
            }
            output.Raw("],\"frames\":[");
            for (std::size_t i = 0; i < snapshot.recentFrames.size(); ++i)
            {
                if (i != 0) output.Raw(",");
                const auto& frame = snapshot.recentFrames[i];
                output.Raw("{\"frameNumber\":"); output.Unsigned64(frame.frameNumber);
                output.Raw(",\"startTimestampNs\":"); output.Unsigned64(frame.startTimestampNs);
                output.Raw(",\"durationNs\":"); output.Unsigned64(frame.durationNs);
                output.Raw(",\"framesPerSecond\":"); output.Double(frame.framesPerSecond);
                output.Raw(",\"metrics\":[");
                for (std::size_t j = 0; j < frame.metrics.size(); ++j)
                {
                    if (j != 0) output.Raw(",");
                    WriteMetric(output, frame.metrics[j]);
                }
                output.Raw("]}");
            }
            output.Raw("],\"resources\":[");
            for (std::size_t i = 0; i < snapshot.resources.size(); ++i)
            {
                if (i != 0) output.Raw(",");
                const auto& resource = snapshot.resources[i];
                output.Raw("{\"id\":"); output.Unsigned64(resource.id);
                output.Raw(",\"kind\":"); output.String(ResourceKindName(resource.kind));
                output.Raw(",\"label\":"); output.String(resource.label);
                output.Raw(",\"format\":"); output.String(resource.format);
                output.Raw(",\"width\":"); output.Number(resource.width);
                output.Raw(",\"height\":"); output.Number(resource.height);
                output.Raw(",\"depth\":"); output.Number(resource.depth);
                output.Raw(",\"mipCount\":"); output.Number(resource.mipCount);
                output.Raw(",\"estimatedBytes\":"); output.Unsigned64(resource.estimatedBytes);
                output.Raw(",\"byteAccuracy\":"); output.String(AccuracyName(resource.byteAccuracy));
                output.Raw("}");
            }
            output.Raw("]}");
            return output.Take();
        }

        std::string EventsJson(const EventsResponse& response)
        {
            JsonWriter output;
            output.Raw("{\"oldestAvailableSequence\":"); output.Unsigned64(response.oldestAvailableSequence);
            output.Raw(",\"newestAvailableSequence\":"); output.Unsigned64(response.newestAvailableSequence);
            output.Raw(",\"eventsDroppedBeforeStart\":"); output.Unsigned64(response.eventsDroppedBeforeStart);
            output.Raw(",\"producerEventsDropped\":"); output.Unsigned64(response.producerEventsDropped);
            output.Raw(",\"events\":[");
            for (std::size_t i = 0; i < response.events.size(); ++i)
            {
                if (i != 0) output.Raw(",");
                const auto& resolved = response.events[i];
                const auto& event = resolved.event;
                output.Raw("{\"sequence\":"); output.Unsigned64(event.sequence);
                output.Raw(",\"timestampNs\":"); output.Unsigned64(event.timestampNs);
                output.Raw(",\"durationNs\":"); output.Unsigned64(event.durationNs);
                output.Raw(",\"frameNumber\":"); output.Unsigned64(event.frameNumber);
                output.Raw(",\"threadId\":"); output.Unsigned64(event.threadId);
                output.Raw(",\"correlationId\":"); output.Unsigned64(event.correlationId);
                output.Raw(",\"parentCorrelationId\":"); output.Unsigned64(event.parentCorrelationId);
                output.Raw(",\"value\":"); output.Signed64(event.value);
                output.Raw(",\"nameId\":"); output.Number(event.name);
                output.Raw(",\"name\":"); output.String(resolved.name);
                output.Raw(",\"kind\":"); output.String(EventKindName(event.kind));
                output.Raw(",\"category\":"); output.String(CategoryName(event.category));
                output.Raw("}");
            }
            output.Raw("]}");
            return output.Take();
        }

        std::string Base64(std::span<const std::uint8_t> input)
        {
            constexpr std::string_view alphabet =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string output;
            output.reserve(((input.size() + 2U) / 3U) * 4U);
            for (std::size_t i = 0; i < input.size(); i += 3)
            {
                const auto remaining = input.size() - i;
                const auto value = static_cast<std::uint32_t>(input[i]) << 16U
                    | (remaining > 1 ? static_cast<std::uint32_t>(input[i + 1]) << 8U : 0U)
                    | (remaining > 2 ? static_cast<std::uint32_t>(input[i + 2]) : 0U);
                output.push_back(alphabet[(value >> 18U) & 0x3FU]);
                output.push_back(alphabet[(value >> 12U) & 0x3FU]);
                output.push_back(remaining > 1 ? alphabet[(value >> 6U) & 0x3FU] : '=');
                output.push_back(remaining > 2 ? alphabet[value & 0x3FU] : '=');
            }
            return output;
        }

        std::string PreviewJson(const PreviewResponse& response)
        {
            JsonWriter output;
            output.Raw("{\"status\":"); output.String(PreviewStatusName(response.status));
            output.Raw(",\"ticket\":"); output.Unsigned64(response.ticket);
            output.Raw(",\"width\":"); output.Number(response.width);
            output.Raw(",\"height\":"); output.Number(response.height);
            output.Raw(",\"mimeType\":"); output.String(response.mimeType);
            output.Raw(",\"message\":"); output.String(response.message);
            output.Raw(",\"data\":");
            if (response.encodedImage.empty()) output.Raw("null");
            else output.String(Base64(response.encodedImage));
            output.Raw("}");
            return output.Take();
        }

        std::string ErrorJson(std::string_view message)
        {
            JsonWriter output;
            output.Raw("{\"error\":"); output.String(message); output.Raw("}");
            return output.Take();
        }

        std::string GenerateUiToken()
        {
            std::array<std::uint8_t, 16> bytes{};
            std::string error;
            if (!GenerateSecureRandom(bytes, error))
            {
                throw std::runtime_error("could not generate Inspector UI token: " + error);
            }
            std::ostringstream output;
            output << std::hex << std::setfill('0');
            for (const auto byte : bytes)
            {
                output << std::setw(2) << static_cast<unsigned>(byte);
            }
            return output.str();
        }

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return value;
        }

        struct HttpRequest
        {
            std::string method;
            std::string target;
            std::vector<std::pair<std::string, std::string>> headers;

            [[nodiscard]] std::string_view Header(std::string_view name) const
            {
                for (const auto& [key, value] : headers)
                {
                    if (key == name) return value;
                }
                return {};
            }
        };

        bool ParseHttpRequest(std::span<const std::uint8_t> bytes,
                              HttpRequest& request,
                              std::string& error)
        {
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            const auto headerEnd = text.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
            {
                error = "incomplete HTTP request";
                return false;
            }
            const auto firstLineEnd = text.find("\r\n");
            const auto firstSpace = text.find(' ');
            const auto secondSpace = text.find(' ', firstSpace + 1);
            if (firstLineEnd == std::string::npos || firstSpace == std::string::npos
                || secondSpace == std::string::npos || secondSpace > firstLineEnd
                || text.substr(secondSpace + 1, firstLineEnd - secondSpace - 1) != "HTTP/1.1")
            {
                error = "invalid HTTP request line";
                return false;
            }
            request.method = text.substr(0, firstSpace);
            request.target = text.substr(firstSpace + 1, secondSpace - firstSpace - 1);
            if (request.target.empty() || request.target.size() > 2048)
            {
                error = "invalid HTTP request target";
                return false;
            }
            std::size_t offset = firstLineEnd + 2;
            while (offset < headerEnd)
            {
                const auto end = text.find("\r\n", offset);
                const auto colon = text.find(':', offset);
                if (end == std::string::npos || colon == std::string::npos || colon >= end)
                {
                    error = "invalid HTTP header";
                    return false;
                }
                auto key = ToLower(text.substr(offset, colon - offset));
                auto valueOffset = colon + 1;
                while (valueOffset < end && text[valueOffset] == ' ') ++valueOffset;
                request.headers.emplace_back(std::move(key),
                                             text.substr(valueOffset, end - valueOffset));
                offset = end + 2;
            }
            if (!request.Header("content-length").empty()
                && request.Header("content-length") != "0")
            {
                error = "HTTP request bodies are not accepted";
                return false;
            }
            return true;
        }

        bool ParseUnsignedQuery(std::string_view target,
                                std::string_view key,
                                std::uint64_t& value)
        {
            const auto question = target.find('?');
            if (question == std::string_view::npos) return false;
            auto query = target.substr(question + 1);
            while (!query.empty())
            {
                const auto ampersand = query.find('&');
                const auto pair = query.substr(0, ampersand);
                const auto equals = pair.find('=');
                if (equals != std::string_view::npos && pair.substr(0, equals) == key)
                {
                    const auto text = pair.substr(equals + 1);
                    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
                    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
                }
                if (ampersand == std::string_view::npos) break;
                query.remove_prefix(ampersand + 1);
            }
            return false;
        }
    }

    class WebBridge::Impl
    {
    public:
        explicit Impl(WebBridgeConfiguration configuration)
            : configuration_(std::move(configuration)), uiToken_(GenerateUiToken())
        {
        }

        ~Impl()
        {
            CloseSocket(listener_);
        }

        bool Start(std::string& error)
        {
            listener_ = CreateTcpListener("127.0.0.1", configuration_.httpPort, httpPort_, error);
            return listener_ != InvalidSocket;
        }

        int Run()
        {
            for (;;)
            {
                std::string error;
                const auto socket = AcceptTcp(listener_, 1000, error);
                if (socket == InvalidSocket) continue;
                Handle(socket);
                ShutdownSocket(socket);
                CloseSocket(socket);
            }
        }

        [[nodiscard]] std::uint16_t GetHttpPort() const noexcept { return httpPort_; }

    private:
        void Handle(SocketHandle socket)
        {
            std::vector<std::uint8_t> bytes;
            std::string error;
            if (!ReceiveUntil(socket, bytes, "\r\n\r\n", MaximumHttpRequestBytes,
                              HttpTimeoutMilliseconds, error))
            {
                SendHttp(socket, 400, "application/json", ErrorJson(error));
                return;
            }
            HttpRequest request;
            if (!ParseHttpRequest(bytes, request, error))
            {
                SendHttp(socket, 400, "application/json", ErrorJson(error));
                return;
            }
            const auto expectedHost = "127.0.0.1:" + std::to_string(httpPort_);
            if (request.Header("host") != expectedHost)
            {
                SendHttp(socket, 400, "application/json", ErrorJson("invalid Inspector Host header"));
                return;
            }
            const auto query = request.target.find('?');
            const auto path = request.target.substr(0, query);
            if (request.method == "GET" && path == "/")
            {
                auto html = std::string(InspectorIndexHtml());
                const auto marker = html.find("__CNA_UI_TOKEN__");
                if (marker != std::string::npos) html.replace(marker, 16, uiToken_);
                SendHttp(socket, 200, "text/html; charset=utf-8", html);
                return;
            }
            if (request.method == "GET" && path == "/style.css")
            {
                SendHttp(socket, 200, "text/css; charset=utf-8", InspectorStyleCss());
                return;
            }
            if (request.method == "GET" && path == "/app.js")
            {
                SendHttp(socket, 200, "text/javascript; charset=utf-8", InspectorAppJavaScript());
                return;
            }
            if (!path.starts_with("/api/")
                || request.Header("x-cna-inspector-ui-token") != uiToken_)
            {
                SendHttp(socket, 404, "application/json", ErrorJson("not found"));
                return;
            }
            if (!EnsureConnected(error))
            {
                SendHttp(socket, 503, "application/json", ErrorJson(error));
                return;
            }
            try
            {
                if (request.method == "GET" && path == "/api/session")
                {
                    SendHttp(socket, 200, "application/json", SessionJson(client_.GetSession()));
                    return;
                }
                if (request.method == "GET" && path == "/api/snapshot")
                {
                    SnapshotRequest snapshotRequest;
                    std::uint64_t resources = 0;
                    if (ParseUnsignedQuery(request.target, "resources", resources) && resources == 1)
                    {
                        snapshotRequest.parts = static_cast<std::uint32_t>(SnapshotPart::Resources);
                    }
                    SnapshotResponse response;
                    if (!client_.CaptureSnapshot(snapshotRequest, response, error))
                    {
                        SendHttp(socket, 503, "application/json", ErrorJson(error));
                        return;
                    }
                    SendHttp(socket, 200, "application/json", SnapshotJson(response));
                    return;
                }
                if (request.method == "GET" && path == "/api/events")
                {
                    EventsRequest eventsRequest;
                    std::uint64_t after = 0;
                    std::uint64_t maximum = 0;
                    if (ParseUnsignedQuery(request.target, "after", after)) eventsRequest.afterSequence = after;
                    if (ParseUnsignedQuery(request.target, "max", maximum))
                    {
                        eventsRequest.maximumEvents = static_cast<std::uint32_t>(
                            std::min<std::uint64_t>(maximum, MaximumEventsPerResponse));
                    }
                    EventsResponse response;
                    if (!client_.ReadEvents(eventsRequest, response, error))
                    {
                        SendHttp(socket, 503, "application/json", ErrorJson(error));
                        return;
                    }
                    SendHttp(socket, 200, "application/json", EventsJson(response));
                    return;
                }
                if (request.method == "POST" && path == "/api/preview")
                {
                    std::uint64_t resourceId = 0;
                    if (!ParseUnsignedQuery(request.target, "id", resourceId) || resourceId == 0)
                    {
                        SendHttp(socket, 400, "application/json", ErrorJson("invalid resource id"));
                        return;
                    }
                    PreviewRequest previewRequest;
                    previewRequest.resourceId = resourceId;
                    PreviewResponse response;
                    if (!client_.RequestPreview(previewRequest, response, error))
                    {
                        SendHttp(socket, 503, "application/json", ErrorJson(error));
                        return;
                    }
                    SendHttp(socket, 200, "application/json", PreviewJson(response));
                    return;
                }
                if (request.method == "GET" && path == "/api/preview")
                {
                    PreviewPollRequest poll;
                    if (!ParseUnsignedQuery(request.target, "ticket", poll.ticket) || poll.ticket == 0)
                    {
                        SendHttp(socket, 400, "application/json", ErrorJson("invalid preview ticket"));
                        return;
                    }
                    PreviewResponse response;
                    if (!client_.PollPreview(poll, response, error))
                    {
                        SendHttp(socket, 503, "application/json", ErrorJson(error));
                        return;
                    }
                    SendHttp(socket, 200, "application/json", PreviewJson(response));
                    return;
                }
                SendHttp(socket, 404, "application/json", ErrorJson("not found"));
            }
            catch (const std::exception& exception)
            {
                SendHttp(socket, 500, "application/json", ErrorJson(exception.what()));
            }
        }

        bool EnsureConnected(std::string& error)
        {
            if (client_.IsConnected()) return true;
            return client_.Connect(configuration_.agent, error);
        }

        void SendHttp(SocketHandle socket,
                      int status,
                      std::string_view contentType,
                      std::string_view body)
        {
            const auto reason = status == 200 ? "OK"
                : status == 400 ? "Bad Request"
                : status == 404 ? "Not Found"
                : status == 500 ? "Internal Server Error"
                : "Service Unavailable";
            std::string header = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n"
                + "Content-Type: " + std::string(contentType) + "\r\n"
                + "Content-Length: " + std::to_string(body.size()) + "\r\n"
                + "Connection: close\r\n"
                + "Cache-Control: no-store\r\n"
                + "X-Content-Type-Options: nosniff\r\n"
                + "Referrer-Policy: no-referrer\r\n"
                + "Content-Security-Policy: default-src 'self'; img-src 'self' data:; "
                  "style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'\r\n"
                + "\r\n";
            std::string ignored;
            const auto headerBytes = std::span(
                reinterpret_cast<const std::uint8_t*>(header.data()), header.size());
            const auto bodyBytes = std::span(
                reinterpret_cast<const std::uint8_t*>(body.data()), body.size());
            (void) SendAll(socket, headerBytes, HttpTimeoutMilliseconds, ignored);
            if (!body.empty()) (void) SendAll(socket, bodyBytes, HttpTimeoutMilliseconds, ignored);
        }

        WebBridgeConfiguration configuration_;
        std::string uiToken_;
        Client client_;
        SocketHandle listener_ = InvalidSocket;
        std::uint16_t httpPort_ = 0;
    };

    WebBridge::WebBridge(WebBridgeConfiguration configuration)
        : impl_(std::make_unique<Impl>(std::move(configuration)))
    {
    }

    WebBridge::~WebBridge() = default;

    bool WebBridge::Start(std::string& error)
    {
        return impl_->Start(error);
    }

    int WebBridge::Run()
    {
        return impl_->Run();
    }

    std::uint16_t WebBridge::GetHttpPort() const noexcept
    {
        return impl_->GetHttpPort();
    }
}
