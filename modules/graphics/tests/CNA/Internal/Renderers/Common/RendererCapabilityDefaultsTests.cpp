// SPDX-License-Identifier: MS-PL
// RTR-P3-17: the defaults of the virtuals that replaced the XNA layer's
// #ifdef CNA_RENDERER_* blocks.
// Capability defaults must stay explicitly unknown/zero, and a malformed supported bit must
// never pass the public known-and-supported predicate.
//
// What matters here is precisely the DEFAULT behaviour, because that is what nearly every renderer
// gets. When these decisions lived behind the preprocessor, the #else branch carried the
// renderer-agnostic answer; moving them behind virtuals only preserves behaviour if the defaults
// reproduce that #else branch exactly. A default that silently narrowed a public API -- for
// example a Color-transfer predicate that answered "Color only" instead of "any texel size that is
// a multiple of four" -- would compile, pass most tests, and break real code paths such as
// MouseCursor::FromTexture2D.

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/ShaderDialectEXT.hpp"
#include "CNA/ShaderLanguageEXT.hpp"

#include <limits>
#include <memory>

using CNA::Internal::Renderers::RendererFormatVerdict;

namespace
{
    /// The interface's own defaults, with nothing overridden. Every one of the 23 pure virtuals is
    /// given an inert body; every method under test deliberately keeps its base implementation, so
    /// what this fixture measures is exactly what a renderer inherits by writing nothing.
    class DefaultsOnlyRenderer : public CNA::Internal::Renderers::IGraphicsRenderer
    {
    public:
        void Clear(float, float, float, float) override {}
        void Present() override {}
        void GetViewportSize(int& width, int& height) override { width = 0; height = 0; }
        void SetVirtualResolution(int, int) override {}
        void SetPresentationMode(int) override {}
        // MERGE (PLAT-8): IGraphicsRenderer no longer exposes the toolkit-native
        // window/renderer accessors this mock used to override -- the platform contract owns them
        // now -- so the overrides are gone rather than retyped.
        std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> CreateTexture(
            const CNA::Internal::Graphics::ImageData&) override { return nullptr; }
        std::unique_ptr<CNA::Internal::Renderers::ISpriteBatchRenderer> CreateSpriteBatch() override
        { return nullptr; }
        void SetRenderTargets(
            const CNA::Internal::Renderers::RenderTargetBindingDescriptor*, int) override {}
        void ClearColorAndDepth(float, float, float, float, float) override {}
        void ClearDepth(float) override {}
        void ClearStencil(int) override {}
        void ClearDepthAndStencil(float, int) override {}
        void ClearColorAndStencil(float, float, float, float, int) override {}
        void ClearColorDepthAndStencil(float, float, float, float, float, int) override {}
        void SetDepthTestEnabled(bool) override {}
        void SetDepthWriteEnabled(bool) override {}
        std::unique_ptr<CNA::Internal::Renderers::IVertexBufferRenderer> CreateVertexBuffer(int) override
        { return nullptr; }
        std::unique_ptr<CNA::Internal::Renderers::IIndexBufferRenderer> CreateIndexBuffer16(int) override
        { return nullptr; }
        void DrawColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}
        void DrawIndexedColoredPrimitives(
            const CNA::Internal::Renderers::IVertexBufferRenderer&,
            const CNA::Internal::Renderers::IIndexBufferRenderer&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            const Microsoft::Xna::Framework::Matrix&,
            Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}
    };

    class StandaloneDepthRenderer final : public DefaultsOnlyRenderer
    {
    public:
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsDepthBuffer() const override { return true; }
        [[nodiscard]] bool SupportsStencilBuffer() const override { return false; }
    };

    class StandaloneStencilRenderer final : public DefaultsOnlyRenderer
    {
    public:
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsDepthBuffer() const override { return false; }
        [[nodiscard]] bool SupportsStencilBuffer() const override { return true; }
    };

    class DefaultsOnlyCubeTarget final
        : public CNA::Internal::Renderers::IRenderTargetCubeRenderer
    {
    public:
        [[nodiscard]] int GetSize() const override { return 1; }
        void BindAsRenderTargetFace(int) override {}
        void UnbindAsRenderTarget() override {}
    };
}

TEST(RendererCapabilityDefaultsTest, ProfileCeilingsDefaultToClassicXnaLimits)
{
    // SOFTWARE-179: these are profile rules, not renderer hardware capabilities. Leaving the
    // interface defaults unlimited made every non-D3D9 renderer bypass public XNA restrictions.
    DefaultsOnlyRenderer renderer;

    EXPECT_EQ(renderer.GetMaxTextureSizeForProfileEXT(0), 2048);
    EXPECT_EQ(renderer.GetMaxCubeSizeForProfileEXT(0), 512);
    EXPECT_EQ(renderer.GetMaxVolumeExtentForProfileEXT(0), 0);
    EXPECT_EQ(renderer.GetMaxRenderTargetsForProfileEXT(0), 1);

    EXPECT_EQ(renderer.GetMaxTextureSizeForProfileEXT(1), 4096);
    EXPECT_EQ(renderer.GetMaxCubeSizeForProfileEXT(1), 4096);
    EXPECT_EQ(renderer.GetMaxVolumeExtentForProfileEXT(1), 256);
    EXPECT_EQ(renderer.GetMaxRenderTargetsForProfileEXT(1), 4);
}

TEST(RendererCapabilityDefaultsTest, VolumeExtentZeroIsDistinctFromNoCeiling)
{
    // 0 means "this profile has no volume textures at all", while HiDef's finite extent permits
    // them. This distinction belongs to the profile and therefore applies to every renderer.
    DefaultsOnlyRenderer renderer;
    EXPECT_EQ(renderer.GetMaxVolumeExtentForProfileEXT(0), 0);
    EXPECT_GT(renderer.GetMaxVolumeExtentForProfileEXT(1), 0);
}

TEST(RendererCapabilityDefaultsTest, FormatClassifiersDefaultToDefer)
{
    // Defer is what keeps the framework's own rule authoritative. A default of Supported or
    // Unsupported would make every renderer assert a format policy it does not actually have.
    DefaultsOnlyRenderer renderer;

    for (int format = 0; format < 32; ++format)
    {
        EXPECT_EQ(renderer.ClassifySurfaceFormatEXT(format), RendererFormatVerdict::Defer)
            << "format ordinal " << format;
        EXPECT_EQ(renderer.ClassifyTextureCubeFormatEXT(format), RendererFormatVerdict::Defer)
            << "format ordinal " << format;
        EXPECT_EQ(renderer.ClassifyRenderTargetFormatEXT(format), RendererFormatVerdict::Defer)
            << "format ordinal " << format;
        EXPECT_EQ(renderer.ClassifyRenderTargetCubeFormatEXT(format),
                  RendererFormatVerdict::Defer)
            << "cube-target format ordinal " << format;
        EXPECT_EQ(renderer.ClassifyColorTransferFormatEXT(format), RendererFormatVerdict::Defer)
            << "format ordinal " << format;
    }
}

TEST(RendererCapabilityDefaultsTest, DetailedFormatUsagesDefaultToUnknown)
{
    DefaultsOnlyRenderer renderer;
    for (int format = 0; format < 32; ++format)
    {
        const CNA::RendererFormatSupport support =
            renderer.GetSurfaceFormatUsageSupportEXT(format);
        EXPECT_EQ(support.knownUsages, 0U) << "format ordinal " << format;
        EXPECT_EQ(support.supportedUsages, 0U) << "format ordinal " << format;
    }
}

TEST(RendererCapabilityDefaultsTest, FormatSupportRejectsUnknownAndPartiallyKnownRequests)
{
    constexpr auto storage = CNA::RendererFormatUsage::TextureStorage;
    constexpr auto renderTarget = CNA::RendererFormatUsage::RenderTarget;
    constexpr auto storageAndTarget = storage | renderTarget;

    // Deliberately malformed input: a renderer claims render-target support without classifying
    // that usage. RendererFormatSupport is also the public C++ query result, so its predicate must
    // remain safe even before GraphicsDevice's snapshot builder normalizes the raw masks.
    const CNA::RendererFormatSupport malformed{
        static_cast<std::uint32_t>(storage),
        static_cast<std::uint32_t>(storageAndTarget)};

    EXPECT_TRUE(malformed.IsKnown(storage));
    EXPECT_TRUE(malformed.Supports(storage));
    EXPECT_FALSE(malformed.IsKnown(renderTarget));
    EXPECT_FALSE(malformed.Supports(renderTarget));
    EXPECT_FALSE(malformed.IsKnown(storageAndTarget));
    EXPECT_FALSE(malformed.Supports(storageAndTarget));

    const CNA::RendererFormatSupport empty{};
    EXPECT_FALSE(empty.IsKnown(storage));
    EXPECT_FALSE(empty.Supports(storage));
}

TEST(RendererCapabilityDefaultsTest, CompressedTransferDefaultsToFalseForEveryFormat)
{
    DefaultsOnlyRenderer renderer;
    for (int format = 0; format < 32; ++format)
    {
        EXPECT_FALSE(renderer.IsCompressedTransferFormatEXT(format)) << "format ordinal " << format;
    }
}

TEST(RendererCapabilityDefaultsTest, LoadsCompressedContentNativelyDefaultsToFalse)
{
    // WEBGPU-144 Phase 2: the content loaders keep the historical decode-to-Color behaviour for
    // every renderer that has not explicitly opted in to native compressed content loading.
    DefaultsOnlyRenderer renderer;
    EXPECT_FALSE(renderer.LoadsCompressedContentNativelyEXT());
}

TEST(RendererCapabilityDefaultsTest, AdditionalLimitationsDefaultToNoRendererSpecificText)
{
    DefaultsOnlyRenderer renderer;
    EXPECT_TRUE(renderer.GetAdditionalLimitationsTextEXT().empty());
}

TEST(RendererCapabilityDefaultsTest, NumericLimitsDefaultToExplicitlyUnavailable)
{
    DefaultsOnlyRenderer renderer;

    EXPECT_EQ(renderer.GetMaxUniformBufferBytesEXT(), 0U);
    EXPECT_EQ(renderer.GetMaxSampledTexturesPerShaderStageEXT(), 0);
    EXPECT_EQ(renderer.GetMaxVertexInputBindingsEXT(), 0);
    EXPECT_EQ(renderer.GetMaxVertexInputAttributesEXT(), 0);
    EXPECT_EQ(renderer.GetMaxColorAttachmentsEXT(), 0);
    EXPECT_EQ(renderer.GetMinUniformBufferOffsetAlignmentEXT(), 0U);
}

TEST(RendererCapabilityDefaultsTest, BaseInstanceDrawingDefaultsToUnsupported)
{
    DefaultsOnlyRenderer renderer;
    EXPECT_FALSE(renderer.SupportsBaseInstanceDrawingEXT());
}

// CNA-DEMOS-ISSUE-006: a renderer that has not implemented them must not claim these.
TEST(RendererCapabilityDefaultsTest, LodBiasAndPbrShadingDefaultToUnsupported)
{
    DefaultsOnlyRenderer renderer;
    EXPECT_FALSE(renderer.AppliesSamplerLevelOfDetailBiasEXT());
    EXPECT_FALSE(renderer.ShadesPbrEffectEXT());
    EXPECT_FALSE(renderer.ShadesSkinnedPbrEffectEXT());
    EXPECT_EQ(renderer.MultiSampleMaskSupportEXT(), CNA::RendererFeatureSupport::Unsupported);
    EXPECT_FALSE(renderer.ReadsBackRenderTargetsEXT());
}

TEST(RendererCapabilityDefaultsTest, ShaderDialectOrdinalsArePublicAndStableAndDefaultToUnknown)
{
    // CNA::ShaderDialectEXT is what GraphicsDevice::GetShaderDialectEXT() hands applications and
    // what the C API's CNA_SHADER_DIALECT_* identities mirror, so its values are append-only.
    using CNA::ShaderDialectEXT;

    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::Unknown), 0);
    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::GlslDesktop), 1);
    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::GlslEs), 2);
    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::GlslVulkan), 3);
    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::Hlsl), 4);
    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::Msl), 5);
    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::Wgsl), 6);
    EXPECT_EQ(static_cast<int>(ShaderDialectEXT::SpirV), 7);

    DefaultsOnlyRenderer renderer;
    EXPECT_EQ(renderer.GetShaderDialectEXT(), ShaderDialectEXT::Unknown);
}

TEST(RendererCapabilityDefaultsTest, ShaderLanguageAndStageOrdinalsAreStableAndDefaultToUnsupported)
{
    using CNA::ShaderLanguageEXT;
    using CNA::ShaderStageEXT;

    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::Unknown), 0);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::GlslDesktop), 1);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::GlslEs), 2);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::GlslVulkan), 3);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::Hlsl), 4);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::Msl), 5);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::Wgsl), 6);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::SpirV), 7);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::Dxil), 8);
    EXPECT_EQ(static_cast<int>(ShaderLanguageEXT::Count), 9);

    EXPECT_EQ(static_cast<int>(ShaderStageEXT::Unknown), 0);
    EXPECT_EQ(static_cast<int>(ShaderStageEXT::Vertex), 1);
    EXPECT_EQ(static_cast<int>(ShaderStageEXT::Fragment), 2);
    EXPECT_EQ(static_cast<int>(ShaderStageEXT::Count), 3);

    DefaultsOnlyRenderer renderer;
    for (int language = -1; language <= static_cast<int>(ShaderLanguageEXT::Count); ++language)
    {
        for (int stage = -1; stage <= static_cast<int>(ShaderStageEXT::Count); ++stage)
            EXPECT_FALSE(renderer.SupportsShaderLanguageEXT(language, stage));
    }
}

TEST(RendererCapabilityDefaultsTest, AppliedMultiSampleCountEchoesTheRequest)
{
    // The identity default is the whole point: a renderer that honours the request reports it back.
    // Answering GetMultiSampleCount() instead would report "no MSAA" for every renderer that leaves
    // that at its 0 default -- including renderers that genuinely honoured the request.
    DefaultsOnlyRenderer renderer;
    for (const int requested : {0, 1, 2, 4, 8, 16})
    {
        EXPECT_EQ(renderer.GetAppliedMultiSampleCountEXT(requested), requested);
    }
    EXPECT_EQ(renderer.GetMultiSampleCount(), 0);
}

TEST(RendererCapabilityDefaultsTest, AppliedFormatAccessorsEchoTheRequest)
{
    // The pre-existing members of this family, pinned alongside the new ones so the whole
    // "renderer reports what it actually applied" surface has one consistent default.
    DefaultsOnlyRenderer renderer;
    DefaultsOnlyCubeTarget cubeTarget;
    for (const int requested : {0, 1, 5, 19})
    {
        EXPECT_EQ(renderer.GetAppliedBackBufferFormatEXT(requested), requested);
        EXPECT_EQ(renderer.GetAppliedDepthStencilFormatEXT(requested), requested);
        EXPECT_EQ(cubeTarget.GetAppliedDepthStencilFormatEXT(requested), requested);
    }
}

TEST(RendererCapabilityDefaultsTest, ActiveBackBufferPlanesDistinguishRequestsAndStandaloneStorage)
{
    DefaultsOnlyRenderer combined;
    EXPECT_FALSE(combined.HasRealBackBufferDepthBuffer(false));
    EXPECT_TRUE(combined.HasRealBackBufferDepthBuffer(true));
    EXPECT_FALSE(combined.HasRealBackBufferStencilBuffer(false));
    EXPECT_TRUE(combined.HasRealBackBufferStencilBuffer(true));

    StandaloneDepthRenderer depthOnly;
    EXPECT_TRUE(depthOnly.HasRealBackBufferDepthBuffer(false));
    EXPECT_FALSE(depthOnly.HasRealBackBufferStencilBuffer(true));

    StandaloneStencilRenderer stencilOnly;
    EXPECT_FALSE(stencilOnly.HasRealBackBufferDepthBuffer(true));
    EXPECT_TRUE(stencilOnly.HasRealBackBufferStencilBuffer(false));
}
