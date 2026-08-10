// SPDX-License-Identifier: MS-PL
//
// REMED-CONTENT-002: StorageDevice::DeleteContainer(titleName) built its target path via
// fs::path(storageRoot) / titleName with no containment check -- an absolute titleName (or one
// escaping via "..") resolved outside the storage root and was recursively deleted. No prior test
// coverage existed for DeleteContainer at all.

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

#include "Microsoft/Xna/Framework/Storage/StorageDevice.hpp"

using Microsoft::Xna::Framework::Storage::StorageDevice;

namespace
{
    namespace fs = std::filesystem;

    // Isolates every test in this file to its own real, disposable storage root, matching the
    // established convention in GamerServicesGamerTests.cpp's GamerServicesStoreGuard -- avoids
    // colliding with (or accidentally deleting) any other test's or the real user's storage state.
    class StorageDeviceDeleteContainerTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            StorageDevice::SetAppNameEXT("CnaTestsContent002StorageDevice");
            root_ = StorageDevice::GetStorageRootEXT();
            ASSERT_FALSE(root_.empty());
            fs::create_directories(root_);
        }

        void TearDown() override
        {
            std::error_code ec;
            fs::remove_all(root_, ec);
            StorageDevice::SetAppNameEXT("");
        }

        // Marks that the real storage root itself (and everything under it) is still intact --
        // used to prove a rejected DeleteContainer call touched nothing.
        void CreateMarker(const std::string& relativePath)
        {
            fs::path p = fs::path(root_) / relativePath;
            fs::create_directories(p.parent_path());
            std::ofstream(p) << "marker";
        }

        bool MarkerExists(const std::string& relativePath) const
        {
            std::error_code ec;
            return fs::exists(fs::path(root_) / relativePath, ec);
        }

        std::string root_;
        // StorageDevice's constructor is private; the real acquisition path is
        // BeginShowSelector()/EndShowSelector(), matching the fake-async XNA 4.0 pattern.
        std::unique_ptr<StorageDevice> device_ =
            StorageDevice::EndShowSelector(StorageDevice::BeginShowSelector(nullptr, nullptr).get());
    };
}

TEST_F(StorageDeviceDeleteContainerTest, EmptyTitleNameThrowsInvalidArgument)
{
    EXPECT_THROW(device_->DeleteContainer(""), std::invalid_argument);
}

TEST_F(StorageDeviceDeleteContainerTest, AbsoluteTitleNameThrowsAndDeletesNothing)
{
    CreateMarker("sentinel.txt");
    EXPECT_THROW(device_->DeleteContainer("/etc"), std::invalid_argument);
    EXPECT_TRUE(MarkerExists("sentinel.txt")) << "storage root must be untouched by a rejected call";
}

TEST_F(StorageDeviceDeleteContainerTest, EscapingTitleNameThrowsAndDeletesNothing)
{
    // The classic reproduction from the finding: enough ".." segments to climb above the real
    // storage root (a SDL pref-path directory is always several levels deep), landing on some
    // other, unrelated real directory this test must never actually touch.
    CreateMarker("sentinel.txt");
    EXPECT_THROW(device_->DeleteContainer("../../../../../../../../tmp"), std::invalid_argument);
    EXPECT_TRUE(MarkerExists("sentinel.txt")) << "storage root must be untouched by a rejected call";
}

TEST_F(StorageDeviceDeleteContainerTest, DotTitleNameThrowsAndDoesNotDeleteTheWholeRoot)
{
    CreateMarker("sentinel.txt");
    EXPECT_THROW(device_->DeleteContainer("."), std::invalid_argument);
    EXPECT_TRUE(MarkerExists("sentinel.txt")) << "'.' must not delete the entire storage root";
}

TEST_F(StorageDeviceDeleteContainerTest, SimpleTitleNameDeletesOnlyThatContainer)
{
    CreateMarker("MyGame/save1.dat");
    CreateMarker("sentinel.txt"); // sibling that must survive

    EXPECT_NO_THROW(device_->DeleteContainer("MyGame"));

    EXPECT_FALSE(MarkerExists("MyGame/save1.dat"));
    EXPECT_TRUE(MarkerExists("sentinel.txt"));
}
