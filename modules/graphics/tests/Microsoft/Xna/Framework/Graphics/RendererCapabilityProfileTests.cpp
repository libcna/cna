// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::ThreeDimensionalPipeline) == 0);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::ShaderDialectWgsl) == 29);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::Texture3DSampling) == 30);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::BaseInstanceDrawing) == 31);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::SamplerLevelOfDetailBias) == 32);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::SkinnedPbrEffectShading) == 34);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::Count) == 35);
static_assert(static_cast<std::uint8_t>(CNA::RendererFeatureSupport::Unknown) == 0);
static_assert(static_cast<std::uint8_t>(CNA::RendererFeatureSupport::Restricted) == 3);
static_assert(static_cast<std::uint32_t>(CNA::RendererLimit::MaxTextureDimension) == 0);
static_assert(static_cast<std::uint32_t>(CNA::RendererLimit::MaxUniformBufferBytes) == 11);
static_assert(static_cast<std::uint32_t>(CNA::RendererLimit::MinUniformBufferOffsetAlignment) == 20);

namespace
{
    [[nodiscard]] CNA::RendererFeatureSupport ExpectedSupport(const bool supported)
    {
        return supported ? CNA::RendererFeatureSupport::Supported
                         : CNA::RendererFeatureSupport::Unsupported;
    }
}

TEST(RendererCapabilityProfileTest, EmptyProfileKeepsInvalidAndUnauditedAnswersUnknown)
{
    const CNA::RendererCapabilityProfile profile;
    EXPECT_TRUE(profile.GetRendererName().empty());
    EXPECT_EQ(profile.GetFeature(CNA::RendererFeature::ThreeDimensionalPipeline).support,
              CNA::RendererFeatureSupport::Unknown);
    EXPECT_EQ(profile.GetFeature(static_cast<CNA::RendererFeature>(999)).support,
              CNA::RendererFeatureSupport::Unknown);
    EXPECT_FALSE(profile.Supports(CNA::RendererFeature::ThreeDimensionalPipeline));
    EXPECT_FALSE(profile.GetLimit(CNA::RendererLimit::MaxTextureDimension).known);
    EXPECT_FALSE(profile.GetLimit(static_cast<CNA::RendererLimit>(999)).known);
    EXPECT_EQ(profile.GetSurfaceFormatSupport(999).knownUsages, 0U);
    EXPECT_TRUE(profile.GetAdditionalLimitationsText().empty());
    EXPECT_TRUE(profile.GetEnglishReport().empty());
}

TEST(RendererCapabilityProfileTest, CatalogsAreCompleteStableAndSelfDescribing)
{
    // The ordinals of the retired modern GPU layer stay reserved: the catalogs skip them, and
    // they name nothing.
    constexpr std::uint32_t kReservedFeatures[] = {18, 19, 20, 23};
    constexpr std::uint32_t kReservedLimits[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 15, 19, 21};

    const auto features = CNA::AllRendererFeatures();
    ASSERT_EQ(features.size(), static_cast<std::size_t>(CNA::RendererFeature::Count) -
                                   std::size(kReservedFeatures));
    std::unordered_set<std::string> featureNames;
    for (std::size_t i = 0; i < features.size(); ++i)
    {
        if (i > 0)
            EXPECT_LT(static_cast<std::uint32_t>(features[i - 1]),
                      static_cast<std::uint32_t>(features[i]));
        EXPECT_LT(static_cast<std::uint32_t>(features[i]),
                  static_cast<std::uint32_t>(CNA::RendererFeature::Count));
        const std::string name(CNA::GetRendererFeatureName(features[i]));
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "UnknownRendererFeature");
        EXPECT_TRUE(featureNames.insert(name).second);
        EXPECT_FALSE(CNA::GetRendererFeatureDescription(features[i]).empty());
    }
    EXPECT_EQ(CNA::GetRendererFeatureName(CNA::RendererFeature::Count),
              "UnknownRendererFeature");
    EXPECT_EQ(CNA::GetRendererFeatureDescription(CNA::RendererFeature::Count),
              "Invalid detailed renderer feature identity.");
    const CNA::RendererCapabilityProfile empty;
    for (const std::uint32_t reserved : kReservedFeatures)
    {
        const auto feature = static_cast<CNA::RendererFeature>(reserved);
        EXPECT_EQ(CNA::GetRendererFeatureName(feature), "UnknownRendererFeature") << reserved;
        EXPECT_EQ(std::count(features.begin(), features.end(), feature), 0) << reserved;
        EXPECT_EQ(empty.GetFeature(feature).support, CNA::RendererFeatureSupport::Unknown);
    }

    const auto limits = CNA::AllRendererLimits();
    // Ordinal 21 was the last retired limit, so it sits at Count rather than inside the range.
    const auto reservedInRange = std::count_if(
        std::begin(kReservedLimits), std::end(kReservedLimits), [](const std::uint32_t ordinal) {
            return ordinal < static_cast<std::uint32_t>(CNA::RendererLimit::Count);
        });
    ASSERT_EQ(limits.size(), static_cast<std::size_t>(CNA::RendererLimit::Count) -
                                 static_cast<std::size_t>(reservedInRange));
    std::unordered_set<std::string> limitNames;
    for (std::size_t i = 0; i < limits.size(); ++i)
    {
        if (i > 0)
            EXPECT_LT(static_cast<std::uint32_t>(limits[i - 1]),
                      static_cast<std::uint32_t>(limits[i]));
        EXPECT_LT(static_cast<std::uint32_t>(limits[i]),
                  static_cast<std::uint32_t>(CNA::RendererLimit::Count));
        const std::string name(CNA::GetRendererLimitName(limits[i]));
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "UnknownRendererLimit");
        EXPECT_TRUE(limitNames.insert(name).second);
    }
    EXPECT_EQ(CNA::GetRendererLimitName(CNA::RendererLimit::Count),
              "UnknownRendererLimit");
    for (const std::uint32_t reserved : kReservedLimits)
    {
        const auto limit = static_cast<CNA::RendererLimit>(reserved);
        EXPECT_EQ(CNA::GetRendererLimitName(limit), "UnknownRendererLimit") << reserved;
        EXPECT_EQ(std::count(limits.begin(), limits.end(), limit), 0) << reserved;
        EXPECT_FALSE(empty.GetLimit(limit).known);
    }
}

TEST(RendererCapabilityProfileTest, FormatMasksDistinguishUnknownFromUnsupported)
{
    const auto known = CNA::RendererFormatUsage::TextureStorage |
                       CNA::RendererFormatUsage::RenderTarget;
    const auto texture = static_cast<std::uint32_t>(
        CNA::RendererFormatUsage::TextureStorage);
    const CNA::RendererFormatSupport support{static_cast<std::uint32_t>(known), texture};

    EXPECT_TRUE(support.IsKnown(CNA::RendererFormatUsage::TextureStorage));
    EXPECT_TRUE(support.Supports(CNA::RendererFormatUsage::TextureStorage));
    EXPECT_TRUE(support.IsKnown(CNA::RendererFormatUsage::RenderTarget));
    EXPECT_FALSE(support.Supports(CNA::RendererFormatUsage::RenderTarget));
    EXPECT_FALSE(support.IsKnown(CNA::RendererFormatUsage::Sampled));
    EXPECT_FALSE(support.Supports(CNA::RendererFormatUsage::Sampled));
}

TEST(RendererCapabilityProfileTest, DeviceSnapshotMapsEveryLegacyCapabilityExplicitly)
{
    GraphicsDevice device;
    const auto& profile = device.GetRendererCapabilityProfileEXT();
    EXPECT_EQ(&profile, &device.GetRendererCapabilityProfileEXT());
    EXPECT_EQ(profile.GetRendererName(), device.GetGraphicsRendererName());

    constexpr std::array mappings = {
        std::pair{CNA::RendererFeature::ThreeDimensionalPipeline,
                  CNA::GraphicsCapability::ThreeD},
        std::pair{CNA::RendererFeature::DepthStencilBuffer,
                  CNA::GraphicsCapability::DepthStencilBuffer},
        std::pair{CNA::RendererFeature::MultiSampleAntiAliasing,
                  CNA::GraphicsCapability::MultiSampleAntiAliasing},
        std::pair{CNA::RendererFeature::MultipleRenderTargets,
                  CNA::GraphicsCapability::MultipleRenderTargets},
        std::pair{CNA::RendererFeature::AnisotropicFiltering,
                  CNA::GraphicsCapability::AnisotropicFiltering},
        std::pair{CNA::RendererFeature::WireFrameRasterization,
                  CNA::GraphicsCapability::WireFrame},
        std::pair{CNA::RendererFeature::OcclusionQueries,
                  CNA::GraphicsCapability::OcclusionQuery},
        std::pair{CNA::RendererFeature::ShaderEffects,
                  CNA::GraphicsCapability::CustomEffects},
        std::pair{CNA::RendererFeature::Texture3DStorage,
                  CNA::GraphicsCapability::Texture3D},
        std::pair{CNA::RendererFeature::MultiStreamVertexInput,
                  CNA::GraphicsCapability::MultiStreamVertexInput},
        std::pair{CNA::RendererFeature::InstancedDrawing,
                  CNA::GraphicsCapability::Instancing},
        std::pair{CNA::RendererFeature::StencilBuffer,
                  CNA::GraphicsCapability::StencilBuffer},
        std::pair{CNA::RendererFeature::AdditiveBlending,
                  CNA::GraphicsCapability::AdditiveBlending},
        std::pair{CNA::RendererFeature::CompiledXnaEffects,
                  CNA::GraphicsCapability::CompiledEffects},
        std::pair{CNA::RendererFeature::Float32RenderTargets,
                  CNA::GraphicsCapability::FloatRenderTargets},
        std::pair{CNA::RendererFeature::Float16RenderTargets,
                  CNA::GraphicsCapability::HalfFloatRenderTargets},
        std::pair{CNA::RendererFeature::Float16TextureLinearFiltering,
                  CNA::GraphicsCapability::HalfFloatTextureLinearFiltering}
    };

    for (const auto [feature, legacy] : mappings)
    {
        const bool expected = device.SupportsCapability(legacy);
        EXPECT_EQ(device.GetRendererFeatureSupportEXT(feature), ExpectedSupport(expected));
        EXPECT_EQ(device.SupportsRendererFeatureEXT(feature), expected);
    }
    EXPECT_EQ(device.GetRendererFeatureSupportEXT(CNA::RendererFeature::BaseInstanceDrawing),
              ExpectedSupport(device.GetRenderer().SupportsBaseInstanceDrawingEXT()));
    EXPECT_EQ(device.GetRendererFeatureSupportEXT(CNA::RendererFeature::SamplerLevelOfDetailBias),
              ExpectedSupport(device.GetRenderer().AppliesSamplerLevelOfDetailBiasEXT()));
    EXPECT_EQ(device.GetRendererFeatureSupportEXT(CNA::RendererFeature::PbrEffectShading),
              ExpectedSupport(device.GetRenderer().ShadesPbrEffectEXT()));
    EXPECT_EQ(device.GetRendererFeatureSupportEXT(CNA::RendererFeature::SkinnedPbrEffectShading),
              ExpectedSupport(device.GetRenderer().ShadesSkinnedPbrEffectEXT()));

    for (const CNA::RendererFeature feature : CNA::AllRendererFeatures())
    {
        EXPECT_NE(profile.GetFeature(feature).support, CNA::RendererFeatureSupport::Unknown)
            << CNA::GetRendererFeatureName(feature);
    }
    EXPECT_EQ(device.GetRendererFeatureSupportEXT(static_cast<CNA::RendererFeature>(999)),
              CNA::RendererFeatureSupport::Unknown);
    EXPECT_FALSE(device.SupportsRendererFeatureEXT(static_cast<CNA::RendererFeature>(999)));

    constexpr std::array dialects = {
        CNA::RendererFeature::ShaderDialectGlslDesktop,
        CNA::RendererFeature::ShaderDialectGlslEs,
        CNA::RendererFeature::ShaderDialectGlslVulkan,
        CNA::RendererFeature::ShaderDialectHlsl,
        CNA::RendererFeature::ShaderDialectMsl,
        CNA::RendererFeature::ShaderDialectWgsl
    };
    for (const CNA::RendererFeature dialect : dialects)
    {
        if (device.SupportsRendererFeatureEXT(dialect))
            EXPECT_TRUE(device.SupportsRendererFeatureEXT(
                CNA::RendererFeature::ShaderEffectSourceExecution));
    }
}

TEST(RendererCapabilityProfileTest, DeviceSnapshotExposesLimitsAndHonestFormatKnowledge)
{
    GraphicsDevice device;
    const CNA::RendererLimitValue textureLimit =
        device.GetRendererLimitEXT(CNA::RendererLimit::MaxTextureDimension);
    ASSERT_TRUE(textureLimit.known);
    EXPECT_EQ(textureLimit.value,
              static_cast<std::uint64_t>(device.GetMaxTextureDimension()));
    EXPECT_TRUE(device.GetRendererLimitEXT(CNA::RendererLimit::MaxVertexStreams).known);
    for (const CNA::RendererLimit limit : CNA::AllRendererLimits())
    {
        const CNA::RendererLimitValue value = device.GetRendererLimitEXT(limit);
        EXPECT_TRUE(value.known) << CNA::GetRendererLimitName(limit);
    }
    EXPECT_FALSE(device.GetRendererLimitEXT(static_cast<CNA::RendererLimit>(999)).known);

    constexpr std::uint32_t classified =
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::RenderTarget) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::ColorTransfer);
    for (std::uint32_t ordinal = 0; ordinal < 27; ++ordinal)
    {
        const CNA::RendererFormatSupport support =
            device.GetRendererSurfaceFormatSupportEXT(static_cast<SurfaceFormat>(ordinal));
        EXPECT_EQ(support.knownUsages & classified, classified) << ordinal;
        EXPECT_EQ(support.supportedUsages & ~support.knownUsages, 0U) << ordinal;
    }
    EXPECT_EQ(device.GetRendererSurfaceFormatSupportEXT(static_cast<SurfaceFormat>(999)).knownUsages,
              0U);
}

TEST(RendererCapabilityProfileTest, EnglishReportIsCompleteCachedAndMachineFactsRemainPrimary)
{
    GraphicsDevice device;
    const auto& profile = device.GetRendererCapabilityProfileEXT();
    const std::string report(device.GetRendererCapabilityReportEXT());

    EXPECT_EQ(report, profile.GetEnglishReport());
    EXPECT_GT(report.size(), 2000U);
    EXPECT_NE(report.find("Renderer capability report"), std::string::npos);
    EXPECT_NE(report.find(std::string(device.GetGraphicsRendererName())), std::string::npos);
    EXPECT_NE(report.find("Detailed features"), std::string::npos);
    EXPECT_NE(report.find("Numeric limits"), std::string::npos);
    EXPECT_NE(report.find("Surface-format support"), std::string::npos);
    EXPECT_NE(report.find("Additional limitations"), std::string::npos);
    EXPECT_NE(report.find("Unknown format-usage bits are deliberately not treated as unsupported"),
              std::string::npos);
    EXPECT_FALSE(profile.GetAdditionalLimitationsText().empty());
    for (const CNA::RendererFeature feature : CNA::AllRendererFeatures())
        EXPECT_NE(report.find(CNA::GetRendererFeatureName(feature)), std::string::npos);
}
