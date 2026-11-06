// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#include <stdexcept>
namespace {
namespace S=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
struct Provider:System::IServiceProvider {void* GetService(const std::type_info&)const override{return nullptr;}};
}
TEST(GuestSignOutTest, ThrowingParentHandlerStillDisposesAllGuests) {
    // Oracle: CNA parent-sign-out policy must finish cleanup even when game callbacks throw.
    // XS HandlePlayerSignInChanged IL_0022-0034 disposes the old gamer before its notification;
    // parent->guest cascading itself is replacement-service policy, not managed native evidence.
    const auto previous=S::backend();S::ServiceIdentity alice;alice.userId="a";alice.gamertag="Alice";alice.allowOnlineSessions=true;
    const auto backend=S::makeFakeBackend({alice});S::setBackendForTesting(backend);
    Provider provider;if(!GamerServicesDispatcher::getIsInitializedProperty())GamerServicesDispatcher::Initialize(provider);
    backend->signIn(0,"Alice","fixture");GamerServicesDispatcher::Update();
    backend->signInGuest(1,"Alice (1)",0);backend->signInGuest(2,"Alice (2)",0);GamerServicesDispatcher::Update();
    const auto* gamers=Gamer::getSignedInGamersProperty();
    auto* parent=(*gamers)[0];auto* first=(*gamers)[1];auto* second=(*gamers)[2];
    int parentCalls=0,guestCalls=0;
    const auto token=SignedInGamer::SignedOut.Add([&](System::Object*,const SignedOutEventArgs& e){
        EXPECT_TRUE(e.getGamerProperty()->getIsDisposedProperty());
        if(e.getGamerProperty()==parent){++parentCalls;throw std::runtime_error("parent handler");}
        ++guestCalls;if(e.getGamerProperty()==first)throw std::runtime_error("guest handler");
    });
    backend->signOut(0);
    try{GamerServicesDispatcher::Update();ADD_FAILURE()<<"Expected callback error";}
    catch(const std::runtime_error& e){EXPECT_EQ(std::string("parent handler"),e.what());}
    SignedInGamer::SignedOut.Remove(token);
    EXPECT_TRUE(parent->getIsDisposedProperty());EXPECT_TRUE(first->getIsDisposedProperty());EXPECT_TRUE(second->getIsDisposedProperty());
    EXPECT_EQ(1,parentCalls);EXPECT_EQ(2,guestCalls);EXPECT_EQ(0,gamers->getCountProperty());
    GamerServicesDispatcher::Update();EXPECT_EQ(1,parentCalls);EXPECT_EQ(2,guestCalls);
    for(int slot=0;slot<4;++slot)backend->signOut(slot);GamerServicesDispatcher::Update();S::setBackendForTesting(previous);
}

TEST(GuestSignOutTest, CapturedGuestResultLosesAuthorityWhenEitherSlotChangesOrBackendDies) {
    // WIRE lifetime policy: even a response already removed from the queue is non-authority
    // after either player generation changes. The closure must not own the backend.
    S::ServiceIdentity alice;alice.userId="alice";alice.gamertag="Alice";alice.allowOnlineSessions=true;
    auto backend=S::makeFakeBackend({alice});backend->signIn(0,"Alice","fixture");(void)backend->pump();
    backend->signInGuest(1,"Alice (1)",0);auto first=backend->pump();ASSERT_EQ(1U,first.size());
    ASSERT_TRUE(first[0].isCurrent);EXPECT_TRUE(first[0].isCurrent());
    backend->signOut(0);EXPECT_FALSE(first[0].isCurrent());
    backend->signIn(0,"Alice","fixture");(void)backend->pump();EXPECT_FALSE(first[0].isCurrent());
    backend->signInGuest(1,"Alice (1)",0);auto next=backend->pump();ASSERT_EQ(1U,next.size());EXPECT_TRUE(next[0].isCurrent());
    backend->signOutGuest(1);EXPECT_FALSE(next[0].isCurrent());(void)backend->pump();
    backend->signInGuest(1,"Alice (1)",0);auto last=backend->pump();ASSERT_EQ(1U,last.size());EXPECT_TRUE(last[0].isCurrent());
    std::weak_ptr<S::IGamerServicesBackend> weak=backend;backend.reset();EXPECT_TRUE(weak.expired());EXPECT_FALSE(last[0].isCurrent());
}
