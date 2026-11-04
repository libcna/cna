// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>
#include <stdexcept>
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "System/Threading/EventWaitHandle.hpp"
using Microsoft::Xna::Framework::Storage::StorageDevice;

TEST(StorageAsyncSecondPassTest, CompletionTracksTheExposedManualResetHandle)
{
    // XNA both AsyncResult .ctors IL_0000-0007 create ManualResetEvent(true).
    // get_AsyncWaitHandle IL_0000-0006 returns that field. get_IsCompleted calls
    // WaitOne(0): StorageDeviceAsyncResult IL_0000-000d; container result IL_0000-000c.
    auto selector = StorageDevice::BeginShowSelector(nullptr, nullptr);
    auto device = StorageDevice::EndShowSelector(selector.get());
    auto opening = device->BeginOpenContainer("", nullptr, nullptr);
    for (auto* result : {selector.get(), opening.get()})
    {
        auto& handle = result->getAsyncWaitHandleProperty();
        EXPECT_EQ(&handle, &result->getAsyncWaitHandleProperty());
        EXPECT_TRUE(handle.WaitOne(0));
        EXPECT_TRUE(handle.WaitOne(0)); // observation must not reset a manual-reset handle
        auto* event = dynamic_cast<System::Threading::EventWaitHandle*>(&handle);
        ASSERT_NE(event, nullptr);
        event->Reset();
        EXPECT_FALSE(result->getIsCompletedProperty());
        EXPECT_TRUE(result->getCompletedSynchronouslyProperty());
        EXPECT_EQ(std::any_cast<void*>(result->getAsyncStateProperty()), nullptr);
        event->Set();
        EXPECT_TRUE(result->getIsCompletedProperty());
        EXPECT_TRUE(handle.WaitOne(0));
    }
}

TEST(StorageAsyncSecondPassTest, ThrowingCallbacksPropagateWithoutReturningAResult)
{
    // XNA BeginShowSelector IL_0036-0038 and BeginOpenContainer IL_0012-0014
    // invoke directly, with no exception handler. No filesystem operation is performed.
    struct CallbackError : std::runtime_error { CallbackError() : std::runtime_error("callback") {} };
    int calls = 0;
    auto callback = [&](System::IAsyncResult* result)
    {
        ++calls;
        EXPECT_TRUE(result->getIsCompletedProperty());
        throw CallbackError();
    };
    std::unique_ptr<System::IAsyncResult> result;
    EXPECT_THROW(result = StorageDevice::BeginShowSelector(callback, nullptr), CallbackError);
    EXPECT_EQ(result, nullptr);
    auto selected = StorageDevice::BeginShowSelector(nullptr, nullptr);
    auto device = StorageDevice::EndShowSelector(selected.get());
    EXPECT_THROW(result = device->BeginOpenContainer("", callback, nullptr), CallbackError);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(calls, 2);
}
