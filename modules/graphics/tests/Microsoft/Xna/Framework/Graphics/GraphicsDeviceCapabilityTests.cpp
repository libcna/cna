// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "WireFrameTriangleOracle.hpp"

#ifdef CNA_RENDERER_HEADLESS
#include "CNA/Internal/Renderers/Headless/HeadlessRenderer.hpp"
#endif

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using CNA::GraphicsCapability;

static_assert(static_cast<int>(GraphicsCapability::CompiledEffects) == 13,
              "CompiledEffects must remain appended so existing capability ordinals stay stable");

// This test target is built for multiple renderer families. Per-renderer arms below preserve each
// accepted capability boundary rather than assuming one principal renderer's answers are universal.
//
// plan_opengl1.md phase 12 finding: OPENGL1 is a SECOND genuinely-3D-capable renderer this file's
// original "only ever builds against EasyGL" assumption did not anticipate -- and its real,
// honest capability profile (modules/renderers/opengl1/examples/opengl1_graphics_capability_test.cpp, plan_opengl1.md
// phase 11) legitimately differs from EasyGL's on 3 of these checks: OPENGL1 has no
// ARB_multitexture-based MRT or custom-shader Effect pipeline (this renderer's own design rule:
// "No GLSL/custom ShaderEffect pipeline in the strict OPENGL1 renderer"), but DOES support
// wireframe via real glPolygonMode(GL_LINE); EasyGL instead uses measured triangle-edge
// re-expansion because GLES3 has no polygon mode. Both report working wireframe through different
// implementations. OcclusionQuery (plan_opengl1.md item 23, EasyGL parity, added
// 2026-07-20) is no longer one of the differing checks -- both renderers now genuinely support it
// (OPENGL1 via real ARB_occlusion_query/core-1.5 GL_SAMPLES_PASSED queries).

// OpenGL ES 1.1 is a fixed-function pipeline with no MRT mechanism, no occlusion-query mechanism
// anywhere in the CM registry, and no shader compiler at all. Its `false` for these three is the
// truthful answer, and is asserted here rather than left as a standing red -- the point of the
// capability query is that a caller can trust it.
#if defined(CNA_RENDERER_OPENGLES1)
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = false;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_EASYGL) \
    && (defined(CNA_GL_PROFILE_OPENGLES2) || defined(CNA_GL_PROFILE_WEBGL1))
// The GLSL ES 1.00 profiles of the EasyGL family (OPENGLES2 native; WEBGL1 is Emscripten-only
// and cannot build this suite, listed for completeness) truthfully refuse the ES 3.0-level
// features: core ES 2.0 has no draw-buffers MRT and no query objects. CustomEffects stays true
// -- the ShaderEffect mechanism works; the game's GLSL source must be ES 1.00 under these
// profiles (docs/opengles2-renderer.md), exactly as it must be ES 3.00-compatible elsewhere.
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = false;
constexpr bool kExpectCustomEffects         = true;
#elif defined(CNA_RENDERER_OPENGL1)
// plan_opengl1.md phase 12: OPENGL1 is a second, legitimately-different, equally-honest
// 3D-capable renderer -- no MRT and no custom-shader support in its fixed-function
// pipeline, reported truthfully rather than inherited. Occlusion queries became real in
// item 23 (ARB_occlusion_query/core GL 1.5, GL_SAMPLES_PASSED), so that answer no longer
// differs from EasyGL's.
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = true;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_WICKED)
// plan_wicked.md WICKED-57/68: this renderer answers CustomEffects with a truthful false --
// IEffectRenderer addresses shader constants by name, which needs the SPIR-V reflection this
// renderer does not do, so custom effects are refused at the call site rather than approximated.
// MRT (up to 4 attachments) and occlusion queries (a real GPUQueryHeap with readback) are
// genuinely implemented, so those two keep the default expectation. This arm became reachable
// only when WICKED-78 made a bare device's teardown survivable; the catch-all below had been
// answering for this renderer until then.
constexpr bool kExpectMultipleRenderTargets = true;
constexpr bool kExpectOcclusionQuery        = true;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_SKIA)
// The one 2D-only renderer in this block. All three answers are structural rather than
// not-yet-implemented: SkCanvas produces exactly one colour result (docs/skia-renderer.md's MRT
// row, `Skia_MRT_Rejection`), raster final pixels cannot distinguish positive from zero coverage
// so no samples-passed query is definable at all, and the accepted custom-effect route is the
// narrow opt-in CNA_SKIA_SKSL_V1 ABI rather than the arbitrary-Effect support a true would
// promise. Reported false and asserted here rather than left to the catch-all below, which would
// have claimed all three.
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = false;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_BLEND2D)
// Another 2D-only renderer, same shape as Skia immediately above: Blend2D's BLContext produces
// exactly one colour result (no MRT mechanism), a raster surface has no samples-passed query to
// report, and this renderer implements no custom-shader Effect ABI at all (CreateEffectRenderer
// keeps the shared nullptr default). All three are honest structural refusals, not gaps.
#elif defined(CNA_RENDERER_OPENVG)
// OpenVG is a 2D vector-graphics API with no 3D pipeline, no MRT, and no occlusion-query concept
// at all -- and no programmable shader stage for a genuinely custom Effect (same reasoning as
// Canvas/Skia's own arms just above/below).
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = false;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_PORTABLEGL)
// PortableGL owns exactly one framebuffer per context and creates no render targets at all
// (SetRenderTargets refuses every non-empty binding), has no occlusion-query mechanism, and its
// shader stage is a pair of C function pointers with nothing for a CNA Effect to be compiled into
// (PortableGLSpriteBatchRenderer::SetCustomEffect refuses a non-null Effect rather than drawing
// with the built-in sprite shader). All three answers are structural, and each is backed by a
// refusal in modules/renderers/portablegl/examples/portablegl_rejection_test.cpp.
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = false;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_TINYGL)
// TinyGL answers the same three refusals as PortableGL above, for its own fixed-function reasons:
// CreateRenderTarget2D()/CreateRenderTargetCube() keep IGraphicsRenderer's nullptr defaults and
// SetRenderTargets() refuses a non-empty binding, CreateOcclusionQuery() keeps its nullptr default,
// and TinyGL has no shader stage of any kind for a CNA Effect to be compiled into, so
// CreateEffectRenderer() keeps its nullptr default and TinyGLSpriteBatchRenderer::SetCustomEffect()
// refuses a non-null Effect. Each refusal is backed by
// modules/renderers/tinygl/examples/tinygl_rejection_test.cpp.
constexpr bool kExpectMultipleRenderTargets = false;
constexpr bool kExpectOcclusionQuery        = false;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_DILIGENT)
// plan_diligent.md DILIGENT-42: a third genuinely 3D-capable renderer with its own
// honest, narrower profile at this point in its implementation -- no custom ShaderEffect
// compilation. MRT (DILIGENT-24, up to four attachments) and occlusion queries (DILIGENT-41, a
// real IQuery, exact or binary depending on the device feature) are both real. Each answer
// is reported truthfully rather than inherited from EasyGL, and each moves when its own task lands.
constexpr bool kExpectMultipleRenderTargets = true;
constexpr bool kExpectOcclusionQuery        = true;
constexpr bool kExpectCustomEffects         = false;
#elif defined(CNA_RENDERER_FNA3D)
// plan_fna3d.md: FNA3D's only shader entry point is FNA3D_CreateEffect, which takes a *compiled*
// Direct3D 9 Effect Framework binary and runs it through MojoShader; nothing in the library
// compiles a GLSL/HLSL source string, which is what IEffectRenderer/CreateEffectRenderer is handed.
// The false is therefore structural, not not-yet-implemented, and CreateEffectRenderer returns
// null to match (docs/fna3d-renderer.md). MRT (FNA3D_SetRenderTargets takes the whole ordered set)
// and occlusion queries (FNA3D_CreateQuery/QueryPixelCount) are genuinely implemented.
constexpr bool kExpectMultipleRenderTargets = true;
constexpr bool kExpectOcclusionQuery        = true;
constexpr bool kExpectCustomEffects         = false;
#else
constexpr bool kExpectMultipleRenderTargets = true;
constexpr bool kExpectOcclusionQuery        = true;
constexpr bool kExpectCustomEffects         = true;
#endif

#if defined(CNA_RENDERER_FNA3D)
constexpr bool kExpectCompiledEffects = true;
#else
constexpr bool kExpectCompiledEffects = false;
#endif

// Both of these assert `true` for every 3D-capable renderer. A deliberately 2D-only renderer
// answers false, and that is the correct answer, not a gap -- so it gets its own arm rather than
// a standing red. Only the arm for the renderer being added is written here; the other 2D-only
// identities in this repository are untouched by this file and keep whatever they answer today.
#if defined(CNA_RENDERER_SKIA) || defined(CNA_RENDERER_BLEND2D)
TEST(GraphicsDeviceCapabilityTest, SupportsThreeD)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::ThreeD))
        << "this 2D-only raster renderer claims a 3D pipeline -- every 3D route it owns refuses "
           "through HandleUnsupported3DCall(), so a true report cannot be backed by anything";
}

TEST(GraphicsDeviceCapabilityTest, SupportsDepthStencilBuffer)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::DepthStencilBuffer))
        << "this 2D-only raster renderer claims a depth/stencil buffer -- its render targets have "
           "no attachment, and DepthStencilState::None is accepted only as the absence of one";
}
#elif defined(CNA_RENDERER_OPENVG)
TEST(GraphicsDeviceCapabilityTest, SupportsThreeD)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::ThreeD))
        << "OpenVG (ShivaVG) claims a 3D pipeline -- every 3D route it owns refuses through "
           "HandleUnsupported3DCall(), so a true report cannot be backed by anything";
}

TEST(GraphicsDeviceCapabilityTest, SupportsDepthStencilBuffer)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::DepthStencilBuffer))
        << "OpenVG has no depth/stencil concept whatsoever -- OpenVgRenderer::SupportsDepthStencil "
           "returns false unconditionally";
}
#elif defined(CNA_RENDERER_TINYGL)
// TinyGL splits the pair the arms above keep together: it is genuinely 3D-capable, but its
// ZBuffer carries a depth plane and no stencil plane. DepthStencilBuffer names the pair, so the
// honest answer is false even though the depth half is real and implemented -- see
// TinyGLRenderer::SupportsCapability(). SupportsStencilBuffer below asserts the same truth from
// the renderer's own side.
TEST(GraphicsDeviceCapabilityTest, SupportsThreeD)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::ThreeD))
        << "TinyGL's fixed-function 3D routes (DrawPrimitivesEx/DrawIndexedPrimitivesEx) are real "
           "and covered by modules/renderers/tinygl/examples/tinygl_3d_test.cpp";
}

TEST(GraphicsDeviceCapabilityTest, SupportsDepthStencilBuffer)
{
    GraphicsDevice gd;
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::DepthStencilBuffer))
        << "TinyGL's ZBuffer has a depth plane but no stencil plane, so the depth/stencil pair "
           "this capability names cannot be claimed";
}
#else
TEST(GraphicsDeviceCapabilityTest, SupportsThreeD)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::ThreeD));
}

TEST(GraphicsDeviceCapabilityTest, SupportsDepthStencilBuffer)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::DepthStencilBuffer));
}
#endif

TEST(GraphicsDeviceCapabilityTest, SupportsStencilBuffer)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::StencilBuffer),
              gd.GetRenderer().SupportsStencilBuffer());
}

TEST(GraphicsDeviceCapabilityTest, SupportsMultipleRenderTargets)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets), kExpectMultipleRenderTargets);
}

// The consistency check the two tests above could not make between them.
//
// SupportsMultipleRenderTargets asserted the capability, and
// GraphicsDeviceValidationTest::SetRenderTargets_FourTargets_DoesNotThrow asserted the behaviour,
// in a different file and against a hand-maintained list of renderers. Nothing tied them
// together, so HeadlessRenderer spent a while reporting MultipleRenderTargets as available --
// its SupportsCapability answers true by default -- while SetRenderTargets threw for any count
// above one. Both tests were "passing"; they simply disagreed.
//
// Two targets rather than four on purpose: two is the minimum a renderer must accept for the
// capability to mean anything, so this stays correct for a future renderer whose ceiling is
// lower than four.
TEST(GraphicsDeviceCapabilityTest, TheMultipleRenderTargetCapabilityMatchesWhatBindingActuallyDoes)
{
    GraphicsDevice gd;

    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;

    std::vector<std::unique_ptr<RenderTarget2D>> targets;
    std::vector<RenderTargetBinding> bindings;
    for (int i = 0; i < 2; ++i)
    {
        targets.push_back(std::make_unique<RenderTarget2D>(gd, 4, 4));
        bindings.emplace_back(targets.back().get());
    }

    bool accepted = true;
    try
    {
        gd.SetRenderTargets(bindings);
    }
    catch (const std::exception&)
    {
        // Which exception type is each renderer's own documented choice; that a refusal happened
        // at all is what has to agree with the capability.
        accepted = false;
    }

    EXPECT_EQ(accepted, gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets))
        << "the renderer reports MultipleRenderTargets as "
        << (gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets) ? "available"
                                                                            : "unavailable")
        << " but binding two targets " << (accepted ? "succeeded" : "was refused");

    // Unbind before leaving, so a later test in this process does not inherit targets bound here.
    const std::vector<RenderTargetBinding> none;
    try
    {
        gd.SetRenderTargets(none);
    }
    catch (const std::exception&)
    {
    }
}

TEST(GraphicsDeviceCapabilityTest, SupportsOcclusionQuery)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::OcclusionQuery), kExpectOcclusionQuery);
}

TEST(GraphicsDeviceCapabilityTest, SupportsCustomEffects)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::CustomEffects), kExpectCustomEffects);
}

TEST(GraphicsDeviceCapabilityTest, SupportsCompiledEffectsOnlyOnCompletedBackends)
{
    GraphicsDevice gd;
    EXPECT_EQ(gd.SupportsCapability(GraphicsCapability::CompiledEffects),
              kExpectCompiledEffects);
}

// MSAA/anisotropic filtering are genuinely device/driver-dependent -- don't assert a specific
// value (would make this test flaky across different CI machines/GPUs), just that querying them
// doesn't throw.
TEST(GraphicsDeviceCapabilityTest, MultiSampleAntiAliasingQueryDoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing); });
}

TEST(GraphicsDeviceCapabilityTest, AnisotropicFilteringQueryDoesNotThrow)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::AnisotropicFiltering); });
}

// REMED-CONTENT-001: GetMaxTextureDimension() must report a real, positive, sane ceiling -- shared
// content-reading code (Texture2DContentTypeReader) relies on this to reject malformed/adversarial
// XNB dimensions before any renderer-specific texture creation is attempted.
TEST(GraphicsDeviceCapabilityTest, GetMaxTextureDimensionReturnsSanePositiveValue)
{
    GraphicsDevice gd;
    const int maxDim = gd.GetMaxTextureDimension();
    EXPECT_GT(maxDim, 0);
    // Comfortably above any legitimate real-world game asset, and comfortably below the
    // adversarial int32 values a corrupt/malicious .xnb could declare.
    EXPECT_GE(maxDim, 2048);
    EXPECT_LT(maxDim, 0x7FFFFFFF);
}

// ============================================================================
// REMED-GFX-209 -- the FillMode::WireFrame contract is per renderer, not universal.
//
// WHAT WAS HERE BEFORE. A single test, `DoesNotSupportWireFrame`, asserting
//
//     EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::WireFrame));
//
// in a file that is compiled once per renderer and gated on nothing. It encoded ONE renderer's
// documented gap -- EasyGL's "GLES3 has no polygon mode" entry in plan_graphics.md's XNA 4.0
// coverage table -- as though every renderer shared it, and therefore failed on Software, Headless,
// bgfx, WebGPU, Vulkan and SDL_GPU. It could never pass falsely, so it hid nothing; it was simply
// a standing red in six principal suites.
//
// THE MEASURED CONTRACT, one renderer at a time. Every row below is a real reading from the pixel
// oracle in this file, not a reading of the source:
//
//   Software, Vulkan, bgfx, SDL_GPU, D3D9, D3D11  reports true, renders a real wireframe
//   EasyGL                                        reports true, renders line-expansion wireframe
//   WebGPU                                        reports FALSE, refuses the draw (WEBGPU-115)
//   Headless                                      reports true, rasterizes nothing at all
//   D3D12                                         maps FILL_WIREFRAME through the shared
//                                                 D3DCommon table; no runtime in this environment
//
// So there is no universal answer in either direction. What replaces the old assertion is three
// honest shapes:
//
//   * a POSITIVE PIXEL ORACLE where wireframe genuinely renders;
//   * a NEGATIVE ORACLE where the renderer reports the gap and refuses the draw, proving the
//     refusal mutated nothing and that Solid still works afterwards;
//   * an HONEST SKIP where the renderer has no pixel route to measure at all.
//
// WEBGPU-115 FILLED THE REJECTION ARM. When REMED-GFX-209 measured this contract the arm had an
// EMPTY registration set -- no renderer refused, so the absence was recorded rather than a rejection
// manufactured. WebGPU used to report true, accept the request, build and natively submit a
// distinct pipeline for it, and return a frame byte-identical to Solid. It now reports false and
// throws System::NotSupportedException before anything is queued. The full cardinality and route
// matrix for that boundary lives in WebGpuWireFrameContractTests.cpp; this file keeps the
// per-renderer shape of the contract.
//
// HISTORICAL EASYGL FINDING, NOW RESOLVED. Before REMED-GFX-219 the implementation rendered a
// measured-correct GL_LINES wireframe while the capability under-reported false. The current
// report is true and the oracle below preserves that correction.
// ============================================================================

// The oracle itself -- geometry, probes, colours, the Solid control and the single-draw renderer
// -- lives in WireFrameTriangleOracle.hpp so WEBGPU-115's rejection suite measures the identical
// fixture instead of a copy that can drift from this one. The CNA_WIREFRAME_* selection macros are
// defined there too, for the same reason.
#ifdef CNA_WIREFRAME_PIXEL_ORACLE
using namespace CnaTest::WireFrameOracle;   // NOLINT(google-build-using-namespace)
#endif

// ---------------------------------------------------------------------------
// The capability report, per renderer. Each arm states what THIS renderer answers today; none of
// them claims anything about another.
// ---------------------------------------------------------------------------
TEST(GraphicsDeviceCapabilityTest, WireFrameCapabilityReportIsThisBackendsOwn)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::WireFrame); });
    const bool reported = gd.SupportsCapability(GraphicsCapability::WireFrame);

#if defined(CNA_RENDERER_EASYGL)
    // REMED-GFX-219 landed with the GL-family lane: the EasyGL implementation's GL_LINES
    // re-expansion renders a correct wireframe (the pixel oracle below measures interior 0/1089
    // with all three edges present), so the report now states the capability the renderer
    // genuinely has. True for every GL profile alike -- the emulation draws line primitives and
    // depends on no polygon-mode API. Under OPENGLES2 the renderer's report is additionally
    // conditional on GL_OES_element_index_uint (the emulation's 32-bit line indices are an
    // extension in core ES 2.0); every driver this suite runs on advertises it, so the
    // expectation holds unchanged there.
    EXPECT_TRUE(reported)
        << "the EasyGL-family renderers under-report WireFrame again -- REMED-GFX-219's corrected "
           "report is gone while the GL_LINES emulation still renders a measured-correct wireframe";
#elif defined(CNA_RENDERER_WEBGPU)
    // WEBGPU-115: asserted, not inherited. wgpu-native has no polygon mode at all, so
    // WebGPURenderer::SupportsCapability answers false and the draw-time guard refuses.
    EXPECT_FALSE(reported)
        << "WebGPU claims WireFrame support again -- WEBGPU-115's capability override is gone, and "
           "the renderer has no polygon mode to back the claim with";
#elif defined(CNA_RENDERER_DIRECTX1)
    // DIRECTX1 is 2D-only by design -- DirectX 1 has no Direct3D at all, so there is no polygon fill
    // mode to report on and DirectX1Renderer::SupportsCapability answers false for every
    // capability, WireFrame included. Same truthful-false shape as WebGPU above, for the opposite
    // reason: nothing here could rasterize a triangle in the first place. (DIRECTX2..DIRECTX8 and D3D10
    // report true and take the default arm below -- their fill mode is real, spike-verified on
    // DIRECTX2's own software RGB device and on DIRECTX8/D3D10's DXVK GPU path.)
    EXPECT_FALSE(reported)
        << "DIRECTX1 claims WireFrame support -- this renderer has no 3D pipeline at all, so a true "
           "report cannot be backed by any rendering path";
#elif defined(CNA_RENDERER_SKIA)
    // Skia's selected artifact is a CPU raster 2D surface: SkCanvas has no polygon fill mode, and
    // there is no vertex/primitive route for one to apply to. The refusal half of REMED-GFX-209 is
    // satisfied more strongly than it asks -- Ensure3DSupported() rejects every 3D draw before any
    // vertex input is inspected, so no polygon topology can reach a raster queue to be silently
    // filled solid. Same truthful-false shape as DIRECTX1 and Stub, and like Stub it has no pixel route
    // to measure, which is why WireFrameTriangleOracle.hpp leaves CNA_WIREFRAME_PIXEL_ORACLE
    // undefined here.
    EXPECT_FALSE(reported)
        << "Skia claims WireFrame support -- this raster renderer has no polygon fill mode and no "
           "3D draw route, so a true report cannot be backed by any rendering path";
#elif defined(CNA_RENDERER_BLEND2D)
    // Same truthful-false shape as Skia immediately above: Blend2D's BLContext has no polygon fill
    // mode and no vertex/primitive route at all -- SupportsCapability(ThreeD) is already false, and
    // DrawColoredPrimitives/DrawIndexedColoredPrimitives refuse every 3D draw before any vertex
    // input is inspected, so no polygon topology can reach a raster queue to be silently filled
    // solid. Like Skia, WireFrameTriangleOracle.hpp leaves CNA_WIREFRAME_PIXEL_ORACLE undefined
    // here (no pixel route to measure).
    EXPECT_FALSE(reported)
        << "Blend2D claims WireFrame support -- this raster renderer has no polygon fill mode and "
           "no 3D draw route, so a true report cannot be backed by any rendering path";
#elif defined(CNA_RENDERER_OPENVG)
    // OpenVG is a 2D vector-graphics API: no polygon fill mode, no vertex/primitive route, no 3D
    // pipeline at all. Same truthful-false shape as Skia/DIRECTX1 -- OpenVgRenderer's own 3D
    // pure-virtuals all refuse through HandleUnsupported3DCall() before any topology could reach a
    // draw. Like Skia/DIRECTX1/Stub it has no pixel route to measure, which is why
    // WireFrameTriangleOracle.hpp leaves CNA_WIREFRAME_PIXEL_ORACLE undefined here.
    EXPECT_FALSE(reported)
        << "OpenVG claims WireFrame support -- this renderer has no 3D pipeline at all, so a true "
           "report cannot be backed by any rendering path";
#elif defined(CNA_RENDERER_STUB)
    // Stub answers false to EVERY capability, WireFrame included: it is a no-op renderer that
    // rasterizes nothing and keeps no state, so there is no rendering path a true report could
    // stand on. Same truthful-false shape as DIRECTX1 above.
    //
    // Note what this arm is NOT. Stub is not in the rejection set: unlike WebGPU, it does not throw
    // on a WireFrame draw, because its declared contract is that a real Game loop runs to
    // completion without throwing (docs/stub-renderer.md, modules/renderers/stub/examples/stub_smoke_test.cpp). The
    // refusal obligation exists to stop a renderer from returning a frame that silently lies -- a
    // solid fill presented as a wireframe. Stub returns no frame at all, so it has the third shape
    // this file describes: an honest report with no pixel route to measure, which is why
    // WireFrameTriangleOracle.hpp deliberately leaves CNA_WIREFRAME_PIXEL_ORACLE undefined here.
    //
    // Stub is also stricter than Headless, which reaches the default arm below by INHERITING
    // IGraphicsRenderer's true. Stub overrides SupportsCapability to false precisely so a no-op
    // renderer stops claiming a capability it does not have.
    EXPECT_FALSE(reported)
        << "Stub claims WireFrame support -- its SupportsCapability override is gone and it has "
           "fallen back to IGraphicsRenderer's default true, which no no-op renderer can back";
#else
    // Every other renderer in this file answers true, either because it renders a real wireframe
    // (Software, Vulkan, bgfx, SDL_GPU, D3D9, D3D11, D3D12, OpenGL4 -- the last via desktop core
    // GL's own glPolygonMode, asserted by the pixel oracle below) or because it inherits
    // IGraphicsRenderer's default (Headless, which rasterizes nothing at all).
    EXPECT_TRUE(reported);
#endif
}

#ifdef CNA_WIREFRAME_PIXEL_ORACLE

// ---------------------------------------------------------------------------
// POSITIVE CONTRACT -- renderers that genuinely rasterize a wireframe.
// ---------------------------------------------------------------------------

TEST(GraphicsDeviceCapabilityTest, WireFrameLightsEveryEdgeAndLeavesTheInteriorUnfilled)
{
#if !defined(CNA_WIREFRAME_MEASURED)
    GTEST_SKIP() << kRendererName
                 << " has no runtime in this environment; the oracle compiles but cannot measure";
#elif !defined(CNA_WIREFRAME_RENDERS_EDGES)
    // The rejecting renderer gets its own arm below; asserting the positive contract here would
    // only duplicate that arm's failure mode with a worse diagnostic.
    GTEST_SKIP() << kRendererName
                 << " does not rasterize wireframe; see the deterministic-rejection arm";
#else
    GraphicsDevice gd;
    const Result solid = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid", solid);
    const Result wire = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe", wire);

    ExpectSolidTriangle(solid);
    ASSERT_TRUE(wire.rendered)
        << kRendererName << " refused a WireFrame draw: " << wire.rejection;

    // 1. THE INTERIOR IS EMPTY. This is what separates a wireframe from a solid fill.
    EXPECT_EQ(0, wire.frame.LitIn(kInterior))
        << kRendererName << " filled " << wire.frame.LitIn(kInterior)
        << " interior pixels under FillMode::WireFrame -- that is a solid fill, not a wireframe ("
        << wire.frame.Describe() << ')';

    // 2. EVERY EDGE IS PRESENT. One probe per edge, each disjoint from the others, so a single
    //    dropped edge cannot hide behind the surviving two.
    for (std::size_t i = 0; i < kEdgeProbes.size(); ++i)
    {
        EXPECT_GE(wire.frame.LitIn(kEdgeProbes[i]), 8)
            << kRendererName << " edge " << kEdgeNames[i]
            << " is missing from the wireframe (" << wire.frame.Describe() << ')';
        EXPECT_TRUE(Frame::NearInk(wire.frame.FirstLitIn(kEdgeProbes[i])))
            << kRendererName << " edge " << kEdgeNames[i] << " is "
            << Describe(wire.frame.FirstLitIn(kEdgeProbes[i])) << ", not the ink colour";
    }

    // 3. THE FRAME IS NOT THE CLEAR. A dropped draw lights nothing at all, which the edge probes
    //    already reject; this states the whole-frame form of it so the failure names the cause.
    EXPECT_GT(wire.frame.LitTotal(), 0)
        << kRendererName << " rendered nothing at all under FillMode::WireFrame";

    // 4. THE TWO MODES DIFFER BY AN ORDER OF MAGNITUDE. Edges of this triangle are ~600 pixels;
    //    its interior is 18176. A renderer that quietly promoted the wireframe to a solid fill --
    //    or that widened lines until they became one -- cannot satisfy this.
    EXPECT_LT(wire.frame.LitTotal() * 4, solid.frame.LitTotal())
        << kRendererName << " WireFrame covered " << wire.frame.LitTotal()
        << " pixels against Solid's " << solid.frame.LitTotal()
        << " -- not a measurably smaller figure";

    // 5. NOTHING BUT INK AND CLEAR. A second draw, a retry, or a blend over the first would leave
    //    a third colour somewhere in the frame.
    EXPECT_TRUE(wire.frame.EveryLitPixelIsInk())
        << kRendererName << " WireFrame produced a lit pixel that is neither ink nor clear";
#endif
}

TEST(GraphicsDeviceCapabilityTest, WireFrameAndSolidAlternateWithoutStaleRasterizerState)
{
#if !defined(CNA_WIREFRAME_MEASURED) || !defined(CNA_WIREFRAME_RENDERS_EDGES)
    GTEST_SKIP() << kRendererName << " is not in the measured wireframe-rendering set";
#else
    GraphicsDevice gd;
    // WireFrame -> Solid -> WireFrame. A renderer that caches a pipeline, a polygon mode or an
    // expanded index buffer across state changes produces a different third frame; a renderer that
    // never applied the state in the first place produces three identical solid ones.
    const Result first = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe-1", first);
    const Result solid = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid-2", solid);
    const Result third = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe-3", third);

    ASSERT_TRUE(first.rendered);
    ASSERT_TRUE(third.rendered);
    ExpectSolidTriangle(solid);

    EXPECT_EQ(0, first.frame.LitIn(kInterior)) << kRendererName << ' ' << first.frame.Describe();
    EXPECT_EQ(0, third.frame.LitIn(kInterior))
        << kRendererName << " kept the Solid state after returning to WireFrame -- "
        << third.frame.Describe();
    EXPECT_TRUE(first.frame.pixels == third.frame.pixels)
        << kRendererName << " did not reproduce its own wireframe after a Solid draw ("
        << first.frame.Describe() << " then " << third.frame.Describe() << ')';
    EXPECT_FALSE(first.frame.pixels == solid.frame.pixels)
        << kRendererName << " produced identical frames for WireFrame and Solid";
#endif
}

// ---------------------------------------------------------------------------
// RECOVERY -- applies to every measured renderer, including the one that ignores the request.
// A WireFrame draw must never poison the device, the target or the following frame.
// ---------------------------------------------------------------------------
TEST(GraphicsDeviceCapabilityTest, SolidRendersExactlyAfterAWireFrameDraw)
{
#ifndef CNA_WIREFRAME_MEASURED
    GTEST_SKIP() << kRendererName << " has no runtime in this environment";
#else
    GraphicsDevice gd;
    const Result wire = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe-before-recovery", wire);
#ifdef CNA_WIREFRAME_REJECTED
    // A refusal is this renderer's correct answer (WEBGPU-115) -- what must still hold is that it
    // left nothing behind for the next draw to trip over.
    ASSERT_FALSE(wire.rendered)
        << kRendererName << " accepted a WireFrame draw again -- " << wire.frame.Describe();
    ExpectClearOnly(wire.frame, "the refused WireFrame draw");
#else
    ASSERT_TRUE(wire.rendered)
        << kRendererName << " refused a WireFrame draw: " << wire.rejection;
#endif

    const Result recovered = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid-recovery", recovered);
    ExpectSolidTriangle(recovered);
#endif
}

// ---------------------------------------------------------------------------
// NEGATIVE CONTRACT -- the renderer reports the gap and refuses the draw rather than substituting a
// different rendering mode. WEBGPU-115.
// ---------------------------------------------------------------------------
TEST(GraphicsDeviceCapabilityTest, WireFrameIsRefusedDeterministicallyOnThisRenderer)
{
#ifndef CNA_WIREFRAME_REJECTED
    GTEST_SKIP() << kRendererName << " is not in the WireFrame-rejecting set";
#else
    // The whole point of the boundary is that the two frames are NOT the same picture and NOT both
    // produced: Solid renders exactly, WireFrame throws, and the target the refused draw was aimed
    // at still holds nothing but the clear colour.
    GraphicsDevice gd;
    const Result solid = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid", solid);
    const Result wire = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe", wire);

    ExpectSolidTriangle(solid);
    ASSERT_FALSE(wire.rendered)
        << kRendererName << " accepted a WireFrame draw and produced " << wire.frame.Describe()
        << ". If real wireframe rendering landed, move this renderer into "
           "CNA_WIREFRAME_RENDERS_EDGES";
    // The message has to name the thing that was refused -- a bare "not supported" cannot be acted
    // on, and a message that names something else means a different guard fired.
    EXPECT_NE(std::string::npos, wire.rejection.find("WireFrame"))
        << kRendererName << " refused the draw with a message that does not name FillMode::"
        << "WireFrame: \"" << wire.rejection << '"';
    EXPECT_NE(std::string::npos, wire.rejection.find("SupportsCapability"))
        << kRendererName << " refused the draw without pointing at the capability query: \""
        << wire.rejection << '"';
    ExpectClearOnly(wire.frame, "the refused WireFrame draw");
    EXPECT_FALSE(wire.frame.pixels == solid.frame.pixels)
        << kRendererName << " produced the Solid picture for a refused WireFrame draw";

    // The device survives the refusal: a second Solid draw renders exactly, on a new target.
    const Result again = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid-after-refusal", again);
    ExpectSolidTriangle(again);
    EXPECT_TRUE(again.frame.pixels == solid.frame.pixels)
        << kRendererName << " did not reproduce its own Solid frame after a refused WireFrame draw";
#endif
}

#endif  // CNA_WIREFRAME_PIXEL_ORACLE

// ---------------------------------------------------------------------------
// HONEST SKIP + EXACT CARDINALITY -- Headless has no pixel route at all, and is the one renderer
// that can count native draws exactly.
// ---------------------------------------------------------------------------
#ifdef CNA_RENDERER_HEADLESS

TEST(GraphicsDeviceCapabilityTest, WireFrameHasNoPixelRouteOnThisRenderer)
{
    GTEST_SKIP() << "Headless rasterizes nothing by design -- DrawPrimitivesEx validates its "
                    "arguments and records a trace -- so there is no wireframe to measure. "
                    "Skipped because the route genuinely does not exist here, not to hide a "
                    "wrong result; the cardinality contract below is asserted instead";
}

TEST(GraphicsDeviceCapabilityTest, WireFrameReachesTheRendererAsExactlyOneDraw)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::BufferUsage;
    using Microsoft::Xna::Framework::Graphics::CullMode;
    using Microsoft::Xna::Framework::Graphics::FillMode;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::VertexBuffer;
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    GraphicsDevice gd;
    auto* renderer = dynamic_cast<CNA::Internal::Renderers::Headless::HeadlessRenderer*>(
        &gd.GetRenderer());
    ASSERT_NE(nullptr, renderer);

    const VertexDeclaration decl(
        16,
        {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
         VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
    const std::array<VertexPositionColor, 3> verts{
        VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color::White),
        VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color::White),
        VertexPositionColor(Vector3(0.0f, 0.5f, 0.0f), Color::White)};
    VertexBuffer vb(gd, decl, 3, BufferUsage::None);
    vb.SetData(verts.data(), 3);

    RasterizerState wireframe;
    wireframe.setCullModeProperty(CullMode::None);
    wireframe.setFillModeProperty(FillMode::WireFrame);
    gd.setRasterizerStateProperty(wireframe);

    BasicEffect effect(gd);
    effect.VertexColorEnabled = true;
    effect.Apply();
    gd.SetVertexBuffer(&vb);

    const std::uint64_t rasterBefore = renderer->GetStatistics().rasterizerStateChangeCount;
    const std::uint64_t drawsBefore = renderer->GetStatistics().drawCallCount;
    gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    gd.SetVertexBuffer(nullptr);

    // One public draw, one recorded draw -- no retry, no split, no second pass to emulate edges.
    EXPECT_EQ(drawsBefore + 1, renderer->GetStatistics().drawCallCount)
        << "one public WireFrame DrawPrimitives must reach the renderer exactly once";
    // The rasterizer state reached the renderer at least once for this draw; Headless discards its
    // contents, which is why this file measures the state's ARRIVAL here and its EFFECT elsewhere.
    EXPECT_GE(renderer->GetStatistics().rasterizerStateChangeCount, rasterBefore);
    EXPECT_EQ(FillMode::WireFrame, gd.getRasterizerStateProperty().getFillModeProperty())
        << "the device silently replaced the requested FillMode";
}

#endif  // CNA_RENDERER_HEADLESS
