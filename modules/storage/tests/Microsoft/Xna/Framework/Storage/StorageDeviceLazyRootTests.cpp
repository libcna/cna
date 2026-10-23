// SPDX-License-Identifier: MS-PL
// Museum B11: application identity is configuration, not a storage operation.
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>

#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"
#include "SharpRuntime/Storage/StoragePaths.hpp"
#include "System/IO/IOException.hpp"

// Desktop roots honor XDG_DATA_HOME; Android and browser roots have separate policies.
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace
{
namespace fs = std::filesystem;
using Microsoft::Xna::Framework::Storage::StorageDevice;
using Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException;
using SharpRuntime::Storage::StoragePaths;

class StorageDeviceLazyRootTest : public ::testing::Test
{
protected:
    void SetEnvironment(const char* value)
    {
#if defined(_WIN32)
        ASSERT_EQ(_putenv_s("XDG_DATA_HOME", value ? value : ""), 0);
#else
        ASSERT_EQ(value ? setenv("XDG_DATA_HOME", value, 1) : unsetenv("XDG_DATA_HOME"), 0);
#endif
    }

    void SetUp() override
    {
        if (const char* value = std::getenv("XDG_DATA_HOME")) previous_ = value;
        root_ = fs::temp_directory_path() /
            ("CnaStorageLazy-" + std::to_string(std::random_device{}()));
        ASSERT_TRUE(fs::create_directory(root_));
        owned_ = true;
        data_ = root_ / "data";
        SetEnvironment(data_.string().c_str());
    }

    void TearDown() override
    {
        SetEnvironment(previous_ ? previous_->c_str() : nullptr);
        StorageDevice::SetAppNameEXT("");
        if (owned_)
        {
            std::error_code error;
            fs::remove_all(root_, error);
        }
    }

    fs::path root_, data_;
    std::optional<std::string> previous_;
    bool owned_ = false;
};

TEST_F(StorageDeviceLazyRootTest, SettingTheNameDoesNotCreateAnyDirectory)
{
    ASSERT_NO_THROW(StorageDevice::SetAppNameEXT("first"));
    EXPECT_FALSE(fs::exists(data_));
    EXPECT_EQ(fs::path(StorageDevice::GetStorageRootEXT()), data_ / "first");
    EXPECT_TRUE(fs::is_directory(data_ / "first"));
}

TEST_F(StorageDeviceLazyRootTest, AnUnavailableRootFailsOnlyWhenStorageIsAccessed)
{
    std::ofstream(data_) << "not a directory";
    ASSERT_NO_THROW(StorageDevice::SetAppNameEXT("unavailable"));
    auto selection = StorageDevice::BeginShowSelector(nullptr, nullptr);
    auto device = StorageDevice::EndShowSelector(selection.get());
    EXPECT_FALSE(device->getIsConnectedProperty());
    auto opening = device->BeginOpenContainer("saves", nullptr, nullptr);
    // Genuine XNA construction reports a file in the root chain as IOException
    // (StorageContainerCreationOracle.cs); only the CNA extensions report a disconnected device.
    EXPECT_THROW((void)device->EndOpenContainer(opening.get()), System::IO::IOException);
    EXPECT_THROW((void)StoragePaths::GetIsolatedStorageRoot(), StorageDeviceNotConnectedException);
    EXPECT_TRUE(fs::is_regular_file(data_));
}

TEST_F(StorageDeviceLazyRootTest, SharpRuntimeFirstAccessUsesTheConfiguredApplication)
{
    StorageDevice::SetAppNameEXT("first");
    EXPECT_FALSE(fs::exists(data_));
    EXPECT_EQ(StoragePaths::GetIsolatedStorageRoot(), data_ / "first" / ".cna_isolated_storage");
    StorageDevice::SetAppNameEXT("second");
    EXPECT_FALSE(fs::exists(data_ / "second"));
    EXPECT_EQ(StoragePaths::GetIsolatedStorageRoot(), data_ / "second" / ".cna_isolated_storage");
    EXPECT_EQ(fs::path(StorageDevice::GetStorageRootEXT()), data_ / "second");
}

TEST_F(StorageDeviceLazyRootTest, ReplacingTheNameBeforeFirstAccessDoesNotCreateTheOldRoot)
{
    StorageDevice::SetAppNameEXT("unused");
    StorageDevice::SetAppNameEXT("selected");
    EXPECT_FALSE(fs::exists(data_));
    EXPECT_EQ(StoragePaths::GetIsolatedStorageRoot(), data_ / "selected" / ".cna_isolated_storage");
    EXPECT_FALSE(fs::exists(data_ / "unused"));
}

TEST_F(StorageDeviceLazyRootTest, AFailedAccessDoesNotCacheAnUnavailableRoot)
{
    std::ofstream(data_) << "temporary blocker";
    ASSERT_NO_THROW(StorageDevice::SetAppNameEXT("recoverable"));
    EXPECT_THROW((void)StoragePaths::GetIsolatedStorageRoot(), StorageDeviceNotConnectedException);
    ASSERT_TRUE(fs::remove(data_));
    EXPECT_EQ(StoragePaths::GetIsolatedStorageRoot(), data_ / "recoverable" / ".cna_isolated_storage");
    EXPECT_EQ(fs::path(StorageDevice::GetStorageRootEXT()), data_ / "recoverable");
}
}
#endif
