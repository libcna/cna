// SPDX-License-Identifier: MS-PL
//
// StorageDevice.DeviceChanged. Oracle: the XNA 4.0 Microsoft.Xna.Framework.Storage.dll IL.
// The event is static: add_DeviceChanged/remove_DeviceChanged are static accessors that
// Delegate.Combine/Delegate.Remove one process-wide field (IL_000a). Its only invocation is the
// private OnDeviceChanged (IL_0000-IL_000c, Invoke(null, args)), which StorageDevice..cctor
// registers with FrameworkCallbackLinker (IL_0000-IL_000c); Microsoft.Xna.Framework.dll calls it
// only from FrameworkDispatcher.Update for a native storage-device notification
// (Update IL_008e-IL_0090, IL_00ec-IL_00f1). No StorageDevice or StorageContainer method touches
// the field. CNA has no such native producer, so what is checkable here is that the event belongs
// to the process rather than to a device, and that ordinary storage use never raises it.

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageContainer.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/IO/Stream.hpp"

using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Storage::StorageDevice;

namespace
{
    namespace fs = std::filesystem;
    using Token = System::EventHandler<System::EventArgs>::Token;

    std::unique_ptr<StorageDevice> Select()
    {
        return StorageDevice::EndShowSelector(StorageDevice::BeginShowSelector(nullptr, nullptr).get());
    }

    class StorageDeviceChangedTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            std::random_device entropy;
            name_ = "CnaDeviceChanged-" + std::to_string(entropy()) + "-" + std::to_string(entropy());
            StorageDevice::SetAppNameEXT(name_);
            root_ = StorageDevice::GetStorageRootEXT();
            ASSERT_FALSE(root_.empty());
        }

        void TearDown() override
        {
            // The event is process-wide; leave no subscription behind for later tests.
            for (const Token token : tokens_) StorageDevice::DeviceChanged.Remove(token);
            std::error_code ignored;
            // Only this test's own uniquely named application root.
            if (fs::path(root_).filename().string().find("CnaDeviceChanged-") == 0)
                fs::remove_all(root_, ignored);
            StorageDevice::SetAppNameEXT("");
        }

        Token Subscribe(int& counter)
        {
            const Token token = StorageDevice::DeviceChanged.Add(
                [&counter](System::Object*, const System::EventArgs&) { ++counter; });
            tokens_.push_back(token);
            return token;
        }

        std::string name_;
        std::string root_;
        std::vector<Token> tokens_;
    };
}

// Every selector overload, the device properties, a container's whole life (open, write, delete,
// dispose), DeleteContainer and device destruction: none of them is the native notification, so
// none raises DeviceChanged.
TEST_F(StorageDeviceChangedTest, OrdinaryStorageUseNeverRaisesIt)
{
    int raised = 0;
    Subscribe(raised);

    auto shared = Select();
    auto player = StorageDevice::EndShowSelector(
        StorageDevice::BeginShowSelector(PlayerIndex::One, nullptr, nullptr).get());
    auto sized = StorageDevice::EndShowSelector(
        StorageDevice::BeginShowSelector(16, 1, nullptr, nullptr).get());
    auto sizedPlayer = StorageDevice::EndShowSelector(
        StorageDevice::BeginShowSelector(PlayerIndex::Two, 16, 1, nullptr, nullptr).get());
    EXPECT_TRUE(shared->getIsConnectedProperty());
    EXPECT_GE(shared->getTotalSpaceProperty(), shared->getFreeSpaceProperty());

    auto container = shared->EndOpenContainer(shared->BeginOpenContainer(name_, nullptr, nullptr).get());
    {
        auto stream = container->CreateFile("save.bin");
        const SharpRuntime::bytecs bytes[] = {1, 2, 3};
        stream->Write(bytes, 0, 3);
    }
    ASSERT_TRUE(container->FileExists("save.bin"));
    container->DeleteFile("save.bin");
    auto playerContainer =
        player->EndOpenContainer(player->BeginOpenContainer(name_, nullptr, nullptr).get());
    container->Dispose();
    container.reset();
    playerContainer.reset();
    shared->DeleteContainer(name_);
    player->DeleteContainer(name_);
    shared.reset();
    player.reset();
    sized.reset();
    sizedPlayer.reset();

    EXPECT_EQ(raised, 0);
}

// The subscriptions belong to the process: they exist before any device is selected and survive
// every device's selection, use and destruction. Raising the event from here stands in for the
// absent native producer only to observe which subscriptions are still attached; the arguments
// mirror XNA's (null sender, EventArgs.Empty).
TEST_F(StorageDeviceChangedTest, SubscriptionsAreProcessWideAndOutliveEveryDevice)
{
    int first = 0;
    int second = 0;
    const Token firstToken = Subscribe(first);
    Subscribe(second);

    {
        auto device = Select();
        auto container = device->EndOpenContainer(device->BeginOpenContainer(name_, nullptr, nullptr).get());
        container->Dispose();
        container.reset();
        device->DeleteContainer(name_);
    }
    {
        auto again = StorageDevice::EndShowSelector(
            StorageDevice::BeginShowSelector(PlayerIndex::Three, nullptr, nullptr).get());
    }

    StorageDevice::DeviceChanged.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 1);

    // remove_DeviceChanged detaches exactly the one handler.
    StorageDevice::DeviceChanged.Remove(firstToken);
    StorageDevice::DeviceChanged.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
}
