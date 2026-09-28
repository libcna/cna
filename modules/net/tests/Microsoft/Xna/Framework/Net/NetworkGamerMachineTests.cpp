// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotImplementedException.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::Net;

// --- NetworkMachine ---

TEST(NetworkMachineTest, DefaultGamersCollectionIsEmpty) {
    NetworkMachine m = NetworkMachine::CreateInternal();
    EXPECT_EQ(m.getGamersProperty().getCountProperty(), 0);
}

// Reference NetworkMachine.RemoveFromSession checks, in order: no gamers, gamer left, local
// machine, not the host (the transports are covered by the ENet and service session tests).
TEST(NetworkMachineTest, RemoveFromSessionFollowsTheReferenceChecks) {
    const NetworkMachine empty = NetworkMachine::CreateInternal();
    EXPECT_THROW(empty.RemoveFromSession(), System::ObjectDisposedException);
    NetworkGamer left = NetworkGamer::CreateInternal(nullptr);
    left.SetHasLeftSession(true);
    NetworkMachine leftMachine = NetworkMachine::CreateInternal();
    leftMachine.AddGamerInternal(&left);
    EXPECT_THROW(leftMachine.RemoveFromSession(), System::InvalidOperationException);
    NetworkGamer remote = NetworkGamer::CreateInternal(nullptr);
    NetworkMachine remoteMachine = NetworkMachine::CreateInternal();
    remoteMachine.AddGamerInternal(&remote);
    EXPECT_THROW(remoteMachine.RemoveFromSession(), System::InvalidOperationException);
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
    // Id/IsHost are real per-instance state (see DEFERRED.md item #20 in the sibling
    // cna-samples repo), not FNA's hardcoded 0/true stubs — NetworkSession/ENetBackend set
    // them explicitly; a bare CreateInternal() with no owning session defaults to false.
    EXPECT_FALSE(g.getIsHostProperty());
    EXPECT_FALSE(g.getIsLocalProperty());
    EXPECT_FALSE(g.getIsMutedByLocalUserProperty());
    EXPECT_FALSE(g.getIsPrivateSlotProperty());
    EXPECT_FALSE(g.getIsReadyProperty());
    EXPECT_FALSE(g.getIsTalkingProperty());
    EXPECT_EQ(g.getRoundtripTimeProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(g.getSessionProperty(), nullptr);
}

TEST(NetworkGamerTest, SetIdUpdatesProperty) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    EXPECT_EQ(g.getIdProperty(), 0);
    g.SetId(7);
    EXPECT_EQ(g.getIdProperty(), 7);
}

TEST(NetworkGamerTest, SetIsHostUpdatesProperty) {
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    EXPECT_FALSE(g.getIsHostProperty());
    g.SetIsHost(true);
    EXPECT_TRUE(g.getIsHostProperty());
    g.SetIsHost(false);
    EXPECT_FALSE(g.getIsHostProperty());
}

namespace Microsoft::Xna::Framework::Net {
struct NetworkGamerReadyTestAccess {
    static void Apply(NetworkGamer& gamer, bool value) { gamer.SetIsReadyInternal(value); }
};
}

TEST(NetworkGamerTest, SetIsReadyPropertyRefusesAGamerThatIsNotLocal) {
    // Reference NetworkGamer.IsReady setter: only local gamers of a session in the Lobby state.
    NetworkGamer g = NetworkGamer::CreateInternal(nullptr);
    EXPECT_THROW(g.setIsReadyProperty(true), System::InvalidOperationException);
    EXPECT_FALSE(g.getIsReadyProperty());
    NetworkGamerReadyTestAccess::Apply(g, true);
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
