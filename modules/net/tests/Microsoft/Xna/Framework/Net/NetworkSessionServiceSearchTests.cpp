// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IServiceProvider.hpp"
#include <memory>
#include <array>
#include <utility>
#include <thread>

namespace Service=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Net;
namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override {return nullptr;}
};
class ServiceSearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_=Service::backend();
        std::vector<Service::ServiceIdentity> identities;
        for(auto [id,tag]:{std::pair{"a","Alice"},{"b","Bob"},{"c","Charlie"},{"d","Dana"}}) {
            Service::ServiceIdentity identity;identity.userId=id;identity.gamertag=tag;identity.allowOnlineSessions=true;
            identities.push_back(identity);
        }
        service=Service::makeFakeBackend(std::move(identities));Service::setBackendForTesting(service);
        if(!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider_);
        for(int slot=0;slot<4;++slot) service->signIn(slot,std::array{"Alice","Bob","Charlie","Dana"}[slot],"fixture");
        GamerServicesDispatcher::Update();
        ASSERT_EQ(4,Gamer::getSignedInGamersProperty()->getCountProperty());
        settings.maxGamers=6;settings.privateSlots=1;settings.properties[0]=37;settings.properties[7]=-2147483647-1;
        player=service->sessionDirectory().create("a",{"a","c"},Service::ServiceSessionKind::PlayerMatch,settings);
        ranked=service->sessionDirectory().create("b",{"b","d"},Service::ServiceSessionKind::Ranked,settings);
    }
    void TearDown() override {
        for(int slot=0;slot<4;++slot) service->signOut(slot);
        GamerServicesDispatcher::Update();Service::setBackendForTesting(previous_);
    }
    SignedInGamer* gamer(int slot) {return (*Gamer::getSignedInGamersProperty())[slot];}
    NetworkSessionProperties properties() {NetworkSessionProperties value;for(int index=0;index<8;++index)value.setItem(index,settings.properties[index]);return value;}
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_,service;
    Service::ServiceSessionSettings settings;
    Service::ServiceSessionSnapshot player,ranked;
};
}
TEST_F(ServiceSearchTest, SearchIsPendingUntilUpdateAndMetadataSurvivesEnd) {
    int callbacks=0;System::IAsyncResult* observed=nullptr;const auto thread=std::this_thread::get_id();
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::PlayerMatch,
        std::vector<SignedInGamer*>{gamer(1),gamer(3)},properties(),[&](auto& value) {
            ++callbacks;observed=&value;EXPECT_EQ(thread,std::this_thread::get_id());
            EXPECT_TRUE(value.getIsCompletedProperty());EXPECT_TRUE(value.getAsyncWaitHandleProperty().WaitOne(0));
        },42));
    EXPECT_FALSE(result->getIsCompletedProperty());EXPECT_FALSE(result->getCompletedSynchronouslyProperty());
    EXPECT_FALSE(result->getAsyncWaitHandleProperty().WaitOne(0));EXPECT_EQ(0,callbacks);
    EXPECT_THROW((void)NetworkSession::EndCreate(result.get()),System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::BeginFind(NetworkSessionType::Ranked,1,{}, {},{}),System::InvalidOperationException);
    GamerServicesDispatcher::Update();EXPECT_EQ(1,callbacks);EXPECT_EQ(result.get(),observed);
    auto found=NetworkSession::EndFind(result.get());ASSERT_EQ(1,found.getCountProperty());
    const auto& item=std::as_const(found)[0];EXPECT_EQ("Alice",item.getHostGamertagProperty());
    EXPECT_EQ(2,item.getCurrentGamerCountProperty());EXPECT_EQ(3,item.getOpenPublicGamerSlotsProperty());
    EXPECT_EQ(1,item.getOpenPrivateGamerSlotsProperty());EXPECT_EQ(37,item.getSessionPropertiesProperty().getItem(0));
    EXPECT_EQ(-2147483647-1,item.getSessionPropertiesProperty().getItem(7));
    EXPECT_TRUE(item.GetConnectAddress().empty());EXPECT_EQ(0,item.GetConnectPort());
    EXPECT_EQ(42,std::any_cast<int>(result->getAsyncStateProperty()));
    EXPECT_THROW((void)NetworkSession::EndFind(result.get()),System::InvalidOperationException);
    GamerServicesDispatcher::Update();EXPECT_EQ(1,callbacks);
}
TEST_F(ServiceSearchTest, EndWaitsByPumpingAndCallbackCanConsumeWithoutFreeingMetadata) {
    int callbacks=0;std::optional<AvailableNetworkSessionCollection> found;
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::Ranked,1,
        properties(),[&](auto& value) {++callbacks;found.emplace(NetworkSession::EndFind(&value));},"state"));
    EXPECT_FALSE(found.has_value());GamerServicesDispatcher::Update();
    ASSERT_TRUE(found.has_value());ASSERT_EQ(1,found->getCountProperty());EXPECT_EQ("Bob",std::as_const(*found)[0].getHostGamertagProperty());
    EXPECT_EQ(1,callbacks);EXPECT_EQ("state",std::any_cast<const char*>(result->getAsyncStateProperty()));
    EXPECT_THROW((void)NetworkSession::EndFind(result.get()),System::InvalidOperationException);
    auto synchronous=NetworkSession::Find(NetworkSessionType::Ranked,1,properties());
    EXPECT_EQ(1,synchronous.getCountProperty());
}
TEST_F(ServiceSearchTest, NullableFiltersCategoryCapacityAndPlayingPolicyComeFromService) {
    auto filters=properties();filters[0]=38;
    EXPECT_EQ(0,NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1)},filters).getCountProperty());
    filters[0]=std::nullopt;
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1)},filters).getCountProperty());
    EXPECT_EQ(0,NetworkSession::Find(NetworkSessionType::Ranked,std::vector<SignedInGamer*>{gamer(1)},filters).getCountProperty());
    settings.state=Service::ServiceSessionState::Playing;
    player=service->sessionDirectory().update("a",player.session,player.revision,settings);
    EXPECT_EQ(0,NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1)},filters).getCountProperty());
    settings.allowJoinInProgress=true;
    player=service->sessionDirectory().update("a",player.session,player.revision,settings);
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::PlayerMatch,std::vector<SignedInGamer*>{gamer(1)},filters).getCountProperty());
    EXPECT_EQ(0,NetworkSession::Find(NetworkSessionType::PlayerMatch,
        std::vector<SignedInGamer*>{gamer(1),gamer(3),gamer(0),gamer(2)},filters).getCountProperty());
}
TEST_F(ServiceSearchTest, InvalidExplicitAccountsDoNotStrandTheSingleton) {
    auto foreign=SignedInGamer::CreateInternal("Bob",true);
    for(const auto& users:{std::vector<SignedInGamer*>{},std::vector<SignedInGamer*>{nullptr},
        std::vector<SignedInGamer*>{gamer(1),gamer(1)},std::vector<SignedInGamer*>{&foreign},
        std::vector<SignedInGamer*>{reinterpret_cast<SignedInGamer*>(0x1)}})
        EXPECT_THROW((void)NetworkSession::BeginFind(NetworkSessionType::PlayerMatch,users,{}, {},{}),System::ArgumentException);
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::Ranked,1,properties()).getCountProperty());
}
TEST_F(ServiceSearchTest, DroppedPendingResultSuppressesCallbackAndReleasesBusyState) {
    int callbacks=0;const int before=NetworkSession::GetActiveActionInstanceCountForTesting();
    {
        std::unique_ptr<System::IAsyncResult> pending(NetworkSession::BeginFind(NetworkSessionType::PlayerMatch,
            std::vector<SignedInGamer*>{gamer(1)},properties(),[&](auto&) {++callbacks;},{}));
    }
    EXPECT_EQ(before,NetworkSession::GetActiveActionInstanceCountForTesting());GamerServicesDispatcher::Update();
    EXPECT_EQ(0,callbacks);EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::Ranked,1,properties()).getCountProperty());
}
TEST_F(ServiceSearchTest, AuthenticationLossIsDeferredToEndAndDoesNotStrandBusyState) {
    int callbacks=0;service->signOut(1);
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::PlayerMatch,
        std::vector<SignedInGamer*>{gamer(1)},properties(),[&](auto&) {++callbacks;},{}));
    EXPECT_FALSE(result->getIsCompletedProperty());
    EXPECT_THROW((void)NetworkSession::EndFind(result.get()),GamerServicesNotAvailableException);
    EXPECT_EQ(1,callbacks);EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_THROW((void)NetworkSession::EndFind(result.get()),System::InvalidOperationException);
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::Ranked,1,properties()).getCountProperty());
}

TEST_F(ServiceSearchTest, EndClaimsConsumptionBeforePumpingReentrantCallback) {
    int callbacks=0;
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::Ranked,1,
        properties(),[&](auto& value) {++callbacks;EXPECT_THROW((void)NetworkSession::EndFind(&value),System::InvalidOperationException);},{}));
    EXPECT_EQ(1,NetworkSession::EndFind(result.get()).getCountProperty());EXPECT_EQ(1,callbacks);
}
TEST_F(ServiceSearchTest, ThrowingUpdateCallbackLeavesCompletionConsumableExactlyOnce) {
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::Ranked,1,
        properties(),[](auto&) {throw std::runtime_error("callback");},{}));
    EXPECT_THROW(GamerServicesDispatcher::Update(),std::runtime_error);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(1,NetworkSession::EndFind(result.get()).getCountProperty());
    EXPECT_THROW((void)NetworkSession::EndFind(result.get()),System::InvalidOperationException);
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::Ranked,1,properties()).getCountProperty());
}
TEST_F(ServiceSearchTest, QueueSaturationRefusesBeginWithoutLeakingOrStrandingBusyState) {
    const int before=NetworkSession::GetActiveActionInstanceCountForTesting();
    // SetUp's Update can leave service work queued (an invitation poll follows a backend change),
    // so fill to the refusal instead of assuming all 128 slots are free.
    int queued=0;
    for(;queued<=128;++queued) {
        try {service->submit([]{},[]{});} catch(const GamerServicesNotAvailableException&) {break;}
    }
    ASSERT_LE(queued,128);
    EXPECT_THROW((void)NetworkSession::BeginFind(NetworkSessionType::Ranked,1,properties(),{},{}),GamerServicesNotAvailableException);
    EXPECT_EQ(before,NetworkSession::GetActiveActionInstanceCountForTesting());
    for(int index=0;index<4;++index) GamerServicesDispatcher::Update();
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::Ranked,1,properties()).getCountProperty());
}
TEST_F(ServiceSearchTest, ImplicitSearchHonorsRequestedCapacityWithOneSignedInGamer) {
    for(int slot=1;slot<4;++slot) service->signOut(slot);
    GamerServicesDispatcher::Update();
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::Ranked,1,properties()).getCountProperty());
    EXPECT_EQ(0,NetworkSession::Find(NetworkSessionType::Ranked,4,properties()).getCountProperty());
}

TEST_F(ServiceSearchTest, ResultOwnsExecutorWithoutAQueuedSelfRetentionCycle) {
    std::weak_ptr<Service::IGamerServicesBackend> oldBackend=service;
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::Ranked,1,properties(),{},{}));
    auto replacement=Service::makeFakeBackend({});Service::setBackendForTesting(replacement);
    service.reset();EXPECT_FALSE(oldBackend.expired());
    result.reset();EXPECT_TRUE(oldBackend.expired());
    service=std::move(replacement);
}

TEST_F(ServiceSearchTest, PendingEndRetainsSearchAuthorityAfterReplacementAndDiscardsOldSignOut) {
    int callbacks=0;const auto thread=std::this_thread::get_id();
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::Ranked,1,
        properties(),[&](auto& value) {
            ++callbacks;EXPECT_EQ(thread,std::this_thread::get_id());
            EXPECT_THROW((void)NetworkSession::EndFind(&value),System::InvalidOperationException);
        },73));
    service->signOut(1);std::weak_ptr<Service::IGamerServicesBackend> origin=service;
    auto replacement=Service::makeFakeBackend({});Service::setBackendForTesting(replacement);service=std::move(replacement);
    auto found=NetworkSession::EndFind(result.get());ASSERT_EQ(1,found.getCountProperty());
    EXPECT_EQ("Bob",std::as_const(found)[0].getHostGamertagProperty());EXPECT_EQ(1,callbacks);
    EXPECT_EQ(4,Gamer::getSignedInGamersProperty()->getCountProperty());
    EXPECT_EQ(73,std::any_cast<int>(result->getAsyncStateProperty()));EXPECT_TRUE(result->getAsyncWaitHandleProperty().WaitOne(0));
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());EXPECT_FALSE(origin.expired());
    EXPECT_THROW((void)NetworkSession::EndFind(result.get()),System::InvalidOperationException);
    result.reset();EXPECT_TRUE(origin.expired());
}
TEST_F(ServiceSearchTest, RetainedOriginFailureIsConsumedOnceAndReleasesBusyState) {
    service->signOut(0);int callbacks=0;
    std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginFind(NetworkSessionType::Ranked,1,
        properties(),[&](auto&) {++callbacks;},{}));
    auto original=service;auto replacement=Service::makeFakeBackend({});Service::setBackendForTesting(replacement);service=std::move(replacement);
    EXPECT_THROW((void)NetworkSession::EndFind(result.get()),GamerServicesNotAvailableException);
    EXPECT_EQ(1,callbacks);EXPECT_THROW((void)NetworkSession::EndFind(result.get()),System::InvalidOperationException);
    Service::setBackendForTesting(original);service=std::move(original);service->signIn(0,"Alice","fixture");GamerServicesDispatcher::Update();
    EXPECT_EQ(1,NetworkSession::Find(NetworkSessionType::Ranked,1,properties()).getCountProperty());
}
