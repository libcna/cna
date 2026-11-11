// SPDX-License-Identifier: MS-PL
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/NetDiscoveryProtocol.hpp"
#include "CNA/Internal/Net/SearchAddress.hpp"
#include "CNA/Net/SystemLinkDiscovery.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NetworkNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/ArgumentException.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <chrono>
#include <future>
#include <netinet/in.h>
#include <optional>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using CNA::Internal::Net::DiscoveryAnnounceMessage;
using CNA::Internal::Net::DiscoveryMessageTag;
using CNA::Internal::Net::DiscoveryQosProbeMessage;
using CNA::Internal::Net::DiscoveryUpstreamInviteMessage;
using CNA::Internal::Net::DiscoveryUpstreamReportMessage;
using CNA::Internal::Net::kQosProbeCount;
using CNA::Internal::Net::ENetBackend;
using CNA::Internal::Net::ENetDiscoveryService;
using CNA::Internal::Net::NetDiscoveryProtocol;
using CNA::Internal::Net::ParseSearchAddress;
using CNA::Net::SystemLinkDiscovery;
using Microsoft::Xna::Framework::GamerServices::NetworkNotAvailableException;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::Net::AvailableNetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSession;
using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
using Microsoft::Xna::Framework::Net::NetworkSessionType;

namespace
{
    // The settings are process-wide; every test leaves them at their defaults.
    class SystemLinkDiscoveryTest : public ::testing::Test
    {
    protected:
        void TearDown() override
        {
            SystemLinkDiscovery::setExtraSearchAddressesProperty({});
            SystemLinkDiscovery::setHostPortProperty(0);
        }
    };
}

TEST_F(SystemLinkDiscoveryTest, DefaultsToBroadcastOnlyAndAnEphemeralHostPort)
{
    EXPECT_TRUE(SystemLinkDiscovery::getExtraSearchAddressesProperty().empty());
    EXPECT_EQ(SystemLinkDiscovery::getHostPortProperty(), 0);
    EXPECT_EQ(SystemLinkDiscovery::DiscoveryPort, 61190);
}

TEST_F(SystemLinkDiscoveryTest, SearchAddressesSplitIntoHostAndDiscoveryPort)
{
    const auto plain = ParseSearchAddress("192.168.1.20", 61190);
    ASSERT_TRUE(plain.has_value());
    EXPECT_EQ(plain->Host, "192.168.1.20");
    EXPECT_EQ(plain->Port, 61190);

    const auto named = ParseSearchAddress("game.example.org:40000", 61190);
    ASSERT_TRUE(named.has_value());
    EXPECT_EQ(named->Host, "game.example.org");
    EXPECT_EQ(named->Port, 40000);

    EXPECT_EQ(ParseSearchAddress("host:1", 61190)->Port, 1);
    EXPECT_EQ(ParseSearchAddress("host:65535", 61190)->Port, 65535);

    for (const char* malformed : {"", " host", "ho st", "host:", ":61190", "host:0", "host:65536", "host:99999x",
                                  "host:123456", "::1", "fe80::1:61190", "host:-1", "host:+5"})
        EXPECT_FALSE(ParseSearchAddress(malformed, 61190).has_value()) << '"' << malformed << '"';

    // The resolver reads the name only up to a NUL, so this would query 127.0.0.1:40000.
    using namespace std::string_view_literals;
    EXPECT_FALSE(ParseSearchAddress("127.0.0.1\0.invalid:40000"sv, 61190).has_value());
    EXPECT_FALSE(ParseSearchAddress("host\0"sv, 61190).has_value());
}

TEST_F(SystemLinkDiscoveryTest, TheSearchAddressesAreKeptInTheirOrder)
{
    SystemLinkDiscovery::setExtraSearchAddressesProperty({"10.0.0.2", "far.example.org:40000", "10.0.0.2"});
    EXPECT_EQ(SystemLinkDiscovery::getExtraSearchAddressesProperty(),
              (std::vector<std::string>{"10.0.0.2", "far.example.org:40000", "10.0.0.2"}));
    SystemLinkDiscovery::setExtraSearchAddressesProperty({});
    EXPECT_TRUE(SystemLinkDiscovery::getExtraSearchAddressesProperty().empty());
}

TEST_F(SystemLinkDiscoveryTest, AMalformedSearchAddressIsRefusedAndThePreviousListKept)
{
    SystemLinkDiscovery::setExtraSearchAddressesProperty({"10.0.0.2"});
    EXPECT_THROW(SystemLinkDiscovery::setExtraSearchAddressesProperty({"10.0.0.3", "bad host"}),
                 System::ArgumentException);
    EXPECT_THROW(SystemLinkDiscovery::setExtraSearchAddressesProperty({"10.0.0.3:0"}), System::ArgumentException);
    EXPECT_THROW(SystemLinkDiscovery::setExtraSearchAddressesProperty({std::string("10.0.0.3\0.invalid", 17)}),
                 System::ArgumentException);
    EXPECT_EQ(SystemLinkDiscovery::getExtraSearchAddressesProperty(), (std::vector<std::string>{"10.0.0.2"}));
}

TEST_F(SystemLinkDiscoveryTest, TheHostPortIsKept)
{
    SystemLinkDiscovery::setHostPortProperty(40001);
    EXPECT_EQ(SystemLinkDiscovery::getHostPortProperty(), 40001);
    SystemLinkDiscovery::setHostPortProperty(0);
    EXPECT_EQ(SystemLinkDiscovery::getHostPortProperty(), 0);
}

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
namespace
{
    // A stand-in for a SystemLink host on another network: a plain UDP socket on a loopback port
    // that is not the discovery port, so only a unicast query to that exact address reaches it.
    class FakeFarHost
    {
    public:
        explicit FakeFarHost(const char* ip = "127.0.0.1", uint16_t port = 0) : ip_(ip)
        {
            socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            inet_pton(AF_INET, ip, &address.sin_addr);
            bound_ = socket_ >= 0 && ::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
            socklen_t length = sizeof(address);
            if (bound_ && ::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) == 0)
                port_ = ntohs(address.sin_port);
        }
        ~FakeFarHost() { if (socket_ >= 0) ::close(socket_); }
        FakeFarHost(const FakeFarHost&) = delete;
        FakeFarHost& operator=(const FakeFarHost&) = delete;

        [[nodiscard]] bool Ready() const { return bound_ && port_ != 0; }
        [[nodiscard]] uint16_t Port() const { return port_; }
        [[nodiscard]] std::string SearchAddress() const { return ip_ + ":" + std::to_string(port_); }

        // The next datagram within timeoutMs, with its sender.
        std::optional<std::pair<std::vector<SharpRuntime::bytecs>, sockaddr_in>> Receive(int timeoutMs) const
        {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket_, &readable);
            timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            if (::select(socket_ + 1, &readable, nullptr, nullptr, &timeout) <= 0) return std::nullopt;
            std::array<unsigned char, 1500> buffer{};
            sockaddr_in from{};
            socklen_t fromLength = sizeof(from);
            const ssize_t received = ::recvfrom(socket_, buffer.data(), buffer.size(), 0,
                                                reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (received <= 0) return std::nullopt;
            return std::make_pair(std::vector<SharpRuntime::bytecs>(buffer.begin(), buffer.begin() + received), from);
        }

        // Waits for one discovery query and returns where it came from.
        std::optional<sockaddr_in> WaitForQuery() const
        {
            const auto datagram = Receive(1000);
            if (!datagram || NetDiscoveryProtocol::PeekTag(datagram->first) != DiscoveryMessageTag::Query)
                return std::nullopt;
            return datagram->second;
        }

        bool Send(const sockaddr_in& to, const std::vector<SharpRuntime::bytecs>& bytes) const
        {
            return ::sendto(socket_, bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to))
                == static_cast<ssize_t>(bytes.size());
        }

        // Waits for one discovery query and answers it with announce; false when none arrives.
        bool AnswerOneQuery(const DiscoveryAnnounceMessage& announce) const
        {
            const auto from = WaitForQuery();
            return from && Send(*from, NetDiscoveryProtocol::Encode(announce));
        }

    private:
        std::string ip_;
        int socket_ = -1;
        bool bound_ = false;
        uint16_t port_ = 0;
    };

    DiscoveryAnnounceMessage FarAnnounce(const std::string& gamertag, uint16_t connectPort, uint64_t instance)
    {
        DiscoveryAnnounceMessage announce;
        announce.ConnectPort = connectPort;
        announce.CurrentGamerCount = 1;
        announce.MaxGamers = 4;
        announce.OpenPublicSlots = 3;
        announce.HostGamertag = gamertag;
        announce.HostInstance = instance;
        return announce;
    }

    // Other test processes on this machine may host real sessions; only the fakes count here.
    std::vector<AvailableNetworkSession> FoundFar(const std::vector<AvailableNetworkSession>& found)
    {
        std::vector<AvailableNetworkSession> far;
        std::copy_if(found.begin(), found.end(), std::back_inserter(far),
                     [](const AvailableNetworkSession& session) { return session.getHostGamertagProperty().rfind("Far", 0) == 0; });
        return far;
    }

    // An ephemeral UDP port that was free a moment ago.
    uint16_t FreeUdpPort()
    {
        const int probe = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (probe < 0 || ::bind(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) return 0;
        socklen_t length = sizeof(address);
        ::getsockname(probe, reinterpret_cast<sockaddr*>(&address), &length);
        ::close(probe);
        return ntohs(address.sin_port);
    }

    std::unique_ptr<NetworkSession> CreateSystemLinkSession(SignedInGamer& gamer)
    {
        return std::unique_ptr<NetworkSession>(NetworkSession::Create(
            NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    }
}

TEST_F(SystemLinkDiscoveryTest, ASearchAlsoQueriesEachExtraAddressByUnicast)
{
    FakeFarHost far;
    ASSERT_TRUE(far.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty({far.SearchAddress()});

    auto answered = std::async(std::launch::async, [&] { return far.AnswerOneQuery(FarAnnounce("FarHost", 4243, 77)); });
    const auto found = FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink));
    ASSERT_TRUE(answered.get()) << "the far host received no unicast query";
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].getHostGamertagProperty(), "FarHost");
    EXPECT_EQ(found[0].GetConnectAddress(), "127.0.0.1");
    EXPECT_EQ(found[0].GetConnectPort(), 4243);
    EXPECT_EQ(found[0].GetSessionType(), NetworkSessionType::SystemLink);
}

TEST_F(SystemLinkDiscoveryTest, WithoutTheAddressTheFarHostIsNotQueried)
{
    FakeFarHost far;
    ASSERT_TRUE(far.Ready());
    auto answered = std::async(std::launch::async, [&] { return far.AnswerOneQuery(FarAnnounce("FarHost", 4243, 77)); });
    EXPECT_TRUE(FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)).empty());
    EXPECT_FALSE(answered.get());
}

TEST_F(SystemLinkDiscoveryTest, TwoHostsOnTheSameFixedPortAreListedApart)
{
    FakeFarHost first, second;
    ASSERT_TRUE(first.Ready() && second.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty({first.SearchAddress(), second.SearchAddress()});

    auto a = std::async(std::launch::async, [&] { return first.AnswerOneQuery(FarAnnounce("FarFirst", 40000, 1)); });
    auto b = std::async(std::launch::async, [&] { return second.AnswerOneQuery(FarAnnounce("FarSecond", 40000, 2)); });
    auto found = FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink));
    ASSERT_TRUE(a.get() && b.get());
    ASSERT_EQ(found.size(), 2u);
    std::vector<std::string> names{found[0].getHostGamertagProperty(), found[1].getHostGamertagProperty()};
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names, (std::vector<std::string>{"FarFirst", "FarSecond"}));
}

TEST_F(SystemLinkDiscoveryTest, OneHostReachedByTwoPathsIsListedOnce)
{
    FakeFarHost first, second;
    ASSERT_TRUE(first.Ready() && second.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty({first.SearchAddress(), second.SearchAddress()});

    // The same instance answering twice, as one host does when a query reaches it by broadcast
    // and by its address; a host without an instance id (0) is identified by its port alone.
    auto a = std::async(std::launch::async, [&] { return first.AnswerOneQuery(FarAnnounce("FarHost", 40000, 9)); });
    auto b = std::async(std::launch::async, [&] { return second.AnswerOneQuery(FarAnnounce("FarHost", 40000, 9)); });
    EXPECT_EQ(FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)).size(), 1u);
    ASSERT_TRUE(a.get() && b.get());

    a = std::async(std::launch::async, [&] { return first.AnswerOneQuery(FarAnnounce("FarOld", 40002, 0)); });
    b = std::async(std::launch::async, [&] { return second.AnswerOneQuery(FarAnnounce("FarOld", 40002, 0)); });
    EXPECT_EQ(FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)).size(), 1u);
    ASSERT_TRUE(a.get() && b.get());
}

TEST_F(SystemLinkDiscoveryTest, AnAddressThatDoesNotResolveIsSkipped)
{
    FakeFarHost far;
    ASSERT_TRUE(far.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty({"cna-no-such-host.invalid", far.SearchAddress()});
    auto answered = std::async(std::launch::async, [&] { return far.AnswerOneQuery(FarAnnounce("FarHost", 4243, 5)); });
    std::vector<AvailableNetworkSession> found;
    EXPECT_NO_THROW(found = FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)));
    EXPECT_TRUE(answered.get());
    EXPECT_EQ(found.size(), 1u);
}

TEST_F(SystemLinkDiscoveryTest, ACreatedSessionBindsTheFixedHostPortAndAnnouncesIt)
{
    const uint16_t port = FreeUdpPort();
    ASSERT_NE(port, 0);
    SystemLinkDiscovery::setHostPortProperty(port);
    SignedInGamer gamer = SignedInGamer::CreateInternal("FixedPortHost");
    auto session = CreateSystemLinkSession(gamer);
    session->Update();
    EXPECT_EQ(ENetBackend::GetBoundPort(session.get()), port);

    const auto found = ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink);
    const auto listed = std::find_if(found.begin(), found.end(),
        [](const AvailableNetworkSession& s) { return s.getHostGamertagProperty() == "FixedPortHost"; });
    ASSERT_NE(listed, found.end());
    EXPECT_EQ(listed->GetConnectPort(), port);
    session->Dispose();
}

TEST_F(SystemLinkDiscoveryTest, ABusyFixedHostPortFailsCreate)
{
    const int holder = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(holder, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    ASSERT_EQ(::bind(holder, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
    socklen_t length = sizeof(address);
    ASSERT_EQ(::getsockname(holder, reinterpret_cast<sockaddr*>(&address), &length), 0);
    SystemLinkDiscovery::setHostPortProperty(ntohs(address.sin_port));

    SignedInGamer gamer = SignedInGamer::CreateInternal("BusyPortHost");
    EXPECT_THROW((void)CreateSystemLinkSession(gamer), NetworkNotAvailableException);
    ::close(holder);

    // An ephemeral port again once the setting is cleared.
    SystemLinkDiscovery::setHostPortProperty(0);
    auto session = CreateSystemLinkSession(gamer);
    EXPECT_NE(ENetBackend::GetBoundPort(session.get()), 0);
    session->Dispose();
}

// Two hosts on the same fixed connect port each invite this machine's upstream train to their own
// responder; both get one. A duplicated query brings one host's invitation twice, and that host
// still gets one train.
TEST_F(SystemLinkDiscoveryTest, TwoHostsOnTheSameFixedPortEachGetAnUpstreamTrain)
{
    FakeFarHost first, second;
    ASSERT_TRUE(first.Ready() && second.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty({first.SearchAddress(), second.SearchAddress()});

    const auto host = [](const FakeFarHost& fake, const char* gamertag, uint64_t instance, int invitations)
    {
        const auto from = fake.WaitForQuery();
        if (!from || !fake.Send(*from, NetDiscoveryProtocol::Encode(FarAnnounce(gamertag, 40000, instance)))) return -1;
        DiscoveryUpstreamInviteMessage invite;
        invite.ConnectPort = 40000;
        invite.ResponderPort = fake.Port();
        for (int each = 0; each < invitations; ++each) fake.Send(*from, NetDiscoveryProtocol::Encode(invite));
        int probes = 0;
        while (const auto datagram = fake.Receive(500))
            if (NetDiscoveryProtocol::PeekTag(datagram->first) == DiscoveryMessageTag::UpstreamProbe) ++probes;
        return probes;
    };
    auto a = std::async(std::launch::async, host, std::cref(first), "FarFirst", 1, 1);
    auto b = std::async(std::launch::async, host, std::cref(second), "FarSecond", 2, 2);
    EXPECT_EQ(FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)).size(), 2u);
    EXPECT_EQ(a.get(), static_cast<int>(kQosProbeCount));
    EXPECT_EQ(b.get(), static_cast<int>(kQosProbeCount));
}

#if defined(__linux__)
namespace
{
    int Downstream(const std::vector<AvailableNetworkSession>& found, const std::string& gamertag)
    {
        for (const auto& session : found)
            if (session.getHostGamertagProperty() == gamertag)
                return session.getQualityOfServiceProperty().getBytesPerSecondDownstreamProperty();
        ADD_FAILURE() << gamertag << " was not found";
        return -1;
    }

    int Upstream(const std::vector<AvailableNetworkSession>& found, const std::string& gamertag)
    {
        for (const auto& session : found)
            if (session.getHostGamertagProperty() == gamertag)
                return session.getQualityOfServiceProperty().getBytesPerSecondUpstreamProperty();
        ADD_FAILURE() << gamertag << " was not found";
        return -1;
    }

    std::vector<SharpRuntime::bytecs> Invite(uint16_t connectPort, uint16_t responderPort)
    {
        DiscoveryUpstreamInviteMessage invite;
        invite.ConnectPort = connectPort;
        invite.ResponderPort = responderPort;
        return NetDiscoveryProtocol::Encode(invite);
    }

    std::vector<SharpRuntime::bytecs> Report(uint16_t connectPort, int bytesPerSecond)
    {
        DiscoveryUpstreamReportMessage report;
        report.ConnectPort = connectPort;
        report.BytesPerSecond = bytesPerSecond;
        return NetDiscoveryProtocol::Encode(report);
    }

    // Collects one upstream train; the sender of its probes, to report back to.
    std::optional<sockaddr_in> ReceiveUpstreamTrain(const FakeFarHost& fake)
    {
        int probes = 0;
        std::optional<sockaddr_in> from;
        while (probes < kQosProbeCount)
        {
            const auto datagram = fake.Receive(500);
            if (!datagram) return std::nullopt;
            if (NetDiscoveryProtocol::PeekTag(datagram->first) != DiscoveryMessageTag::UpstreamProbe) continue;
            ++probes;
            from = datagram->second;
        }
        return from;
    }

    std::vector<SharpRuntime::bytecs> Probe(uint16_t connectPort, uint8_t index)
    {
        DiscoveryQosProbeMessage probe;
        probe.ConnectPort = connectPort;
        probe.Index = index;
        return NetDiscoveryProtocol::Encode(probe);
    }
}

// Two hosts on the same fixed connect port at different addresses (Linux answers on all of
// 127.0.0.0/8): the second host's probes overtake its announce, and they time its path, not the
// first host's.
TEST_F(SystemLinkDiscoveryTest, ProbesAreTheirOwnHostsWhenTwoHostsShareAFixedPort)
{
    FakeFarHost first("127.0.0.1"), second("127.0.0.2");
    ASSERT_TRUE(first.Ready() && second.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty({first.SearchAddress(), second.SearchAddress()});

    for (const bool firstAnnouncesBeforeTheProbes : {true, false})
    {
        SCOPED_TRACE(firstAnnouncesBeforeTheProbes ? "first host listed before the probes" : "probes before both announces");
        auto hosts = std::async(std::launch::async, [&]
        {
            const auto toFirst = first.WaitForQuery();
            const auto toSecond = second.WaitForQuery();
            if (!toFirst || !toSecond) return false;
            const auto firstAnnounce = NetDiscoveryProtocol::Encode(FarAnnounce("FarFirst", 40000, 1));
            bool sent = true;
            if (firstAnnouncesBeforeTheProbes) sent = first.Send(*toFirst, firstAnnounce) && sent;
            for (uint8_t index = 0; index < kQosProbeCount; ++index) sent = second.Send(*toSecond, Probe(40000, index)) && sent;
            if (!firstAnnouncesBeforeTheProbes) sent = first.Send(*toFirst, firstAnnounce) && sent;
            return second.Send(*toSecond, NetDiscoveryProtocol::Encode(FarAnnounce("FarSecond", 40000, 2))) && sent;
        });
        const auto found = FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink));
        ASSERT_TRUE(hosts.get());
        ASSERT_EQ(found.size(), 2u);
        EXPECT_EQ(Downstream(found, "FarFirst"), 0);
        EXPECT_GT(Downstream(found, "FarSecond"), 0);
    }
}

// A host's discovery listener shares port 61190, and Linux delivers a unicast datagram to the
// newest binding of it. A process that only searches must not hold one, or a remote unicast query
// (SystemLinkDiscovery's extra search addresses) meant for a host on this machine reaches the
// searcher and is dropped.
TEST_F(SystemLinkDiscoveryTest, ASearchLeavesTheSharedDiscoveryPortToTheHost)
{
    const int host = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(host, 0);
    const int reuse = 1;
    ASSERT_EQ(::setsockopt(host, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)), 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(SystemLinkDiscovery::DiscoveryPort);
    ASSERT_EQ(::bind(host, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0)
        << "another process holds port 61190 without SO_REUSEADDR";

    (void)ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink);
    // The search's own broadcast and loopback queries reached the listener too.
    std::array<unsigned char, 1500> drain{};
    while (::recv(host, drain.data(), drain.size(), MSG_DONTWAIT) > 0) { }

    // The query a far searcher sends to this machine's address.
    const int far = ::socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(far, 0);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(SystemLinkDiscovery::DiscoveryPort);
    inet_pton(AF_INET, "127.0.0.1", &target.sin_addr);
    CNA::Internal::Net::DiscoveryQueryMessage query;
    query.SessionTypeFilter = NetworkSessionType::SystemLink;
    const auto bytes = NetDiscoveryProtocol::Encode(query);
    ASSERT_EQ(::sendto(far, bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target)),
              static_cast<ssize_t>(bytes.size()));

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(host, &readable);
    timeval timeout{1, 0};
    EXPECT_GT(::select(host + 1, &readable, nullptr, nullptr, &timeout), 0)
        << "the unicast query to port 61190 did not reach the host's listener";
    ::close(far);
    ::close(host);
}

// A host on the searcher's machine answers by two addresses (here 127.0.0.1 and 127.0.0.3); its
// invitation and report take the second while another host on the same fixed port is listed. The
// report is still the first host's.
TEST_F(SystemLinkDiscoveryTest, AReportByAHostsSecondAddressIsItsOwnBesideASamePortHost)
{
    FakeFarHost firstPath("127.0.0.1"), secondPath("127.0.0.3"), other("127.0.0.2");
    ASSERT_TRUE(firstPath.Ready() && secondPath.Ready() && other.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty(
        {firstPath.SearchAddress(), secondPath.SearchAddress(), other.SearchAddress()});

    auto hosts = std::async(std::launch::async, [&]
    {
        const auto toFirst = firstPath.WaitForQuery();
        const auto toSecond = secondPath.WaitForQuery();
        const auto toOther = other.WaitForQuery();
        if (!toFirst || !toSecond || !toOther) return false;
        bool sent = firstPath.Send(*toFirst, NetDiscoveryProtocol::Encode(FarAnnounce("FarHost", 40000, 1)));
        sent = secondPath.Send(*toSecond, NetDiscoveryProtocol::Encode(FarAnnounce("FarHost", 40000, 1))) && sent;
        sent = other.Send(*toOther, NetDiscoveryProtocol::Encode(FarAnnounce("FarOther", 40000, 2))) && sent;
        sent = secondPath.Send(*toSecond, Invite(40000, secondPath.Port())) && sent;
        const auto searcher = ReceiveUpstreamTrain(secondPath);
        return searcher && secondPath.Send(*searcher, Report(40000, 12345)) && sent;
    });
    const auto found = FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink));
    ASSERT_TRUE(hosts.get());
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(Upstream(found, "FarHost"), 12345);
    EXPECT_EQ(Upstream(found, "FarOther"), 0);
}

// Two machines choose their responder ports independently, so two hosts on the same fixed connect
// port can invite to the same responder port number; each still gets a train.
TEST_F(SystemLinkDiscoveryTest, TwoHostsWithOneResponderPortNumberEachGetAnUpstreamTrain)
{
    FakeFarHost first("127.0.0.1");
    ASSERT_TRUE(first.Ready());
    FakeFarHost second("127.0.0.2", first.Port());
    ASSERT_TRUE(second.Ready()) << "127.0.0.2:" << first.Port() << " is taken";
    SystemLinkDiscovery::setExtraSearchAddressesProperty({first.SearchAddress(), second.SearchAddress()});

    const auto host = [](const FakeFarHost& fake, const char* gamertag, uint64_t instance)
    {
        const auto from = fake.WaitForQuery();
        if (!from || !fake.Send(*from, NetDiscoveryProtocol::Encode(FarAnnounce(gamertag, 40000, instance)))
            || !fake.Send(*from, Invite(40000, fake.Port())))
            return false;
        return ReceiveUpstreamTrain(fake).has_value();
    };
    auto a = std::async(std::launch::async, host, std::cref(first), "FarFirst", 1);
    auto b = std::async(std::launch::async, host, std::cref(second), "FarSecond", 2);
    EXPECT_EQ(FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink)).size(), 2u);
    EXPECT_TRUE(a.get()) << "the first host got no upstream train";
    EXPECT_TRUE(b.get()) << "the second host got no upstream train";
}

// A report that overtakes its host's announce waits for that host instead of going to the only
// listing with its port.
TEST_F(SystemLinkDiscoveryTest, AReportBeforeItsHostsAnnounceWaitsForThatHost)
{
    FakeFarHost first("127.0.0.1"), second("127.0.0.2");
    ASSERT_TRUE(first.Ready() && second.Ready());
    SystemLinkDiscovery::setExtraSearchAddressesProperty({first.SearchAddress(), second.SearchAddress()});

    auto hosts = std::async(std::launch::async, [&]
    {
        const auto toFirst = first.WaitForQuery();
        const auto toSecond = second.WaitForQuery();
        if (!toFirst || !toSecond) return false;
        bool sent = first.Send(*toFirst, NetDiscoveryProtocol::Encode(FarAnnounce("FarFirst", 40000, 1)));
        sent = second.Send(*toSecond, Report(40000, 22222)) && sent;
        return second.Send(*toSecond, NetDiscoveryProtocol::Encode(FarAnnounce("FarSecond", 40000, 2))) && sent;
    });
    const auto found = FoundFar(ENetDiscoveryService::FindSessions(NetworkSessionType::SystemLink));
    ASSERT_TRUE(hosts.get());
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(Upstream(found, "FarFirst"), 0);
    EXPECT_EQ(Upstream(found, "FarSecond"), 22222);
}
#endif
#endif
