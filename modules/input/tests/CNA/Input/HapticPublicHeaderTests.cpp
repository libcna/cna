// SPDX-License-Identifier: MS-PL
// ISSUE-009: consumers must not need the private platform implementation to construct a device.
#include "CNA/Input/HapticDevice.hpp"
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

namespace
{
    template<class T> concept Complete = requires { sizeof(T); };
    static_assert(!Complete<CNA::Platform::IPlatformHapticDevice>);
    static_assert(std::is_nothrow_default_constructible_v<CNA::Input::HapticDevice>);

    struct Owner
    {
        CNA::Input::HapticDevice device;
    };
}

TEST(HapticPublicHeaderTest, DefaultConstructionCreatesAClosedDevice)
{
    CNA::Input::HapticDevice device;
    EXPECT_FALSE(device.IsOpenEXT());
    EXPECT_TRUE(device.GetNameEXT().empty());
    EXPECT_EQ(device.GetCapabilitiesEXT(), CNA::Input::HapticCapabilitiesEXT{});
    EXPECT_FALSE(device.InitRumbleEXT());
    EXPECT_FALSE(device.PlayRumbleEXT(0.5f, 1));
    device.Dispose();
    device.Dispose();
    EXPECT_FALSE(device.IsOpenEXT());
}

TEST(HapticPublicHeaderTest, DefaultConstructedMemberCanBeMovedAndDisposed)
{
    Owner owner;
    CNA::Input::HapticDevice moved(std::move(owner.device));
    EXPECT_FALSE(owner.device.IsOpenEXT());
    EXPECT_FALSE(moved.IsOpenEXT());
    Owner destination;
    destination.device = std::move(moved);
    EXPECT_FALSE(destination.device.IsOpenEXT());
    EXPECT_FALSE(moved.IsOpenEXT());
    destination.device.Dispose();
}
