// SPDX-License-Identifier: MS-PL
// Genuine XNA StorageContractsOracle.cs: disposed validation precedes all public access.
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include "Microsoft/Xna/Framework/Storage/StorageContainer.hpp"
#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::Storage;
namespace fs = std::filesystem;
namespace
{
class StorageContainerLifetimeTest : public ::testing::Test
{
protected:
    std::unique_ptr<StorageDevice> device;
    std::unique_ptr<StorageContainer> container;
    fs::path root;
    int events = 0;
    bool ownsFixture = false;

    void SetUp() override
    {
        std::random_device entropy;
        const std::string name = "CnaStorageLifetime-" + std::to_string(entropy()) +
                                 "-" + std::to_string(entropy());
        root = fs::path(StorageDevice::GetStorageRootEXT()) / name / "AllPlayers";
        ASSERT_FALSE(fs::exists(root.parent_path()));
        auto selection = StorageDevice::BeginShowSelector(nullptr, nullptr);
        device = StorageDevice::EndShowSelector(selection.get());
        auto operation = device->BeginOpenContainer(name, nullptr, nullptr);
        container = device->EndOpenContainer(operation.get());
        ASSERT_TRUE(fs::is_directory(root));
        ownsFixture = true;
        std::ofstream seed(root / "seed.bin", std::ios::binary);
        seed.write("seed", 4);
        ASSERT_TRUE(seed.good());
        fs::create_directory(root / "child");
    }

    void TearDown() override
    {
        container.reset();
        device.reset();
        if (!ownsFixture) return;
        // Only this test's named fixtures; never recursively delete the storage root.
        for (const char* name : {"seed.bin", "new.bin", "child", "new"})
        {
            std::error_code ignored;
            fs::remove(root / name, ignored);
        }
        std::error_code ignored;
        fs::remove(root, ignored);
        fs::remove(root.parent_path(), ignored);
    }
};
}

TEST_F(StorageContainerLifetimeTest, LivePropertiesAndRepeatedDisposalPreserveTheGenuineEventContract)
{
    EXPECT_FALSE(container->getIsDisposedProperty());
    EXPECT_EQ(&container->getStorageDeviceProperty(), device.get());
    EXPECT_EQ(container->getDisplayNameProperty(), root.parent_path().filename().string());
    EXPECT_TRUE(container->FileExists("seed.bin"));
    EXPECT_TRUE(container->DirectoryExists("child"));
    container->Disposing += [this](System::Object* sender, const System::EventArgs& args)
    {
        EXPECT_EQ(sender, container.get());
        EXPECT_EQ(&args, &System::EventArgs::Empty);
        EXPECT_TRUE(container->getIsDisposedProperty());
        ++events;
    };
    container->Dispose();
    container->Dispose();
    EXPECT_TRUE(container->getIsDisposedProperty());
    EXPECT_EQ(events, 1);
    EXPECT_EQ(container->GetTypeName(), "Microsoft.Xna.Framework.Storage.StorageContainer");
}

TEST_F(StorageContainerLifetimeTest, AllFifteenPublicAccessPathsRejectDisposalBeforeFilesystemEffects)
{
    using namespace System::IO;
    container->Dispose();
    EXPECT_THROW((void)container->getDisplayNameProperty(), System::ObjectDisposedException);
    EXPECT_THROW((void)container->getStorageDeviceProperty(), System::ObjectDisposedException);
    EXPECT_THROW(container->CreateDirectory("new"), System::ObjectDisposedException);
    EXPECT_THROW(container->DeleteDirectory("child"), System::ObjectDisposedException);
    EXPECT_THROW((void)container->DirectoryExists("child"), System::ObjectDisposedException);
    EXPECT_THROW((void)container->GetDirectoryNames(), System::ObjectDisposedException);
    EXPECT_THROW((void)container->GetDirectoryNames("*"), System::ObjectDisposedException);
    EXPECT_THROW((void)container->CreateFile("new.bin"), System::ObjectDisposedException);
    EXPECT_THROW(container->DeleteFile("seed.bin"), System::ObjectDisposedException);
    EXPECT_THROW((void)container->FileExists("seed.bin"), System::ObjectDisposedException);
    EXPECT_THROW((void)container->GetFileNames(), System::ObjectDisposedException);
    EXPECT_THROW((void)container->GetFileNames("*"), System::ObjectDisposedException);
    EXPECT_THROW((void)container->OpenFile("seed.bin", FileMode::Open), System::ObjectDisposedException);
    EXPECT_THROW((void)container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read),
                 System::ObjectDisposedException);
    EXPECT_THROW((void)container->OpenFile("seed.bin", FileMode::Open, FileAccess::Read, FileShare::Read),
                 System::ObjectDisposedException);
    EXPECT_TRUE(fs::is_regular_file(root / "seed.bin"));
    EXPECT_TRUE(fs::is_directory(root / "child"));
    EXPECT_FALSE(fs::exists(root / "new.bin"));
    EXPECT_FALSE(fs::exists(root / "new"));
}

TEST_F(StorageContainerLifetimeTest, DisposedStatePrecedesPathAndPatternValidation)
{
    container->Dispose();
    EXPECT_THROW((void)container->FileExists(""), System::ObjectDisposedException);
    EXPECT_THROW(container->CreateDirectory("../outside"), System::ObjectDisposedException);
    EXPECT_THROW((void)container->GetFileNames(""), System::ObjectDisposedException);
}

TEST_F(StorageContainerLifetimeTest, DisposingEventGuardsContainerAccessButRetainsFilesAndExistingStreams)
{
    auto stream = container->OpenFile("seed.bin", System::IO::FileMode::Open, System::IO::FileAccess::Read);
    container->Disposing += [this, &stream](System::Object*, const System::EventArgs&)
    {
        ++events;
        EXPECT_TRUE(container->getIsDisposedProperty());
        EXPECT_THROW((void)container->FileExists("seed.bin"), System::ObjectDisposedException);
        EXPECT_THROW((void)container->GetFileNames(), System::ObjectDisposedException);
        EXPECT_TRUE(fs::is_regular_file(root / "seed.bin"));
        EXPECT_EQ(stream->ReadByte(), 's');
    };
    container->Dispose();
    container->Dispose();
    EXPECT_EQ(events, 1);
    stream->setPositionProperty(0);
    EXPECT_EQ(stream->ReadByte(), 's');
}
