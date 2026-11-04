// SPDX-License-Identifier: MS-PL
#include "StorageSecondPassFixture.hpp"
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace Tq042
{
TEST_F(StorageSecondPassTest, RemovingTheSaveDirectoryDoesNotInventUnlimitedDriveCapacity)
{
    // XNA FreeSpace/TotalSpace IL_0011-0017 query DriveInfo, not the save directory.
    // Deleting a directory does not disconnect its filesystem. Capacity is unchanged;
    // available bytes may vary under concurrent writers, so assert their valid interval.
    (void)StorageDevice::GetStorageRootEXT(); // exercise the already-cached-root branch
    const auto expected = fs::space(scratch).capacity;
    container.reset();
    ASSERT_GT(fs::remove_all(app), 0U); // exclusively this private temporary application
    EXPECT_TRUE(device->getIsConnectedProperty());
    EXPECT_EQ(device->getTotalSpaceProperty(), static_cast<long long>(expected));
    const auto available = device->getFreeSpaceProperty();
    EXPECT_GE(available, 0);
    EXPECT_LE(available, static_cast<long long>(expected));
    EXPECT_FALSE(fs::exists(app));
}
}
#endif
