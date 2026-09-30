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
#include <atomic>
#include <chrono>
#include <enet/enet.h>
#include <memory>
#include <stdexcept>
#include <thread>

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

        // One host found by a search: its announce, round trip, and the arrivals of its QoS probe
        // train (GSP-L6), timed as they arrive while FindSessions() waits on the socket.
        struct Found
        {
            DiscoveryAnnounceMessage Announce;
            std::string Address;
            double RoundtripMs = 0.0;
            int FirstIndex = -1, LastIndex = -1;
            std::chrono::steady_clock::time_point FirstArrival, LastArrival;
            // What the host's responder measured of this machine's train to it.
            int UpstreamBytesPerSecond = 0;
        };

        // Set only for the duration of FindSessions(); PollOnce()/HandleReceived() append any
        // DiscoveryAnnounce arriving while a search is in progress. Left null between searches so
        // Poll() (the passive host-side responder, called every NetworkSession::Update()) safely
        // ignores stray announces.
        std::vector<Found>* currentResults_ = nullptr;
        // Probe arrivals for hosts whose announce has not arrived yet (UDP may reorder).
        std::vector<std::pair<uint16_t, std::chrono::steady_clock::time_point>>* earlyProbes_ = nullptr;

        // Records one probe of a host's first train; a repeated train (the host answered the
        // broadcast and the loopback copy of the query) is ignored.
        void RecordProbe(Found& found, const DiscoveryQosProbeMessage& probe, std::chrono::steady_clock::time_point at)
        {
            if (found.FirstIndex < 0) { found.FirstIndex = found.LastIndex = probe.Index; found.FirstArrival = found.LastArrival = at; return; }
            if (probe.Index <= found.LastIndex) return;
            found.LastIndex = probe.Index; found.LastArrival = at;
        }

        // Bytes per second of a train: the probes after the first over the time they took to arrive.
        void SendTo(ENetSocket sock, const ENetAddress& address, const std::vector<SharpRuntime::bytecs>& bytes);

        int TrainRate(int firstIndex, int lastIndex, std::chrono::steady_clock::time_point first, std::chrono::steady_clock::time_point last)
        {
            if (firstIndex < 0 || lastIndex <= firstIndex) return 0;
            const double seconds = std::max(1e-6, std::chrono::duration<double>(last - first).count());
            const double rate = static_cast<double>(lastIndex - firstIndex) * static_cast<double>(kQosProbeBytes) / seconds;
            return static_cast<int>(std::min(rate, 2147483647.0));
        }

        int DownstreamEstimate(const Found& found)
        {
            return TrainRate(found.FirstIndex, found.LastIndex, found.FirstArrival, found.LastArrival);
        }

        // A hosting machine's upstream responder: its own socket and thread, so a querier's train is
        // timed as it arrives, not when the game next polls discovery at a frame boundary.
        class UpstreamResponder
        {
        public:
            explicit UpstreamResponder(uint16_t connectPort) : connectPort_(connectPort)
            {
                ENetLibrary::EnsureInitialized();
                socket_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
                if (socket_ == ENET_SOCKET_NULL) return;
                ENetAddress address{};
                address.host = ENET_HOST_ANY;
                address.port = 0;
                if (enet_socket_bind(socket_, &address) != 0 || enet_socket_get_address(socket_, &address) != 0)
                {
                    enet_socket_destroy(socket_);
                    socket_ = ENET_SOCKET_NULL;
                    return;
                }
                port_ = address.port;
                thread_ = std::thread([this] { Run(); });
            }
            ~UpstreamResponder()
            {
                stop_ = true;
                if (thread_.joinable()) thread_.join();
                if (socket_ != ENET_SOCKET_NULL) enet_socket_destroy(socket_);
            }
            UpstreamResponder(const UpstreamResponder&) = delete;
            UpstreamResponder& operator=(const UpstreamResponder&) = delete;
            /** Port queriers send their trains to; 0 when the responder could not start. */
            uint16_t Port() const { return port_; }

        private:
            struct Train
            {
                ENetAddress From{};
                int FirstIndex = -1, LastIndex = -1, Count = 0;
                std::chrono::steady_clock::time_point First, Last, Started;
            };
            static constexpr std::size_t MaxTrains = 64;
            static constexpr auto TrainWindow = std::chrono::milliseconds(250);

            void Report(const Train& train)
            {
                DiscoveryUpstreamReportMessage report;
                report.ConnectPort = connectPort_;
                report.BytesPerSecond = TrainRate(train.FirstIndex, train.LastIndex, train.First, train.Last);
                SendTo(socket_, train.From, NetDiscoveryProtocol::Encode(report));
            }

            void Run()
            {
                std::vector<Train> trains;
                while (!stop_)
                {
                    enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
                    if (enet_socket_wait(socket_, &condition, 20) == 0 && (condition & ENET_SOCKET_WAIT_RECEIVE))
                    {
                        ENetAddress from{};
                        std::array<SharpRuntime::bytecs, 1500> buffer{};
                        ENetBuffer enetBuffer{};
                        enetBuffer.data = buffer.data();
                        enetBuffer.dataLength = buffer.size();
                        const int received = enet_socket_receive(socket_, &from, &enetBuffer, 1);
                        const auto at = std::chrono::steady_clock::now();
                        if (received == static_cast<int>(kQosProbeBytes) && buffer[0] == static_cast<SharpRuntime::bytecs>(DiscoveryMessageTag::UpstreamProbe))
                        {
                            try
                            {
                                const auto probe = NetDiscoveryProtocol::DecodeQosProbe(std::vector<SharpRuntime::bytecs>(buffer.begin(), buffer.begin() + received));
                                if (probe.ConnectPort == connectPort_) Record(trains, from, probe, at);
                            }
                            catch (const std::exception&)
                            {
                                // A malformed datagram off the LAN: dropped.
                            }
                        }
                    }
                    // A train that lost its tail is measured from what arrived.
                    const auto now = std::chrono::steady_clock::now();
                    for (auto train = trains.begin(); train != trains.end();)
                    {
                        if (now - train->Started < TrainWindow) { ++train; continue; }
                        Report(*train);
                        train = trains.erase(train);
                    }
                }
            }

            void Record(std::vector<Train>& trains, const ENetAddress& from, const DiscoveryQosProbeMessage& probe, std::chrono::steady_clock::time_point at)
            {
                auto train = std::find_if(trains.begin(), trains.end(),
                    [&](const Train& known) { return known.From.host == from.host && known.From.port == from.port; });
                if (train == trains.end())
                {
                    if (trains.size() >= MaxTrains) return;
                    Train fresh;
                    fresh.From = from;
                    fresh.Count = probe.Count;
                    fresh.FirstIndex = fresh.LastIndex = probe.Index;
                    fresh.First = fresh.Last = fresh.Started = at;
                    trains.push_back(fresh);
                    return;
                }
                if (probe.Index <= train->LastIndex) return;
                train->LastIndex = probe.Index;
                train->Last = at;
                if (probe.Index + 1 == train->Count)
                {
                    Report(*train);
                    trains.erase(train);
                }
            }

            uint16_t connectPort_ = 0, port_ = 0;
            ENetSocket socket_ = ENET_SOCKET_NULL;
            std::atomic<bool> stop_{false};
            std::thread thread_;
        };
        std::unique_ptr<UpstreamResponder> responder_;
        // Hosts this search already sent an upstream train to (a duplicated query brings two invitations).
        std::vector<uint16_t> upstreamTrains_;

        // Task 4.2: when currentResults_ is set (a search is in progress), the wall-clock moment
        // FindSessions() sent its Query - used to measure a real round-trip time for each Announce
        // reply's QualityOfService, instead of always reporting the same hardcoded stub.
        std::chrono::steady_clock::time_point queryStartTime_;

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
            // Task 6.5: lets multiple independent OS processes on the same machine each bind this
            // exact well-known port (kDiscoveryPort) simultaneously - required so two genuinely
            // separate NetworkSession-hosting processes (see TwoProcessLoopbackTest.cpp, where
            // both the host and client role each independently call NetworkSession::Create with
            // NetworkSessionType::SystemLink, and therefore each independently reach this exact
            // bind) don't fail to bind at all. Confirmed empirically reliable on Linux (this
            // project's primary dev/CI target): POSIX UDP requires SO_REUSEADDR on *every* socket
            // sharing a port for the bind itself to succeed on all of them (a socket without it
            // cannot coexist with one that has it - directly observed while building Task 6.3's
            // own test harness, where a plain, non-REUSEADDR blocking socket failed to bind
            // against a REUSEADDR socket that already held the port), and TwoProcessLoopbackTest's
            // own repeated real two-process runs are direct, ongoing proof this exact scenario
            // works in practice, not just in theory. Windows' SO_REUSEADDR has different, looser,
            // well-documented semantics than POSIX (it can permit binding over an already-bound
            // socket regardless of that other socket's own REUSEADDR state) - not independently
            // verified on Windows in this sandboxed, Linux-only dev environment, but if anything
            // this makes bind() succeeding there less of a concern, not more. Web (Emscripten) has
            // this entire class permanently disabled already (see the class's own doc comment) -
            // moot there. Android is Linux-kernel-based and expected to match POSIX, but likewise
            // not independently verified here.
            //
            // Which specific socket actually *receives* a given incoming datagram when multiple
            // sockets share one port is inherently OS-arbitrary either way (unlike SO_REUSEPORT,
            // which isn't used here and isn't needed for this client-queries/host-replies model -
            // there is no per-connection load-balancing to distribute). PollOnce's own dedup logic
            // just below (see "Dedup by connect port alone") already treats a query/reply
            // potentially arriving more than once, via more than one local path, as an expected,
            // handled case rather than an error - the correct mitigation for that arbitrariness,
            // not a workaround for a bug.
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

        // The gamertag to put on the wire for gamer: a local gamer's signed-in profile, else the
        // received remote identity. Mirrors ENetBackend.cpp's own WireGamertagFor helper
        // (duplicated rather than shared, since the two .cpp files are otherwise independent).
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

        void ReplyToQuery(ENetSocket sock, const ENetAddress& queryingAddress, const DiscoveryQueryMessage& query)
        {
            if (registeredHost_ == nullptr)
            {
                return;
            }

            // Task 1.5: SessionTypeFilter was written by every querying client but never actually
            // read back out server-side - a registered host used to answer *any* Query datagram
            // regardless of the claimed filter. Only reply if this host's own session type is what
            // the client is actually searching for.
            if (query.SessionTypeFilter != registeredHost_->getSessionTypeProperty())
            {
                return;
            }

            DiscoveryAnnounceMessage announce;
            announce.ConnectPort = registeredHostPort_;
            announce.CurrentGamerCount = registeredHost_->getAllGamersProperty().getCountProperty();
            announce.MaxGamers = registeredHost_->getMaxGamersProperty();
            announce.OpenPrivateSlots = registeredHost_->getPrivateGamerSlotsProperty();
            // SystemLink does not track per-gamer slot occupancy (only online sessions set
            // NetworkGamer::getIsPrivateSlotProperty()), so private slots are reported as fully
            // open and public slots are simply "max minus private minus current".
            announce.OpenPublicSlots = std::max(
                0, announce.MaxGamers - announce.OpenPrivateSlots - announce.CurrentGamerCount
            );
            // Task 6.7: registeredHost_->getHostProperty() has no null-check here, but is
            // confirmed always non-null in practice: RegisterHost (the only way registeredHost_
            // ever becomes non-null) is only ever called from ENetBackend::StartHosting, itself
            // only ever called at the very end of NetworkSession's own constructor - by which
            // point host_ (= localGamers_[0]) has already been set, or the constructor itself
            // already threw beforehand (an empty local-gamer list makes that exact indexing throw
            // first - see Task 6.1). Every real Poll()/FindSessions() call that reaches this line
            // (ENetDiscoveryServiceTest's FindSessionsDiscoversRegisteredHost and
            // ReplyToQueryOnlyAnswersWhenSessionTypeFilterMatchesTheHost) already exercises this
            // path on every test run; a future refactor that broke this ordering would surface as
            // an immediate crash there, not a silent gap.
            announce.HostGamertag = WireGamertagFor(registeredHost_->getHostProperty());
            announce.Properties = registeredHost_->getSessionPropertiesProperty();

            SendTo(sock, queryingAddress, NetDiscoveryProtocol::Encode(announce));
            // Back to back, so their spacing at the querier measures the path's bandwidth.
            DiscoveryQosProbeMessage probe;
            probe.ConnectPort = announce.ConnectPort;
            for (uint8_t index = 0; index < kQosProbeCount; ++index)
            {
                probe.Index = index;
                SendTo(sock, queryingAddress, NetDiscoveryProtocol::Encode(probe));
            }
            // And the other direction: the querier times nothing it sends, so the responder does.
            if (responder_ && responder_->Port() != 0)
            {
                DiscoveryUpstreamInviteMessage invite;
                invite.ConnectPort = announce.ConnectPort;
                invite.ResponderPort = responder_->Port();
                SendTo(sock, queryingAddress, NetDiscoveryProtocol::Encode(invite));
            }
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
                        ReplyToQuery(sock, fromAddress, NetDiscoveryProtocol::DecodeQuery(data));
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
                                [&](const Found& existing) { return existing.Announce.ConnectPort == announce.ConnectPort; }
                            );
                            if (!alreadyKnown)
                            {
                                // Task 4.2: a real measurement (the wall-clock round-trip between
                                // sending the Query and receiving this specific Announce reply),
                                // not the always-hardcoded-stub QualityOfService::CreateInternal().
                                auto elapsed = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
                                    std::chrono::steady_clock::now() - queryStartTime_
                                );
                                Found found;
                                found.Announce = announce;
                                found.Address = address;
                                found.RoundtripMs = elapsed.count();
                                // Probes that overtook the announce.
                                for (const auto& [port, at] : *earlyProbes_)
                                    if (port == announce.ConnectPort)
                                    {
                                        DiscoveryQosProbeMessage early;
                                        early.ConnectPort = port;
                                        early.Index = static_cast<uint8_t>(found.FirstIndex < 0 ? 0 : found.LastIndex + 1);
                                        RecordProbe(found, early, at);
                                    }
                                currentResults_->push_back(std::move(found));
                            }
                        }
                        break;
                    case DiscoveryMessageTag::UpstreamInvite:
                        if (currentResults_ != nullptr)
                        {
                            const auto invite = NetDiscoveryProtocol::DecodeUpstreamInvite(data);
                            if (std::find(upstreamTrains_.begin(), upstreamTrains_.end(), invite.ConnectPort) != upstreamTrains_.end()
                                || upstreamTrains_.size() >= 64)
                                break;
                            upstreamTrains_.push_back(invite.ConnectPort);
                            ENetAddress responder = fromAddress;
                            responder.port = invite.ResponderPort;
                            DiscoveryQosProbeMessage probe;
                            probe.ConnectPort = invite.ConnectPort;
                            for (uint8_t index = 0; index < kQosProbeCount; ++index)
                            {
                                probe.Index = index;
                                SendTo(sock, responder, NetDiscoveryProtocol::EncodeUpstreamProbe(probe));
                            }
                        }
                        break;
                    case DiscoveryMessageTag::UpstreamReport:
                        if (currentResults_ != nullptr)
                        {
                            const auto report = NetDiscoveryProtocol::DecodeUpstreamReport(data);
                            auto found = std::find_if(currentResults_->begin(), currentResults_->end(),
                                [&](const Found& existing) { return existing.Announce.ConnectPort == report.ConnectPort; });
                            if (found != currentResults_->end()) found->UpstreamBytesPerSecond = report.BytesPerSecond;
                        }
                        break;
                    case DiscoveryMessageTag::QosProbe:
                        if (currentResults_ != nullptr)
                        {
                            const auto at = std::chrono::steady_clock::now();
                            const auto probe = NetDiscoveryProtocol::DecodeQosProbe(data);
                            auto found = std::find_if(currentResults_->begin(), currentResults_->end(),
                                [&](const Found& existing) { return existing.Announce.ConnectPort == probe.ConnectPort; });
                            if (found != currentResults_->end()) RecordProbe(*found, probe, at);
                            else if (earlyProbes_->size() < 64) earlyProbes_->emplace_back(probe.ConnectPort, at);
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
            CurrentResultsGuard(std::vector<Found>* results, std::vector<std::pair<uint16_t, std::chrono::steady_clock::time_point>>* early)
            {
                earlyProbes_ = early;
                currentResults_ = results;
            }
            ~CurrentResultsGuard()
            {
                currentResults_ = nullptr;
                earlyProbes_ = nullptr;
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
        responder_ = std::make_unique<UpstreamResponder>(connectPort);
    }

    void ENetDiscoveryService::UnregisterHost(NetworkSession* session)
    {
        if (registeredHost_ == session)
        {
            registeredHost_ = nullptr;
            registeredHostPort_ = 0;
            responder_.reset();
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

    QualityOfService ENetDiscoveryService::Measured(double roundtripMs, int downstream, int upstream)
    {
        auto measured = QualityOfService::CreateInternal(System::TimeSpan::FromMilliseconds(roundtripMs), downstream);
        measured.bytesPerSecondUpstream_ = upstream;
        return measured;
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

        queryStartTime_ = std::chrono::steady_clock::now(); // Task 4.2

        ENetAddress broadcastAddress;
        broadcastAddress.host = ENET_HOST_BROADCAST;
        broadcastAddress.port = kDiscoveryPort;
        SendTo(sock, broadcastAddress, bytes);

        ENetAddress loopbackAddress{};
        enet_address_set_host_ip(&loopbackAddress, "127.0.0.1");
        loopbackAddress.port = kDiscoveryPort;
        SendTo(sock, loopbackAddress, bytes);

        std::vector<Found> found;
        std::vector<std::pair<uint16_t, std::chrono::steady_clock::time_point>> early;
        CurrentResultsGuard resultsGuard(&found, &early);
        upstreamTrains_.clear();

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

        std::vector<AvailableNetworkSession> results;
        for (const auto& host : found)
        {
            const auto& announce = host.Announce;
            results.push_back(AvailableNetworkSession::CreateInternal(
                announce.CurrentGamerCount,
                announce.HostGamertag,
                announce.OpenPrivateSlots,
                announce.OpenPublicSlots,
                announce.Properties,
                Measured(host.RoundtripMs, DownstreamEstimate(host), host.UpstreamBytesPerSecond),
                host.Address,
                announce.ConnectPort,
                // Every listing found here is SystemLink: FindSessions returns early for any other type.
                NetworkSessionType::SystemLink
            ));
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

    // Platform-independent: the browser build finds no sessions, but NetworkSession filters the
    // (empty) result through the same rule.
    std::vector<AvailableNetworkSession> ENetDiscoveryService::Matching(std::vector<AvailableNetworkSession> found,
        int localGamers, const Microsoft::Xna::Framework::Net::NetworkSessionProperties& search)
    {
        std::vector<AvailableNetworkSession> matching;
        for (auto& session : found)
        {
            if (session.getOpenPublicGamerSlotsProperty() < localGamers) continue;
            bool matches = true;
            for (int index = 0; index < search.getCountProperty() && matches; ++index)
            {
                const auto wanted = search.getItem(index);
                matches = !wanted.has_value() || session.getSessionPropertiesProperty().getItem(index) == wanted;
            }
            if (matches) matching.push_back(std::move(session));
        }
        return matching;
    }
}
