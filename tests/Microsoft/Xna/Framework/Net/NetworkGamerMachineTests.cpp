// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include "System/NotImplementedException.hpp"

using namespace Microsoft::Xna::Framework::Net;

// --- NetworkMachine ---

TEST(NetworkMachineTest, DefaultGamersCollectionIsEmpty) {
    NetworkMachine m = NetworkMachine::CreateInternal();
    EXPECT_EQ(m.getGamersProperty().getCountProperty(), 0);
}

TEST(NetworkMachineTest, RemoveFromSessionThrows) {
    NetworkMachine m = NetworkMachine::CreateInternal();
    EXPECT_THROW(m.RemoveFromSession(), System::NotImplementedException);
}

// --- NetworkGamer ---

TEST(NetworkGamerTest, ConstructedWithStubGamertag) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    EXPECT_EQ(g.getGamertagProperty(), "Stub Gamer");
    EXPECT_EQ(g.getDisplayNameProperty(), "Stub Gamer");
}

TEST(NetworkGamerTest, DefaultPropertyValues) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    EXPECT_FALSE(g.getHasLeftSessionProperty());
    EXPECT_FALSE(g.getHasVoiceProperty());
    EXPECT_EQ(g.getIdProperty(), 0);
    EXPECT_FALSE(g.getIsGuestProperty());
    EXPECT_TRUE(g.getIsHostProperty());
    EXPECT_FALSE(g.getIsLocalProperty());
    EXPECT_FALSE(g.getIsMutedByLocalUserProperty());
    EXPECT_FALSE(g.getIsPrivateSlotProperty());
    EXPECT_FALSE(g.getIsReadyProperty());
    EXPECT_FALSE(g.getIsTalkingProperty());
    EXPECT_EQ(g.getRoundtripTimeProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(g.getSessionProperty(), nullptr);
}

TEST(NetworkGamerTest, SetIsReadyProperty) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    g.setIsReadyProperty(true);
    EXPECT_TRUE(g.getIsReadyProperty());
}

TEST(NetworkGamerTest, GetSetMachineProperty) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    NetworkMachine m = NetworkMachine::CreateInternal();
    g.setMachineProperty(m);
    EXPECT_EQ(g.getMachineProperty().getGamersProperty().getCountProperty(), 0);
}

TEST(NetworkGamerTest, SessionPointerStored) {
    // NetworkSession isn't ported yet; use an opaque non-null sentinel address.
    auto* fakeSession = reinterpret_cast<NetworkSession*>(0x1);
    NetworkGamer g = NetworkGamer::CreateInternal(fakeSession);
    EXPECT_EQ(g.getSessionProperty(), fakeSession);
}

TEST(NetworkGamerTest, CreateInternalWithCustomGamertag) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr, "RemotePlayer");
    EXPECT_EQ(g.getGamertagProperty(), "RemotePlayer");
    EXPECT_EQ(g.getDisplayNameProperty(), "RemotePlayer");
}

TEST(NetworkGamerTest, SetHasLeftSessionUpdatesProperty) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    EXPECT_FALSE(g.getHasLeftSessionProperty());
    g.SetHasLeftSession(true);
    EXPECT_TRUE(g.getHasLeftSessionProperty());
}
