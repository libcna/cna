// SPDX-License-Identifier: MS-PL
// plans/plan_runtimerenderer.md RTR-P3-17: the defaults of the virtuals that replaced the XNA layer's
// #ifdef CNA_RENDERER_* blocks.
// plans/plan_modern.md MOD-2224: capability defaults must stay explicitly unknown/zero, and a
// malformed supported bit must never pass the public known-and-supported predicate.
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
    class DefaultsOnlyRenderer final : public CNA::Internal::Renderers::IGraphicsRenderer
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

    class DefaultsOnlyTexture2DArrayRenderer final
        : public CNA::Internal::Renderers::ITexture2DArrayRenderer
    {
    };

    class DefaultsOnlyStorageTexture2DRenderer final
        : public CNA::Internal::Renderers::IStorageTexture2DRenderer
    {
    };

    class DefaultsOnlyEffectRenderer final
        : public CNA::Internal::Renderers::IEffectRenderer
    {
    public:
        bool CompileProgram(const std::string&, const std::string&) override { return false; }
        void Bind() override {}
        void Unbind() override {}
        [[nodiscard]] bool IsValid() const override { return false; }
        [[nodiscard]] std::string GetCompileError() const override { return {}; }
    };

    class DefaultsOnlyComputeRenderer final
        : public CNA::Internal::Renderers::IComputeShaderRenderer
    {
    public:
        bool CompileProgram(const std::string&) override { return false; }
        void Bind() override {}
        [[nodiscard]] bool IsValid() const override { return false; }
        [[nodiscard]] std::string GetCompileError() const override { return {}; }
    };
}

TEST(RendererCapabilityDefaultsTest, ProfileCeilingsDefaultToNoCeiling)
{
    // The behaviour every renderer except D3D9 had: no GraphicsProfile ceiling is enforced at all.
    // Reporting a finite number here would impose a limit on renderers that never had one.
    DefaultsOnlyRenderer renderer;
    const int unlimited = (std::numeric_limits<int>::max)();

    for (const int profile : {0, 1})
    {
        EXPECT_EQ(renderer.GetMaxTextureSizeForProfileEXT(profile), unlimited);
        EXPECT_EQ(renderer.GetMaxCubeSizeForProfileEXT(profile), unlimited);
        EXPECT_EQ(renderer.GetMaxVolumeExtentForProfileEXT(profile), unlimited);
        EXPECT_EQ(renderer.GetMaxRenderTargetsForProfileEXT(profile), unlimited);
    }
}

TEST(RendererCapabilityDefaultsTest, VolumeExtentZeroIsDistinctFromNoCeiling)
{
    // 0 means "this profile has no volume textures at all" (genuinely true for Reach on D3D9), not
    // "no ceiling". The default must never be 0, or every renderer would refuse Texture3D outright.
    DefaultsOnlyRenderer renderer;
    EXPECT_NE(renderer.GetMaxVolumeExtentForProfileEXT(0), 0);
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
        EXPECT_EQ(renderer.ClassifyRenderTargetFormatEXT(format), RendererFormatVerdict::Defer)
            << "format ordinal " << format;
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

TEST(RendererCapabilityDefaultsTest, ModernGpuLimitsDefaultToExplicitlyUnavailable)
{
    DefaultsOnlyRenderer renderer;

    EXPECT_EQ(renderer.GetMaxStorageBufferBytesEXT(), 0U);
    EXPECT_EQ(renderer.GetMaxUniformBufferBytesEXT(), 0U);
    EXPECT_EQ(renderer.GetMaxComputeStorageBufferBindingsEXT(), 0);
    EXPECT_EQ(renderer.GetMaxTextureArrayLayersEXT(), 0);
    EXPECT_EQ(renderer.GetMaxSampledTexturesPerShaderStageEXT(), 0);
    EXPECT_EQ(renderer.GetMaxStorageImagesPerShaderStageEXT(), 0);
    EXPECT_EQ(renderer.GetMaxVertexInputBindingsEXT(), 0);
    EXPECT_EQ(renderer.GetMaxVertexInputAttributesEXT(), 0);
    EXPECT_EQ(renderer.GetMaxColorAttachmentsEXT(), 0);
    EXPECT_EQ(renderer.GetMinStorageBufferOffsetAlignmentEXT(), 0U);
    EXPECT_EQ(renderer.GetMinUniformBufferOffsetAlignmentEXT(), 0U);
    EXPECT_EQ(renderer.GetTimestampPeriodPicosecondsEXT(), 0U);
}

TEST(RendererCapabilityDefaultsTest, TextureArrayFactoryDefaultsToUnsupported)
{
    DefaultsOnlyRenderer renderer;
    EXPECT_EQ(renderer.CreateTexture2DArrayEXT(4, 3, 2, 1, 0, UINT32_C(1)), nullptr);
}

TEST(RendererCapabilityDefaultsTest, StorageTextureFactoryDefaultsToUnsupported)
{
    DefaultsOnlyRenderer renderer;
    EXPECT_EQ(renderer.CreateStorageTexture2DEXT(4, 3, 1, 0, UINT32_C(2)), nullptr);
}

TEST(RendererCapabilityDefaultsTest, TextureArrayOperationsAndBindingDefaultToUnsupported)
{
    DefaultsOnlyTexture2DArrayRenderer texture;
    DefaultsOnlyEffectRenderer effect;
    unsigned char bytes[4]{};

    EXPECT_FALSE(texture.SetData(0, 0, 0, 0, 1, 1, bytes, sizeof(bytes)));
    EXPECT_FALSE(texture.GetData(0, 0, 0, 0, 1, 1, bytes, sizeof(bytes)));
    EXPECT_FALSE(effect.BindTexture2DArrayEXT(0, {}));
}

TEST(RendererCapabilityDefaultsTest, StorageTextureOperationsAndBindingsDefaultToUnsupported)
{
    DefaultsOnlyStorageTexture2DRenderer texture;
    DefaultsOnlyEffectRenderer effect;
    DefaultsOnlyComputeRenderer compute;
    unsigned char bytes[4]{};

    EXPECT_FALSE(texture.SetData(0, 0, 0, 1, 1, bytes, sizeof(bytes)));
    EXPECT_FALSE(texture.GetData(0, 0, 0, 1, 1, bytes, sizeof(bytes)));
    EXPECT_FALSE(effect.BindStorageTexture2DEXT(0, {}));
    EXPECT_FALSE(compute.BindStorageTexture2DEXT(0, {}, 1));
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
