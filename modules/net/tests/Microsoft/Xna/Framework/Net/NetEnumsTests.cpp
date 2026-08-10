// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinError.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionState.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"

using namespace Microsoft::Xna::Framework::Net;

TEST(NetworkSessionTypeTest, ValuesExist) {
    EXPECT_EQ(NetworkSessionType::Local,                 NetworkSessionType::Local);
    EXPECT_EQ(NetworkSessionType::SystemLink,             NetworkSessionType::SystemLink);
    EXPECT_EQ(NetworkSessionType::PlayerMatch,            NetworkSessionType::PlayerMatch);
    EXPECT_EQ(NetworkSessionType::Ranked,                 NetworkSessionType::Ranked);
    EXPECT_EQ(NetworkSessionType::LocalWithLeaderboards,  NetworkSessionType::LocalWithLeaderboards);
    EXPECT_NE(NetworkSessionType::Local,                  NetworkSessionType::Ranked);
}

// Task 5.4: NetworkSessionType is serialized as a raw byte on the wire (NetPacketCodec.cpp/
// NetDiscoveryProtocol.cpp both do static_cast<NetworkSessionType>(reader.ReadByte()) and the
// reverse) - a silent reordering of the enumerators would desync wire compatibility between old
// and new builds with nothing in the test suite to catch it, since the ValuesExist test above only
// checks each enumerator equals itself (tautological; passes regardless of ordinal value).
TEST(NetworkSessionTypeTest, OrdinalValuesMatchFNAAndAreWireStable) {
    EXPECT_EQ(static_cast<int>(NetworkSessionType::Local), 0);
    EXPECT_EQ(static_cast<int>(NetworkSessionType::SystemLink), 1);
    EXPECT_EQ(static_cast<int>(NetworkSessionType::PlayerMatch), 2);
    EXPECT_EQ(static_cast<int>(NetworkSessionType::Ranked), 3);
    EXPECT_EQ(static_cast<int>(NetworkSessionType::LocalWithLeaderboards), 4);
}

TEST(NetworkSessionStateTest, ValuesExist) {
    EXPECT_EQ(NetworkSessionState::Lobby,   NetworkSessionState::Lobby);
    EXPECT_EQ(NetworkSessionState::Playing, NetworkSessionState::Playing);
    EXPECT_EQ(NetworkSessionState::Ended,   NetworkSessionState::Ended);
    EXPECT_NE(NetworkSessionState::Lobby,   NetworkSessionState::Ended);
}

TEST(NetworkSessionEndReasonTest, ValuesExist) {
    EXPECT_EQ(NetworkSessionEndReason::ClientSignedOut,  NetworkSessionEndReason::ClientSignedOut);
    EXPECT_EQ(NetworkSessionEndReason::HostEndedSession, NetworkSessionEndReason::HostEndedSession);
    EXPECT_EQ(NetworkSessionEndReason::RemovedByHost,    NetworkSessionEndReason::RemovedByHost);
    EXPECT_EQ(NetworkSessionEndReason::Disconnected,     NetworkSessionEndReason::Disconnected);
    EXPECT_NE(NetworkSessionEndReason::ClientSignedOut,  NetworkSessionEndReason::Disconnected);
}

TEST(NetworkSessionJoinErrorTest, ValuesExist) {
    EXPECT_EQ(NetworkSessionJoinError::SessionNotFound,    NetworkSessionJoinError::SessionNotFound);
    EXPECT_EQ(NetworkSessionJoinError::SessionNotJoinable, NetworkSessionJoinError::SessionNotJoinable);
    EXPECT_EQ(NetworkSessionJoinError::SessionFull,        NetworkSessionJoinError::SessionFull);
    EXPECT_NE(NetworkSessionJoinError::SessionNotFound,    NetworkSessionJoinError::SessionFull);
}

TEST(SendDataOptionsTest, ValuesExist) {
    EXPECT_EQ(SendDataOptions::None,            SendDataOptions::None);
    EXPECT_EQ(SendDataOptions::Reliable,        SendDataOptions::Reliable);
    EXPECT_EQ(SendDataOptions::InOrder,         SendDataOptions::InOrder);
    EXPECT_EQ(SendDataOptions::ReliableInOrder, SendDataOptions::ReliableInOrder);
    EXPECT_EQ(SendDataOptions::Chat,            SendDataOptions::Chat);
    EXPECT_NE(SendDataOptions::None,            SendDataOptions::Chat);
}

// Task 5.4: SendDataOptions is serialized as a raw byte too (AppDataMessage.Options in
// NetPacketCodec.cpp) - same wire-desync risk as NetworkSessionType above.
TEST(SendDataOptionsTest, OrdinalValuesMatchFNAAndAreWireStable) {
    EXPECT_EQ(static_cast<int>(SendDataOptions::None), 0);
    EXPECT_EQ(static_cast<int>(SendDataOptions::Reliable), 1);
    EXPECT_EQ(static_cast<int>(SendDataOptions::InOrder), 2);
    EXPECT_EQ(static_cast<int>(SendDataOptions::ReliableInOrder), 3);
    EXPECT_EQ(static_cast<int>(SendDataOptions::Chat), 4);
}
