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
#include <vector>

#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <future>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using CNA::Internal::Net::DiscoveryAnnounceMessage;
using CNA::Internal::Net::DiscoveryMessageTag;
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
        FakeFarHost()
        {
            socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = 0;
            inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
            bound_ = socket_ >= 0 && ::bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
            socklen_t length = sizeof(address);
            if (bound_ && ::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &length) == 0)
                port_ = ntohs(address.sin_port);
        }
        ~FakeFarHost() { if (socket_ >= 0) ::close(socket_); }
        FakeFarHost(const FakeFarHost&) = delete;
        FakeFarHost& operator=(const FakeFarHost&) = delete;

        [[nodiscard]] bool Ready() const { return bound_ && port_ != 0; }
        [[nodiscard]] std::string SearchAddress() const { return "127.0.0.1:" + std::to_string(port_); }

        // Waits for one discovery query and answers it with announce; false when none arrives.
        bool AnswerOneQuery(const DiscoveryAnnounceMessage& announce) const
        {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket_, &readable);
            timeval timeout{1, 0};
            if (::select(socket_ + 1, &readable, nullptr, nullptr, &timeout) <= 0) return false;
            std::array<unsigned char, 1500> buffer{};
            sockaddr_in from{};
            socklen_t fromLength = sizeof(from);
            const ssize_t received = ::recvfrom(socket_, buffer.data(), buffer.size(), 0,
                                                reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (received <= 0) return false;
            const std::vector<SharpRuntime::bytecs> query(buffer.begin(), buffer.begin() + received);
            if (NetDiscoveryProtocol::PeekTag(query) != DiscoveryMessageTag::Query) return false;
            const auto reply = NetDiscoveryProtocol::Encode(announce);
            return ::sendto(socket_, reply.data(), reply.size(), 0, reinterpret_cast<const sockaddr*>(&from), fromLength)
                == static_cast<ssize_t>(reply.size());
        }

    private:
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
#endif
