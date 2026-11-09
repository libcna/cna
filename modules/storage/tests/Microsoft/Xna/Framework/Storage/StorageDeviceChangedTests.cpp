// SPDX-License-Identifier: MS-PL
//
// StorageDevice.DeviceChanged. Oracle: the XNA 4.0 Microsoft.Xna.Framework.Storage.dll IL.
// The event is static: add_DeviceChanged/remove_DeviceChanged are static accessors that
// Delegate.Combine/Delegate.Remove one process-wide field (IL_000a). Its only invocation is the
// private OnDeviceChanged (IL_0000-IL_000c, Invoke(null, args)), which StorageDevice..cctor
// registers with FrameworkCallbackLinker (IL_0000-IL_000c); Microsoft.Xna.Framework.dll calls it
// only from FrameworkDispatcher.Update for a native storage-device notification
// (Update IL_008e-IL_0090, IL_00ec-IL_00f1). No StorageDevice or StorageContainer method touches
// the field. CNA's producer (HND-TQ-04, owner decision 2026-11-09, option 1) is the storage root's
// volume -- a volume arriving or leaving at or above the root -- polled from
// FrameworkDispatcher::Update() through the slot the storage module installs.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "CNA/Internal/FrameworkNotifications.hpp"
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

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/stat.h>

namespace
{
    // The dispatcher's storage slot, as FrameworkDispatcher::Update() runs it.
    void DispatchStorageNotifications()
    {
        auto* const poll = CNA::Internal::FrameworkNotifications::StorageDevices.load();
        ASSERT_NE(poll, nullptr) << "linking StorageDevice installs its poll";
        poll();
    }

    unsigned long long DeviceOf(const fs::path& path)
    {
        struct stat status {};
        return ::stat(path.c_str(), &status) == 0 ? static_cast<unsigned long long>(status.st_dev) : 0ull;
    }

    void PointAt(const fs::path& link, const fs::path& target)
    {
        std::error_code ignored;
        fs::remove(link, ignored);
        fs::create_directory_symlink(target, link);
    }
}

// HND-TQ-04, option 1: a change of the volume holding the storage root raises DeviceChanged once
// per transition, with a null sender, at the dispatcher's next update; deleting the root on the
// same volume, a quiet update and a change of the root itself (a new baseline) do not. The volume
// switch is real: the root's parent is a symlink retargeted between a tmpfs directory (/dev/shm)
// and one on the test's own filesystem, so the stat of the root's deepest existing ancestor moves
// between two devices exactly as an unmount or a mount at that point would move it.
TEST_F(StorageDeviceChangedTest, AVolumeArrivingOrLeavingAtTheRootRaisesItAtTheNextUpdate)
{
    const fs::path disk = fs::current_path() / ("cna-volume-disk-" + name_);
    const fs::path memory = fs::path("/dev/shm") / ("cna-volume-shm-" + name_);
    const fs::path base = fs::current_path() / ("cna-volume-base-" + name_);
    std::error_code ignored;
    if (!fs::is_directory("/dev/shm") || !fs::create_directories(memory, ignored))
        GTEST_SKIP() << "no writable /dev/shm";
    fs::create_directories(disk);
    fs::create_directories(base);
    struct Cleanup
    {
        std::vector<fs::path> paths;
        std::string xdg;
        bool hadXdg = false;
        ~Cleanup()
        {
            if (hadXdg) ::setenv("XDG_DATA_HOME", xdg.c_str(), 1); else ::unsetenv("XDG_DATA_HOME");
            std::error_code error;
            for (const fs::path& path : paths)
                if (path.filename().string().find("cna-volume-") == 0) fs::remove_all(path, error);
        }
    } cleanup{{disk, memory, base}};
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) { cleanup.xdg = xdg; cleanup.hadXdg = true; }
    if (DeviceOf(disk) == DeviceOf(memory) || DeviceOf(disk) == 0)
        GTEST_SKIP() << "/dev/shm and the working directory are one filesystem here";

    const fs::path link = base / "volume";
    PointAt(link, memory);
    ::setenv("XDG_DATA_HOME", link.c_str(), 1);
    StorageDevice::SetAppNameEXT(name_ + "-volume");
    const fs::path root = StorageDevice::GetStorageRootEXT();
    ASSERT_EQ(root.parent_path(), link);

    int raised = 0;
    System::Object* sender = reinterpret_cast<System::Object*>(this);
    tokens_.push_back(StorageDevice::DeviceChanged.Add(
        [&](System::Object* from, const System::EventArgs&) { ++raised; sender = from; }));

    DispatchStorageNotifications();     // the first poll is the baseline
    DispatchStorageNotifications();
    EXPECT_EQ(raised, 0);

    PointAt(link, disk);                // the root's volume leaves; another one is there
    EXPECT_EQ(raised, 0) << "nothing is delivered before the dispatcher runs";
    DispatchStorageNotifications();
    EXPECT_EQ(raised, 1);
    EXPECT_EQ(sender, nullptr);
    DispatchStorageNotifications();
    EXPECT_EQ(raised, 1) << "one transition, one event";

    fs::create_directories(root);       // the save directory comes and goes on the same volume
    DispatchStorageNotifications();
    fs::remove_all(root);
    DispatchStorageNotifications();
    EXPECT_EQ(raised, 1);

    PointAt(link, memory);              // and the first volume arrives again
    DispatchStorageNotifications();
    EXPECT_EQ(raised, 2);

    StorageDevice::SetAppNameEXT(name_ + "-other");
    (void)StorageDevice::GetStorageRootEXT();
    PointAt(link, disk);                // a new root starts a new baseline, whatever volume it is on
    DispatchStorageNotifications();
    EXPECT_EQ(raised, 2);
}
#endif
