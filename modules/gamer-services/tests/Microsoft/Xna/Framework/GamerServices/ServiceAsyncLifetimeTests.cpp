// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../../src/Internal/ServiceAsyncResult.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#include <memory>
#include <array>
#include <utility>
#include <optional>
#include <thread>

namespace Service=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
namespace {
struct Provider final : System::IServiceProvider {
    void* GetService(const std::type_info&) const override {return nullptr;}
};
enum class Operation { Profile,Lookup,Award,Achievements,Read };
class ServiceAsyncLifetimeTest : public ::testing::TestWithParam<Operation> {
protected:
    void SetUp() override {
        previous=Service::backend();
        Service::ServiceIdentity alice;alice.userId="a";alice.gamertag="Alice";alice.allowOnlineSessions=true;
        Service::ServiceIdentity bob;bob.userId="b";bob.gamertag="Bob";bob.allowOnlineSessions=true;
        Service::ServiceAchievement achievement;achievement.key="first";achievement.name="First";achievement.score=10;
        Service::ServiceLeaderboardFixture board;board.key="score";
        board.entries={{"a","Alice",100,1,{}},{"b","Bob",50,2,{}}};
        service=Service::makeFakeBackend({alice,bob},{achievement},{board});Service::setBackendForTesting(service);
        if(!GamerServicesDispatcher::getIsInitializedProperty()) GamerServicesDispatcher::Initialize(provider);
        service->signIn(0,"Alice","fixture");service->signIn(1,"Bob","fixture");GamerServicesDispatcher::Update();
        ASSERT_EQ(2,Gamer::getSignedInGamersProperty()->getCountProperty());
        identity.setKeyProperty("score");
    }
    void TearDown() override {
        service->signOut(0);service->signOut(1);GamerServicesDispatcher::Update();Service::setBackendForTesting(previous);
    }
    SignedInGamer* actor() {return (*Gamer::getSignedInGamersProperty())[0];}
    std::unique_ptr<System::IAsyncResult> begin(System::AsyncCallback callback={}) {
        switch(GetParam()) {
            case Operation::Profile:return std::unique_ptr<System::IAsyncResult>(actor()->BeginGetProfile(std::move(callback),42));
            case Operation::Lookup:return std::unique_ptr<System::IAsyncResult>(Gamer::BeginGetFromGamertag("Alice",std::move(callback),42));
            case Operation::Award:return std::unique_ptr<System::IAsyncResult>(actor()->BeginAwardAchievement("first",std::move(callback),42));
            case Operation::Achievements:return std::unique_ptr<System::IAsyncResult>(actor()->BeginGetAchievements(std::move(callback),42));
            case Operation::Read:return std::unique_ptr<System::IAsyncResult>(LeaderboardReader::BeginRead(identity,0,1,std::move(callback),42));
        }
        return {};
    }
    Provider provider;
    LeaderboardIdentity identity;
    std::shared_ptr<Service::IGamerServicesBackend> previous,service;
};
}
TEST_P(ServiceAsyncLifetimeTest, PendingResultRetainsOriginWithoutAQueuedSelfCycle) {
    int callbacks=0;auto result=begin([&](auto&) {++callbacks;});
    EXPECT_FALSE(result->getIsCompletedProperty());
    std::weak_ptr<Service::IGamerServicesBackend> origin=service;
    auto replacement=Service::makeFakeBackend({});Service::setBackendForTesting(replacement);
    service.reset();EXPECT_FALSE(origin.expired());
    result.reset();EXPECT_TRUE(origin.expired());
    service=std::move(replacement);GamerServicesDispatcher::Update();EXPECT_EQ(0,callbacks);
}
TEST_P(ServiceAsyncLifetimeTest, CompletedMetadataAndLogicalValueReleaseBackendWithoutACycle) {
    int callbacks=0;auto result=begin([&](auto& value) {++callbacks;EXPECT_TRUE(value.getIsCompletedProperty());});
    GamerServicesDispatcher::Update();ASSERT_TRUE(result->getIsCompletedProperty());EXPECT_EQ(1,callbacks);
    EXPECT_EQ(42,std::any_cast<int>(result->getAsyncStateProperty()));
    std::weak_ptr<Service::IGamerServicesBackend> origin=service;
    auto replacement=Service::makeFakeBackend({});Service::setBackendForTesting(replacement);
    service.reset();EXPECT_FALSE(origin.expired());
    result.reset();EXPECT_TRUE(origin.expired());service=std::move(replacement);
}
TEST_P(ServiceAsyncLifetimeTest, PublicEndConsumesOnceOnUpdateOwnerAndKeepsMetadataAlive) {
    const auto thread=std::this_thread::get_id();int callbacks=0;
    auto result=begin([&](auto&) {++callbacks;EXPECT_EQ(thread,std::this_thread::get_id());});
    switch(GetParam()) {
        case Operation::Profile: {
            std::unique_ptr<GamerProfile> profile(actor()->EndGetProfile(result.get()));EXPECT_NE(nullptr,profile);break;
        }
        case Operation::Lookup: {
            std::unique_ptr<Gamer> found(Gamer::EndGetFromGamertag(result.get()));EXPECT_EQ("Alice",found->getGamertagProperty());break;
        }
        case Operation::Award:actor()->EndAwardAchievement(result.get());break;
        case Operation::Achievements:EXPECT_EQ(1,actor()->EndGetAchievements(result.get()).getCountProperty());break;
        case Operation::Read: {
            auto reader=LeaderboardReader::EndRead(result.get());EXPECT_EQ(2,reader.getTotalLeaderboardSizeProperty());
            auto entries=reader.getEntriesProperty();EXPECT_EQ("Alice",std::as_const(entries)[0].getGamerProperty()->getGamertagProperty());reader.Dispose();break;
        }
    }
    EXPECT_EQ(1,callbacks);EXPECT_TRUE(result->getAsyncWaitHandleProperty().WaitOne(0));
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());EXPECT_EQ(42,std::any_cast<int>(result->getAsyncStateProperty()));
    const std::array operations{"profile","lookup","award","achievements","leaderboard-read"};
    const void* owner=(GetParam()==Operation::Lookup || GetParam()==Operation::Read)?nullptr:actor();
    EXPECT_THROW((void)Service::ServiceAsyncResult::end(result.get(),operations[static_cast<int>(GetParam())],owner),System::InvalidOperationException);
}
INSTANTIATE_TEST_SUITE_P(ServiceFamilies,ServiceAsyncLifetimeTest,::testing::Values(
    Operation::Profile,Operation::Lookup,Operation::Award,Operation::Achievements,Operation::Read));

class ServiceReadLifetimeTest : public ServiceAsyncLifetimeTest {};
TEST_F(ServiceReadLifetimeTest, ReaderPagingRetainsOriginalContextAndDisposeReleasesIt) {
    auto reader=LeaderboardReader::Read(identity,0,1);
    std::weak_ptr<Service::IGamerServicesBackend> origin=service;
    auto replacement=Service::makeFakeBackend({});Service::setBackendForTesting(replacement);service.reset();
    EXPECT_FALSE(origin.expired());
    std::unique_ptr<System::IAsyncResult> page(reader.BeginPageDown({},{}));
    reader.EndPageDown(page.get());auto entries=reader.getEntriesProperty();
    ASSERT_EQ(1,entries.getCountProperty());EXPECT_EQ("Bob",std::as_const(entries)[0].getGamerProperty()->getGamertagProperty());
    page.reset();reader.Dispose();EXPECT_TRUE(origin.expired());service=std::move(replacement);
}
TEST_F(ServiceReadLifetimeTest, DisposedReaderAndAbandonedPageCannotUseReleasedContext) {
    auto reader=LeaderboardReader::Read(identity,0,1);
    std::weak_ptr<Service::IGamerServicesBackend> origin=service;
    auto replacement=Service::makeFakeBackend({});Service::setBackendForTesting(replacement);service.reset();
    int callbacks=0;std::unique_ptr<System::IAsyncResult> page(reader.BeginPageDown([&](auto&) {++callbacks;},{}));
    reader.Dispose();EXPECT_FALSE(origin.expired());page.reset();EXPECT_TRUE(origin.expired());
    GamerServicesDispatcher::Update();EXPECT_EQ(0,callbacks);service=std::move(replacement);
}

TEST_F(ServiceReadLifetimeTest, DisposedEmptyServiceReaderKeepsItsDisposalContractAfterReleasingQuery) {
    auto reader=LeaderboardReader::Read(identity,std::vector<Gamer*>{},actor(),1);
    EXPECT_EQ(0,reader.getTotalLeaderboardSizeProperty());reader.Dispose();
    EXPECT_TRUE(reader.getIsDisposedProperty());
    EXPECT_THROW((void)reader.getEntriesProperty(),System::ObjectDisposedException);
    EXPECT_THROW((void)reader.getCanPageDownProperty(),System::ObjectDisposedException);
    EXPECT_THROW((void)reader.getCanPageUpProperty(),System::ObjectDisposedException);
    EXPECT_THROW((void)reader.getLeaderboardIdentityProperty(),System::ObjectDisposedException);
    EXPECT_THROW((void)reader.getPageStartProperty(),System::ObjectDisposedException);
    EXPECT_THROW((void)reader.getTotalLeaderboardSizeProperty(),System::ObjectDisposedException);
    EXPECT_THROW((void)reader.BeginPageDown({},{}),System::ObjectDisposedException);
    EXPECT_THROW((void)reader.BeginPageUp({},{}),System::ObjectDisposedException);
    EXPECT_THROW(reader.EndPageDown(nullptr),System::ObjectDisposedException);
    EXPECT_THROW(reader.EndPageUp(nullptr),System::ObjectDisposedException);
    reader.Dispose();
}
