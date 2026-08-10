// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/ENetLibrary.hpp"

#include <stdexcept>

namespace CNA::Internal::Net
{
    ENetHostHandle::ENetHostHandle(::ENetHost* host)
        : host_(host)
    {
    }

    ENetHostHandle ENetHostHandle::CreateHost(uint16_t port, size_t maxPeers, size_t channelLimit)
    {
        ENetLibrary::EnsureInitialized();

        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = port;

        ::ENetHost* host = enet_host_create(&address, maxPeers, channelLimit, 0, 0);
        if (!host)
        {
            throw std::runtime_error("Failed to create ENet host.");
        }
        return ENetHostHandle(host);
    }

    ENetHostHandle ENetHostHandle::CreateClient(size_t channelLimit)
    {
        ENetLibrary::EnsureInitialized();

        ::ENetHost* host = enet_host_create(nullptr, 1, channelLimit, 0, 0);
        if (!host)
        {
            throw std::runtime_error("Failed to create ENet client host.");
        }
        return ENetHostHandle(host);
    }

    ENetHostHandle::ENetHostHandle(ENetHostHandle&& other) noexcept
        : host_(other.host_)
    {
        other.host_ = nullptr;
    }

    ENetHostHandle& ENetHostHandle::operator=(ENetHostHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (host_)
            {
                enet_host_destroy(host_);
            }
            host_ = other.host_;
            other.host_ = nullptr;
        }
        return *this;
    }

    ENetHostHandle::~ENetHostHandle()
    {
        if (host_)
        {
            enet_host_destroy(host_);
        }
    }

    uint16_t ENetHostHandle::getBoundPortProperty() const
    {
        return host_ ? host_->address.port : 0;
    }

    ENetPeer* ENetHostHandle::Connect(const std::string& address, uint16_t port, size_t channelCount) const
    {
        ENetAddress enetAddress;
        if (enet_address_set_host_ip(&enetAddress, address.c_str()) != 0 &&
            enet_address_set_host(&enetAddress, address.c_str()) != 0)
        {
            throw std::runtime_error("Failed to resolve ENet host address: " + address);
        }
        enetAddress.port = port;

        ENetPeer* peer = enet_host_connect(host_, &enetAddress, channelCount, 0);
        if (!peer)
        {
            throw std::runtime_error("Failed to initiate ENet connection.");
        }
        return peer;
    }

    int ENetHostHandle::Service(uint32_t timeoutMs, ENetEvent& outEvent) const
    {
        return enet_host_service(host_, &outEvent, timeoutMs);
    }

    void ENetHostHandle::Send(ENetPeer* peer, uint8_t channel, const void* data, size_t length, uint32_t flags) const
    {
        ENetPacket* packet = enet_packet_create(data, length, flags);
        if (!packet)
        {
            return;
        }
        if (enet_peer_send(peer, channel, packet) < 0)
        {
            enet_packet_destroy(packet);
        }
    }

    void ENetHostHandle::Broadcast(uint8_t channel, const void* data, size_t length, uint32_t flags) const
    {
        ENetPacket* packet = enet_packet_create(data, length, flags);
        if (!packet)
        {
            return;
        }
        enet_host_broadcast(host_, channel, packet);
    }

    void ENetHostHandle::Flush() const
    {
        enet_host_flush(host_);
    }

    void ENetHostHandle::Disconnect(ENetPeer* peer, uint32_t data) const
    {
        enet_peer_disconnect(peer, data);
    }
}
