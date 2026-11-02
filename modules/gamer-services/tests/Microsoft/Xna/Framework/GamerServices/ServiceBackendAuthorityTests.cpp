// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/GamerServices/BackendConfiguration.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "../../../../../src/Internal/ServiceAsyncResult.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <semaphore>
#include <thread>

namespace {
namespace Service=CNA::Internal::GamerServices;
namespace Deployment=CNA::GamerServices;
using Unavailable=Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;
class ServiceBackendAuthorityTest : public ::testing::Test {
protected:
    void SetUp() override {previous=Service::backend();}
    void TearDown() override {Service::setBackendForTesting(previous);Deployment::setConfigurationOverride({});}
    std::shared_ptr<Service::IGamerServicesBackend> configured(Deployment::Configuration value) {
        Deployment::setConfigurationOverride(std::move(value));Service::setBackendForTesting({});return Service::backend();
    }
    std::shared_ptr<Service::IGamerServicesBackend> previous;
};
}
TEST_F(ServiceBackendAuthorityTest, LaterDeploymentOverrideCannotRedirectAnExistingBackend) {
    const Deployment::Configuration first{"https://first.example/cna/v1","first"};
    const auto origin=configured(first);
    Deployment::setConfigurationOverride(Deployment::Configuration{"https://second.example/cna/v1","second","/tmp/unused-second-trust.pem"});
    EXPECT_EQ("second",Deployment::resolveConfiguration().gameId);
    const auto captured=Service::configurationForBackend(*origin);
    EXPECT_EQ(first.endpoint,captured.endpoint);EXPECT_EQ(first.gameId,captured.gameId);
    EXPECT_EQ(first.caBundle,captured.caBundle);EXPECT_FALSE(captured.insecureLoopback);
}
TEST_F(ServiceBackendAuthorityTest, ReplacementAndReturnedCopyDoNotMutateOriginalTitleAuthority) {
    const auto first=configured({"https://first.example/cna/v1","first"});
    const auto second=configured({"https://second.example/cna/v1","second"});
    auto copy=Service::configurationForBackend(*first);copy.endpoint="https://other.example/cna/v1";copy.gameId="other";
    EXPECT_EQ("first",Service::configurationForBackend(*first).gameId);
    EXPECT_EQ("https://first.example/cna/v1",Service::configurationForBackend(*first).endpoint);
    EXPECT_EQ("second",Service::configurationForBackend(*second).gameId);
}
TEST_F(ServiceBackendAuthorityTest, FakeAndUnconfiguredBackendsHaveNoImplicitNetworkAuthority) {
    const auto fake=Service::makeFakeBackend({});EXPECT_THROW((void)Service::configurationForBackend(*fake),Unavailable);
    const auto offline=configured({});EXPECT_THROW((void)Service::configurationForBackend(*offline),Unavailable);
}
TEST_F(ServiceBackendAuthorityTest, ExplicitLoopbackDevelopmentChoiceIsBoundToItsOrigin) {
    const auto origin=configured({"http://127.0.0.1:61199/cna/v1","development",{},true});
    Deployment::setConfigurationOverride(Deployment::Configuration{"https://second.example/cna/v1","second"});
    const auto captured=Service::configurationForBackend(*origin);
    EXPECT_EQ("http://127.0.0.1:61199/cna/v1",captured.endpoint);EXPECT_EQ("development",captured.gameId);
    EXPECT_TRUE(captured.insecureLoopback);
}

#ifndef __EMSCRIPTEN__
namespace {
template<class T>
void RequireWorkerReady(std::future<T>& future)
{
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
    {
        std::fputs("service worker interleaving exceeded five seconds\n", stderr);
        std::abort();
    }
}
}

TEST_F(ServiceBackendAuthorityTest, AbandoningResultWhileWorkerRunsSuppressesItsCompletion)
{
    auto service = configured({});
    std::binary_semaphore release{0};
    std::promise<void> entered, following;
    auto enteredFuture = entered.get_future();
    auto followingFuture = following.get_future();
    std::atomic<bool> timedOut{false};
    int callbacks = 0;
    std::unique_ptr<System::IAsyncResult> result(Service::ServiceAsyncResult::begin(
        "safe005", nullptr, [&](Service::IGamerServicesBackend&) -> std::any
        {
            entered.set_value();
            if (!release.try_acquire_for(std::chrono::seconds(5))) timedOut.store(true);
            return 42;
        }, [&](auto&) { ++callbacks; }, {}, service));
    RequireWorkerReady(enteredFuture);
    result.reset(); // Release belongs to the owner; the logical work remains in flight.
    service->submit([&] { following.set_value(); }, {});
    release.release();
    RequireWorkerReady(followingFuture); // FIFO worker has enqueued the abandoned completion.
    auto events = service->pump();
    ASSERT_FALSE(events.empty());
    for (auto& event : events) if (event.completion) event.completion();
    EXPECT_FALSE(timedOut.load());
    EXPECT_EQ(callbacks, 0);
}

TEST_F(ServiceBackendAuthorityTest, AbandoningResultAlreadyInDispatchSnapshotSuppressesCallback)
{
    auto service = configured({});
    std::promise<void> following;
    auto followingFuture = following.get_future();
    int callbacks = 0;
    std::unique_ptr<System::IAsyncResult> result(Service::ServiceAsyncResult::begin(
        "safe005", nullptr, [](auto&) -> std::any { return 42; },
        [&](auto&) { ++callbacks; }, {}, service));
    service->submit([&] { following.set_value(); }, {});
    RequireWorkerReady(followingFuture);
    auto events = service->pump();
    ASSERT_FALSE(events.empty());
    EXPECT_FALSE(result->getIsCompletedProperty()); // Publication belongs to dispatch, not work.
    result.reset();
    for (auto& event : events) if (event.completion) event.completion();
    EXPECT_EQ(callbacks, 0);
}

TEST_F(ServiceBackendAuthorityTest, WorkerCompletionPublishesExactlyOnceOnDispatchOwner)
{
    auto service = configured({});
    std::promise<void> following;
    auto followingFuture = following.get_future();
    const auto owner = std::this_thread::get_id();
    std::thread::id worker;
    int callbacks = 0;
    std::unique_ptr<System::IAsyncResult> result(Service::ServiceAsyncResult::begin(
        "safe005", nullptr, [&](auto&) -> std::any
        {
            worker = std::this_thread::get_id();
            return 42;
        }, [&](auto& value)
        {
            ++callbacks;
            EXPECT_EQ(std::this_thread::get_id(), owner);
            EXPECT_TRUE(value.getIsCompletedProperty());
            EXPECT_TRUE(value.getAsyncWaitHandleProperty().WaitOne(0));
        }, {}, service));
    service->submit([&] { following.set_value(); }, {});
    RequireWorkerReady(followingFuture);
    EXPECT_NE(worker, owner);
    EXPECT_EQ(callbacks, 0);
    EXPECT_FALSE(result->getIsCompletedProperty());
    auto events = service->pump();
    ASSERT_FALSE(events.empty());
    for (auto& event : events) if (event.completion) event.completion();
    EXPECT_EQ(callbacks, 1);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(Service::ServiceAsyncResult::end(result.get(), "safe005", nullptr)), 42);
    for (auto& event : service->pump()) if (event.completion) event.completion();
    EXPECT_EQ(callbacks, 1);
}

TEST_F(ServiceBackendAuthorityTest, BackendDestructionJoinsInFlightWorkWithoutDispatchingCallback)
{
    auto service = configured({});
    Service::setBackendForTesting(previous);
    std::binary_semaphore release{0};
    std::promise<void> entered, destroying, destroyed;
    auto enteredFuture = entered.get_future();
    auto destroyingFuture = destroying.get_future();
    auto destroyedFuture = destroyed.get_future();
    std::atomic<bool> timedOut{false}, workExited{false}, premature{false};
    int callbacks = 0;
    service->submit([&]
    {
        entered.set_value();
        if (!release.try_acquire_for(std::chrono::seconds(5))) timedOut.store(true);
        workExited.store(true);
    }, [&] { ++callbacks; });
    RequireWorkerReady(enteredFuture);
    std::thread teardown([&, owned = std::move(service)]() mutable
    {
        destroying.set_value();
        owned.reset();
        premature.store(!workExited.load());
        destroyed.set_value();
    });
    RequireWorkerReady(destroyingFuture);
    EXPECT_EQ(destroyedFuture.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
    release.release();
    RequireWorkerReady(destroyedFuture);
    teardown.join();
    EXPECT_TRUE(workExited.load());
    EXPECT_FALSE(timedOut.load());
    EXPECT_FALSE(premature.load());
    EXPECT_EQ(callbacks, 0);
}
#endif
