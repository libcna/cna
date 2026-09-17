// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Inspector/Protocol.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace CNA::Inspector
{
    /** @brief Synchronous connection settings used by the out-of-process Inspector bridge. */
    struct ClientConfiguration
    {
        std::string host = "127.0.0.1";
        std::uint16_t port = 0;
        std::string authenticationToken;
        std::string clientName = "cna-inspector";
        std::uint32_t timeoutMilliseconds = 3000;
    };

    /** @brief Synchronous binary-protocol client for the separate Inspector process. */
    class Client
    {
    public:
        /** @brief Creates a disconnected client. */
        Client();
        /** @brief Closes any active connection. */
        ~Client();

        /**
         * @brief Clients cannot be copied.
         * @param other Client that would otherwise be copied.
         */
        Client(const Client& other) = delete;

        /**
         * @brief Clients cannot be copy-assigned.
         * @param other Client that would otherwise be assigned.
         * @return This client is never returned because assignment is deleted.
         */
        Client& operator=(const Client& other) = delete;

        /**
         * @brief Connects, authenticates, and negotiates capabilities.
         * @param configuration Agent endpoint and authentication settings.
         * @param error Receives a connection or negotiation error.
         * @return True when the client is ready for requests.
         */
        [[nodiscard]] bool Connect(const ClientConfiguration& configuration, std::string& error);

        /** @brief Closes the active connection; safe to call more than once. */
        void Disconnect() noexcept;

        /**
         * @brief Returns whether a connection has completed negotiation.
         * @return True when requests can be sent.
         */
        [[nodiscard]] bool IsConnected() const noexcept;

        /**
         * @brief Returns the latest negotiated session description.
         * @return Session description retained until the next successful connection.
         */
        [[nodiscard]] const HelloResponse& GetSession() const noexcept;

        /**
         * @brief Requests selected parts of the current diagnostics snapshot.
         * @param request Selective snapshot request.
         * @param response Receives the response.
         * @param error Receives a transport or protocol error.
         * @return True on success.
         */
        [[nodiscard]] bool CaptureSnapshot(const SnapshotRequest& request,
                                           SnapshotResponse& response,
                                           std::string& error);

        /**
         * @brief Reads diagnostics events after a retained sequence cursor.
         * @param request Cursor and maximum event count.
         * @param response Receives resolved events and loss metadata.
         * @param error Receives a transport or protocol error.
         * @return True on success.
         */
        [[nodiscard]] bool ReadEvents(const EventsRequest& request,
                                      EventsResponse& response,
                                      std::string& error);

        /**
         * @brief Explicitly requests one resource preview.
         * @param request Validated preview bounds and resource identifier.
         * @param response Receives preview state.
         * @param error Receives a transport or protocol error.
         * @return True on protocol success, including an unavailable preview.
         */
        [[nodiscard]] bool RequestPreview(const PreviewRequest& request,
                                          PreviewResponse& response,
                                          std::string& error);

        /**
         * @brief Polls a previously returned asynchronous preview ticket.
         * @param request Ticket to poll.
         * @param response Receives preview state.
         * @param error Receives a transport or protocol error.
         * @return True on protocol success.
         */
        [[nodiscard]] bool PollPreview(const PreviewPollRequest& request,
                                       PreviewResponse& response,
                                       std::string& error);

        /**
         * @brief Verifies that the negotiated agent still responds.
         * @param error Receives a transport or protocol error.
         * @return True when a matching pong is received.
         */
        [[nodiscard]] bool Ping(std::string& error);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
