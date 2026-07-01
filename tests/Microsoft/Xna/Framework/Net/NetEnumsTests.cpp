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
