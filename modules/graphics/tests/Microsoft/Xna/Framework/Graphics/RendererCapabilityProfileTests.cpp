// SPDX-License-Identifier: MS-PL

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::ThreeDimensionalPipeline) == 0);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::ShaderDialectWgsl) == 29);
static_assert(static_cast<std::uint32_t>(CNA::RendererFeature::Count) == 30);
static_assert(static_cast<std::uint8_t>(CNA::RendererFeatureSupport::Unknown) == 0);
static_assert(static_cast<std::uint8_t>(CNA::RendererFeatureSupport::Restricted) == 3);
static_assert(static_cast<std::uint32_t>(CNA::RendererLimit::MaxTextureDimension) == 0);
static_assert(static_cast<std::uint32_t>(CNA::RendererLimit::MaxVertexShaderStorageBlocks) == 9);
static_assert(static_cast<std::uint32_t>(CNA::RendererLimit::Count) == 10);

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
    const auto features = CNA::AllRendererFeatures();
    ASSERT_EQ(features.size(), static_cast<std::size_t>(CNA::RendererFeature::Count));
    std::unordered_set<std::string> featureNames;
    for (std::size_t i = 0; i < features.size(); ++i)
    {
        EXPECT_EQ(static_cast<std::size_t>(features[i]), i);
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

    const auto limits = CNA::AllRendererLimits();
    ASSERT_EQ(limits.size(), static_cast<std::size_t>(CNA::RendererLimit::Count));
    std::unordered_set<std::string> limitNames;
    for (std::size_t i = 0; i < limits.size(); ++i)
    {
        EXPECT_EQ(static_cast<std::size_t>(limits[i]), i);
        const std::string name(CNA::GetRendererLimitName(limits[i]));
        EXPECT_FALSE(name.empty());
        EXPECT_NE(name, "UnknownRendererLimit");
        EXPECT_TRUE(limitNames.insert(name).second);
    }
    EXPECT_EQ(CNA::GetRendererLimitName(CNA::RendererLimit::Count),
              "UnknownRendererLimit");
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
                  CNA::GraphicsCapability::HalfFloatTextureLinearFiltering},
        std::pair{CNA::RendererFeature::ComputeShaders,
                  CNA::GraphicsCapability::ComputeShaders},
        std::pair{CNA::RendererFeature::IndirectDrawing,
                  CNA::GraphicsCapability::IndirectDraw}
    };

    for (const auto [feature, legacy] : mappings)
    {
        const bool expected = device.SupportsCapability(legacy);
        EXPECT_EQ(device.GetRendererFeatureSupportEXT(feature), ExpectedSupport(expected));
        EXPECT_EQ(device.SupportsRendererFeatureEXT(feature), expected);
    }

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
    EXPECT_FALSE(device.GetRendererLimitEXT(static_cast<CNA::RendererLimit>(999)).known);

    constexpr std::uint32_t classified =
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::TextureStorage) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::RenderTarget) |
        static_cast<std::uint32_t>(CNA::RendererFormatUsage::ColorTransfer);
    for (std::uint32_t ordinal = 0; ordinal < 27; ++ordinal)
    {
        const CNA::RendererFormatSupport support =
            device.GetRendererSurfaceFormatSupportEXT(static_cast<SurfaceFormat>(ordinal));
        EXPECT_EQ(support.knownUsages, classified) << ordinal;
        EXPECT_EQ(support.supportedUsages & ~support.knownUsages, 0U) << ordinal;
        EXPECT_FALSE(support.IsKnown(CNA::RendererFormatUsage::Sampled)) << ordinal;
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
