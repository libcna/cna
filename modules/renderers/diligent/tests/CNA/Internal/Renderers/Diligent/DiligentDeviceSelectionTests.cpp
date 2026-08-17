// SPDX-License-Identifier: MS-PL
//
// plan_diligent.md DILIGENT-15: GTest coverage for the parts of the Diligent renderer that make a
// decision before any device exists. Diligent is the only CNA renderer whose native API is chosen at
// runtime, so the preference order and the CNA_DILIGENT_DEVICE override are real, testable logic --
// and they are the one piece of this renderer that can be verified with no GPU, no window and no
// display, which is exactly the situation on a headless build machine (see plan_diligent.md's
// "Verification status"). Everything that needs a real device lives in the Diligent_* CTest
// binaries instead.
#include <gtest/gtest.h>

// plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_DILIGENT is defined project-wide.
#if defined(CNA_RENDERER_DILIGENT) || defined(CNA_RENDERER_PRESENT_DILIGENT)
#include "CNA/Internal/Renderers/Diligent/DiligentRenderer.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// Matches DiligentRenderer.hpp's own alias: the CNA namespace here is itself named
// `Diligent`, so an unqualified `Diligent::X` would resolve to this namespace and fail.
namespace Dg = ::Diligent;

using CNA::Internal::Renderers::Diligent::ComputeDiligentDepthBiasRawUnits;
using CNA::Internal::Renderers::Diligent::DiligentDeviceType;
using CNA::Internal::Renderers::Diligent::EvaluateCapability;
using CNA::Internal::Renderers::Diligent::GetDeviceTypeName;
using CNA::Internal::Renderers::Diligent::GetDeviceTypePreferenceOrder;
using CNA::Internal::Renderers::Diligent::ParseDeviceTypeOverride;

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
    ASSERT_FALSE(order.empty()) << "the DILIGENT renderer built no Diligent engine at all";

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

// plan_diligent.md DILIGENT-64: PipelineKey::depthBias used to be packed into a single signed byte
// (lround(depthBias * 1000) & 0xFF) inside PipelineKey::raster, which silently wraps sign once the
// scaled value leaves [-128, 127] -- e.g. +0.129 (scaled 129) would come back out of an int8_t cast
// as -127. ComputeDiligentDepthBiasRawUnits() now stores the full lossless Int32 instead; these are
// exactly the boundary/sign cases the old packing got wrong, verifiable with no GPU present.
TEST(DiligentDeviceSelectionTest, DepthBiasRawUnitsMatchesTheOldPackingWithinItsFormerRange)
{
    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(0.0f), 0);
    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(0.127f), 127);
    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(-0.128f), -128);
}

TEST(DiligentDeviceSelectionTest, DepthBiasRawUnitsNeverWrapsSignPastTheOldByteBoundary)
{
    // The old `& 0xFF` packing made +0.128/+0.129 (scaled 128/129) come back as -128/-127 once
    // reinterpreted as a signed byte -- a positive bias silently becoming a negative one.
    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(0.128f), 128);
    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(0.129f), 129);
    EXPECT_GT(ComputeDiligentDepthBiasRawUnits(0.129f), 0);

    // The old packing's negative side already round-tripped correctly at exactly -0.128 (the byte
    // minimum), but anything past it silently aliased to a positive value the wrong sign entirely
    // once cast back to int8_t (e.g. scaled -129 & 0xFF = 0x7F = +127).
    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(-0.129f), -129);
    EXPECT_LT(ComputeDiligentDepthBiasRawUnits(-0.129f), 0);
}

TEST(DiligentDeviceSelectionTest, DepthBiasRawUnitsClampsInsteadOfOverflowingOnExtremeInput)
{
    const std::int32_t maxUnits = std::numeric_limits<std::int32_t>::max();
    const std::int32_t minUnits = std::numeric_limits<std::int32_t>::min();

    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(1.0e10f), maxUnits);
    EXPECT_EQ(ComputeDiligentDepthBiasRawUnits(-1.0e10f), minUnits);
}

// plan_diligent.md DILIGENT-61: SupportsCapability() used to answer WireFrame/OcclusionQuery
// unconditionally `true` and AnisotropicFiltering from a device-TYPE guess (`!= OpenGL`) instead of
// the actual device facts DiligentRenderer::CreateOcclusionQuery()/ApplySamplerState()
// themselves depend on. EvaluateCapability() is the decision logic extracted into a free function
// over already-queried facts, exercisable here with synthetic Dg::DeviceFeatures values -- no GPU
// required, matching DILIGENT-3's own reason for using free functions for renderer decisions.
namespace
{
    Dg::DeviceFeatures FeaturesWith(Dg::DEVICE_FEATURE_STATE wireframe,
                                    Dg::DEVICE_FEATURE_STATE occlusion,
                                    Dg::DEVICE_FEATURE_STATE binaryOcclusion)
    {
        Dg::DeviceFeatures features{};
        features.WireframeFill = wireframe;
        features.OcclusionQueries = occlusion;
        features.BinaryOcclusionQueries = binaryOcclusion;
        return features;
    }
}

TEST(DiligentDeviceSelectionTest, EvaluateCapabilityStructuralCapabilitiesAreAlwaysTrue)
{
    const Dg::DeviceFeatures noFeatures = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_DISABLED, Dg::DEVICE_FEATURE_STATE_DISABLED,
        Dg::DEVICE_FEATURE_STATE_DISABLED);

    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::ThreeD, noFeatures, 1, false));
    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::DepthStencilBuffer, noFeatures, 1, false));
    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::Texture3D, noFeatures, 1, false));
    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::MultipleRenderTargets, noFeatures, 1, false));
    EXPECT_FALSE(EvaluateCapability(CNA::GraphicsCapability::CustomEffects, noFeatures, 16, true));
}

TEST(DiligentDeviceSelectionTest, EvaluateCapabilityWireFrameFollowsTheDeviceFeature)
{
    const Dg::DeviceFeatures enabled = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_ENABLED, Dg::DEVICE_FEATURE_STATE_DISABLED,
        Dg::DEVICE_FEATURE_STATE_DISABLED);
    const Dg::DeviceFeatures disabled = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_DISABLED, Dg::DEVICE_FEATURE_STATE_DISABLED,
        Dg::DEVICE_FEATURE_STATE_DISABLED);

    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::WireFrame, enabled, 1, false));
    EXPECT_FALSE(EvaluateCapability(CNA::GraphicsCapability::WireFrame, disabled, 1, false));
}

TEST(DiligentDeviceSelectionTest, EvaluateCapabilityOcclusionQueryAcceptsEitherExactOrBinary)
{
    const Dg::DeviceFeatures neither = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_DISABLED, Dg::DEVICE_FEATURE_STATE_DISABLED,
        Dg::DEVICE_FEATURE_STATE_DISABLED);
    const Dg::DeviceFeatures exactOnly = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_DISABLED, Dg::DEVICE_FEATURE_STATE_ENABLED,
        Dg::DEVICE_FEATURE_STATE_DISABLED);
    const Dg::DeviceFeatures binaryOnly = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_DISABLED, Dg::DEVICE_FEATURE_STATE_DISABLED,
        Dg::DEVICE_FEATURE_STATE_ENABLED);

    // This exact disjunction is what CreateOcclusionQuery()'s own constructor throws against --
    // SupportsCapability() must never say true immediately before that throws.
    EXPECT_FALSE(EvaluateCapability(CNA::GraphicsCapability::OcclusionQuery, neither, 1, false));
    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::OcclusionQuery, exactOnly, 1, false));
    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::OcclusionQuery, binaryOnly, 1, false));
}

TEST(DiligentDeviceSelectionTest, EvaluateCapabilityAnisotropicFilteringFollowsMaxAnisotropy)
{
    const Dg::DeviceFeatures noFeatures = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_DISABLED, Dg::DEVICE_FEATURE_STATE_DISABLED,
        Dg::DEVICE_FEATURE_STATE_DISABLED);

    EXPECT_FALSE(EvaluateCapability(CNA::GraphicsCapability::AnisotropicFiltering, noFeatures, 1, false));
    EXPECT_TRUE(EvaluateCapability(CNA::GraphicsCapability::AnisotropicFiltering, noFeatures, 16, false));
}

TEST(DiligentDeviceSelectionTest, EvaluateCapabilityMultiSampleAntiAliasingFollowsTheProbe)
{
    const Dg::DeviceFeatures noFeatures = FeaturesWith(
        Dg::DEVICE_FEATURE_STATE_DISABLED, Dg::DEVICE_FEATURE_STATE_DISABLED,
        Dg::DEVICE_FEATURE_STATE_DISABLED);

    EXPECT_FALSE(
        EvaluateCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing, noFeatures, 1, false));
    EXPECT_TRUE(
        EvaluateCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing, noFeatures, 1, true));
}

#endif // CNA_RENDERER_DILIGENT / CNA_RENDERER_PRESENT_DILIGENT
