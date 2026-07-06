// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/ENetLibrary.hpp"
#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <enet/enet.h>
#include <stdexcept>

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
    using Microsoft::Xna::Framework::Net::NetworkGamer;
    using Microsoft::Xna::Framework::Net::QualityOfService;

#ifndef __EMSCRIPTEN__

    namespace
    {
        constexpr uint16_t kDiscoveryPort = 61190;
        constexpr uint32_t kSearchWindowMs = 150;

        NetworkSession* registeredHost_ = nullptr;
        uint16_t registeredHostPort_ = 0;
        ENetSocket socket_ = ENET_SOCKET_NULL;

        // Set only for the duration of FindSessions(); PollOnce()/HandleReceived() append any
        // DiscoveryAnnounce arriving while a search is in progress. Left null between searches so
        // Poll() (the passive host-side responder, called every NetworkSession::Update()) safely
        // ignores stray announces.
        std::vector<AvailableNetworkSession>* currentResults_ = nullptr;

        ENetSocket EnsureSocket()
        {
            if (socket_ != ENET_SOCKET_NULL)
            {
                return socket_;
            }

            ENetLibrary::EnsureInitialized();

            ENetSocket sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
            if (sock == ENET_SOCKET_NULL)
            {
                throw std::runtime_error("Failed to create discovery UDP socket.");
            }
            enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);
            enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
            enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);

            ENetAddress address;
            address.host = ENET_HOST_ANY;
            address.port = kDiscoveryPort;
            if (enet_socket_bind(sock, &address) != 0)
            {
                enet_socket_destroy(sock);
                throw std::runtime_error("Failed to bind discovery UDP socket.");
            }

            socket_ = sock;
            return socket_;
        }

        // The gamertag to put on the wire for gamer. Mirrors ENetBackend.cpp's own
        // WireGamertagFor helper (duplicated rather than shared, since the two .cpp files are
        // otherwise independent) — see there for why LocalNetworkGamer needs this indirection.
        std::string WireGamertagFor(NetworkGamer* gamer)
        {
            if (auto* local = dynamic_cast<LocalNetworkGamer*>(gamer))
            {
                return local->getSignedInGamerProperty()->getGamertagProperty();
            }
            return gamer->getGamertagProperty();
        }

        void SendTo(ENetSocket sock, const ENetAddress& address, const std::vector<SharpRuntime::bytecs>& bytes)
        {
            // ENetBuffer's member order is platform-dependent (win32.h vs unix.h swap
            // data/dataLength), so field-by-field assignment is used instead of positional init.
            ENetBuffer buffer{};
            buffer.data = const_cast<SharpRuntime::bytecs*>(bytes.data());
            buffer.dataLength = bytes.size();
            enet_socket_send(sock, &address, &buffer, 1);
        }

        void ReplyToQuery(ENetSocket sock, const ENetAddress& queryingAddress)
        {
            if (registeredHost_ == nullptr)
            {
                return;
            }

            DiscoveryAnnounceMessage announce;
            announce.ConnectPort = registeredHostPort_;
            announce.CurrentGamerCount = registeredHost_->getAllGamersProperty().getCountProperty();
            announce.MaxGamers = registeredHost_->getMaxGamersProperty();
            announce.OpenPrivateSlots = registeredHost_->getPrivateGamerSlotsProperty();
            // Nothing in this codebase tracks per-gamer slot occupancy (NetworkGamer::
            // getIsPrivateSlotProperty() is a never-toggled stub), so private slots are reported
            // as fully open and public slots are simply "max minus private minus current".
            announce.OpenPublicSlots = std::max(
                0, announce.MaxGamers - announce.OpenPrivateSlots - announce.CurrentGamerCount
            );
            announce.HostGamertag = WireGamertagFor(registeredHost_->getHostProperty());
            announce.Properties = registeredHost_->getSessionPropertiesProperty();

            SendTo(sock, queryingAddress, NetDiscoveryProtocol::Encode(announce));
        }

        void HandleReceived(ENetSocket sock, const ENetAddress& fromAddress, const std::vector<SharpRuntime::bytecs>& data)
        {
            if (data.empty())
            {
                return;
            }

            // Task 1.4: this datagram comes off unauthenticated broadcast UDP (any LAN device, or
            // a spoofed source, can send here) - PeekTag/DecodeAnnounce throw std::runtime_error on
            // malformed input (see Tasks 1.1/1.2), which used to propagate straight out of Poll()
            // (called from every NetworkSession::Update()) or FindSessions() into the caller's own
            // game loop. Drop the offending datagram and keep discovery running instead.
            try
            {
                switch (NetDiscoveryProtocol::PeekTag(data))
                {
                    case DiscoveryMessageTag::Query:
                        ReplyToQuery(sock, fromAddress);
                        break;
                    case DiscoveryMessageTag::Announce:
                        if (currentResults_ != nullptr)
                        {
                            DiscoveryAnnounceMessage announce = NetDiscoveryProtocol::DecodeAnnounce(data);
                            std::array<char, 64> ip{};
                            enet_address_get_host_ip(&fromAddress, ip.data(), ip.size());
                            std::string address(ip.data());

                            // Dedup by connect port alone: the same query goes out via both broadcast
                            // and an explicit loopback copy, and on this machine a broadcast send can
                            // arrive back at our own socket by more than one path (each observed under
                            // a different source address) — so the same host's reply can legitimately
                            // arrive more than once. A given ephemeral ENet connect port uniquely
                            // identifies one hosted session, regardless of which local path its
                            // announce happened to take.
                            bool alreadyKnown = std::any_of(
                                currentResults_->begin(), currentResults_->end(),
                                [&](const AvailableNetworkSession& existing) {
                                    return existing.GetConnectPort() == announce.ConnectPort;
                                }
                            );
                            if (!alreadyKnown)
                            {
                                currentResults_->push_back(AvailableNetworkSession::CreateInternal(
                                    announce.CurrentGamerCount,
                                    announce.HostGamertag,
                                    announce.OpenPrivateSlots,
                                    announce.OpenPublicSlots,
                                    announce.Properties,
                                    QualityOfService::CreateInternal(),
                                    address,
                                    announce.ConnectPort
                                ));
                            }
                        }
                        break;
                }
            }
            catch (const std::exception&)
            {
                // Malformed/crafted discovery datagram - drop it and keep discovery running.
            }
        }

        // Task 1.3: currentResults_ points at a stack-local std::vector owned by FindSessions()'s
        // own call frame (see below). Resetting it to nullptr via a plain assignment at the end
        // of FindSessions() is only reached on the normal, no-exception path - if
        // HandleReceived/DecodeAnnounce throws mid-poll (e.g. a still-malformed packet reaching
        // some future decode path, or any other exception), the throw unwinds straight past that
        // reset, leaving currentResults_ dangling at the about-to-be-destroyed `results` local.
        // The *next* Poll() call (driven from every NetworkSession::Update()) then writes through
        // it. This guard's destructor runs during unwinding too, so the reset always happens.
        class CurrentResultsGuard
        {
        public:
            explicit CurrentResultsGuard(std::vector<AvailableNetworkSession>* results)
            {
                currentResults_ = results;
            }
            ~CurrentResultsGuard()
            {
                currentResults_ = nullptr;
            }
            CurrentResultsGuard(const CurrentResultsGuard&) = delete;
            CurrentResultsGuard& operator=(const CurrentResultsGuard&) = delete;
        };

        // Waits up to timeoutMs for one datagram and processes it. Returns true if a message was
        // handled (so Poll()'s drain loop can keep going without waiting), false on timeout.
        bool PollOnce(ENetSocket sock, uint32_t timeoutMs)
        {
            enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
            enet_socket_wait(sock, &condition, timeoutMs);
            if (!(condition & ENET_SOCKET_WAIT_RECEIVE))
            {
                return false;
            }

            ENetAddress fromAddress{};
            std::array<SharpRuntime::bytecs, 1500> buffer{};
            ENetBuffer enetBuffer{};
            enetBuffer.data = buffer.data();
            enetBuffer.dataLength = buffer.size();
            int received = enet_socket_receive(sock, &fromAddress, &enetBuffer, 1);
            if (received <= 0)
            {
                return false;
            }

            std::vector<SharpRuntime::bytecs> data(buffer.begin(), buffer.begin() + received);
            HandleReceived(sock, fromAddress, data);
            return true;
        }
    }

    void ENetDiscoveryService::RegisterHost(NetworkSession* session, uint16_t connectPort)
    {
        EnsureSocket();
        registeredHost_ = session;
        registeredHostPort_ = connectPort;
    }

    void ENetDiscoveryService::UnregisterHost(NetworkSession* session)
    {
        if (registeredHost_ == session)
        {
            registeredHost_ = nullptr;
            registeredHostPort_ = 0;
        }
    }

    void ENetDiscoveryService::Poll()
    {
        if (socket_ == ENET_SOCKET_NULL)
        {
            return;
        }
        while (PollOnce(socket_, 0)) { }
    }

    std::vector<AvailableNetworkSession> ENetDiscoveryService::FindSessions(NetworkSessionType sessionTypeFilter)
    {
        if (sessionTypeFilter != NetworkSessionType::SystemLink)
        {
            return {};
        }

        ENetSocket sock = EnsureSocket();

        DiscoveryQueryMessage query;
        query.SessionTypeFilter = sessionTypeFilter;
        auto bytes = NetDiscoveryProtocol::Encode(query);

        ENetAddress broadcastAddress;
        broadcastAddress.host = ENET_HOST_BROADCAST;
        broadcastAddress.port = kDiscoveryPort;
        SendTo(sock, broadcastAddress, bytes);

        ENetAddress loopbackAddress{};
        enet_address_set_host_ip(&loopbackAddress, "127.0.0.1");
        loopbackAddress.port = kDiscoveryPort;
        SendTo(sock, loopbackAddress, bytes);

        std::vector<AvailableNetworkSession> results;
        CurrentResultsGuard resultsGuard(&results);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kSearchWindowMs);
        while (true)
        {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                break;
            }
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            PollOnce(sock, static_cast<uint32_t>(remaining));
        }

        return results;
    }

#else // __EMSCRIPTEN__

    // Raw UDP broadcast/unicast has no equivalent on the Web platform at all (no browser, and no
    // Node.js `ws` package, can send a raw datagram) - this is a permanent platform constraint,
    // not a TODO (see NEXT.md and this class's own header doc comment).

    void ENetDiscoveryService::RegisterHost(NetworkSession*, uint16_t) { }
    void ENetDiscoveryService::UnregisterHost(NetworkSession*) { }
    void ENetDiscoveryService::Poll() { }

    std::vector<AvailableNetworkSession> ENetDiscoveryService::FindSessions(NetworkSessionType)
    {
        return {};
    }

#endif // __EMSCRIPTEN__
}
