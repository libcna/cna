// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-34-style extraction: SelectMetalPipelineKind() only reads GpuDrawParams
// (plain C++, zero Objective-C dependency) and a stride -- arguably the single most safety-critical
// dispatch decision in the whole Metal renderer, since it decides which concrete MSL shader pair
// every 3D draw call actually uses, yet until now it had never actually been executed by anything,
// only read carefully. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
//
// Covers every real branch and, just as importantly, the PRECEDENCE order between them (pbr(+skinned)
// > skinned > envMapping > dualTexture > textured > colored), matching
// EasyGLRenderer::SelectProgram()'s own real precedence -- several tests deliberately set
// MULTIPLE flags at once specifically to prove the precedence order is actually enforced, not just
// that each flag works in isolation.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalSelectPipelineKind.hpp"

using namespace CNA::Internal::Renderers::Metal;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::ITextureRenderer;

namespace
{
    // Never dereferenced by SelectMetalPipelineKind; it exists only to populate unrelated captured
    // state while tests prove that effect flags and stride, not texture-pointer presence, dispatch.
    const ITextureRenderer* FakeTexture() { return reinterpret_cast<const ITextureRenderer*>(0x1); }
}

TEST(MetalSelectPipelineKind, NullParamsFallsBackToColored16AtStride16)
{
    EXPECT_EQ(SelectMetalPipelineKind(16, nullptr), MetalPipelineKind::Colored16);
}

TEST(MetalSelectPipelineKind, NullParamsThrowsForAnyOtherStride)
{
    EXPECT_THROW((void)SelectMetalPipelineKind(20, nullptr), std::runtime_error);
}

TEST(MetalSelectPipelineKind, ColoredRequiresExactlyStride16)
{
    GpuDrawParams p;
    EXPECT_EQ(SelectMetalPipelineKind(16, &p), MetalPipelineKind::Colored16);
    EXPECT_THROW((void)SelectMetalPipelineKind(17, &p), std::runtime_error);
}

TEST(MetalSelectPipelineKind, CanonicalStockPipelineSelectsByStrideNotTexturePointer)
{
    GpuDrawParams p;
    EXPECT_EQ(SelectMetalPipelineKind(20, &p), MetalPipelineKind::Textured20);
    EXPECT_EQ(SelectMetalPipelineKind(24, &p), MetalPipelineKind::ColorTex24);
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::LitTex32);
    EXPECT_THROW((void)SelectMetalPipelineKind(28, &p), std::runtime_error);
}

TEST(MetalSelectPipelineKind, Stride32SelectsVertexLitOnlyWhenLightingOnAndNotPreferPerPixel)
{
    GpuDrawParams p;

    p.lightingEnabled = false; p.preferPerPixelLighting = false;
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::LitTex32);

    p.lightingEnabled = true; p.preferPerPixelLighting = true;
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::LitTex32);

    p.lightingEnabled = true; p.preferPerPixelLighting = false;
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::LitTex32VertexLit);

    // lightingEnabled=false + preferPerPixelLighting=true is a real, if unusual, GpuDrawParams
    // combination -- must still degrade to per-pixel (lighting off wins regardless).
    p.lightingEnabled = false; p.preferPerPixelLighting = true;
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::LitTex32);
}

TEST(MetalSelectPipelineKind, DualTextureSelectsByFlagAndStrideWithNeutralTextureFallbacks)
{
    GpuDrawParams p; p.dualTexture = true;
    EXPECT_EQ(SelectMetalPipelineKind(20, &p), MetalPipelineKind::DualTex20);
    EXPECT_EQ(SelectMetalPipelineKind(24, &p), MetalPipelineKind::DualTex24Colored);
    EXPECT_THROW((void)SelectMetalPipelineKind(16, &p), std::runtime_error);
}

TEST(MetalSelectPipelineKind, DualTextureWithoutTexturesStillSelectsItsPipeline)
{
    GpuDrawParams p; p.dualTexture = true; p.texture0 = nullptr;
    p.texture1 = nullptr;
    EXPECT_EQ(SelectMetalPipelineKind(20, &p), MetalPipelineKind::DualTex20);
}

TEST(MetalSelectPipelineKind, DualTextureTakesPrecedenceOverPlainTextured)
{
    // dualTexture=true with a valid texture and stride 24 must select the dual-texture kind, not
    // fall through to the plain textured ColorTex24 path -- proves the precedence check itself, not
    // just that DualTexture works when nothing else is set.
    GpuDrawParams p; p.dualTexture = true; p.texture0 = FakeTexture();
    EXPECT_EQ(SelectMetalPipelineKind(24, &p), MetalPipelineKind::DualTex24Colored);
}

TEST(MetalSelectPipelineKind, EnvironmentMapRequiresStride32)
{
    GpuDrawParams p; p.envMapping = true;
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::EnvMap32);
    EXPECT_THROW((void)SelectMetalPipelineKind(20, &p), std::runtime_error);
}

TEST(MetalSelectPipelineKind, UntexturedLitBasicEffectUsesLitPipeline)
{
    GpuDrawParams p;
    p.textureEnabled = false;
    p.texture0 = nullptr;
    p.lightingEnabled = true;
    p.preferPerPixelLighting = false;
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::LitTex32VertexLit);
    p.preferPerPixelLighting = true;
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::LitTex32);
}

TEST(MetalSelectPipelineKind, EnvironmentMapTakesPrecedenceOverDualTextureAndTextured)
{
    GpuDrawParams p; p.envMapping = true; p.dualTexture = true; p.texture0 = FakeTexture();
    EXPECT_EQ(SelectMetalPipelineKind(32, &p), MetalPipelineKind::EnvMap32);
}

TEST(MetalSelectPipelineKind, SkinnedSelectsByStrideAndVertexLitGate)
{
    GpuDrawParams p; p.skinned = true;
    EXPECT_EQ(SelectMetalPipelineKind(52, &p), MetalPipelineKind::Skinned52);
    EXPECT_EQ(SelectMetalPipelineKind(56, &p), MetalPipelineKind::Skinned56);
    EXPECT_THROW((void)SelectMetalPipelineKind(20, &p), std::runtime_error);

    p.lightingEnabled = true; p.preferPerPixelLighting = false;
    EXPECT_EQ(SelectMetalPipelineKind(52, &p), MetalPipelineKind::Skinned52VertexLit);
    EXPECT_EQ(SelectMetalPipelineKind(56, &p), MetalPipelineKind::Skinned56VertexLit);
}

TEST(MetalSelectPipelineKind, SkinnedTakesPrecedenceOverEnvironmentMapDualTextureAndTextured)
{
    GpuDrawParams p;
    p.skinned = true; p.envMapping = true; p.dualTexture = true; p.texture0 = FakeTexture();
    EXPECT_EQ(SelectMetalPipelineKind(52, &p), MetalPipelineKind::Skinned52);
}

TEST(MetalSelectPipelineKind, PbrUnskinnedRequiresStride48)
{
    GpuDrawParams p; p.pbr = true;
    EXPECT_EQ(SelectMetalPipelineKind(48, &p), MetalPipelineKind::Pbr48);
    EXPECT_THROW((void)SelectMetalPipelineKind(52, &p), std::runtime_error);
}

TEST(MetalSelectPipelineKind, PbrSkinnedRequiresStride68)
{
    GpuDrawParams p; p.pbr = true; p.skinned = true;
    EXPECT_EQ(SelectMetalPipelineKind(68, &p), MetalPipelineKind::SkinnedPbr68);
    EXPECT_THROW((void)SelectMetalPipelineKind(52, &p), std::runtime_error);
}

TEST(MetalSelectPipelineKind, PbrTakesPrecedenceOverEverythingElseIncludingSkinnedAloneCheck)
{
    // pbr && skinned must reach the SkinnedPbr68 branch, not the plain Skinned52/56 branch --
    // proves pbr's own precedence check runs (and short-circuits) before skinned's own branch is
    // ever reached, even though skinned is also true here.
    GpuDrawParams p;
    p.pbr = true; p.skinned = true; p.envMapping = true; p.dualTexture = true; p.texture0 = FakeTexture();
    EXPECT_EQ(SelectMetalPipelineKind(68, &p), MetalPipelineKind::SkinnedPbr68);

    GpuDrawParams p2;
    p2.pbr = true; p2.envMapping = true; p2.dualTexture = true; p2.texture0 = FakeTexture();
    EXPECT_EQ(SelectMetalPipelineKind(48, &p2), MetalPipelineKind::Pbr48);
}
