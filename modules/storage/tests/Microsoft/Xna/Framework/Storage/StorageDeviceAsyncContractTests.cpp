// SPDX-License-Identifier: MS-PL
// Genuine XNA public-call evidence: cna-demos StorageDeviceAsyncOracle.cs (46 checks).
#include <gtest/gtest.h>
#include <any>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <typeinfo>
#include <type_traits>

#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Threading/WaitHandle.hpp"

using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Storage::StorageDevice;

namespace
{
template<class Exception, class Action>
void ExpectExact(Action action, const char* parameter = nullptr)
{
    try
    {
        action();
        ADD_FAILURE() << "expected " << typeid(Exception).name();
    }
    catch (const Exception& error)
    {
        EXPECT_TRUE(typeid(error) == typeid(Exception)) << "actual " << typeid(error).name();
        if constexpr (std::is_base_of_v<System::ArgumentException, Exception>)
            if (parameter != nullptr) EXPECT_EQ(error.getParamNameProperty(), parameter);
    }
    catch (const std::exception& error)
    {
        ADD_FAILURE() << "wrong exception " << typeid(error).name() << ": " << error.what();
    }
}

std::unique_ptr<StorageDevice> Device()
{
    auto result = StorageDevice::BeginShowSelector(nullptr, nullptr);
    return StorageDevice::EndShowSelector(result.get());
}
}

TEST(StorageDeviceAsyncContractTest, SelectorCompletesSynchronouslyAndAcceptsTheXnaPlayerValues)
{
    for (const int player : {0, 1, 2, 3, 255})
    {
        SCOPED_TRACE(player);
        int state = 42;
        System::IAsyncResult* observed = nullptr;
        auto result = StorageDevice::BeginShowSelector(static_cast<PlayerIndex>(player), 0, -1,
            [&](System::IAsyncResult* value)
            {
                observed = value;
                EXPECT_TRUE(value->getIsCompletedProperty());
                EXPECT_TRUE(value->getCompletedSynchronouslyProperty());
                EXPECT_EQ(std::any_cast<void*>(value->getAsyncStateProperty()), &state);
            }, &state);
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result.get(), observed);
        EXPECT_TRUE(result->getAsyncWaitHandleProperty().WaitOne(0));
        EXPECT_NE(StorageDevice::EndShowSelector(result.get()), nullptr);
    }
}

TEST(StorageDeviceAsyncContractTest, NegativeSizeIsRejectedBeforeInvokingEitherSelectorCallback)
{
    bool callback = false;
    auto notify = [&](System::IAsyncResult*) { callback = true; };
    ExpectExact<System::ArgumentOutOfRangeException>([&]
        { (void)StorageDevice::BeginShowSelector(-1, 0, notify, nullptr); }, "sizeInBytes");
    ExpectExact<System::ArgumentOutOfRangeException>([&]
        { (void)StorageDevice::BeginShowSelector(PlayerIndex::One, -1, 0, notify, nullptr); }, "sizeInBytes");
    EXPECT_FALSE(callback);
}

TEST(StorageDeviceAsyncContractTest, InvalidPlayerIsRejectedBeforeSizeAndThroughTheShortOverload)
{
    for (const int player : {-1, 4, 254, 256})
    {
        SCOPED_TRACE(player);
        ExpectExact<System::ArgumentOutOfRangeException>([&]
            { (void)StorageDevice::BeginShowSelector(static_cast<PlayerIndex>(player), -1, -1, nullptr, nullptr); }, "player");
        ExpectExact<System::ArgumentOutOfRangeException>([&]
            { (void)StorageDevice::BeginShowSelector(static_cast<PlayerIndex>(player), nullptr, nullptr); }, "player");
    }
}

TEST(StorageDeviceAsyncContractTest, SelectorCanOnlyEndOnceIncludingInsideItsCallback)
{
    auto result = StorageDevice::BeginShowSelector(nullptr, nullptr);
    EXPECT_NE(StorageDevice::EndShowSelector(result.get()), nullptr);
    ExpectExact<System::InvalidOperationException>([&] { (void)StorageDevice::EndShowSelector(result.get()); });
    auto consumed = StorageDevice::BeginShowSelector([](System::IAsyncResult* value)
        { EXPECT_NE(StorageDevice::EndShowSelector(value), nullptr); }, nullptr);
    ExpectExact<System::InvalidOperationException>([&] { (void)StorageDevice::EndShowSelector(consumed.get()); });
}

TEST(StorageDeviceAsyncContractTest, NullAndMismatchedResultTypesHaveTheExactXnaException)
{
    auto owner = Device();
    ExpectExact<System::ArgumentNullException>([] { (void)StorageDevice::EndShowSelector(nullptr); }, "result");
    ExpectExact<System::ArgumentNullException>([&] { (void)owner->EndOpenContainer(nullptr); }, "result");
    auto selector = StorageDevice::BeginShowSelector(nullptr, nullptr);
    ExpectExact<System::ArgumentNullException>([&] { (void)owner->EndOpenContainer(selector.get()); }, "result");
    EXPECT_NE(StorageDevice::EndShowSelector(selector.get()), nullptr);
    auto container = owner->BeginOpenContainer("", nullptr, nullptr);
    ExpectExact<System::ArgumentNullException>([&] { (void)StorageDevice::EndShowSelector(container.get()); }, "result");
    ExpectExact<System::ArgumentNullException>([&] { (void)owner->EndOpenContainer(container.get()); });
    ExpectExact<System::InvalidOperationException>([&] { (void)owner->EndOpenContainer(container.get()); });
}

TEST(StorageDeviceAsyncContractTest, EmptyNameValidationIsDeferredAndFailedEndConsumesTheResult)
{
    auto owner = Device();
    int state = 42;
    System::IAsyncResult* observed = nullptr;
    auto result = owner->BeginOpenContainer("", [&](System::IAsyncResult* value) { observed = value; }, &state);
    EXPECT_EQ(observed, result.get());
    EXPECT_EQ(std::any_cast<void*>(result->getAsyncStateProperty()), &state);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_TRUE(result->getCompletedSynchronouslyProperty());
    EXPECT_TRUE(result->getAsyncWaitHandleProperty().WaitOne(0));
    ExpectExact<System::ArgumentNullException>([&] { (void)owner->EndOpenContainer(result.get()); },
        "You must pass in a non-null title name.");
    ExpectExact<System::InvalidOperationException>([&] { (void)owner->EndOpenContainer(result.get()); });
}

TEST(StorageDeviceAsyncContractTest, WrongOwnerConsumesTheResultBeforeDisplayNameValidation)
{
    auto owner = Device(), other = Device();
    auto result = owner->BeginOpenContainer("", nullptr, nullptr);
    ExpectExact<System::ArgumentException>([&] { (void)other->EndOpenContainer(result.get()); },
        "The IAsyncResult parameter to this \"End\" function must be obtained by calling the matching \"Begin.\"");
    ExpectExact<System::InvalidOperationException>([&] { (void)owner->EndOpenContainer(result.get()); });
}

TEST(StorageDeviceAsyncContractTest, ContainerEndInsideItsCallbackConsumesTheResultEvenWhenItFails)
{
    auto owner = Device();
    auto result = owner->BeginOpenContainer("", [&](System::IAsyncResult* value)
        { ExpectExact<System::ArgumentNullException>([&] { (void)owner->EndOpenContainer(value); }); }, nullptr);
    ExpectExact<System::InvalidOperationException>([&] { (void)owner->EndOpenContainer(result.get()); });
}

TEST(StorageDeviceAsyncContractTest, SuccessfulContainerEndIsOneShotAndTheSharedPlayerSentinelUsesAllPlayers)
{
    namespace fs = std::filesystem;
    std::random_device entropy;
    const std::string name = "CnaStorageAsync-" + std::to_string(entropy()) + "-" + std::to_string(entropy());
    const fs::path fixture = fs::path(StorageDevice::GetStorageRootEXT()) / name;
    ASSERT_TRUE(fs::create_directory(fixture));
    struct Cleanup
    {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{fixture}; // only the newly-created, uniquely named test fixture
    auto selected = StorageDevice::BeginShowSelector(static_cast<PlayerIndex>(255), nullptr, nullptr);
    auto owner = StorageDevice::EndShowSelector(selected.get());
    auto result = owner->BeginOpenContainer(name, nullptr, nullptr);
    auto container = owner->EndOpenContainer(result.get());
    ASSERT_NE(container, nullptr);
    EXPECT_EQ(&container->getStorageDeviceProperty(), owner.get());
    EXPECT_TRUE(fs::is_directory(fixture / "AllPlayers"));
    EXPECT_FALSE(fs::exists(fixture / "Player256"));
    ExpectExact<System::InvalidOperationException>([&] { (void)owner->EndOpenContainer(result.get()); });
}
