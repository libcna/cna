// SPDX-License-Identifier: MS-PL
// Genuine XNA evidence: cna-demos StorageResidualPathsOracle.cs (real DeleteContainer and container
// operations in an isolated Wine profile) and DirectoryDeletionErrorsOracle.cs (actual .NET 8).
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <typeinfo>
#include <vector>

#if !defined(_WIN32)
#  include <unistd.h>
#endif

#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDeviceNotConnectedException.hpp"
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

class StorageResidualPathsTest : public ::testing::Test
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
            ("CnaStorageResidual-" + std::to_string(std::random_device{}()));
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
            for (const fs::path& path : restore_)
                fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, error);
            fs::remove_all(root_, error);
        }
    }

    static std::unique_ptr<StorageDevice> Device(std::optional<PlayerIndex> player = std::nullopt)
    {
        auto selection = player ? StorageDevice::BeginShowSelector(*player, nullptr, nullptr)
                                : StorageDevice::BeginShowSelector(nullptr, nullptr);
        return StorageDevice::EndShowSelector(selection.get());
    }

    static std::unique_ptr<StorageContainer> Open(StorageDevice& device, const std::string& name)
    {
        auto opening = device.BeginOpenContainer(name, nullptr, nullptr);
        return device.EndOpenContainer(opening.get());
    }

    static void Seed(const fs::path& path)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << "seed";
        ASSERT_TRUE(fs::is_regular_file(path));
    }

    void Restrict(const fs::path& path, fs::perms permissions)
    {
        restore_.push_back(path);
        fs::permissions(path, permissions);
    }

    fs::path root_, data_, app_;
    std::optional<std::string> previous_;
    std::vector<fs::path> restore_;
    bool owned_ = false;
};

TEST_F(StorageResidualPathsTest, DeleteContainerRemovesOnlyThisDevicesPlayerFolder)
{
    auto shared = Device();
    auto one = Device(PlayerIndex::One);
    { auto c = Open(*shared, "both"); (void)c->CreateFile("shared.bin"); }
    { auto c = Open(*one, "both"); (void)c->CreateFile("one.bin"); }

    shared->DeleteContainer("both");
    EXPECT_FALSE(fs::exists(app_ / "both" / "AllPlayers"));
    EXPECT_TRUE(fs::is_regular_file(app_ / "both" / "Player1" / "one.bin"));
    EXPECT_TRUE(fs::is_directory(app_ / "both"));

    one->DeleteContainer("both");
    EXPECT_FALSE(fs::exists(app_ / "both" / "Player1"));
}

TEST_F(StorageResidualPathsTest, DeleteContainerOfAMissingContainerCreatesNothing)
{
    auto device = Device();
    EXPECT_NO_THROW(device->DeleteContainer("missing"));
    EXPECT_FALSE(fs::exists(data_));
}

TEST_F(StorageResidualPathsTest, DeleteContainerLeavesAFileAtTheNameAlone)
{
    Seed(app_ / "blocked");
    Seed(app_ / "player" / "AllPlayers");
    auto device = Device();
    EXPECT_NO_THROW(device->DeleteContainer("blocked"));
    EXPECT_NO_THROW(device->DeleteContainer("player"));
    EXPECT_TRUE(fs::is_regular_file(app_ / "blocked"));
    EXPECT_TRUE(fs::is_regular_file(app_ / "player" / "AllPlayers"));
}

TEST_F(StorageResidualPathsTest, ExistsQueriesBeneathAFileAnswerFalse)
{
    auto device = Device();
    auto container = Open(*device, "queries");
    (void)container->CreateFile("seed.bin");
    EXPECT_FALSE(container->FileExists("seed.bin/child"));
    EXPECT_FALSE(container->DirectoryExists("seed.bin/child"));
}

#if !defined(_WIN32)
TEST_F(StorageResidualPathsTest, OperationsBeneathAnUnsearchableDirectoryReportTheFilesystemError)
{
    if (::geteuid() == 0) GTEST_SKIP() << "permissions do not bind the superuser";
    auto device = Device();
    auto container = Open(*device, "locked");
    container->CreateDirectory("locked/sub");
    (void)container->CreateFile("locked/file.bin");
    Restrict(app_ / "locked" / "AllPlayers" / "locked", fs::perms::owner_read | fs::perms::owner_write);

    EXPECT_FALSE(container->FileExists("locked/file.bin"));
    EXPECT_FALSE(container->DirectoryExists("locked/sub"));
    Exact<System::UnauthorizedAccessException>(
        [&] { (void)container->OpenFile("locked/file.bin", System::IO::FileMode::Open); });
    Exact<System::UnauthorizedAccessException>([&] { (void)container->CreateFile("locked/new.bin"); });
    Exact<System::UnauthorizedAccessException>([&] { container->DeleteFile("locked/file.bin"); });
    Exact<System::UnauthorizedAccessException>([&] { container->CreateDirectory("locked/new"); });
    Exact<System::UnauthorizedAccessException>([&] { container->DeleteDirectory("locked/sub"); });
}

// No per-user data folder and no working directory leave nothing to resolve the root from. That is
// a CNA root policy without an XNA counterpart; it must not escape as a std::filesystem error.
TEST_F(StorageResidualPathsTest, AnUnresolvableRootIsADisconnectedDevice)
{
    struct Variable
    {
        const char* name;
        std::optional<std::string> value;
    };
    std::vector<Variable> saved;
    for (const char* name : {"HOME", "LOCALAPPDATA"})
    {
        const char* value = std::getenv(name);
        saved.push_back({name, value ? std::optional<std::string>(value) : std::nullopt});
        ASSERT_EQ(unsetenv(name), 0);
    }
    SetEnvironment(nullptr);
    const fs::path previous = fs::current_path();
    const fs::path gone = root_ / "gone";
    ASSERT_TRUE(fs::create_directory(gone));
    ASSERT_EQ(::chdir(gone.c_str()), 0);
    ASSERT_TRUE(fs::remove(gone));

    Exact<Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException>(
        [] { (void)StorageDevice::GetStorageRootEXT(); });
    auto device = Device();
    auto opening = device->BeginOpenContainer("any", nullptr, nullptr);
    Exact<Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException>(
        [&] { (void)device->EndOpenContainer(opening.get()); });

    ASSERT_EQ(::chdir(previous.c_str()), 0);
    for (const Variable& variable : saved)
        if (variable.value) ASSERT_EQ(setenv(variable.name, variable.value->c_str(), 1), 0);
}

TEST_F(StorageResidualPathsTest, DeleteContainerReportsAnUndeletableFileAsUnauthorizedAccess)
{
    if (::geteuid() == 0) GTEST_SKIP() << "permissions do not bind the superuser";
    auto device = Device();
    {
        auto container = Open(*device, "undeletable");
        container->CreateDirectory("inner");
        (void)container->CreateFile("inner/file.bin");
    }
    const fs::path inner = app_ / "undeletable" / "AllPlayers" / "inner";
    Restrict(inner, fs::perms::owner_read | fs::perms::owner_exec);
    Exact<System::UnauthorizedAccessException>([&] { device->DeleteContainer("undeletable"); });
    EXPECT_TRUE(fs::is_regular_file(inner / "file.bin"));
}
#endif
}
#endif
