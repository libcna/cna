// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/GamerServices/ServiceUpdateSubscription.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IServiceProvider.hpp"
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

namespace Service=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override {return nullptr;}
};
class ServiceUpdateSubscriptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous=Service::backend();
        Service::ServiceIdentity alice;alice.userId="a";alice.gamertag="Alice";
        Service::ServiceIdentity bob;bob.userId="b";bob.gamertag="Bob";
        service=Service::makeFakeBackend({alice,bob});Service::setBackendForTesting(service);
        if(!GamerServicesDispatcher::getIsInitializedProperty())GamerServicesDispatcher::Initialize(provider);
        GamerServicesDispatcher::Update();
    }
    void TearDown() override {
        service->signOut(0);service->signOut(1);GamerServicesDispatcher::Update();Service::setBackendForTesting(previous);
    }
    Provider provider;
    std::shared_ptr<Service::IGamerServicesBackend> previous,service;
};
}
TEST_F(ServiceUpdateSubscriptionTest, DispatchRunsOnOwnerAfterBackendCompletions) {
    int calls=0;bool completion=false;const auto owner=std::this_thread::get_id();
    Service::ServiceUpdateSubscription observer([&]{++calls;EXPECT_TRUE(completion);EXPECT_EQ(owner,std::this_thread::get_id());});
    service->submit([]{},[&]{completion=true;});EXPECT_EQ(0,calls);
    GamerServicesDispatcher::Update();EXPECT_EQ(1,calls);GamerServicesDispatcher::Update();EXPECT_EQ(2,calls);
}
TEST_F(ServiceUpdateSubscriptionTest, CancelAndDestructionReleaseCallbackOwnershipWithoutRegistryCycle) {
    auto retained=std::make_shared<int>(1);std::weak_ptr<int> weak=retained;int calls=0;
    auto observer=std::make_unique<Service::ServiceUpdateSubscription>([retained,&calls]{++calls;});retained.reset();
    EXPECT_FALSE(weak.expired());observer->cancel();observer->cancel();EXPECT_TRUE(weak.expired());
    GamerServicesDispatcher::Update();EXPECT_EQ(0,calls);observer.reset();
    auto another=std::make_shared<int>(2);weak=another;
    {Service::ServiceUpdateSubscription subscription([another]{ });another.reset();EXPECT_FALSE(weak.expired());}
    EXPECT_TRUE(weak.expired());GamerServicesDispatcher::Update();EXPECT_EQ(0,calls);
}
TEST_F(ServiceUpdateSubscriptionTest, CancellationSuppressesAnObserverInTheCurrentSnapshot) {
    int first=0,second=0;std::unique_ptr<Service::ServiceUpdateSubscription> later;
    Service::ServiceUpdateSubscription early([&]{++first;later.reset();});
    later=std::make_unique<Service::ServiceUpdateSubscription>([&]{++second;});
    GamerServicesDispatcher::Update();EXPECT_EQ(1,first);EXPECT_EQ(0,second);
    GamerServicesDispatcher::Update();EXPECT_EQ(2,first);EXPECT_EQ(0,second);
}
TEST_F(ServiceUpdateSubscriptionTest, SelfDestructionKeepsTheExecutingCallbackAliveUntilReturn) {
    int calls=0;auto retained=std::make_shared<int>(3);std::weak_ptr<int> weak=retained;
    std::unique_ptr<Service::ServiceUpdateSubscription> observer;
    observer=std::make_unique<Service::ServiceUpdateSubscription>([retained,&observer,&weak,&calls]{
        ++calls;observer.reset();EXPECT_FALSE(weak.expired());EXPECT_EQ(3,*retained);
    });retained.reset();GamerServicesDispatcher::Update();EXPECT_EQ(1,calls);EXPECT_TRUE(weak.expired());
    GamerServicesDispatcher::Update();EXPECT_EQ(1,calls);
}
TEST_F(ServiceUpdateSubscriptionTest, RegistrationDuringDispatchStartsAtTheFollowingBoundary) {
    int calls=0;std::unique_ptr<Service::ServiceUpdateSubscription> later;
    Service::ServiceUpdateSubscription early([&]{if(!later)later=std::make_unique<Service::ServiceUpdateSubscription>([&]{++calls;});});
    GamerServicesDispatcher::Update();EXPECT_EQ(0,calls);GamerServicesDispatcher::Update();EXPECT_EQ(1,calls);
}
TEST_F(ServiceUpdateSubscriptionTest, NestedUpdateSkipsTheExecutingObserverAndProgressesAnotherOperation) {
    int first=0,second=0;bool nested=false;
    Service::ServiceUpdateSubscription early([&]{++first;if(!nested){nested=true;GamerServicesDispatcher::Update();}});
    Service::ServiceUpdateSubscription later([&]{++second;});
    GamerServicesDispatcher::Update();EXPECT_EQ(1,first);EXPECT_EQ(2,second);
    GamerServicesDispatcher::Update();EXPECT_EQ(2,first);EXPECT_EQ(3,second);
}
TEST_F(ServiceUpdateSubscriptionTest, NestedIdentityPublicationWaitsForTheNextOuterBoundary) {
    bool nested=false;
    Service::ServiceUpdateSubscription observer([&]{if(!nested){nested=true;service->signIn(1,"Bob","fixture");
        GamerServicesDispatcher::Update();EXPECT_EQ(1,Gamer::getSignedInGamersProperty()->getCountProperty());}});
    service->signIn(0,"Alice","fixture");GamerServicesDispatcher::Update();
    EXPECT_EQ(1,Gamer::getSignedInGamersProperty()->getCountProperty());GamerServicesDispatcher::Update();
    EXPECT_EQ(2,Gamer::getSignedInGamersProperty()->getCountProperty());
}
TEST_F(ServiceUpdateSubscriptionTest, ObserverErrorDoesNotStarveTheBatchAndExecutionGuardResets) {
    std::vector<int> calls;
    Service::ServiceUpdateSubscription first([&]{calls.push_back(1);throw std::runtime_error("first");});
    Service::ServiceUpdateSubscription second([&]{calls.push_back(2);throw std::runtime_error("second");});
    Service::ServiceUpdateSubscription third([&]{calls.push_back(3);});
    for(int attempt=0;attempt<2;++attempt) {
        try{GamerServicesDispatcher::Update();FAIL()<<"Expected observer error";}
        catch(const std::runtime_error& error){EXPECT_STREQ("first",error.what());}
    }
    EXPECT_EQ((std::vector<int>{1,2,3,1,2,3}),calls);
}
TEST_F(ServiceUpdateSubscriptionTest, BackendCallbackErrorDoesNotStarveUpdateObservers) {
    int calls=0;Service::ServiceUpdateSubscription observer([&]{++calls;throw std::runtime_error("observer");});
    service->submit([]{},[]{throw std::runtime_error("completion");});
    try{GamerServicesDispatcher::Update();FAIL()<<"Expected completion error";}
    catch(const std::runtime_error& error){EXPECT_STREQ("completion",error.what());}
    EXPECT_EQ(1,calls);
}
TEST_F(ServiceUpdateSubscriptionTest, NestedCallbackErrorStillDrainsOtherCompletionsAndObservers) {
    int completions=0,observations=0;bool nested=false;
    Service::ServiceUpdateSubscription early([&]{if(!nested){nested=true;
        service->submit([]{},[]{throw std::runtime_error("nested");});
        service->submit([]{},[&]{++completions;});GamerServicesDispatcher::Update();}});
    Service::ServiceUpdateSubscription later([&]{++observations;});
    try{GamerServicesDispatcher::Update();FAIL()<<"Expected nested error";}
    catch(const std::runtime_error& error){EXPECT_STREQ("nested",error.what());}
    EXPECT_EQ(1,completions);EXPECT_EQ(2,observations);
    GamerServicesDispatcher::Update();EXPECT_EQ(3,observations);
}
TEST_F(ServiceUpdateSubscriptionTest, EmptyCallbacksAndForeignThreadDispatchAreRefusedBeforeDelivery) {
    EXPECT_THROW(Service::ServiceUpdateSubscription observer({}),System::ArgumentNullException);
    int calls=0;Service::ServiceUpdateSubscription observer([&]{++calls;});
    bool registerRefused=false,dispatchRefused=false;
    std::thread worker([&]{
        try{Service::ServiceUpdateSubscription invalid([]{});}catch(const System::InvalidOperationException&){registerRefused=true;}
        try{Service::dispatchServiceUpdates();}catch(const System::InvalidOperationException&){dispatchRefused=true;}
    });worker.join();EXPECT_TRUE(registerRefused);EXPECT_TRUE(dispatchRefused);EXPECT_EQ(0,calls);
    GamerServicesDispatcher::Update();EXPECT_EQ(1,calls);
}

TEST_F(ServiceUpdateSubscriptionTest, AutomaticPresenceSaturationStillDrainsWorkAndProgressesObservers) {
    service->signIn(0,"Alice","fixture");service->signIn(1,"Bob","fixture");GamerServicesDispatcher::Update();
    // The social watcher reads accounts that just signed in at once; that read finishes first.
    GamerServicesDispatcher::Update();
    service->changeFriend("a","Bob","add");service->changeFriend("b","Alice","accept");
    auto* alice=(*Gamer::getSignedInGamersProperty())[0];
    alice->getPresenceProperty().setPresenceModeProperty(GamerPresenceMode::Level);
    alice->getPresenceProperty().setPresenceValueProperty(7);
    int completions=0,progress=0;
    for(int index=0;index<128;++index)service->submit([]{},[&]{++completions;});
    Service::ServiceUpdateSubscription observer([&]{++progress;});
    EXPECT_NO_THROW(GamerServicesDispatcher::Update());EXPECT_EQ(32,completions);EXPECT_EQ(1,progress);
    for(int index=0;index<4;++index)EXPECT_NO_THROW(GamerServicesDispatcher::Update());
    EXPECT_EQ(128,completions);EXPECT_EQ(5,progress);
    ASSERT_EQ(1,service->friends("b").size());EXPECT_EQ("Level 7",service->friends("b")[0].presence);
}

#if GTEST_HAS_DEATH_TEST
// AM4-323. A subscription owned by a static constructed BEFORE the update registry is destroyed
// AFTER it at exit -- AvatarDescription's description cache is one. The registry was a
// function-local static and was gone by then, so the subscription's cancel wrote into its freed
// handler: an AddressSanitizer build reports a heap-use-after-free and exits non-zero, a plain build
// corrupts the heap (the Linux CnaTests' crash at exit). "threadsafe" re-executes this one test, so
// in the child the static really is constructed before the registry.
TEST(ServiceUpdateSubscriptionExitTest, ASubscriptionOwnedByAnEarlierStaticCancelsSafelyAtExit) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_EXIT(
        {
            static std::vector<std::unique_ptr<Service::ServiceUpdateSubscription>> earlier;
            earlier.push_back(std::make_unique<Service::ServiceUpdateSubscription>([] {}));
            std::exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}
#endif
