// SPDX-License-Identifier: MS-PL

#include "CNA/Inspector/Client.hpp"

#include "InternalSocket.hpp"

#include <exception>
#include <functional>

namespace CNA::Inspector
{
    class Client::Impl
    {
    public:
        ~Impl()
        {
            Disconnect();
        }

        bool Connect(const ClientConfiguration& configuration, std::string& error)
        {
            Disconnect();
            if (configuration.host.empty() || configuration.port == 0
                || configuration.authenticationToken.empty()
                || configuration.authenticationToken.size() > 256
                || configuration.timeoutMilliseconds == 0)
            {
                error = "Inspector client configuration is incomplete or invalid";
                return false;
            }
            configuration_ = configuration;
            HelloRequest hello;
            hello.clientName = configuration.clientName;
            hello.authenticationToken = configuration.authenticationToken;
            Packet helloPacket;
            try
            {
                helloPacket = ProtocolCodec::Encode(hello, nextRequestId_);
            }
            catch (const std::exception& exception)
            {
                error = std::string("invalid Inspector client identity: ") + exception.what();
                return false;
            }
            socket_ = Detail::ConnectTcp(configuration.host, configuration.port,
                                         configuration.timeoutMilliseconds, error);
            if (socket_ == Detail::InvalidSocket)
            {
                return false;
            }

            const auto requestId = nextRequestId_++;
            helloPacket.header.requestId = requestId;
            Packet response;
            if (!Detail::SendPacket(socket_, helloPacket,
                                    configuration.timeoutMilliseconds, error)
                || !Detail::ReceivePacket(socket_, response, configuration.timeoutMilliseconds, error)
                || !ValidateEnvelope(response, requestId, error))
            {
                Disconnect();
                return false;
            }
            if (response.header.type == MessageType::Error)
            {
                DecodeError(response, error);
                Disconnect();
                return false;
            }
            if (!ProtocolCodec::Decode(response, session_, error)
                || session_.selectedMajorVersion != ProtocolMajorVersion
                || session_.selectedMinorVersion > ProtocolMinorVersion
                || session_.providerInterfaceVersion != Diagnostics::IDiagnosticsProvider::InterfaceVersion)
            {
                if (error.empty())
                {
                    error = "Inspector negotiated an unsupported provider or protocol version";
                }
                Disconnect();
                return false;
            }
            connected_ = true;
            return true;
        }

        void Disconnect() noexcept
        {
            connected_ = false;
            Detail::ShutdownSocket(socket_);
            Detail::CloseSocket(socket_);
            socket_ = Detail::InvalidSocket;
        }

        bool RoundTrip(Packet request,
                       MessageType expectedType,
                       const std::function<bool(const Packet&, std::string&)>& decoder,
                       std::string& error,
                       ErrorResponse* protocolError = nullptr)
        {
            if (!connected_)
            {
                error = "Inspector client is disconnected";
                return false;
            }
            request.header.requestId = nextRequestId_++;
            Packet response;
            if (!Detail::SendPacket(socket_, request, configuration_.timeoutMilliseconds, error)
                || !Detail::ReceivePacket(socket_, response, configuration_.timeoutMilliseconds, error)
                || !ValidateEnvelope(response, request.header.requestId, error))
            {
                Disconnect();
                return false;
            }
            if (response.header.type == MessageType::Error)
            {
                ErrorResponse decoded;
                std::string decodeError;
                if (!ProtocolCodec::Decode(response, decoded, decodeError))
                {
                    error = decodeError;
                    Disconnect();
                    return false;
                }
                if (protocolError) *protocolError = decoded;
                error = decoded.message;
                return false;
            }
            if (response.header.type != expectedType || !decoder(response, error))
            {
                if (error.empty()) error = "Inspector returned an unexpected response type";
                Disconnect();
                return false;
            }
            return true;
        }

        [[nodiscard]] bool IsConnected() const noexcept { return connected_; }
        [[nodiscard]] const HelloResponse& GetSession() const noexcept { return session_; }

    private:
        bool ValidateEnvelope(const Packet& packet, std::uint64_t requestId, std::string& error)
        {
            if (packet.header.majorVersion != ProtocolMajorVersion
                || packet.header.minorVersion > ProtocolMinorVersion
                || packet.header.requestId != requestId)
            {
                error = "Inspector response envelope does not match the negotiated request";
                return false;
            }
            return true;
        }

        void DecodeError(const Packet& packet, std::string& error)
        {
            ErrorResponse response;
            std::string decodeError;
            if (ProtocolCodec::Decode(packet, response, decodeError))
            {
                error = response.message;
            }
            else
            {
                error = decodeError;
            }
        }

        ClientConfiguration configuration_;
        Detail::SocketHandle socket_ = Detail::InvalidSocket;
        HelloResponse session_;
        std::uint64_t nextRequestId_ = 1;
        bool connected_ = false;
    };

    Client::Client() : impl_(std::make_unique<Impl>()) {}

    Client::~Client() = default;

    bool Client::Connect(const ClientConfiguration& configuration, std::string& error)
    {
        return impl_->Connect(configuration, error);
    }

    void Client::Disconnect() noexcept
    {
        impl_->Disconnect();
    }

    bool Client::IsConnected() const noexcept
    {
        return impl_->IsConnected();
    }

    const HelloResponse& Client::GetSession() const noexcept
    {
        return impl_->GetSession();
    }

    bool Client::CaptureSnapshot(const SnapshotRequest& request,
                                 SnapshotResponse& response,
                                 std::string& error)
    {
        auto packet = ProtocolCodec::Encode(request, 0);
        return impl_->RoundTrip(std::move(packet), MessageType::SnapshotResponse,
            [&](const Packet& result, std::string& decodeError) {
                return ProtocolCodec::Decode(result, response, decodeError);
            }, error);
    }

    bool Client::ReadEvents(const EventsRequest& request,
                            EventsResponse& response,
                            std::string& error)
    {
        auto packet = ProtocolCodec::Encode(request, 0);
        return impl_->RoundTrip(std::move(packet), MessageType::EventsResponse,
            [&](const Packet& result, std::string& decodeError) {
                return ProtocolCodec::Decode(result, response, decodeError);
            }, error);
    }

    bool Client::RequestPreview(const PreviewRequest& request,
                                PreviewResponse& response,
                                std::string& error)
    {
        auto packet = ProtocolCodec::Encode(request, 0);
        ErrorResponse protocolError;
        const auto success = impl_->RoundTrip(std::move(packet), MessageType::PreviewResponse,
            [&](const Packet& result, std::string& decodeError) {
                return ProtocolCodec::Decode(result, response, decodeError);
            }, error, &protocolError);
        if (!success && protocolError.code == ErrorCode::Unavailable)
        {
            response = {};
            response.status = PreviewStatus::Unavailable;
            response.message = protocolError.message;
            error.clear();
            return true;
        }
        return success;
    }

    bool Client::PollPreview(const PreviewPollRequest& request,
                             PreviewResponse& response,
                             std::string& error)
    {
        auto packet = ProtocolCodec::Encode(request, 0);
        return impl_->RoundTrip(std::move(packet), MessageType::PreviewResponse,
            [&](const Packet& result, std::string& decodeError) {
                return ProtocolCodec::Decode(result, response, decodeError);
            }, error);
    }

    bool Client::Ping(std::string& error)
    {
        Packet packet;
        packet.header.type = MessageType::Ping;
        return impl_->RoundTrip(std::move(packet), MessageType::Pong,
            [](const Packet& result, std::string& decodeError) {
                if (!result.payload.empty())
                {
                    decodeError = "Inspector Pong payload must be empty";
                    return false;
                }
                return true;
            }, error);
    }
}
