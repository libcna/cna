// SPDX-License-Identifier: MS-PL
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/ENetLibrary.hpp"
#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "CNA/Internal/Net/SearchAddress.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Net/SystemLinkDiscovery.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"
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
#include <random>
#include <stdexcept>
#include <thread>

#if defined(__APPLE__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::GamerServices::NetworkNotAvailableException;
    using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
    using Microsoft::Xna::Framework::Net::NetworkGamer;
    using Microsoft::Xna::Framework::Net::QualityOfService;

#ifndef __EMSCRIPTEN__

    namespace
    {
        constexpr uint16_t kDiscoveryPort = CNA::Net::SystemLinkDiscovery::DiscoveryPort;
        constexpr uint32_t kSearchWindowMs = 150;

        NetworkSession* registeredHost_ = nullptr;
        uint16_t registeredHostPort_ = 0;
        uint64_t registeredHostInstance_ = 0;
        ENetSocket socket_ = ENET_SOCKET_NULL;

        // One host found by a search: its announce, round trip, and the arrivals of its QoS probe
        // train (GSP-L6), timed as they arrive while FindSessions() waits on the socket.
        struct Found
        {
            DiscoveryAnnounceMessage Announce;
            std::string Address;
            // The other addresses this host's duplicate announces came from: a host on this machine
            // answers the broadcast and the loopback copy of a query, and each reply can arrive
            // under a different source address. Its probes, invitation and report each follow one
            // of those paths.
            std::vector<std::string> Aliases;
            double RoundtripMs = 0.0;
            // One probe train per path the host answered by: two paths' trains never mix, since
            // splicing them would time one train's tail by another's start.
            // Probes carry no train identifier, so a train is told apart by its indices: each index
            // arrives once per train, in any order.
            struct ProbeTrain
            {
                std::string Address;
                // The sender's train identifier; 0 from an older host, whose trains are told apart
                // by their indices alone.
                std::uint32_t TrainId = 0;
                // Indices received; Received counts them (the span alone would rank two of eight
                // probes like eight).
                std::uint32_t Seen = 0;
                int Received = 0;
                int MinIndex = 0, MaxIndex = 0;
                // An index arrived a second time: the host's repeated train (it answered two
                // copies of the query) has begun on this path, and its probes would time this
                // train's tail by the repeat's.
                bool Closed = false;
                // The first and the last arrival, whatever their order.
                std::chrono::steady_clock::time_point FirstArrival, LastArrival;
            };
            std::vector<ProbeTrain> Trains;
            // What the host's responder measured of this machine's train to it.
            int UpstreamBytesPerSecond = 0;
        };

        // Set only for the duration of FindSessions(); PollOnce()/HandleReceived() append any
        // DiscoveryAnnounce arriving while a search is in progress. Left null between searches so
        // Poll() (the passive host-side responder, called every NetworkSession::Update()) safely
        // ignores stray announces.
        std::vector<Found>* currentResults_ = nullptr;
        // A probe arrival for a host whose announce has not arrived yet (UDP may reorder). Its
        // source address is kept with the port, because two hosts can share a fixed port.
        struct EarlyProbe
        {
            uint16_t ConnectPort = 0;
            std::string Address;
            uint8_t Index = 0;
            std::uint32_t TrainId = 0;
            std::chrono::steady_clock::time_point At;
        };
        std::vector<EarlyProbe>* earlyProbes_ = nullptr;

        // A fresh nonzero identifier for each train this process sends.
        std::uint32_t NextTrainId()
        {
            static std::mt19937 trains{std::random_device{}()};
            std::uint32_t id = 0;
            do { id = trains(); } while (id == 0);
            return id;
        }

        bool AnsweredFrom(const Found& found, const std::string& address)
        {
            return found.Address == address
                || std::find(found.Aliases.begin(), found.Aliases.end(), address) != found.Aliases.end();
        }

        // Records one probe of the first train that came by one of a host's paths; a repeated train
        // on that path (the host answered the broadcast and the loopback copy of the query) is
        // ignored from the first index it repeats.
        void RecordProbe(Found& found, const std::string& address, const DiscoveryQosProbeMessage& probe,
                         std::chrono::steady_clock::time_point at)
        {
            if (probe.Index >= kQosProbeCount) return;
            // An identified train is its own entry: a repeat has another identifier, and a
            // duplicated probe is only ignored. An older host's trains share one entry per path.
            auto train = std::find_if(found.Trains.begin(), found.Trains.end(),
                [&](const Found::ProbeTrain& candidate)
                { return candidate.Address == address && candidate.TrainId == probe.TrainId; });
            if (train == found.Trains.end())
            {
                if (found.Trains.size() >= 32) return;
                found.Trains.push_back(Found::ProbeTrain{address, probe.TrainId});
                train = std::prev(found.Trains.end());
            }
            if (train->Closed) return;
            const std::uint32_t bit = 1u << probe.Index;
            if ((train->Seen & bit) != 0)
            {
                if (probe.TrainId == 0) train->Closed = true;
                return;
            }
            train->Seen |= bit;
            if (train->Received++ == 0)
            {
                train->MinIndex = train->MaxIndex = probe.Index;
                train->FirstArrival = at;
            }
            train->MinIndex = std::min<int>(train->MinIndex, probe.Index);
            train->MaxIndex = std::max<int>(train->MaxIndex, probe.Index);
            train->LastArrival = at;
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

        // The host's train with the most probes received, then the widest; on a tie, the one by
        // the address it is listed with. Its rate is the received probes after the first over the
        // time between the first and the last arrival, so reordering inside the train does not
        // change it and a lost probe is not counted as received.
        int DownstreamEstimate(const Found& found)
        {
            const Found::ProbeTrain* best = nullptr;
            for (const Found::ProbeTrain& train : found.Trains)
            {
                if (best == nullptr) { best = &train; continue; }
                const int span = train.MaxIndex - train.MinIndex;
                const int bestSpan = best->MaxIndex - best->MinIndex;
                if (train.Received != best->Received ? train.Received > best->Received
                    : span != bestSpan ? span > bestSpan
                    : train.Address == found.Address)
                    best = &train;
            }
            if (best == nullptr || best->Received < 2) return 0;
            return TrainRate(0, best->Received - 1, best->FirstArrival, best->LastArrival);
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
            // One train from one querier socket, told apart by its identifier (an older querier
            // sends 0). Its rate counts the probes that arrived, timed from the first arrival to the
            // last, so a reordered or lost probe neither ends it early nor counts as received.
            struct Train
            {
                ENetAddress From{};
                std::uint32_t TrainId = 0;
                std::uint32_t Seen = 0;
                int Received = 0, Count = 0;
                std::chrono::steady_clock::time_point First, Last;
            };
            static constexpr std::size_t MaxTrains = 64;
            // A train is reported once every probe arrived, or this long after its last arrival
            // (its tail was lost); the receive wait shortens to the next such moment.
            static constexpr auto TrainIdle = std::chrono::milliseconds(20);

            // Identified trains already reported: a straggler or duplicate of one must not open a
            // new record whose report would replace the measurement.
            struct ReportedTrain
            {
                enet_uint32 Host = 0;
                enet_uint16 Port = 0;
                std::uint32_t TrainId = 0;
            };
            std::vector<ReportedTrain> reported_;

            void Report(const Train& train)
            {
                if (train.TrainId != 0)
                {
                    if (reported_.size() >= MaxTrains) reported_.erase(reported_.begin());
                    reported_.push_back(ReportedTrain{train.From.host, train.From.port, train.TrainId});
                }
                // One probe measures nothing; reporting 0 would only overwrite a real result.
                if (train.Received < 2) return;
                DiscoveryUpstreamReportMessage report;
                report.ConnectPort = connectPort_;
                report.BytesPerSecond = TrainRate(0, train.Received - 1, train.First, train.Last);
                SendTo(socket_, train.From, NetDiscoveryProtocol::Encode(report));
            }

            void Run()
            {
                std::vector<Train> trains;
                while (!stop_)
                {
                    enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
                    enet_uint32 waitMs = 20;
                    const auto before = std::chrono::steady_clock::now();
                    for (const Train& train : trains)
                    {
                        const auto due = std::chrono::duration_cast<std::chrono::milliseconds>(
                            train.Last + TrainIdle - before).count() + 1;
                        waitMs = std::min<enet_uint32>(waitMs, static_cast<enet_uint32>(std::max<long long>(due, 1)));
                    }
                    if (enet_socket_wait(socket_, &condition, waitMs) == 0 && (condition & ENET_SOCKET_WAIT_RECEIVE))
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
                        if (now - train->Last < TrainIdle) { ++train; continue; }
                        Report(*train);
                        train = trains.erase(train);
                    }
                }
            }

            void Record(std::vector<Train>& trains, const ENetAddress& from, const DiscoveryQosProbeMessage& probe, std::chrono::steady_clock::time_point at)
            {
                if (probe.Index >= 32) return;
                if (probe.TrainId != 0 && std::any_of(reported_.begin(), reported_.end(),
                        [&](const ReportedTrain& done)
                        { return done.Host == from.host && done.Port == from.port && done.TrainId == probe.TrainId; }))
                    return;
                auto train = std::find_if(trains.begin(), trains.end(),
                    [&](const Train& known)
                    {
                        return known.From.host == from.host && known.From.port == from.port
                            && known.TrainId == probe.TrainId;
                    });
                const std::uint32_t bit = 1u << probe.Index;
                if (train != trains.end() && (train->Seen & bit) != 0)
                {
                    // An identified train's duplicate is ignored. An older querier's index seen
                    // again is its next train: report this one, then time the new one.
                    if (probe.TrainId != 0) return;
                    Report(*train);
                    trains.erase(train);
                    train = trains.end();
                }
                if (train == trains.end())
                {
                    if (trains.size() >= MaxTrains) return;
                    Train fresh;
                    fresh.From = from;
                    fresh.TrainId = probe.TrainId;
                    fresh.Count = probe.Count;
                    fresh.First = at;
                    trains.push_back(fresh);
                    train = std::prev(trains.end());
                }
                train->Seen |= bit;
                ++train->Received;
                train->Last = at;
                if (train->Received == train->Count)
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
        // Hosts this search already sent an upstream train to: a duplicated query brings one host's
        // invitation twice, by either of its paths, while two hosts on the same fixed connect port
        // -- even with the same ephemeral responder port, chosen independently on two machines --
        // each get one. A host is its listing when the invitation's address belongs to one, else
        // that address.
        struct UpstreamTrain
        {
            std::string Host;
            uint16_t ConnectPort = 0;
            uint16_t ResponderPort = 0;
        };
        std::vector<UpstreamTrain> upstreamTrains_;
        // Upstream reports whose host is not listed yet (UDP may reorder), by source address. One
        // that no announce of the search claims is dropped: with hosts sharing a fixed port, a port
        // does not tell which of them sent it, and a host measured nowhere reports 0.
        struct EarlyReport
        {
            uint16_t ConnectPort = 0;
            std::string Address;
            int BytesPerSecond = 0;
        };
        std::vector<EarlyReport> earlyReports_;

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
                throw NetworkNotAvailableException("Failed to create discovery UDP socket.");
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
            // Broadcast queries reach the shared listeners, but unicast delivery to a shared port
            // depends on the OS and bind order. FindSessions uses a private reply socket so the
            // host cannot receive its own reply instead of the searching process. Replies to
            // duplicate broadcast/loopback queries are still deduplicated by connect port.
            enet_socket_set_option(sock, ENET_SOCKOPT_REUSEADDR, 1);
#if defined(__APPLE__)
            // Darwin keeps BSD's rule: SO_REUSEADDR alone lets a second UDP socket share a port
            // only for a multicast address, so the second process's bind of kDiscoveryPort failed
            // with EADDRINUSE and a client could never search while a host on the same Mac was
            // registered. SO_REUSEPORT on every sharer is BSD's spelling of what Linux grants
            // through SO_REUSEADDR; broadcast datagrams still reach every sharer.
            {
                const int reusePort = 1;
                (void)setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reusePort, sizeof(reusePort));
            }
#endif
            enet_socket_set_option(sock, ENET_SOCKOPT_BROADCAST, 1);
            enet_socket_set_option(sock, ENET_SOCKOPT_NONBLOCK, 1);

            ENetAddress address;
            address.host = ENET_HOST_ANY;
            address.port = kDiscoveryPort;
            if (enet_socket_bind(sock, &address) != 0)
            {
                enet_socket_destroy(sock);
                throw NetworkNotAvailableException("Failed to bind discovery UDP socket.");
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
            // A session whose host has left has no Host until a host change names the next one
            // (XNA ProcessGamerLeft IL_006d..007e), as during a SystemLink host migration; it has
            // no host to advertise, so it answers nothing until then.
            NetworkGamer* const host = registeredHost_->getHostProperty();
            if (host == nullptr)
            {
                return;
            }
            // UB-011: a session whose host is now another machine (a migration moved it away) no
            // longer hosts what it would announce; that machine answers instead.
            if (!registeredHost_->getIsHostProperty())
            {
                return;
            }

            DiscoveryAnnounceMessage announce;
            announce.ConnectPort = registeredHostPort_;
            announce.HostInstance = registeredHostInstance_;
            announce.CurrentGamerCount = registeredHost_->getAllGamersProperty().getCountProperty();
            announce.MaxGamers = registeredHost_->getMaxGamersProperty();
            announce.OpenPrivateSlots = registeredHost_->getPrivateGamerSlotsProperty();
            // SystemLink does not track per-gamer slot occupancy (only online sessions set
            // NetworkGamer::getIsPrivateSlotProperty()), so private slots are reported as fully
            // open and public slots are simply "max minus private minus current".
            announce.OpenPublicSlots = std::max(
                0, announce.MaxGamers - announce.OpenPrivateSlots - announce.CurrentGamerCount
            );
            announce.HostGamertag = WireGamertagFor(host);
            announce.Properties = registeredHost_->getSessionPropertiesProperty();

            SendTo(sock, queryingAddress, NetDiscoveryProtocol::Encode(announce));
            // Back to back, so their spacing at the querier measures the path's bandwidth.
            DiscoveryQosProbeMessage probe;
            probe.ConnectPort = announce.ConnectPort;
            probe.TrainId = NextTrainId();
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

        std::string HostIp(const ENetAddress& address)
        {
            std::array<char, 64> ip{};
            enet_address_get_host_ip(&address, ip.data(), ip.size());
            return std::string(ip.data());
        }

        // The found host a probe, invitation or report belongs to: the one with its connect port
        // that announced from the same address. Two hosts can share a fixed port, so the port alone
        // never decides. Every path a host answers by carries its announce first, and its probes
        // and invitation leave the same socket after it; the report leaves the host's responder
        // toward the address this machine sent its train to, the invitation's.
        std::vector<Found>::iterator FoundHost(uint16_t connectPort, const ENetAddress& fromAddress)
        {
            const std::string ip = HostIp(fromAddress);
            return std::find_if(currentResults_->begin(), currentResults_->end(),
                [&](const Found& found) { return found.Announce.ConnectPort == connectPort && AnsweredFrom(found, ip); });
        }

        // Probes and reports that overtook the announce of the path they came by. The probes start
        // that path's own train, in their arrival order.
        void AdoptEarlyArrivals(Found& found, const std::string& address)
        {
            for (const EarlyProbe& arrival : *earlyProbes_)
                if (arrival.ConnectPort == found.Announce.ConnectPort && arrival.Address == address)
                {
                    DiscoveryQosProbeMessage early;
                    early.ConnectPort = arrival.ConnectPort;
                    early.Index = arrival.Index;
                    early.TrainId = arrival.TrainId;
                    RecordProbe(found, address, early, arrival.At);
                }
            for (const EarlyReport& report : earlyReports_)
                if (report.ConnectPort == found.Announce.ConnectPort && report.Address == address)
                    found.UpstreamBytesPerSecond = report.BytesPerSecond;
        }

        // Each extra search address that resolves, with its discovery port.
        std::vector<ENetAddress> ResolveExtraSearchAddresses()
        {
            std::vector<ENetAddress> resolved;
            for (const auto& entry : CNA::Net::SystemLinkDiscovery::getExtraSearchAddressesProperty())
            {
                const auto parsed = ParseSearchAddress(entry, kDiscoveryPort);
                ENetAddress address{};
                if (!parsed || enet_address_set_host(&address, parsed->Host.c_str()) != 0)
                {
                    CNA::Logger::Warn("[ENetDiscoveryService] SystemLink search address \"" + entry
                            + "\" does not resolve; it is skipped in this search.",
                        CNA::LogCategory::APPLICATION);
                    continue;
                }
                address.port = parsed->Port;
                resolved.push_back(address);
            }
            return resolved;
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
                            std::string address = HostIp(fromAddress);

                            // Dedup by connect port alone: the same query goes out via both broadcast
                            // and an explicit loopback copy, and on this machine a broadcast send can
                            // arrive back at our own socket by more than one path (each observed under
                            // a different source address) — so the same host's reply can legitimately
                            // arrive more than once. A given ephemeral ENet connect port uniquely
                            // identifies one hosted session, regardless of which local path its
                            // announce happened to take.
                            // A host's instance id also tells apart two hosts that bound the same
                            // fixed port (CNA::Net::SystemLinkDiscovery::setHostPortProperty). A host
                            // that sends none (0, an older CNA) is identified by its port and its
                            // gamertag, so two such hosts on one port still list apart unless they
                            // also share the gamertag; an address it already answered from stays
                            // its own even if the gamertag changed between two replies.
                            const auto sameHost = [&](bool byAddress)
                            {
                                return [&, byAddress](const Found& existing)
                                {
                                    return existing.Announce.ConnectPort == announce.ConnectPort
                                        && existing.Announce.HostInstance == announce.HostInstance
                                        && (announce.HostInstance != 0
                                            || (byAddress ? AnsweredFrom(existing, address)
                                                          : existing.Announce.HostGamertag == announce.HostGamertag));
                                };
                            };
                            // A legacy host's own address decides before its gamertag, which another
                            // host on the port may share.
                            auto known = std::find_if(currentResults_->begin(), currentResults_->end(), sameHost(true));
                            if (known == currentResults_->end())
                                known = std::find_if(currentResults_->begin(), currentResults_->end(), sameHost(false));
                            if (known != currentResults_->end())
                            {
                                if (!AnsweredFrom(*known, address) && known->Aliases.size() < 8)
                                {
                                    known->Aliases.push_back(address);
                                    AdoptEarlyArrivals(*known, address);
                                }
                            }
                            else
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
                                AdoptEarlyArrivals(found, address);
                                currentResults_->push_back(std::move(found));
                            }
                        }
                        break;
                    case DiscoveryMessageTag::UpstreamInvite:
                        if (currentResults_ != nullptr)
                        {
                            const auto invite = NetDiscoveryProtocol::DecodeUpstreamInvite(data);
                            const auto host = FoundHost(invite.ConnectPort, fromAddress);
                            const UpstreamTrain train{
                                host != currentResults_->end()
                                    ? "#" + std::to_string(host - currentResults_->begin())
                                    : HostIp(fromAddress),
                                invite.ConnectPort, invite.ResponderPort};
                            // A train sent before the listing existed was keyed by the address
                            // that is now one of the listing's.
                            const auto sameHost = [&](const UpstreamTrain& sent)
                            {
                                return sent.ConnectPort == train.ConnectPort && sent.ResponderPort == train.ResponderPort
                                    && (sent.Host == train.Host
                                        || (host != currentResults_->end() && AnsweredFrom(*host, sent.Host)));
                            };
                            if (std::any_of(upstreamTrains_.begin(), upstreamTrains_.end(), sameHost)
                                || upstreamTrains_.size() >= 64)
                                break;
                            upstreamTrains_.push_back(train);
                            ENetAddress responder = fromAddress;
                            responder.port = invite.ResponderPort;
                            DiscoveryQosProbeMessage probe;
                            probe.ConnectPort = invite.ConnectPort;
                            probe.TrainId = NextTrainId();
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
                            auto found = FoundHost(report.ConnectPort, fromAddress);
                            if (found != currentResults_->end()) found->UpstreamBytesPerSecond = report.BytesPerSecond;
                            else if (earlyReports_.size() < 64)
                                earlyReports_.push_back(EarlyReport{report.ConnectPort, HostIp(fromAddress), report.BytesPerSecond});
                        }
                        break;
                    case DiscoveryMessageTag::QosProbe:
                        if (currentResults_ != nullptr)
                        {
                            const auto at = std::chrono::steady_clock::now();
                            const auto probe = NetDiscoveryProtocol::DecodeQosProbe(data);
                            auto found = FoundHost(probe.ConnectPort, fromAddress);
                            if (found != currentResults_->end()) RecordProbe(*found, HostIp(fromAddress), probe, at);
                            else if (earlyProbes_->size() < 64)
                                earlyProbes_->push_back(EarlyProbe{probe.ConnectPort, HostIp(fromAddress), probe.Index, probe.TrainId, at});
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
            CurrentResultsGuard(std::vector<Found>* results, std::vector<EarlyProbe>* early)
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

        // A unicast reply to the shared discovery port can reach another process: Linux selects
        // the newest binding, while Darwin selects the oldest. A private ephemeral source port
        // makes replies return to this search regardless of when a local host started.
        class SearchSocket
        {
        public:
            SearchSocket()
            {
                socket_ = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
                if (socket_ == ENET_SOCKET_NULL)
                {
                    throw NetworkNotAvailableException("Failed to create discovery search UDP socket.");
                }
                ENetAddress address{};
                address.host = ENET_HOST_ANY;
                address.port = 0;
                if (enet_socket_set_option(socket_, ENET_SOCKOPT_BROADCAST, 1) != 0 ||
                    enet_socket_set_option(socket_, ENET_SOCKOPT_NONBLOCK, 1) != 0 ||
                    enet_socket_bind(socket_, &address) != 0)
                {
                    enet_socket_destroy(socket_);
                    socket_ = ENET_SOCKET_NULL;
                    throw NetworkNotAvailableException("Failed to configure discovery search UDP socket.");
                }
            }
            ~SearchSocket()
            {
                if (socket_ != ENET_SOCKET_NULL) enet_socket_destroy(socket_);
            }
            SearchSocket(const SearchSocket&) = delete;
            SearchSocket& operator=(const SearchSocket&) = delete;
            ENetSocket Get() const { return socket_; }

        private:
            ENetSocket socket_ = ENET_SOCKET_NULL;
        };

#if defined(__APPLE__)
        // Darwin can refuse the limited broadcast address with EHOSTUNREACH; also query each
        // active interface's own broadcast address there.
        void SendToEveryInterfaceBroadcast(ENetSocket sock, const std::vector<SharpRuntime::bytecs>& bytes)
        {
            ifaddrs* interfaces = nullptr;
            if (getifaddrs(&interfaces) != 0)
            {
                return;
            }
            for (const ifaddrs* each = interfaces; each != nullptr; each = each->ifa_next)
            {
                if (each->ifa_addr == nullptr || each->ifa_addr->sa_family != AF_INET ||
                    each->ifa_broadaddr == nullptr || (each->ifa_flags & IFF_UP) == 0 ||
                    (each->ifa_flags & IFF_BROADCAST) == 0)
                {
                    continue;
                }
                ENetAddress address{};
                address.host = reinterpret_cast<const sockaddr_in*>(each->ifa_broadaddr)->sin_addr.s_addr;
                address.port = kDiscoveryPort;
                SendTo(sock, address, bytes);
            }
            freeifaddrs(interfaces);
        }
#endif

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
        static std::mt19937_64 instances{std::random_device{}()};
        do { registeredHostInstance_ = instances(); } while (registeredHostInstance_ == 0);
        responder_ = std::make_unique<UpstreamResponder>(connectPort);
    }

    void ENetDiscoveryService::UnregisterHost(NetworkSession* session)
    {
        if (registeredHost_ == session)
        {
            registeredHost_ = nullptr;
            registeredHostPort_ = 0;
            registeredHostInstance_ = 0;
            responder_.reset();
            // Only a host listens on the shared port. A binding left open here would be the newest
            // on Linux and take the unicast queries meant for a host that started earlier on this
            // machine, then drop them, since this process no longer answers.
            if (socket_ != ENET_SOCKET_NULL)
            {
                enet_socket_destroy(socket_);
                socket_ = ENET_SOCKET_NULL;
            }
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

        // A search itself needs only its private reply socket. The shared port is open only while
        // this process hosts, and is then polled so that host still answers other machines; a
        // searcher that opened it would, on Linux, take unicast queries meant for a host on this
        // machine and drop them (see UnregisterHost).
        const ENetSocket sock = socket_;
        // Opening the shared listener initialized ENet (WSAStartup on Windows) before a search's
        // own socket and name lookups; a search that opens none initializes it itself.
        ENetLibrary::EnsureInitialized();

        DiscoveryQueryMessage query;
        query.SessionTypeFilter = sessionTypeFilter;
        auto bytes = NetDiscoveryProtocol::Encode(query);
        // Resolved before the clock starts, so a slow name lookup is not a host's round trip.
        const std::vector<ENetAddress> extraAddresses = ResolveExtraSearchAddresses();

        queryStartTime_ = std::chrono::steady_clock::now(); // Task 4.2

        // The shared port cannot reliably receive this search's replies from another process.
        const SearchSocket search;
        const ENetSocket replies = search.Get();
#if defined(__APPLE__)
        SendToEveryInterfaceBroadcast(replies, bytes);
#endif

        ENetAddress broadcastAddress;
        broadcastAddress.host = ENET_HOST_BROADCAST;
        broadcastAddress.port = kDiscoveryPort;
        SendTo(replies, broadcastAddress, bytes);

        ENetAddress loopbackAddress{};
        enet_address_set_host_ip(&loopbackAddress, "127.0.0.1");
        loopbackAddress.port = kDiscoveryPort;
        SendTo(replies, loopbackAddress, bytes);
        for (const ENetAddress& extra : extraAddresses) SendTo(replies, extra, bytes);

        std::vector<Found> found;
        std::vector<EarlyProbe> early;
        CurrentResultsGuard resultsGuard(&found, &early);
        upstreamTrains_.clear();
        earlyReports_.clear();

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kSearchWindowMs);
        while (true)
        {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                break;
            }
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            // The shared socket still answers other machines' queries while this one waits.
            PollOnce(replies, static_cast<uint32_t>(std::min<long long>(remaining, 5)));
            if (sock != ENET_SOCKET_NULL)
                while (PollOnce(sock, 0)) { }
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
    // not a TODO (see this class's own header doc comment).

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
