// SPDX-License-Identifier: MS-PL

#include "CNA/Inspector/Agent.hpp"

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/TargetPlatform.hpp"
#include "CNA/Version.hpp"
#include "InternalSocket.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace CNA::Inspector
{
    namespace
    {
        constexpr std::uint32_t HandshakeTimeoutMilliseconds = 3000;
        constexpr std::uint32_t SocketTimeoutMilliseconds = 30000;

        std::string GetPlatformBackendName()
        {
#if defined(CNA_PLATFORM_SDL3)
            return "SDL3";
#elif defined(CNA_PLATFORM_SDL2)
            return "SDL2";
#elif defined(CNA_PLATFORM_WIN32)
            return "WIN32";
#elif defined(CNA_PLATFORM_X11)
            return "X11";
#elif defined(CNA_PLATFORM_WAYLAND)
            return "WAYLAND";
#elif defined(CNA_PLATFORM_HEADLESS)
            return "HEADLESS";
#elif defined(CNA_PLATFORM_TERMINAL)
            return "TERMINAL";
#else
            return "UNKNOWN";
#endif
        }

        std::string GetBuildConfigurationName()
        {
#if defined(NDEBUG)
            return "Release";
#else
            return "Debug";
#endif
        }

        bool GenerateToken(std::string& token, std::string& error)
        {
            std::array<std::uint8_t, 32> bytes{};
            if (!Detail::GenerateSecureRandom(bytes, error))
            {
                error = "could not generate Inspector authentication token: " + error;
                return false;
            }
            std::ostringstream output;
            output << std::hex << std::setfill('0');
            for (const auto byte : bytes)
            {
                output << std::setw(2) << static_cast<unsigned>(byte);
            }
            token = output.str();
            return true;
        }

        bool TokensEqual(std::string_view expected, std::string_view actual)
        {
            const auto length = std::max(expected.size(), actual.size());
            std::uint8_t difference = static_cast<std::uint8_t>(expected.size() != actual.size());
            for (std::size_t i = 0; i < length; ++i)
            {
                const auto left = i < expected.size() ? static_cast<std::uint8_t>(expected[i]) : 0U;
                const auto right = i < actual.size() ? static_cast<std::uint8_t>(actual[i]) : 0U;
                difference |= left ^ right;
            }
            return difference == 0;
        }

        bool IsPreviewable(Diagnostics::ResourceKind kind)
        {
            return kind == Diagnostics::ResourceKind::Texture2D
                || kind == Diagnostics::ResourceKind::TextureCube
                || kind == Diagnostics::ResourceKind::RenderTarget2D
                || kind == Diagnostics::ResourceKind::RenderTargetCube;
        }

        bool IsSupportedPreviewMime(std::string_view mimeType)
        {
            return mimeType == "image/png" || mimeType == "image/jpeg"
                || mimeType == "image/webp";
        }
    }

    class Agent::Impl
    {
    public:
        Impl(Diagnostics::IDiagnosticsProvider& provider, AgentConfiguration configuration)
            : provider_(provider), configuration_(std::move(configuration))
        {
        }

        ~Impl()
        {
            Stop();
        }

        bool Start(std::string& error)
        {
            if (configuration_.bindAddress.empty())
            {
                error = "Inspector bind address cannot be empty";
                return false;
            }
            localOnly_ = Detail::IsLoopbackAddress(configuration_.bindAddress);
            if (!localOnly_ && !configuration_.allowRemote)
            {
                error = "non-loopback Inspector binding requires allowRemote=true";
                return false;
            }
            if (configuration_.metadata.size() > 64)
            {
                error = "Inspector session metadata is limited to 64 entries";
                return false;
            }
            if (configuration_.authenticationToken.size() > 256)
            {
                error = "Inspector authentication token exceeds 256 bytes";
                return false;
            }
            if (configuration_.authenticationToken.empty()
                && !GenerateToken(configuration_.authenticationToken, error))
            {
                return false;
            }
            configuration_.maximumRequestsPerSecond = std::clamp(
                configuration_.maximumRequestsPerSecond, 1U, 1024U);
            configuration_.maximumPendingPreviews = std::clamp(
                configuration_.maximumPendingPreviews, 1U, 16U);
            configuration_.previewCooldownMilliseconds = std::clamp(
                configuration_.previewCooldownMilliseconds, 50U, 60000U);
            if (configuration_.platformBackend.empty())
            {
                configuration_.platformBackend = GetPlatformBackendName();
            }
            if (configuration_.buildConfiguration.empty())
            {
                configuration_.buildConfiguration = GetBuildConfigurationName();
            }
            try
            {
                HelloRequest authenticationValidation;
                authenticationValidation.authenticationToken = configuration_.authenticationToken;
                (void)ProtocolCodec::Encode(authenticationValidation, 0);
                HelloResponse identityValidation;
                identityValidation.applicationName = configuration_.applicationName;
                identityValidation.platformBackend = configuration_.platformBackend;
                identityValidation.buildConfiguration = configuration_.buildConfiguration;
                identityValidation.metadata = configuration_.metadata;
                (void)ProtocolCodec::Encode(identityValidation, 0);
            }
            catch (const std::exception& exception)
            {
                error = std::string("invalid Inspector identity configuration: ") + exception.what();
                return false;
            }

            listener_ = Detail::CreateTcpListener(configuration_.bindAddress,
                                                   configuration_.port, port_, error);
            if (listener_ == Detail::InvalidSocket)
            {
                return false;
            }
            stopping_.store(false, std::memory_order_release);
            running_.store(true, std::memory_order_release);
            try
            {
                worker_ = std::thread([this] { Run(); });
            }
            catch (const std::exception& exception)
            {
                running_.store(false, std::memory_order_release);
                Detail::CloseSocket(listener_);
                listener_ = Detail::InvalidSocket;
                error = std::string("could not start Inspector agent thread: ") + exception.what();
                return false;
            }
            return true;
        }

        void Stop() noexcept
        {
            if (!running_.exchange(false, std::memory_order_acq_rel) && !worker_.joinable())
            {
                return;
            }
            stopping_.store(true, std::memory_order_release);
            {
                std::lock_guard lock(socketMutex_);
                Detail::ShutdownSocket(activeClient_);
            }
            if (worker_.joinable())
            {
                worker_.join();
            }
            Detail::CloseSocket(listener_);
            listener_ = Detail::InvalidSocket;
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            return running_.load(std::memory_order_acquire);
        }

        [[nodiscard]] const std::string& GetBindAddress() const noexcept
        {
            return configuration_.bindAddress;
        }

        [[nodiscard]] std::uint16_t GetPort() const noexcept
        {
            return port_;
        }

        [[nodiscard]] const std::string& GetAuthenticationToken() const noexcept
        {
            return configuration_.authenticationToken;
        }

    private:
        void Run() noexcept
        {
            while (!stopping_.load(std::memory_order_acquire))
            {
                std::string error;
                const auto client = Detail::AcceptTcp(listener_, 200, error);
                if (client == Detail::InvalidSocket)
                {
                    continue;
                }
                {
                    std::lock_guard lock(socketMutex_);
                    activeClient_ = client;
                }
                HandleClient(client);
                pendingPreviews_.clear();
                {
                    std::lock_guard lock(socketMutex_);
                    if (activeClient_ == client)
                    {
                        activeClient_ = Detail::InvalidSocket;
                    }
                }
                Detail::ShutdownSocket(client);
                Detail::CloseSocket(client);
            }
            running_.store(false, std::memory_order_release);
        }

        void HandleClient(Detail::SocketHandle client) noexcept
        {
            Packet packet;
            std::string error;
            if (!Detail::ReceivePacket(client, packet, HandshakeTimeoutMilliseconds, error))
            {
                return;
            }
            if (packet.header.majorVersion != ProtocolMajorVersion
                || packet.header.minorVersion > ProtocolMinorVersion)
            {
                SendError(client, packet.header.requestId, ErrorCode::UnsupportedVersion,
                          "Inspector packet envelope version is unsupported");
                return;
            }
            HelloRequest request;
            if (packet.header.type != MessageType::ClientHello
                || !ProtocolCodec::Decode(packet, request, error))
            {
                SendError(client, packet.header.requestId, ErrorCode::InvalidRequest,
                          "the first Inspector message must be a valid ClientHello");
                return;
            }
            if (request.minimumMajorVersion > ProtocolMajorVersion
                || request.maximumMajorVersion < ProtocolMajorVersion)
            {
                SendError(client, packet.header.requestId, ErrorCode::UnsupportedVersion,
                          "Inspector protocol version 1 is required");
                return;
            }
            if (!TokensEqual(configuration_.authenticationToken, request.authenticationToken))
            {
                SendError(client, packet.header.requestId, ErrorCode::Unauthorized,
                          "Inspector authentication failed");
                return;
            }

            HelloResponse hello;
            try
            {
                const auto snapshot = provider_.CaptureSnapshot();
                hello.providerInterfaceVersion = Diagnostics::IDiagnosticsProvider::InterfaceVersion;
                hello.capabilities = static_cast<std::uint64_t>(Capability::Snapshots)
                    | static_cast<std::uint64_t>(Capability::Accuracy)
                    | static_cast<std::uint64_t>(Capability::Discontinuities);
                if (snapshot.buildMode >= Diagnostics::Mode::Stats)
                {
                    hello.capabilities |= static_cast<std::uint64_t>(Capability::ResourceMetadata);
                }
                if (snapshot.buildMode >= Diagnostics::Mode::Full)
                {
                    hello.capabilities |= static_cast<std::uint64_t>(Capability::Events)
                        | static_cast<std::uint64_t>(Capability::CpuZones);
                }
                if (configuration_.previewProvider)
                {
                    hello.capabilities |= static_cast<std::uint64_t>(Capability::ResourcePreviews);
                }
            }
            catch (...)
            {
                SendError(client, packet.header.requestId, ErrorCode::InternalError,
                          "diagnostics provider failed during negotiation");
                return;
            }
            hello.capabilities &= request.requestedCapabilities;
            hello.maximumPayloadBytes = static_cast<std::uint32_t>(MaximumPayloadBytes);
            hello.maximumEventsPerResponse = static_cast<std::uint32_t>(MaximumEventsPerResponse);
            hello.maximumPreviewBytes = static_cast<std::uint32_t>(MaximumPreviewBytes);
            hello.localOnly = localOnly_;
            hello.applicationName = configuration_.applicationName;
            hello.cnaVersion = std::string(CNA::getVersionString());
            hello.targetPlatform = CNA::getCurrentPlatformName();
            hello.platformBackend = configuration_.platformBackend;
            hello.graphicsRenderer = std::string(CNA::getCurrentGraphicsRendererName());
            hello.buildConfiguration = configuration_.buildConfiguration;
            hello.metadata = configuration_.metadata;
            try
            {
                if (!Detail::SendPacket(client, ProtocolCodec::Encode(hello, packet.header.requestId),
                                        SocketTimeoutMilliseconds, error))
                {
                    return;
                }
            }
            catch (...)
            {
                SendError(client, packet.header.requestId, ErrorCode::InternalError,
                          "session metadata could not be encoded");
                return;
            }

            auto rateWindow = std::chrono::steady_clock::now();
            std::uint32_t requestCount = 0;
            while (!stopping_.load(std::memory_order_acquire))
            {
                if (!Detail::ReceivePacket(client, packet, SocketTimeoutMilliseconds, error))
                {
                    return;
                }
                if (packet.header.majorVersion != ProtocolMajorVersion
                    || packet.header.minorVersion > ProtocolMinorVersion)
                {
                    SendError(client, packet.header.requestId, ErrorCode::UnsupportedVersion,
                              "Inspector packet envelope version changed after negotiation");
                    return;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now - rateWindow >= std::chrono::seconds(1))
                {
                    rateWindow = now;
                    requestCount = 0;
                }
                if (++requestCount > configuration_.maximumRequestsPerSecond)
                {
                    SendError(client, packet.header.requestId, ErrorCode::Busy,
                              "Inspector request-rate limit exceeded");
                    continue;
                }
                if (!HandleRequest(client, packet, hello.capabilities))
                {
                    return;
                }
            }
        }

        bool HandleRequest(Detail::SocketHandle client,
                           const Packet& packet,
                           std::uint64_t capabilities) noexcept
        {
            std::string error;
            try
            {
                switch (packet.header.type)
                {
                    case MessageType::SnapshotRequest:
                        return HandleSnapshot(client, packet, capabilities);
                    case MessageType::EventsRequest:
                        return HandleEvents(client, packet, capabilities);
                    case MessageType::PreviewRequest:
                        return HandlePreviewRequest(client, packet, capabilities);
                    case MessageType::PreviewPollRequest:
                        return HandlePreviewPoll(client, packet, capabilities);
                    case MessageType::Ping:
                    {
                        if (!packet.payload.empty())
                        {
                            SendError(client, packet.header.requestId, ErrorCode::InvalidRequest,
                                      "Ping payload must be empty");
                            return true;
                        }
                        Packet pong;
                        pong.header.type = MessageType::Pong;
                        pong.header.requestId = packet.header.requestId;
                        return Detail::SendPacket(client, pong, SocketTimeoutMilliseconds, error);
                    }
                    default:
                        SendError(client, packet.header.requestId, ErrorCode::InvalidRequest,
                                  "message type is not valid after negotiation");
                        return true;
                }
            }
            catch (const std::length_error&)
            {
                SendError(client, packet.header.requestId, ErrorCode::LimitExceeded,
                          "Inspector response exceeds a configured protocol bound");
                return true;
            }
            catch (...)
            {
                SendError(client, packet.header.requestId, ErrorCode::InternalError,
                          "Inspector request failed without affecting the application");
                return true;
            }
        }

        bool HandleSnapshot(Detail::SocketHandle client,
                            const Packet& packet,
                            std::uint64_t capabilities)
        {
            if (!HasCapability(capabilities, Capability::Snapshots))
            {
                SendError(client, packet.header.requestId, ErrorCode::Unavailable,
                          "snapshot capability was not negotiated");
                return true;
            }
            SnapshotRequest request;
            std::string error;
            if (!ProtocolCodec::Decode(packet, request, error))
            {
                SendError(client, packet.header.requestId, ErrorCode::InvalidRequest, error);
                return true;
            }
            if (HasSnapshotPart(request.parts, SnapshotPart::Resources)
                && !HasCapability(capabilities, Capability::ResourceMetadata))
            {
                SendError(client, packet.header.requestId, ErrorCode::Unavailable,
                          "resource metadata capability was not negotiated");
                return true;
            }
            auto snapshot = provider_.CaptureSnapshot();
            SnapshotResponse response;
            response.includedParts = request.parts;
            response.metricCount = static_cast<std::uint32_t>(snapshot.metrics.size());
            response.frameCount = static_cast<std::uint32_t>(snapshot.recentFrames.size());
            response.resourceCount = static_cast<std::uint32_t>(snapshot.resources.size());
            if (!HasSnapshotPart(request.parts, SnapshotPart::Metrics)) snapshot.metrics.clear();
            if (!HasSnapshotPart(request.parts, SnapshotPart::Frames)) snapshot.recentFrames.clear();
            if (!HasSnapshotPart(request.parts, SnapshotPart::Resources)) snapshot.resources.clear();
            response.snapshot = std::move(snapshot);
            return Detail::SendPacket(client, ProtocolCodec::Encode(response, packet.header.requestId),
                                      SocketTimeoutMilliseconds, error);
        }

        bool HandleEvents(Detail::SocketHandle client,
                          const Packet& packet,
                          std::uint64_t capabilities)
        {
            if (!HasCapability(capabilities, Capability::Events))
            {
                SendError(client, packet.header.requestId, ErrorCode::Unavailable,
                          "event capability is unavailable or was not negotiated");
                return true;
            }
            EventsRequest request;
            std::string error;
            if (!ProtocolCodec::Decode(packet, request, error))
            {
                SendError(client, packet.header.requestId, ErrorCode::InvalidRequest, error);
                return true;
            }
            auto batch = provider_.ReadEvents(request.afterSequence, request.maximumEvents);
            EventsResponse response;
            response.oldestAvailableSequence = batch.oldestAvailableSequence;
            response.newestAvailableSequence = batch.newestAvailableSequence;
            response.eventsDroppedBeforeStart = batch.eventsDroppedBeforeStart;
            response.producerEventsDropped = batch.producerEventsDropped;
            response.events.reserve(batch.events.size());
            for (auto& event : batch.events)
            {
                response.events.push_back({event, provider_.ResolveName(event.name)});
            }
            return Detail::SendPacket(client, ProtocolCodec::Encode(response, packet.header.requestId),
                                      SocketTimeoutMilliseconds, error);
        }

        bool HandlePreviewRequest(Detail::SocketHandle client,
                                  const Packet& packet,
                                  std::uint64_t capabilities)
        {
            if (!HasCapability(capabilities, Capability::ResourcePreviews)
                || !configuration_.previewProvider)
            {
                SendError(client, packet.header.requestId, ErrorCode::Unavailable,
                          "resource previews are unavailable in this application");
                return true;
            }
            PreviewRequest request;
            std::string error;
            if (!ProtocolCodec::Decode(packet, request, error))
            {
                SendError(client, packet.header.requestId, ErrorCode::InvalidRequest, error);
                return true;
            }
            if (pendingPreviews_.size() >= configuration_.maximumPendingPreviews)
            {
                SendError(client, packet.header.requestId, ErrorCode::Busy,
                          "maximum pending preview count reached");
                return true;
            }
            const auto now = std::chrono::steady_clock::now();
            if (lastPreviewRequest_.time_since_epoch().count() != 0
                && now - lastPreviewRequest_
                    < std::chrono::milliseconds(configuration_.previewCooldownMilliseconds))
            {
                SendError(client, packet.header.requestId, ErrorCode::Busy,
                          "preview requests are throttled");
                return true;
            }

            const auto snapshot = provider_.CaptureSnapshot();
            const auto resource = std::find_if(snapshot.resources.begin(), snapshot.resources.end(),
                [&](const auto& value) { return value.id == request.resourceId; });
            if (resource == snapshot.resources.end() || !IsPreviewable(resource->kind))
            {
                SendError(client, packet.header.requestId, ErrorCode::Unavailable,
                          "resource is absent or is not a previewable texture/render target");
                return true;
            }
            lastPreviewRequest_ = now;
            auto response = configuration_.previewProvider->RequestPreview(request);
            if (!ValidatePreview(response, request.maximumWidth, request.maximumHeight,
                                 request.maximumBytes, error))
            {
                SendError(client, packet.header.requestId, ErrorCode::InternalError, error);
                return true;
            }
            if (response.status == PreviewStatus::Pending)
            {
                if (!pendingPreviews_.emplace(response.ticket, request).second)
                {
                    SendError(client, packet.header.requestId, ErrorCode::InternalError,
                              "preview provider reused a pending ticket");
                    return true;
                }
            }
            return Detail::SendPacket(client, ProtocolCodec::Encode(response, packet.header.requestId),
                                      SocketTimeoutMilliseconds, error);
        }

        bool HandlePreviewPoll(Detail::SocketHandle client,
                               const Packet& packet,
                               std::uint64_t capabilities)
        {
            if (!HasCapability(capabilities, Capability::ResourcePreviews)
                || !configuration_.previewProvider)
            {
                SendError(client, packet.header.requestId, ErrorCode::Unavailable,
                          "resource previews are unavailable in this application");
                return true;
            }
            PreviewPollRequest request;
            std::string error;
            if (!ProtocolCodec::Decode(packet, request, error))
            {
                SendError(client, packet.header.requestId, ErrorCode::InvalidRequest, error);
                return true;
            }
            const auto pending = pendingPreviews_.find(request.ticket);
            if (pending == pendingPreviews_.end())
            {
                SendError(client, packet.header.requestId, ErrorCode::InvalidRequest,
                          "preview ticket is unknown or no longer pending");
                return true;
            }
            auto response = configuration_.previewProvider->PollPreview(request.ticket);
            if (!ValidatePreview(response, pending->second.maximumWidth,
                                 pending->second.maximumHeight,
                                 pending->second.maximumBytes, error)
                || (response.ticket != 0 && response.ticket != request.ticket))
            {
                pendingPreviews_.erase(pending);
                SendError(client, packet.header.requestId, ErrorCode::InternalError,
                          error.empty() ? "preview provider changed the ticket" : error);
                return true;
            }
            response.ticket = request.ticket;
            if (response.status != PreviewStatus::Pending)
            {
                pendingPreviews_.erase(request.ticket);
            }
            return Detail::SendPacket(client, ProtocolCodec::Encode(response, packet.header.requestId),
                                      SocketTimeoutMilliseconds, error);
        }

        bool ValidatePreview(const PreviewResponse& response,
                             std::uint32_t maximumWidth,
                             std::uint32_t maximumHeight,
                             std::uint32_t maximumBytes,
                             std::string& error) const
        {
            if (response.status == PreviewStatus::Pending && response.ticket == 0)
            {
                error = "preview provider returned a pending result without a ticket";
                return false;
            }
            if (response.status != PreviewStatus::Ready
                && (response.width != 0 || response.height != 0
                    || !response.mimeType.empty() || !response.encodedImage.empty()))
            {
                error = "preview provider attached image data to a non-ready result";
                return false;
            }
            if (response.status == PreviewStatus::Ready
                && (response.width == 0 || response.width > maximumWidth
                    || response.height == 0 || response.height > maximumHeight
                    || response.encodedImage.empty()
                    || response.encodedImage.size() > maximumBytes
                    || !IsSupportedPreviewMime(response.mimeType)))
            {
                error = "preview provider returned an invalid or over-limit image";
                return false;
            }
            return true;
        }

        void SendError(Detail::SocketHandle client,
                       std::uint64_t requestId,
                       ErrorCode code,
                       std::string_view message) noexcept
        {
            try
            {
                std::string ignored;
                (void) Detail::SendPacket(client,
                    ProtocolCodec::Encode(ErrorResponse{code, std::string(message)}, requestId),
                    SocketTimeoutMilliseconds, ignored);
            }
            catch (...)
            {
            }
        }

        Diagnostics::IDiagnosticsProvider& provider_;
        AgentConfiguration configuration_;
        Detail::SocketHandle listener_ = Detail::InvalidSocket;
        Detail::SocketHandle activeClient_ = Detail::InvalidSocket;
        std::uint16_t port_ = 0;
        bool localOnly_ = true;
        std::atomic<bool> stopping_{false};
        std::atomic<bool> running_{false};
        std::thread worker_;
        std::mutex socketMutex_;
        std::unordered_map<std::uint64_t, PreviewRequest> pendingPreviews_;
        std::chrono::steady_clock::time_point lastPreviewRequest_{};
    };

    Agent::Agent(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    Agent::~Agent() = default;

    std::unique_ptr<Agent> Agent::Start(const AgentConfiguration& configuration,
                                        std::string& error)
    {
        return Start(Diagnostics::GetProvider(), configuration, error);
    }

    std::unique_ptr<Agent> Agent::Start(Diagnostics::IDiagnosticsProvider& provider,
                                        const AgentConfiguration& configuration,
                                        std::string& error)
    {
        auto impl = std::make_unique<Impl>(provider, configuration);
        if (!impl->Start(error))
        {
            return nullptr;
        }
        return std::unique_ptr<Agent>(new Agent(std::move(impl)));
    }

    void Agent::Stop() noexcept
    {
        if (impl_) impl_->Stop();
    }

    bool Agent::IsRunning() const noexcept
    {
        return impl_ && impl_->IsRunning();
    }

    const std::string& Agent::GetBindAddress() const noexcept
    {
        return impl_->GetBindAddress();
    }

    std::uint16_t Agent::GetPort() const noexcept
    {
        return impl_ ? impl_->GetPort() : 0;
    }

    const std::string& Agent::GetAuthenticationToken() const noexcept
    {
        static const std::string empty;
        return impl_ ? impl_->GetAuthenticationToken() : empty;
    }
}
