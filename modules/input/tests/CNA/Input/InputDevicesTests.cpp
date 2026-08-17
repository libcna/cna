// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/InputDeviceInfo.hpp"
#include "CNA/Input/InputDevices.hpp"
#include "CNA/Platform/CannedInputDevices.hpp"

#include <memory>
#include <vector>

using CNA::Input::InputDeviceInfoEXT;
using CNA::Input::InputDevices;
using CNA::Platform::InputDeviceInfo;
using CNA::Platform::InputDeviceKind;
using CNA::Platform::Testing::CannedInputDevicePlatform;
using CNA::Platform::Testing::ScopedCurrentPlatform;

namespace
{
    // CI has no predictable set of physical mice, keyboards or touch devices, so the enumeration
    // is scripted. It arrives through the platform contract rather than through the old
    // per-subsystem backend seam, which PLAT-77 concluded should go away rather than be renamed.
    class CnaInputDevicesTest : public ::testing::Test
    {
    protected:
        CannedInputDevicePlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override { installed = std::make_unique<ScopedCurrentPlatform>(platform); }
        void TearDown() override { installed.reset(); }
    };
}

TEST_F(CnaInputDevicesTest, EachCategoryForwardsItsEnumeration)
{
    platform.Canned().Set(InputDeviceKind::Mouse,
                          {{1, InputDeviceKind::Mouse, "Primary Mouse"},
                           {2, InputDeviceKind::Mouse, "Trackpad"}});
    platform.Canned().Set(InputDeviceKind::Keyboard,
                          {{10, InputDeviceKind::Keyboard, "Internal Keyboard"}});
    platform.Canned().Set(InputDeviceKind::Touch,
                          {{100, InputDeviceKind::Touch, "Touchscreen"},
                           {101, InputDeviceKind::Touch, "Pen Digitizer"}});

    const auto mice = InputDevices::GetMiceEXT();
    ASSERT_EQ(mice.size(), 2u);
    EXPECT_EQ(mice[0], (InputDeviceInfoEXT{1, "Primary Mouse"}));
    EXPECT_EQ(mice[1].id, 2u);
    EXPECT_EQ(mice[1].name, "Trackpad");

    const auto keyboards = InputDevices::GetKeyboardsEXT();
    ASSERT_EQ(keyboards.size(), 1u);
    EXPECT_EQ(keyboards[0], (InputDeviceInfoEXT{10, "Internal Keyboard"}));

    const auto touch = InputDevices::GetTouchDevicesEXT();
    ASSERT_EQ(touch.size(), 2u);
    EXPECT_EQ(touch[1], (InputDeviceInfoEXT{101, "Pen Digitizer"}));
}

TEST_F(CnaInputDevicesTest, EmptyEnumerationYieldsEmptyLists)
{
    EXPECT_TRUE(InputDevices::GetMiceEXT().empty());
    EXPECT_TRUE(InputDevices::GetKeyboardsEXT().empty());
    EXPECT_TRUE(InputDevices::GetTouchDevicesEXT().empty());
}

TEST(CnaInputDeviceInfoEXTTest, EqualityComparesIdAndName)
{
    EXPECT_EQ((InputDeviceInfoEXT{1, "a"}), (InputDeviceInfoEXT{1, "a"}));
    EXPECT_NE((InputDeviceInfoEXT{1, "a"}), (InputDeviceInfoEXT{2, "a"}));
    EXPECT_NE((InputDeviceInfoEXT{1, "a"}), (InputDeviceInfoEXT{1, "b"}));
}
