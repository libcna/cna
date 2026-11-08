// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/IServiceProvider.hpp"
#include <memory>

namespace {
using namespace Microsoft::Xna::Framework::Net;
using namespace Microsoft::Xna::Framework::GamerServices;
namespace Service = CNA::Internal::GamerServices;
using CNA::Internal::Net::ENetBackend;

// Expectations in this file are CNA policy (owner 2026-11-08), not measured native XNA
// sign-out/election behavior. Explicit transport-result tests elsewhere retain their IL oracle.
struct Provider : System::IServiceProvider {
    void* GetService(const std::type_info&) const override { return nullptr; }
};
class NetworkSessionDeparturePolicyTest : public ::testing::Test {
protected:
    Provider provider;
    std::shared_ptr<Service::IGamerServicesBackend> previous, backend;
    std::unique_ptr<NetworkSession> session;
    void SetUp() override {
        previous = Service::backend();
        std::vector<Service::ServiceIdentity> identities;
        for (const auto* tag : {"A", "B", "C", "D"}) {
            Service::ServiceIdentity identity; identity.userId=tag; identity.gamertag=tag;
            identities.push_back(identity);
        }
        backend = Service::makeFakeBackend(identities); Service::setBackendForTesting(backend);
        if (!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider);
        for (int slot=0; slot<4; ++slot) backend->signIn(slot, std::string(1, 'A'+slot), "fixture");
        GamerServicesDispatcher::Update();
    }
    void TearDown() override {
        session.reset();
        for (int slot=0; slot<4; ++slot) backend->signOut(slot);
        GamerServicesDispatcher::Update(); Service::setBackendForTesting(previous);
    }
    SignedInGamer* gamer(int slot) { return (*Gamer::getSignedInGamersProperty())[slot]; }
    void create(int count) {
        std::vector<SignedInGamer*> locals;
        for (int slot=0; slot<count; ++slot) locals.push_back(gamer(slot));
        session.reset(NetworkSession::Create(NetworkSessionType::Local, locals, 8, 0, {}));
    }
    void signOut(int slot) { backend->signOut(slot); GamerServicesDispatcher::Update(); }
};

TEST_F(NetworkSessionDeparturePolicyTest, HostReplacementUsesJoinOrderForBothMigrationFlags) {
    for (bool migration : {false, true}) {
        SCOPED_TRACE(migration);
        create(3); session->setAllowHostMigrationProperty(migration);
        auto* first=session->getLocalGamersProperty()[0];
        auto* second=session->getLocalGamersProperty()[1];
        auto* third=session->getLocalGamersProperty()[2];
        // Wire ordering is deliberately different from join order (e.g. recycled wire IDs).
        second->SetId(9); third->SetId(2); ENetBackend::OrderTransportGamers(session.get());
        std::vector<std::string> events;
        session->GamerLeft += [&](auto*, const GamerLeftEventArgs& e) {
            EXPECT_EQ(first,e.getGamerProperty()); EXPECT_EQ(second,session->getHostProperty());
            EXPECT_TRUE(session->getIsHostProperty()); events.push_back("left");
        };
        session->HostChanged += [&](auto*, const HostChangedEventArgs& e) {
            EXPECT_EQ(first,e.getOldHostProperty()); EXPECT_EQ(second,e.getNewHostProperty());
            events.push_back("host");
        };
        session->SessionEnded += [&](auto*,const NetworkSessionEndedEventArgs&){events.push_back("end");};
        session->RemoveGamer(first,NetworkSessionEndReason::Disconnected);
        EXPECT_TRUE(events.empty());
        session->Update();
        EXPECT_EQ((std::vector<std::string>{"left","host"}),events);
        EXPECT_TRUE(second->getIsHostProperty()); EXPECT_FALSE(third->getIsHostProperty());
        EXPECT_EQ(NetworkSessionState::Lobby,session->getSessionStateProperty());
        session->Update(); EXPECT_EQ(2u,events.size());
        session.reset();
    }
}

TEST_F(NetworkSessionDeparturePolicyTest, NonHostAndLastLocalDepartureNeverSynthesizeAHost) {
    create(2); auto* first=session->getLocalGamersProperty()[0]; auto* second=session->getLocalGamersProperty()[1];
    int changes=0; std::vector<std::string> events;
    session->HostChanged += [&](auto*,const HostChangedEventArgs&){++changes;};
    session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){events.push_back("left");};
    session->SessionEnded += [&](auto*,const NetworkSessionEndedEventArgs& e){
        EXPECT_EQ(NetworkSessionEndReason::ClientSignedOut,e.getEndReasonProperty()); events.push_back("end");
    };
    session->RemoveGamer(second,NetworkSessionEndReason::ClientSignedOut); session->Update();
    EXPECT_EQ(first,session->getHostProperty()); EXPECT_EQ(0,changes);
    session->RemoveGamer(first,NetworkSessionEndReason::ClientSignedOut); session->Update();
    EXPECT_EQ(nullptr,session->getHostProperty()); EXPECT_EQ(0,changes);
    EXPECT_EQ((std::vector<std::string>{"left","left","end"}),events);
    EXPECT_EQ(NetworkSessionState::Ended,session->getSessionStateProperty());
}

TEST_F(NetworkSessionDeparturePolicyTest, SignedOutNonHostLeavesAtUpdateAndKeepsSession) {
    create(2); auto* leaving=session->getLocalGamersProperty()[1]; auto* identity=gamer(1);
    int left=0,ended=0;
    session->GamerLeft += [&](auto*,const GamerLeftEventArgs& e){
        EXPECT_EQ(leaving,e.getGamerProperty()); EXPECT_TRUE(identity->getIsDisposedProperty());
        EXPECT_EQ(identity,leaving->getSignedInGamerProperty()); ++left;
    };
    session->SessionEnded += [&](auto*,const NetworkSessionEndedEventArgs&){++ended;};
    signOut(1); EXPECT_EQ(0,left); EXPECT_EQ(2,session->getLocalGamersProperty().getCountProperty());
    session->Update(); EXPECT_EQ(1,left); EXPECT_EQ(0,ended);
    ASSERT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    EXPECT_EQ("A",session->getHostProperty()->getGamertagProperty());
    EXPECT_TRUE(leaving->getHasLeftSessionProperty());
    EXPECT_EQ(leaving,session->getPreviousGamersProperty()[0]);
    EXPECT_EQ(NetworkSessionState::Lobby,session->getSessionStateProperty());
    session->Update(); EXPECT_EQ(1,left);
}

TEST_F(NetworkSessionDeparturePolicyTest, SignedOutHostSelectsSurvivorWithActualOldHost) {
    create(2); auto* old=session->getHostProperty(); auto* next=session->getLocalGamersProperty()[1];
    std::vector<std::string> events;
    session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){EXPECT_EQ(next,session->getHostProperty());events.push_back("left");};
    session->HostChanged += [&](auto*,const HostChangedEventArgs& e){
        EXPECT_EQ(old,e.getOldHostProperty()); EXPECT_EQ(next,e.getNewHostProperty());events.push_back("host");
    };
    signOut(0); session->Update();
    EXPECT_EQ((std::vector<std::string>{"left","host"}),events);
    EXPECT_EQ(NetworkSessionState::Lobby,session->getSessionStateProperty());
}

TEST_F(NetworkSessionDeparturePolicyTest, LastSignedOutLocalEndsOnceWithClientSignedOut) {
    create(1); std::vector<std::string> events;
    session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){
        EXPECT_EQ(NetworkSessionState::Ended,session->getSessionStateProperty());events.push_back("left");
    };
    session->SessionEnded += [&](auto*,const NetworkSessionEndedEventArgs& e){
        EXPECT_EQ(NetworkSessionEndReason::ClientSignedOut,e.getEndReasonProperty());events.push_back("end");
    };
    int changes=0;session->HostChanged += [&](auto*,const HostChangedEventArgs&){++changes;};
    signOut(0); session->Update();
    EXPECT_EQ((std::vector<std::string>{"left","end"}),events); EXPECT_EQ(0,changes);
    EXPECT_EQ(0,session->getAllGamersProperty().getCountProperty()); EXPECT_EQ(nullptr,session->getHostProperty());
    EXPECT_THROW(session->StartGame(),System::InvalidOperationException);
    session->Update(); EXPECT_EQ(2u,events.size());
    session->Dispose(); EXPECT_THROW(session->Update(),System::ObjectDisposedException);
}

TEST_F(NetworkSessionDeparturePolicyTest, SignedOutPendingAddRefusesAtCompletionAndNeverJoins) {
    create(1); auto* pending=gamer(1); session->AddLocalGamer(pending);
    int joined=0,left=0;session->GamerJoined += [&](auto*,const GamerJoinedEventArgs& e){if(static_cast<LocalNetworkGamer*>(e.getGamerProperty())->getSignedInGamerProperty()==pending)++joined;};
    session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){++left;};
    signOut(1);
    // Completion adopts the existing AddLocalGamer disposed-identity refusal, XNA IL_000e..0025.
    try { session->Update(); FAIL()<<"Disposed pending identity joined"; }
    catch (const System::ObjectDisposedException& e) { EXPECT_EQ("gamer",e.getObjectNameProperty()); }
    EXPECT_EQ(0,joined); EXPECT_EQ(0,left); EXPECT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    EXPECT_EQ(1u,session->GetOwnedGamerCountForTesting()); EXPECT_EQ(0,session->getPreviousGamersProperty().getCountProperty());
    session->Update(); EXPECT_EQ(0,joined);
}

TEST_F(NetworkSessionDeparturePolicyTest, OneRefusedAddDoesNotLoseOtherAddsOrSignOutResults) {
    create(2);auto* pending=gamer(2);auto* valid=gamer(3);
    session->AddLocalGamer(pending);session->AddLocalGamer(valid);signOut(1);signOut(2);
    int left=0;session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){++left;};
    EXPECT_THROW(session->Update(),System::ObjectDisposedException); EXPECT_EQ(1,left);
    ASSERT_EQ(2,session->getLocalGamersProperty().getCountProperty());
    EXPECT_EQ(valid,session->getLocalGamersProperty()[1]->getSignedInGamerProperty());
    EXPECT_EQ(NetworkSessionState::Lobby,session->getSessionStateProperty());
    session->Update();EXPECT_EQ(1,left);
}

TEST_F(NetworkSessionDeparturePolicyTest, RepeatedSignOutAndReusedSlotDoNotReplaceSessionIdentity) {
    create(2);auto* old=gamer(1);auto* leaving=session->getLocalGamersProperty()[1];
    signOut(1);backend->signIn(1,"B","fixture");GamerServicesDispatcher::Update();
    ASSERT_NE(old,gamer(1));
    SignedInGamer::SignedOut.Raise(nullptr,SignedOutEventArgs(old));
    int left=0;session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){++left;};
    session->Update();EXPECT_EQ(1,left);EXPECT_EQ(old,leaving->getSignedInGamerProperty());
    EXPECT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    session->AddLocalGamer(gamer(1));session->Update();
    EXPECT_EQ(2,session->getLocalGamersProperty().getCountProperty());EXPECT_EQ(1,left);
}

TEST_F(NetworkSessionDeparturePolicyTest, UnrelatedSameTagNotificationDoesNotRemoveLocal) {
    create(1);auto unrelated=SignedInGamer::CreateInternal("A");
    SignedInGamer::SignedOut.Raise(nullptr,SignedOutEventArgs(&unrelated));session->Update();
    EXPECT_EQ(1,session->getLocalGamersProperty().getCountProperty());
    EXPECT_EQ(NetworkSessionState::Lobby,session->getSessionStateProperty());
}

TEST_F(NetworkSessionDeparturePolicyTest, EarlierSignOutSubscriberCanDestroySessionSafely) {
    // EventHandler invokes a subscriber snapshot. The net subscription must not capture raw this.
    const auto token=SignedInGamer::SignedOut.Add([&](auto*,const SignedOutEventArgs&){session.reset();});
    create(1);signOut(0);SignedInGamer::SignedOut.Remove(token);EXPECT_EQ(nullptr,session);
    backend->signIn(0,"A","fixture");GamerServicesDispatcher::Update();create(1);
    session->Dispose();signOut(0); // unsubscribed before freeing any gamer pointers
    EXPECT_TRUE(session->getIsDisposedProperty());
}

TEST_F(NetworkSessionDeparturePolicyTest, ThrowingEarlierSignOutHandlerCannotKeepDisposedIdentityInSession) {
    const auto token=SignedInGamer::SignedOut.Add([](auto*,const SignedOutEventArgs&){throw System::InvalidOperationException("application handler");});
    create(2);backend->signOut(1);
    EXPECT_THROW(GamerServicesDispatcher::Update(),System::InvalidOperationException);
    SignedInGamer::SignedOut.Remove(token);
    int left=0;session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){++left;};
    session->Update();EXPECT_EQ(1,left);EXPECT_EQ(1,session->getLocalGamersProperty().getCountProperty());
}

TEST_F(NetworkSessionDeparturePolicyTest, MultipleSignOutsInOneBatchNeverElectAnAlreadyDepartedLocal) {
    create(3);int changes=0,left=0,ended=0;
    session->HostChanged += [&](auto*,const HostChangedEventArgs&){++changes;};
    session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){++left;};
    session->SessionEnded += [&](auto*,const NetworkSessionEndedEventArgs& e){EXPECT_EQ(NetworkSessionEndReason::ClientSignedOut,e.getEndReasonProperty());++ended;};
    for(int slot=0;slot<3;++slot)backend->signOut(slot);GamerServicesDispatcher::Update();session->Update();
    EXPECT_EQ(3,left);EXPECT_EQ(1,ended);EXPECT_EQ(0,changes);EXPECT_EQ(nullptr,session->getHostProperty());
    session->Update();EXPECT_EQ(3,left);EXPECT_EQ(1,ended);
}

TEST_F(NetworkSessionDeparturePolicyTest, SignOutAfterAnExistingEndDoesNotRaiseSessionEndedTwice) {
    create(1);int ended=0,left=0;
    session->SessionEnded += [&](auto*,const NetworkSessionEndedEventArgs&){++ended;};
    session->GamerLeft += [&](auto*,const GamerLeftEventArgs&){++left;};
    NetworkSession::NetworkEvent event;event.Type=NetworkSession::NetworkEventType::StateChange;
    event.State=NetworkSessionState::Ended;event.Reason=NetworkSessionEndReason::Disconnected;
    session->SendNetworkEvent(event);session->Update();ASSERT_EQ(1,ended);
    signOut(0);session->Update();EXPECT_EQ(1,left);EXPECT_EQ(1,ended);
    EXPECT_EQ(0,session->getLocalGamersProperty().getCountProperty());
}

TEST(NetworkDepartureProtocolTest, NewControlPacketsHaveLiteralBoundedLayouts) {
    using namespace CNA::Internal::Net;
    // Owner-authorized protocol: tag05 + old/new IDs, tag0e + requested ID; byte literals are the spec.
    EXPECT_EQ((std::vector<SharpRuntime::bytecs>{0x05,0x12,0x34}),NetPacketCodec::Encode(HostChangeBroadcastMessage{0x12,0x34}));
    EXPECT_EQ((std::vector<SharpRuntime::bytecs>{0x0e,0x34}),NetPacketCodec::Encode(LocalGamerLeaveMessage{0x34}));
    const auto change=NetPacketCodec::DecodeHostChangeBroadcast({0x05,0x12,0x34});EXPECT_EQ(0x12,change.OldHost);EXPECT_EQ(0x34,change.Successor);
    EXPECT_EQ(0x34,NetPacketCodec::DecodeLocalGamerLeave({0x0e,0x34}).WireId);
    EXPECT_THROW((void)NetPacketCodec::DecodeHostChangeBroadcast({0x05,0x12}),System::ArgumentException);
    EXPECT_THROW((void)NetPacketCodec::DecodeHostChangeBroadcast({0x05,0x12,0x34,0}),System::ArgumentException);
    EXPECT_THROW((void)NetPacketCodec::DecodeLocalGamerLeave({0x0e}),System::ArgumentException);
    EXPECT_THROW((void)NetPacketCodec::DecodeLocalGamerLeave({0x0e,0x34,0}),System::ArgumentException);
}
}
