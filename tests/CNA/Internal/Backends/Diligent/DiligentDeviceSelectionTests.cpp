// SPDX-License-Identifier: MS-PL
//
// plan_diligent.md DILIGENT-15: GTest coverage for the parts of the Diligent backend that make a
// decision before any device exists. Diligent is the only CNA backend whose native API is chosen at
// runtime, so the preference order and the CNA_DILIGENT_DEVICE override are real, testable logic --
// and they are the one piece of this backend that can be verified with no GPU, no window and no
// display, which is exactly the situation on a headless build machine (see plan_diligent.md's
// "Verification status"). Everything that needs a real device lives in the Diligent_* CTest
// binaries instead.
#include <gtest/gtest.h>

#if defined(CNA_BACKEND_DILIGENT)
#include "CNA/Internal/Backends/Diligent/DiligentGraphicsBackend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::Internal::Backends::Diligent::DiligentDeviceType;
using CNA::Internal::Backends::Diligent::GetDeviceTypeName;
using CNA::Internal::Backends::Diligent::GetDeviceTypePreferenceOrder;
using CNA::Internal::Backends::Diligent::ParseDeviceTypeOverride;

namespace
{
    bool Contains(const std::vector<DiligentDeviceType>& order, DiligentDeviceType type)
    {
        return std::find(order.begin(), order.end(), type) != order.end();
    }

    /// Index of @p type in @p order, or -1 when it is absent.
    int IndexOf(const std::vector<DiligentDeviceType>& order, DiligentDeviceType type)
    {
        const auto it = std::find(order.begin(), order.end(), type);
        return it == order.end() ? -1 : static_cast<int>(std::distance(order.begin(), it));
    }
}

TEST(DiligentDeviceSelectionTest, DeviceTypeNamesAreStableAndDistinct)
{
    EXPECT_STREQ(GetDeviceTypeName(DiligentDeviceType::D3D12), "Direct3D12");
    EXPECT_STREQ(GetDeviceTypeName(DiligentDeviceType::Vulkan), "Vulkan");
    EXPECT_STREQ(GetDeviceTypeName(DiligentDeviceType::D3D11), "Direct3D11");
    EXPECT_STREQ(GetDeviceTypeName(DiligentDeviceType::OpenGL), "OpenGL");
}

TEST(DiligentDeviceSelectionTest, PreferenceOrderIsNonEmptyAndFreeOfDuplicates)
{
    const std::vector<DiligentDeviceType> order = GetDeviceTypePreferenceOrder();
    ASSERT_FALSE(order.empty()) << "the DILIGENT backend built no Diligent engine at all";

    std::vector<DiligentDeviceType> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(std::unique(sorted.begin(), sorted.end()), sorted.end());
}

TEST(DiligentDeviceSelectionTest, PreferenceOrderRanksExplicitApisAheadOfOpenGL)
{
    const std::vector<DiligentDeviceType> order = GetDeviceTypePreferenceOrder();

    // OpenGL is the last-resort device type: it is the one Diligent supports everywhere but with
    // the fewest guarantees (see plan_diligent.md design decision 9).
    if (Contains(order, DiligentDeviceType::OpenGL) && order.size() > 1)
        EXPECT_EQ(IndexOf(order, DiligentDeviceType::OpenGL), static_cast<int>(order.size()) - 1);

    if (Contains(order, DiligentDeviceType::D3D12) && Contains(order, DiligentDeviceType::D3D11))
        EXPECT_LT(IndexOf(order, DiligentDeviceType::D3D12), IndexOf(order, DiligentDeviceType::D3D11));

    if (Contains(order, DiligentDeviceType::Vulkan) && Contains(order, DiligentDeviceType::D3D11))
        EXPECT_LT(IndexOf(order, DiligentDeviceType::Vulkan), IndexOf(order, DiligentDeviceType::D3D11));
}

TEST(DiligentDeviceSelectionTest, OverrideSelectsExactlyOneDeviceType)
{
    EXPECT_EQ(ParseDeviceTypeOverride("vulkan"), std::vector{DiligentDeviceType::Vulkan});
    EXPECT_EQ(ParseDeviceTypeOverride("opengl"), std::vector{DiligentDeviceType::OpenGL});
    EXPECT_EQ(ParseDeviceTypeOverride("d3d11"), std::vector{DiligentDeviceType::D3D11});
    EXPECT_EQ(ParseDeviceTypeOverride("d3d12"), std::vector{DiligentDeviceType::D3D12});
}

TEST(DiligentDeviceSelectionTest, OverrideAcceptsAliasesAndIsCaseInsensitive)
{
    EXPECT_EQ(ParseDeviceTypeOverride("VK"), std::vector{DiligentDeviceType::Vulkan});
    EXPECT_EQ(ParseDeviceTypeOverride("Vulkan"), std::vector{DiligentDeviceType::Vulkan});
    EXPECT_EQ(ParseDeviceTypeOverride("GL"), std::vector{DiligentDeviceType::OpenGL});
    EXPECT_EQ(ParseDeviceTypeOverride("gles"), std::vector{DiligentDeviceType::OpenGL});
    EXPECT_EQ(ParseDeviceTypeOverride("DX12"), std::vector{DiligentDeviceType::D3D12});
    EXPECT_EQ(ParseDeviceTypeOverride("Direct3D11"), std::vector{DiligentDeviceType::D3D11});
}

TEST(DiligentDeviceSelectionTest, AutoAndEmptyFallBackToThePreferenceOrder)
{
    EXPECT_EQ(ParseDeviceTypeOverride("auto"), GetDeviceTypePreferenceOrder());
    EXPECT_EQ(ParseDeviceTypeOverride(""), GetDeviceTypePreferenceOrder());
    EXPECT_EQ(ParseDeviceTypeOverride("AUTO"), GetDeviceTypePreferenceOrder());
}

TEST(DiligentDeviceSelectionTest, UnknownOverrideThrowsInsteadOfSilentlyFallingBack)
{
    // A typo in CNA_DILIGENT_DEVICE must not quietly run on a different device than the one asked
    // for -- that would make a device-specific bug report describe the wrong device.
    EXPECT_THROW(ParseDeviceTypeOverride("metal"), std::runtime_error);
    EXPECT_THROW(ParseDeviceTypeOverride("webgpu"), std::runtime_error);
    EXPECT_THROW(ParseDeviceTypeOverride("vulcan"), std::runtime_error);
}

#endif // CNA_BACKEND_DILIGENT
