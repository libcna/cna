// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/InputDeviceInfo.hpp"
#include "CNA/Input/InputDevices.hpp"
#include "CNA/Internal/Input/SystemDeviceBackend.hpp"

#include <vector>

using CNA::Input::InputDeviceInfoEXT;
using CNA::Input::InputDevices;
using CNA::Internal::Input::ISystemDeviceBackend;
using CNA::Internal::Input::SetSystemDeviceBackendForTests;

namespace
{
    // A fake device enumeration source, so InputDevices is exercised deterministically (CI has no
    // predictable set of physical mice/keyboards/touch devices).
    class FakeSystemDeviceBackend final : public ISystemDeviceBackend
    {
    public:
        std::vector<InputDeviceInfoEXT> mice;
        std::vector<InputDeviceInfoEXT> keyboards;
        std::vector<InputDeviceInfoEXT> touchDevices;

        std::vector<InputDeviceInfoEXT> GetMice() override { return mice; }
        std::vector<InputDeviceInfoEXT> GetKeyboards() override { return keyboards; }
        std::vector<InputDeviceInfoEXT> GetTouchDevices() override { return touchDevices; }
    };

    class CnaInputDevicesTest : public ::testing::Test
    {
    protected:
        FakeSystemDeviceBackend fake;
        void SetUp() override { SetSystemDeviceBackendForTests(&fake); }
        void TearDown() override { SetSystemDeviceBackendForTests(nullptr); }
    };
}

TEST_F(CnaInputDevicesTest, EachCategoryForwardsItsEnumeration)
{
    fake.mice = {{1, "Primary Mouse"}, {2, "Trackpad"}};
    fake.keyboards = {{10, "Internal Keyboard"}};
    fake.touchDevices = {{100, "Touchscreen"}, {101, "Pen Digitizer"}};

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
