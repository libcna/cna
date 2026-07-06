// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"

using namespace Microsoft::Xna::Framework::Net;

// --- QualityOfService ---

TEST(QualityOfServiceTest, DefaultValues) {
    auto qos = QualityOfService::CreateInternal();
    EXPECT_EQ(System::TimeSpan::Zero, qos.getAverageRoundtripTimeProperty());
    EXPECT_EQ(0, qos.getBytesPerSecondDownstreamProperty());
    EXPECT_EQ(0, qos.getBytesPerSecondUpstreamProperty());
    EXPECT_TRUE(qos.getIsAvailableProperty());
    EXPECT_EQ(System::TimeSpan::Zero, qos.getMinimumRoundtripTimeProperty());
}

// Task 4.2: the parameterless CreateInternal() always yielded the same hardcoded stub (all-zero
// rates, no matter what) - the only production call site (ENetDiscoveryService.cpp) now uses this
// measured overload instead, with a real wall-clock round-trip time from an actual discovery
// query/reply exchange.
TEST(QualityOfServiceTest, MeasuredOverloadReflectsRealRoundtripTime) {
    auto measured = System::TimeSpan::FromMilliseconds(42.5);
    auto qos = QualityOfService::CreateInternal(measured);
    EXPECT_EQ(measured, qos.getAverageRoundtripTimeProperty());
    EXPECT_EQ(measured, qos.getMinimumRoundtripTimeProperty());
    EXPECT_TRUE(qos.getIsAvailableProperty());
    // Bandwidth isn't measurable from a connectionless discovery query/reply alone - stays 0,
    // same as the parameterless overload.
    EXPECT_EQ(0, qos.getBytesPerSecondDownstreamProperty());
    EXPECT_EQ(0, qos.getBytesPerSecondUpstreamProperty());
}

// --- AvailableNetworkSession ---

namespace {
    AvailableNetworkSession MakeSession(int gamers = 2, const std::string& host = "host1") {
        return AvailableNetworkSession::CreateInternal(
            gamers, host, 1, 3, NetworkSessionProperties{}, QualityOfService::CreateInternal()
        );
    }
}

TEST(AvailableNetworkSessionTest, PropertiesFromCtor) {
    auto session = MakeSession(4, "host-a");
    EXPECT_EQ(4, session.getCurrentGamerCountProperty());
    EXPECT_EQ("host-a", session.getHostGamertagProperty());
    EXPECT_EQ(1, session.getOpenPrivateGamerSlotsProperty());
    EXPECT_EQ(3, session.getOpenPublicGamerSlotsProperty());
    EXPECT_TRUE(session.getQualityOfServiceProperty().getIsAvailableProperty());
    EXPECT_EQ(0, session.getSessionPropertiesProperty().getCountProperty());
}

TEST(AvailableNetworkSessionTest, EqualityAndInequality) {
    auto a = MakeSession(2, "host1");
    auto b = MakeSession(2, "host1");
    auto c = MakeSession(3, "host1");
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(AvailableNetworkSessionTest, DefaultConnectAddressAndPortAreEmpty) {
    auto session = MakeSession();
    EXPECT_EQ(session.GetConnectAddress(), "");
    EXPECT_EQ(session.GetConnectPort(), 0);
}

TEST(AvailableNetworkSessionTest, ConnectAddressAndPortFromCreateInternal) {
    auto session = AvailableNetworkSession::CreateInternal(
        2, "host1", 1, 3, NetworkSessionProperties{}, QualityOfService::CreateInternal(), "192.168.1.5", 12345
    );
    EXPECT_EQ(session.GetConnectAddress(), "192.168.1.5");
    EXPECT_EQ(session.GetConnectPort(), 12345);
}

TEST(AvailableNetworkSessionTest, EqualityConsidersConnectAddressAndPort) {
    auto a = AvailableNetworkSession::CreateInternal(
        2, "host1", 1, 3, NetworkSessionProperties{}, QualityOfService::CreateInternal(), "10.0.0.1", 1000
    );
    auto b = AvailableNetworkSession::CreateInternal(
        2, "host1", 1, 3, NetworkSessionProperties{}, QualityOfService::CreateInternal(), "10.0.0.1", 1000
    );
    auto c = AvailableNetworkSession::CreateInternal(
        2, "host1", 1, 3, NetworkSessionProperties{}, QualityOfService::CreateInternal(), "10.0.0.2", 1000
    );
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// --- AvailableNetworkSessionCollection ---

TEST(AvailableNetworkSessionCollectionTest, EmptyCollection) {
    auto col = AvailableNetworkSessionCollection::CreateInternal({});
    EXPECT_EQ(0, col.getCountProperty());
    EXPECT_FALSE(col.getIsDisposedProperty());
}

TEST(AvailableNetworkSessionCollectionTest, IndexingAndCount) {
    std::vector<AvailableNetworkSession> sessions;
    sessions.push_back(MakeSession(1, "hostA"));
    sessions.push_back(MakeSession(2, "hostB"));
    const auto col = AvailableNetworkSessionCollection::CreateInternal(std::move(sessions));
    EXPECT_EQ(2, col.getCountProperty());
    EXPECT_EQ("hostA", col[0].getHostGamertagProperty());
    EXPECT_EQ("hostB", col[1].getHostGamertagProperty());
}

TEST(AvailableNetworkSessionCollectionTest, Dispose) {
    auto col = AvailableNetworkSessionCollection::CreateInternal({});
    col.Dispose();
    EXPECT_TRUE(col.getIsDisposedProperty());
    col.Dispose(); // idempotent
    EXPECT_TRUE(col.getIsDisposedProperty());
}

TEST(AvailableNetworkSessionCollectionTest, RangeFor) {
    std::vector<AvailableNetworkSession> sessions;
    sessions.push_back(MakeSession(1, "hostA"));
    sessions.push_back(MakeSession(2, "hostB"));
    auto col = AvailableNetworkSessionCollection::CreateInternal(std::move(sessions));
    int count = 0;
    for (const auto& s : col) { (void)s; ++count; }
    EXPECT_EQ(2, count);
}
