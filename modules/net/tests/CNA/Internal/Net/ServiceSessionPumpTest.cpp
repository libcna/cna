// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../src/Internal/ServiceSessionPump.hpp"
#include "System/InvalidOperationException.hpp"

namespace {
using namespace CNA::Internal::Net;
namespace Service=CNA::Internal::GamerServices;
struct Fixture {
    std::shared_ptr<Service::IGamerServicesBackend> backend;
    Service::ServiceSessionSnapshot initial;
    ServiceSessionPump::Time now{};
    Fixture() {
        std::vector<Service::ServiceIdentity> identities;
        for(const std::string user:{"a","b","c","d"}) {
            Service::ServiceIdentity value;value.userId=user;value.gamertag="Tag"+user;value.allowOnlineSessions=true;
            identities.push_back(std::move(value));
        }
        backend=Service::makeFakeBackend(std::move(identities));
        for(int index=0;index<4;++index)backend->signIn(index,std::string("Tag")+static_cast<char>('a'+index),"fixture");
        (void)backend->pump();Service::ServiceSessionSettings settings;settings.maxGamers=6;
        initial=backend->sessionDirectory().create("a",{"a","b"},Service::ServiceSessionKind::PlayerMatch,settings);
    }
    std::unique_ptr<ServiceSessionPump> make(){return std::make_unique<ServiceSessionPump>(backend,"a",initial,[this]{return now;});}
    void advance(int seconds){now+=std::chrono::seconds(seconds);}
    void drain(){for(auto& event:backend->pump())if(event.completion)event.completion();}
};
}
TEST(ServiceSessionPumpTest, InitialAuthorityAndOwnerMustBeValid) {
    Fixture f;
    EXPECT_THROW(ServiceSessionPump(nullptr,"a",f.initial),Service::ServiceOperationError);
    EXPECT_THROW(ServiceSessionPump(f.backend,"c",f.initial),Service::ServiceOperationError);
    auto invalid=f.initial;invalid.members.clear();
    try {ServiceSessionPump pump(f.backend,"a",invalid);FAIL()<<"Expected invalid authority";}
    catch(const Service::ServiceOperationError& error){EXPECT_EQ("INVALID_RESPONSE",error.code);}
}
TEST(ServiceSessionPumpTest, AtMostOneRequestAndOwnerUpdatePublication) {
    Fixture f;auto pump=f.make();for(int index=0;index<100;++index)EXPECT_FALSE(pump->update());
    auto events=f.backend->pump();ASSERT_EQ(1U,events.size());
    auto observed=pump->update();ASSERT_TRUE(observed);ASSERT_TRUE(observed->snapshot);
    EXPECT_EQ(f.initial.session,observed->snapshot->session);EXPECT_EQ(2,observed->snapshot->currentGamers);
    EXPECT_FALSE(observed->renewed);EXPECT_TRUE(observed->failure.empty());EXPECT_FALSE(pump->update());
    EXPECT_TRUE(f.backend->pump().empty());
}
TEST(ServiceSessionPumpTest, MembershipAndGameplayChangesArriveThroughOwnedObservations) {
    Fixture f;auto pump=f.make();(void)pump->update();f.drain();ASSERT_TRUE(pump->update());
    auto& directory=f.backend->sessionDirectory();auto current=directory.join("c",{"c","d"},f.initial.session);
    Service::ServiceSessionSettings settings;settings.maxGamers=6;settings.properties[7]=42;
    settings.state=Service::ServiceSessionState::Playing;
    current=directory.update("a",current.session,current.revision,settings);
    f.advance(1);EXPECT_FALSE(pump->update());f.drain();auto observed=pump->update();
    ASSERT_TRUE(observed&&observed->snapshot);EXPECT_EQ(4,observed->snapshot->currentGamers);
    EXPECT_EQ(current.revision,observed->snapshot->revision);EXPECT_EQ(42,observed->snapshot->properties[7]);
    EXPECT_EQ(Service::ServiceSessionState::Playing,observed->snapshot->state);
}
TEST(ServiceSessionPumpTest, LeaseRenewalIsIndependentOfOrdinaryPolling) {
    Fixture f;auto pump=f.make();(void)pump->update();f.drain();ASSERT_TRUE(pump->update());
    f.advance(29);(void)pump->update();f.drain();auto read=pump->update();ASSERT_TRUE(read);EXPECT_FALSE(read->renewed);
    f.advance(1);(void)pump->update();f.drain();auto renewal=pump->update();ASSERT_TRUE(renewal);EXPECT_TRUE(renewal->renewed);
    f.advance(1);(void)pump->update();f.drain();read=pump->update();ASSERT_TRUE(read);EXPECT_FALSE(read->renewed);
}
TEST(ServiceSessionPumpTest, FailurePublishesOnceAndRetryRenewsAtTheSameOrigin) {
    Fixture f;auto pump=f.make();f.backend->signOut(0);f.drain();
    (void)pump->update();f.drain();auto failure=pump->update();ASSERT_TRUE(failure);
    EXPECT_FALSE(failure->snapshot);EXPECT_FALSE(failure->failure.empty());
    f.advance(60);EXPECT_FALSE(pump->update());EXPECT_TRUE(f.backend->pump().empty());
    f.backend->signIn(0,"Taga","fixture");f.drain();pump->retry();EXPECT_FALSE(pump->update());f.drain();
    auto recovered=pump->update();ASSERT_TRUE(recovered&&recovered->snapshot);EXPECT_TRUE(recovered->renewed);
    EXPECT_EQ(f.initial.machine,recovered->snapshot->machine);
}
TEST(ServiceSessionPumpTest, HostClosureIsAStableFailureAndDoesNotScheduleForever) {
    Fixture f;auto pump=f.make();EXPECT_TRUE(f.backend->sessionDirectory().leave("a",f.initial.session));
    (void)pump->update();f.drain();auto failed=pump->update();ASSERT_TRUE(failed);
    EXPECT_EQ("NOT_FOUND",failed->failure);f.advance(300);EXPECT_FALSE(pump->update());EXPECT_TRUE(f.backend->pump().empty());
}
TEST(ServiceSessionPumpTest, CancellationSuppressesQueuedAndReadyWorkWithoutReleasingMembership) {
    Fixture f;auto pump=f.make();(void)pump->update();pump->cancel();f.drain();EXPECT_FALSE(pump->update());
    EXPECT_THROW(pump->retry(),System::InvalidOperationException);
    EXPECT_EQ(2,f.backend->sessionDirectory().get("a",f.initial.session).currentGamers);
    pump=f.make();(void)pump->update();f.drain();pump->cancel();EXPECT_FALSE(pump->update());
}
TEST(ServiceSessionPumpTest, OriginRetentionDoesNotBecomeAQueuedOwnershipCycle) {
    Fixture f;auto pump=f.make();(void)pump->update();std::weak_ptr<Service::IGamerServicesBackend> weak=f.backend;
    f.backend.reset();EXPECT_FALSE(weak.expired());pump.reset();EXPECT_TRUE(weak.expired());
}
TEST(ServiceSessionPumpTest, BusyRetryAndQueueSaturationFailDeterministically) {
    Fixture f;auto pump=f.make();(void)pump->update();EXPECT_THROW(pump->retry(),System::InvalidOperationException);
    f.drain();ASSERT_TRUE(pump->update());f.advance(1);
    for(int index=0;index<128;++index)f.backend->submit([]{},{});
    auto failure=pump->update();ASSERT_TRUE(failure);EXPECT_EQ("SESSION_SERVICE_UNAVAILABLE",failure->failure);
    for(int batch=0;batch<4;++batch)f.drain();pump->retry();(void)pump->update();f.drain();
    auto recovered=pump->update();ASSERT_TRUE(recovered&&recovered->snapshot);EXPECT_TRUE(recovered->renewed);
}
TEST(ServiceSessionPumpTest, HostPublicationRetriesAStaleRevisionAndRenewsTheLease) {
    Fixture f;auto pump=f.make();(void)pump->update();f.drain();ASSERT_TRUE(pump->update());
    // A remote machine joins after the pump's last read, advancing the revision it will send.
    const auto joined=f.backend->sessionDirectory().join("c",{"c"},f.initial.session);
    Service::ServiceSessionSettings settings;settings.maxGamers=6;settings.properties[1]=9;
    settings.state=Service::ServiceSessionState::Playing;pump->publish(settings);
    // Publication respects the 100 ms spacing from the previous request.
    EXPECT_FALSE(pump->update());EXPECT_TRUE(f.backend->pump().empty());
    f.now+=std::chrono::milliseconds(100);EXPECT_FALSE(pump->update());f.drain();auto published=pump->update();
    ASSERT_TRUE(published&&published->snapshot);EXPECT_TRUE(published->failure.empty());
    EXPECT_TRUE(published->published);EXPECT_TRUE(published->renewed);
    EXPECT_GT(published->snapshot->revision,joined.revision);EXPECT_EQ(9,published->snapshot->properties[1]);
    EXPECT_EQ(Service::ServiceSessionState::Playing,published->snapshot->state);
    // Nothing further is published until a new desire or the ordinary poll interval.
    EXPECT_FALSE(pump->update());EXPECT_TRUE(f.backend->pump().empty());
}
TEST(ServiceSessionPumpTest, LatestDesireWinsAndANewerDesireSurvivesAnInFlightPublication) {
    Fixture f;auto pump=f.make();(void)pump->update();f.drain();ASSERT_TRUE(pump->update());
    Service::ServiceSessionSettings first;first.maxGamers=6;first.properties[0]=1;
    auto second=first;second.properties[0]=2;auto third=first;third.properties[0]=3;
    pump->publish(first);pump->publish(second);f.now+=std::chrono::milliseconds(100);EXPECT_FALSE(pump->update());
    pump->publish(third);f.drain();auto observed=pump->update();
    ASSERT_TRUE(observed&&observed->snapshot);EXPECT_EQ(2,observed->snapshot->properties[0]);
    // The third desire is scheduled after the 100 ms request spacing, not dropped.
    EXPECT_TRUE(f.backend->pump().empty());f.now+=std::chrono::milliseconds(100);EXPECT_FALSE(pump->update());
    f.drain();observed=pump->update();ASSERT_TRUE(observed&&observed->snapshot);
    EXPECT_EQ(3,observed->snapshot->properties[0]);EXPECT_TRUE(observed->published);
}
TEST(ServiceSessionPumpTest, ExpeditedReadsAreCoalescedAndSpacedButNeverRequirePolling) {
    Fixture f;auto pump=f.make();(void)pump->update();
    pump->expedite();pump->expedite();f.drain();ASSERT_TRUE(pump->update());
    // The hint arrived while busy: one immediate follow-up read after the spacing, not a second poll.
    EXPECT_TRUE(f.backend->pump().empty());f.now+=std::chrono::milliseconds(100);(void)pump->update();
    auto events=f.backend->pump();EXPECT_EQ(1U,events.size());for(auto& event:events)if(event.completion)event.completion();
    ASSERT_TRUE(pump->update());pump->expedite();pump->expedite();f.now+=std::chrono::milliseconds(100);
    (void)pump->update();EXPECT_EQ(1U,f.backend->pump().size());
}
TEST(ServiceSessionPumpTest, ACanceledPumpRefusesPublicationAndAStoppedPumpIgnoresHints) {
    Fixture f;auto pump=f.make();pump->cancel();
    EXPECT_THROW(pump->publish({}),System::InvalidOperationException);EXPECT_NO_THROW(pump->expedite());
}
