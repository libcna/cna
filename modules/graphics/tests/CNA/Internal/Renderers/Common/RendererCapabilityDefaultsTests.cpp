// SPDX-License-Identifier: MS-PL
// plans/plan_runtimerenderer.md RTR-P3-17: the defaults of the virtuals that replaced the XNA layer's
// #ifdef CNA_RENDERER_* blocks.
//
// What matters here is precisely the DEFAULT behaviour, because that is what 45 of the 46 renderers
// get. When these decisions lived behind the preprocessor, the #else branch carried the
// renderer-agnostic answer; moving them behind virtuals only preserves behaviour if the defaults
// reproduce that #else branch exactly. A default that silently narrowed a public API -- for
// example a Color-transfer predicate that answered "Color only" instead of "any texel size that is
// a multiple of four" -- would compile, pass most tests, and break real code paths such as
// MouseCursor::FromTexture2D.

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

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
        // MERGE (plans/plan_platform.md PLAT-8): IGraphicsRenderer no longer exposes the toolkit-native
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
        void SetBlendEnabled(bool) override {}
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
        EXPECT_EQ(renderer.ClassifyColorTransferFormatEXT(format), RendererFormatVerdict::Defer)
            << "format ordinal " << format;
    }
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

TEST(RendererCapabilityDefaultsTest, AppliedMultiSampleCountEchoesTheRequest)
{
    // The identity default is the whole point: it is what every renderer except GDI did. Answering
    // GetMultiSampleCount() instead would report "no MSAA" for every renderer that leaves that at
    // its 0 default -- including renderers that genuinely honoured the request.
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
    for (const int requested : {0, 1, 5, 19})
    {
        EXPECT_EQ(renderer.GetAppliedBackBufferFormatEXT(requested), requested);
        EXPECT_EQ(renderer.GetAppliedDepthStencilFormatEXT(requested), requested);
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
