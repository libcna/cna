// SPDX-License-Identifier: MS-PL
// Genuine XNA construction evidence: cna-demos StorageContainerCreationOracle.cs. XNA's
// EndOpenContainer constructs the container, whose DirectoryInfo.Create creates the whole chain
// beneath the device folder and lets the System.IO failure escape unwrapped.
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <typeinfo>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/IO/IOException.hpp"
#include "System/UnauthorizedAccessException.hpp"

// Desktop roots honor XDG_DATA_HOME; Android and browser roots have separate policies.
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace
{
namespace fs = std::filesystem;
using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Storage::StorageContainer;
using Microsoft::Xna::Framework::Storage::StorageDevice;

template<class Exception, class Action>
void Exact(Action action)
{
    try { action(); ADD_FAILURE() << "expected " << typeid(Exception).name(); }
    catch (const std::exception& e)
    {
        EXPECT_TRUE(typeid(e) == typeid(Exception))
            << "wrong exception: " << typeid(e).name() << ": " << e.what();
    }
}

std::string ReadAll(const fs::path& path)
{
    std::ifstream stream(path);
    return std::string(std::istreambuf_iterator<char>(stream), {});
}

class StorageContainerCreationTest : public ::testing::Test
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
            ("CnaStorageCreation-" + std::to_string(std::random_device{}()));
        ASSERT_TRUE(fs::create_directory(root_));
        owned_ = true;
        data_ = root_ / "data";
        app_ = data_ / "title";
        SetEnvironment(data_.string().c_str());
        StorageDevice::SetAppNameEXT("title");
    }

    void TearDown() override
    {
        SetEnvironment(previous_ ? previous_->c_str() : nullptr);
        StorageDevice::SetAppNameEXT("");
        if (owned_)
        {
            std::error_code error;
            fs::permissions(app_ / "denied", fs::perms::owner_all, fs::perm_options::add, error);
            fs::remove_all(root_, error);
        }
    }

    static std::unique_ptr<StorageDevice> Device(std::optional<PlayerIndex> player = std::nullopt)
    {
        auto selection = player ? StorageDevice::BeginShowSelector(*player, nullptr, nullptr)
                                : StorageDevice::BeginShowSelector(nullptr, nullptr);
        return StorageDevice::EndShowSelector(selection.get());
    }

    static void Seed(const fs::path& path)
    {
        std::ofstream(path) << "seed";
        ASSERT_TRUE(fs::is_regular_file(path));
    }

    // Begin completes without touching the filesystem; End constructs, reports the failure as
    // the exact System type, and consumes its result.
    template<class Exception>
    static void ExpectBlocked(StorageDevice& device, const std::string& name)
    {
        bool callback = false;
        std::unique_ptr<System::IAsyncResult> opening;
        ASSERT_NO_THROW(opening = device.BeginOpenContainer(
            name, [&](System::IAsyncResult*) { callback = true; }, nullptr));
        EXPECT_TRUE(callback);
        EXPECT_TRUE(opening->getIsCompletedProperty());
        Exact<Exception>([&] { (void)device.EndOpenContainer(opening.get()); });
        Exact<System::InvalidOperationException>([&] { (void)device.EndOpenContainer(opening.get()); });
    }

    static std::unique_ptr<StorageContainer> Open(StorageDevice& device, const std::string& name)
    {
        auto opening = device.BeginOpenContainer(name, nullptr, nullptr);
        return device.EndOpenContainer(opening.get());
    }

    fs::path root_, data_, app_;
    std::optional<std::string> previous_;
    bool owned_ = false;
};

TEST_F(StorageContainerCreationTest, ConstructionCreatesTheWholeChain)
{
    auto device = Device();
    EXPECT_FALSE(fs::exists(data_));
    EXPECT_NE(Open(*device, "control"), nullptr);
    EXPECT_TRUE(fs::is_directory(app_ / "control" / "AllPlayers"));
}

TEST_F(StorageContainerCreationTest, AFileAtTheContainerNameIsAnIOExceptionAndIsKept)
{
    ASSERT_TRUE(fs::create_directories(app_));
    Seed(app_ / "blocked");
    auto device = Device();
    ExpectBlocked<System::IO::IOException>(*device, "blocked");
    EXPECT_EQ(ReadAll(app_ / "blocked"), "seed");

    ASSERT_TRUE(fs::remove(app_ / "blocked"));
    EXPECT_NE(Open(*device, "blocked"), nullptr);
    EXPECT_TRUE(fs::is_directory(app_ / "blocked" / "AllPlayers"));
}

TEST_F(StorageContainerCreationTest, AFileAtThePlayerFolderIsAnIOException)
{
    ASSERT_TRUE(fs::create_directories(app_ / "player-blocked"));
    Seed(app_ / "player-blocked" / "Player1");
    auto device = Device(PlayerIndex::One);
    ExpectBlocked<System::IO::IOException>(*device, "player-blocked");
    EXPECT_EQ(ReadAll(app_ / "player-blocked" / "Player1"), "seed");
}

TEST_F(StorageContainerCreationTest, AFileAtTheApplicationFolderIsAnIOException)
{
    ASSERT_TRUE(fs::create_directories(data_));
    Seed(app_);
    auto device = Device();
    ExpectBlocked<System::IO::IOException>(*device, "any");
    EXPECT_EQ(ReadAll(app_), "seed");
}

TEST_F(StorageContainerCreationTest, AFileAtTheDataFolderIsAnIOException)
{
    Seed(data_);
    auto device = Device();
    ExpectBlocked<System::IO::IOException>(*device, "any");
    EXPECT_EQ(ReadAll(data_), "seed");

    ASSERT_TRUE(fs::remove(data_));
    EXPECT_NE(Open(*device, "any"), nullptr);
    EXPECT_TRUE(fs::is_directory(app_ / "any" / "AllPlayers"));
}

#if !defined(_WIN32)
TEST_F(StorageContainerCreationTest, AnUnwritableParentIsAnUnauthorizedAccessException)
{
    if (::geteuid() == 0) GTEST_SKIP() << "permissions do not bind the superuser";
    ASSERT_TRUE(fs::create_directories(app_ / "denied"));
    fs::permissions(app_ / "denied", fs::perms::owner_read | fs::perms::owner_exec);
    auto device = Device();
    ExpectBlocked<System::UnauthorizedAccessException>(*device, "denied");
    EXPECT_FALSE(fs::exists(app_ / "denied" / "AllPlayers"));
}

TEST_F(StorageContainerCreationTest, CreateDirectoryBeneathAnUnwritableDirectoryIsUnauthorizedAccess)
{
    if (::geteuid() == 0) GTEST_SKIP() << "permissions do not bind the superuser";
    auto device = Device();
    auto container = Open(*device, "writable");
    const fs::path folder = app_ / "writable" / "AllPlayers";
    fs::permissions(folder, fs::perms::owner_read | fs::perms::owner_exec);
    Exact<System::UnauthorizedAccessException>([&] { container->CreateDirectory("child"); });
    fs::permissions(folder, fs::perms::owner_all);
    EXPECT_FALSE(fs::exists(folder / "child"));
}
#endif
}
#endif
