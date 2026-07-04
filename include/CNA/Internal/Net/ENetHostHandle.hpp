// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <enet/enet.h>

namespace CNA::Internal::Net
{
    /**
     * @brief RAII wrapper around a single ENet host (`::ENetHost*`).
     *
     * An instance is either a "server" host — bound to a local port and able to accept
     * incoming connections — or a "client" host — unbound, able only to make one outgoing
     * connection. This mirrors ENet's own model: every peer in an ENet session, including
     * clients, owns an `ENetHost`.
     *
     * Move-only: destroying the last owner calls `enet_host_destroy()`.
     */
    class ENetHostHandle
    {
    public:
        /**
         * @brief Creates a host bound to a local port, able to accept incoming connections.
         *
         * @param port Port to bind to. Pass 0 (`ENET_PORT_ANY`) for an OS-assigned ephemeral
         *             port — read it back afterward with `getBoundPortProperty()`.
         * @param maxPeers Maximum number of simultaneous connections this host will accept.
         * @param channelLimit Maximum number of channels allowed per connected peer.
         * @return The created host handle.
         */
        [[nodiscard]] static ENetHostHandle CreateHost(uint16_t port, size_t maxPeers, size_t channelLimit);

        /**
         * @brief Creates an unbound host that can make exactly one outgoing connection.
         *
         * @param channelLimit Maximum number of channels to request when connecting.
         * @return The created host handle.
         */
        [[nodiscard]] static ENetHostHandle CreateClient(size_t channelLimit);

        ENetHostHandle(const ENetHostHandle&) = delete;
        ENetHostHandle& operator=(const ENetHostHandle&) = delete;
        /** @brief Transfers ownership of the underlying ENet host. */
        ENetHostHandle(ENetHostHandle&& other) noexcept;
        /** @brief Transfers ownership of the underlying ENet host. */
        ENetHostHandle& operator=(ENetHostHandle&& other) noexcept;
        /** @brief Destroys the underlying ENet host, if still owned. */
        ~ENetHostHandle();

        /**
         * @brief Gets the local port this host is bound to.
         *
         * Only meaningful for hosts created via `CreateHost()`; always 0 for clients.
         *
         * @return The bound port, in host byte order.
         */
        [[nodiscard]] uint16_t getBoundPortProperty() const;

        /**
         * @brief Returns true if this handle still owns a live ENet host.
         *
         * @return true if valid.
         */
        [[nodiscard]] bool IsValid() const { return host_ != nullptr; }

        /**
         * @brief Begins connecting to a remote host.
         *
         * @param address Dotted IPv4 address or resolvable hostname to connect to.
         * @param port Remote port to connect to.
         * @param channelCount Number of channels to request for this connection.
         * @return The connecting peer. The connection is not established until a
         *         `ENET_EVENT_TYPE_CONNECT` event is observed via `Service()`.
         */
        [[nodiscard]] ENetPeer* Connect(const std::string& address, uint16_t port, size_t channelCount) const;

        /**
         * @brief Polls for a single ENet event, waiting at most `timeoutMs` milliseconds.
         *
         * @param timeoutMs Maximum time to wait, in milliseconds. Pass 0 for a non-blocking poll.
         * @param outEvent Receives the polled event.
         * @return > 0 if an event was returned, 0 if the timeout elapsed with no event,
         *         < 0 on error (matches `enet_host_service()`'s own return contract).
         */
        int Service(uint32_t timeoutMs, ENetEvent& outEvent) const;

        /**
         * @brief Sends data to a single peer on the given channel.
         *
         * @param peer The destination peer.
         * @param channel The channel to send on.
         * @param data Pointer to the payload bytes.
         * @param length Number of payload bytes.
         * @param flags ENet packet flags (e.g. `ENET_PACKET_FLAG_RELIABLE`).
         */
        void Send(ENetPeer* peer, uint8_t channel, const void* data, size_t length, uint32_t flags) const;

        /**
         * @brief Sends data to every currently connected peer on the given channel.
         *
         * @param channel The channel to send on.
         * @param data Pointer to the payload bytes.
         * @param length Number of payload bytes.
         * @param flags ENet packet flags (e.g. `ENET_PACKET_FLAG_RELIABLE`).
         */
        void Broadcast(uint8_t channel, const void* data, size_t length, uint32_t flags) const;

        /** @brief Sends any packets queued by `Send()`/`Broadcast()` immediately. */
        void Flush() const;

        /**
         * @brief Requests a graceful disconnect from a peer.
         *
         * @param peer The peer to disconnect.
         * @param data Application-defined data describing the reason, delivered with the
         *             resulting `ENET_EVENT_TYPE_DISCONNECT` event.
         */
        void Disconnect(ENetPeer* peer, uint32_t data) const;

    private:
        explicit ENetHostHandle(::ENetHost* host);

        ::ENetHost* host_;
    };
}
