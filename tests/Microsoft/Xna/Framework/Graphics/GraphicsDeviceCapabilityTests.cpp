// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "WireFrameTriangleOracle.hpp"

#ifdef CNA_BACKEND_HEADLESS
#include "CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.hpp"
#endif

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using CNA::GraphicsCapability;

// This test target only ever builds against a fully 3D-capable backend (EasyGL by default on
// Linux) -- SDL_Renderer/DX3/Canvas each have their own dedicated
// *_graphics_capability_test.cpp example asserting the opposite (nothing supported).

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

TEST(GraphicsDeviceCapabilityTest, SupportsMultipleRenderTargets)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets));
}

TEST(GraphicsDeviceCapabilityTest, SupportsOcclusionQuery)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::OcclusionQuery));
}

TEST(GraphicsDeviceCapabilityTest, SupportsCustomEffects)
{
    GraphicsDevice gd;
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::CustomEffects));
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
// XNB dimensions before any backend-specific texture creation is attempted.
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
// REMED-GFX-209 -- the FillMode::WireFrame contract is per backend, not universal.
//
// WHAT WAS HERE BEFORE. A single test, `DoesNotSupportWireFrame`, asserting
//
//     EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::WireFrame));
//
// in a file that is compiled once per backend and gated on nothing. It encoded ONE backend's
// documented gap -- EasyGL's "GLES3 has no polygon mode" entry in plan_graphics.md's XNA 4.0
// coverage table -- as though every backend shared it, and therefore failed on Software, Headless,
// bgfx, WebGPU, Vulkan and SDL_GPU. It could never pass falsely, so it hid nothing; it was simply
// a standing red in six principal suites.
//
// THE MEASURED CONTRACT, one backend at a time. Every row below is a real reading from the pixel
// oracle in this file, not a reading of the source:
//
//   Software, Vulkan, bgfx, SDL_GPU, D3D9, D3D11  reports true, renders a real wireframe
//   EasyGL                                        reports FALSE, renders a real wireframe anyway
//   WebGPU                                        reports true, renders SOLID geometry
//   Headless                                      reports true, rasterizes nothing at all
//   D3D12                                         maps FILL_WIREFRAME through the shared
//                                                 D3DCommon table; no runtime in this environment
//
// So there is no universal answer in either direction, and -- decisively -- there is no backend
// that REJECTS a WireFrame request. A "deterministic rejection" arm would have an empty
// registration set here, and manufacturing one would need a production change this task must not
// make. What replaces the old assertion is therefore three honest shapes:
//
//   * a POSITIVE PIXEL ORACLE where wireframe genuinely renders;
//   * a DOCUMENTED-DEVIATION oracle where it silently renders solid, asserting that the deviation
//     is real and visible, so the arm fails the moment the deviation is fixed;
//   * an HONEST SKIP where the backend has no pixel route to measure at all.
//
// EASYGL'S REPORT CONTRADICTS ITS OWN RENDERING. `EasyGLGraphicsBackend::SupportsCapability`
// returns false for WireFrame, but `ApplyRasterizerState` sets `wireframe_` and every triangle
// draw re-expands into GL_LINES through `DrawWireframe` -- and the oracle below measures a
// correct wireframe there. The capability report is the stale side, not the renderer. That is a
// production defect, it is outside this task's scope, and it is recorded as REMED-GFX-219; the
// arm below asserts the CURRENT measured value so the baseline is truthful, and fails the moment
// REMED-GFX-219 lands.
// ============================================================================

// The oracle itself -- geometry, probes, colours, the Solid control and the single-draw renderer
// -- lives in WireFrameTriangleOracle.hpp so WEBGPU-115's rejection suite measures the identical
// fixture instead of a copy that can drift from this one. The CNA_WIREFRAME_* selection macros are
// defined there too, for the same reason.
#ifdef CNA_WIREFRAME_PIXEL_ORACLE
using namespace CnaTest::WireFrameOracle;   // NOLINT(google-build-using-namespace)
#endif

// ---------------------------------------------------------------------------
// The capability report, per backend. Each arm states what THIS backend answers today; none of
// them claims anything about another.
// ---------------------------------------------------------------------------
TEST(GraphicsDeviceCapabilityTest, WireFrameCapabilityReportIsThisBackendsOwn)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::WireFrame); });
    const bool reported = gd.SupportsCapability(GraphicsCapability::WireFrame);

#if defined(CNA_BACKEND_EASYGL)
    // EasyGL is the only backend that answers false, and it is the ONE backend whose answer is
    // known to be wrong: the pixel oracle below measures a correct wireframe here. Asserting the
    // current value keeps the baseline truthful and makes this arm fail -- deliberately -- the day
    // REMED-GFX-219 corrects the report.
    EXPECT_FALSE(reported)
        << "EasyGL now reports WireFrame support. If REMED-GFX-219 landed, move this arm to the "
           "true side and delete the contradiction note above";
#else
    // Every other backend in this file answers true, either because it renders a real wireframe
    // (Software, Vulkan, bgfx, SDL_GPU, D3D9, D3D11, D3D12) or because it inherits
    // IGraphicsBackend's default (WebGPU, whose renderer ignores the request -- WEBGPU-115 -- and
    // Headless, which rasterizes nothing at all).
    EXPECT_TRUE(reported);
#endif
}

#ifdef CNA_WIREFRAME_PIXEL_ORACLE

// ---------------------------------------------------------------------------
// POSITIVE CONTRACT -- backends that genuinely rasterize a wireframe.
// ---------------------------------------------------------------------------

TEST(GraphicsDeviceCapabilityTest, WireFrameLightsEveryEdgeAndLeavesTheInteriorUnfilled)
{
#ifndef CNA_WIREFRAME_MEASURED
    GTEST_SKIP() << kBackendName
                 << " has no runtime in this environment; the oracle compiles but cannot measure";
#else
    GraphicsDevice gd;
    const Result solid = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid", solid);
    const Result wire = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe", wire);

    ExpectSolidTriangle(solid);
    ASSERT_TRUE(wire.rendered)
        << kBackendName << " refused a WireFrame draw: " << wire.rejection;

#ifdef CNA_WIREFRAME_RENDERS_EDGES
    // 1. THE INTERIOR IS EMPTY. This is what separates a wireframe from a solid fill, and it is
    //    the assertion WebGPU's silent solid fill fails.
    EXPECT_EQ(0, wire.frame.LitIn(kInterior))
        << kBackendName << " filled " << wire.frame.LitIn(kInterior)
        << " interior pixels under FillMode::WireFrame -- that is a solid fill, not a wireframe ("
        << wire.frame.Describe() << ')';

    // 2. EVERY EDGE IS PRESENT. One probe per edge, each disjoint from the others, so a single
    //    dropped edge cannot hide behind the surviving two.
    for (std::size_t i = 0; i < kEdgeProbes.size(); ++i)
    {
        EXPECT_GE(wire.frame.LitIn(kEdgeProbes[i]), 8)
            << kBackendName << " edge " << kEdgeNames[i]
            << " is missing from the wireframe (" << wire.frame.Describe() << ')';
        EXPECT_TRUE(Frame::NearInk(wire.frame.FirstLitIn(kEdgeProbes[i])))
            << kBackendName << " edge " << kEdgeNames[i] << " is "
            << Describe(wire.frame.FirstLitIn(kEdgeProbes[i])) << ", not the ink colour";
    }

    // 3. THE FRAME IS NOT THE CLEAR. A dropped draw lights nothing at all, which the edge probes
    //    already reject; this states the whole-frame form of it so the failure names the cause.
    EXPECT_GT(wire.frame.LitTotal(), 0)
        << kBackendName << " rendered nothing at all under FillMode::WireFrame";

    // 4. THE TWO MODES DIFFER BY AN ORDER OF MAGNITUDE. Edges of this triangle are ~600 pixels;
    //    its interior is 18176. A backend that quietly promoted the wireframe to a solid fill --
    //    or that widened lines until they became one -- cannot satisfy this.
    EXPECT_LT(wire.frame.LitTotal() * 4, solid.frame.LitTotal())
        << kBackendName << " WireFrame covered " << wire.frame.LitTotal()
        << " pixels against Solid's " << solid.frame.LitTotal()
        << " -- not a measurably smaller figure";

    // 5. NOTHING BUT INK AND CLEAR. A second draw, a retry, or a blend over the first would leave
    //    a third colour somewhere in the frame.
    EXPECT_TRUE(wire.frame.EveryLitPixelIsInk())
        << kBackendName << " WireFrame produced a lit pixel that is neither ink nor clear";
#else
    // WebGPU: the documented deviation gets its own arm below, and asserting the positive contract
    // here would only duplicate its failure.
    GTEST_SKIP() << kBackendName
                 << " does not rasterize wireframe; see the documented-deviation arm";
#endif
#endif
}

TEST(GraphicsDeviceCapabilityTest, WireFrameAndSolidAlternateWithoutStaleRasterizerState)
{
#if !defined(CNA_WIREFRAME_MEASURED) || !defined(CNA_WIREFRAME_RENDERS_EDGES)
    GTEST_SKIP() << kBackendName << " is not in the measured wireframe-rendering set";
#else
    GraphicsDevice gd;
    // WireFrame -> Solid -> WireFrame. A backend that caches a pipeline, a polygon mode or an
    // expanded index buffer across state changes produces a different third frame; a backend that
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

    EXPECT_EQ(0, first.frame.LitIn(kInterior)) << kBackendName << ' ' << first.frame.Describe();
    EXPECT_EQ(0, third.frame.LitIn(kInterior))
        << kBackendName << " kept the Solid state after returning to WireFrame -- "
        << third.frame.Describe();
    EXPECT_TRUE(first.frame.pixels == third.frame.pixels)
        << kBackendName << " did not reproduce its own wireframe after a Solid draw ("
        << first.frame.Describe() << " then " << third.frame.Describe() << ')';
    EXPECT_FALSE(first.frame.pixels == solid.frame.pixels)
        << kBackendName << " produced identical frames for WireFrame and Solid";
#endif
}

// ---------------------------------------------------------------------------
// RECOVERY -- applies to every measured backend, including the one that ignores the request.
// A WireFrame draw must never poison the device, the target or the following frame.
// ---------------------------------------------------------------------------
TEST(GraphicsDeviceCapabilityTest, SolidRendersExactlyAfterAWireFrameDraw)
{
#ifndef CNA_WIREFRAME_MEASURED
    GTEST_SKIP() << kBackendName << " has no runtime in this environment";
#else
    GraphicsDevice gd;
    const Result wire = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe-before-recovery", wire);
    ASSERT_TRUE(wire.rendered)
        << kBackendName << " refused a WireFrame draw: " << wire.rejection;

    const Result recovered = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid-recovery", recovered);
    ExpectSolidTriangle(recovered);
#endif
}

// ---------------------------------------------------------------------------
// DOCUMENTED DEVIATION -- WebGPU accepts FillMode::WireFrame and renders solid geometry.
// ---------------------------------------------------------------------------
TEST(GraphicsDeviceCapabilityTest, WireFrameSilentlyRendersSolidGeometryOnThisBackend)
{
#if !defined(CNA_WIREFRAME_MEASURED) || defined(CNA_WIREFRAME_RENDERS_EDGES)
    GTEST_SKIP() << kBackendName << " is not the silently-solid backend";
#else
    // WEBGPU-115: wgpu-native has no polygon-mode API, so `ApplyRasterizerState` stores
    // `fillModeWireframe_` and no pipeline ever reads it. This is an accepted, already recorded
    // deviation -- it is NOT a new finding. What must not happen is that a test quietly treats
    // solid output as a satisfied wireframe request, so the deviation is asserted here at full
    // strength: the WireFrame frame must be BYTE-IDENTICAL to the Solid one. The day wgpu-native
    // grows a polygon mode, this arm fails and the record has to be corrected.
    GraphicsDevice gd;
    const Result solid = RenderTriangle(gd, FillMode::Solid);
    PrintReading("solid", solid);
    const Result wire = RenderTriangle(gd, FillMode::WireFrame);
    PrintReading("wireframe", wire);

    ExpectSolidTriangle(solid);
    ASSERT_TRUE(wire.rendered)
        << kBackendName << " refused a WireFrame draw: " << wire.rejection;
    EXPECT_EQ(kInteriorArea, wire.frame.LitIn(kInterior))
        << kBackendName << " no longer fills the interior under FillMode::WireFrame -- "
        << wire.frame.Describe();
    EXPECT_TRUE(wire.frame.pixels == solid.frame.pixels)
        << kBackendName
        << " WireFrame output now differs from Solid. If WEBGPU-115 was implemented, move this "
           "backend into CNA_WIREFRAME_RENDERS_EDGES -- " << wire.frame.Describe() << " vs "
        << solid.frame.Describe();
#endif
}

#endif  // CNA_WIREFRAME_PIXEL_ORACLE

// ---------------------------------------------------------------------------
// HONEST SKIP + EXACT CARDINALITY -- Headless has no pixel route at all, and is the one backend
// that can count native draws exactly.
// ---------------------------------------------------------------------------
#ifdef CNA_BACKEND_HEADLESS

TEST(GraphicsDeviceCapabilityTest, WireFrameHasNoPixelRouteOnThisBackend)
{
    GTEST_SKIP() << "Headless rasterizes nothing by design -- DrawPrimitivesEx validates its "
                    "arguments and records a trace -- so there is no wireframe to measure. "
                    "Skipped because the route genuinely does not exist here, not to hide a "
                    "wrong result; the cardinality contract below is asserted instead";
}

TEST(GraphicsDeviceCapabilityTest, WireFrameReachesTheBackendAsExactlyOneDraw)
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
    auto* backend = dynamic_cast<CNA::Internal::Backends::Headless::HeadlessGraphicsBackend*>(
        &gd.GetBackend());
    ASSERT_NE(nullptr, backend);

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

    const std::uint64_t rasterBefore = backend->GetStatistics().rasterizerStateChangeCount;
    const std::uint64_t drawsBefore = backend->GetStatistics().drawCallCount;
    gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
    gd.SetVertexBuffer(nullptr);

    // One public draw, one recorded draw -- no retry, no split, no second pass to emulate edges.
    EXPECT_EQ(drawsBefore + 1, backend->GetStatistics().drawCallCount)
        << "one public WireFrame DrawPrimitives must reach the backend exactly once";
    // The rasterizer state reached the backend at least once for this draw; Headless discards its
    // contents, which is why this file measures the state's ARRIVAL here and its EFFECT elsewhere.
    EXPECT_GE(backend->GetStatistics().rasterizerStateChangeCount, rasterBefore);
    EXPECT_EQ(FillMode::WireFrame, gd.getRasterizerStateProperty().getFillModeProperty())
        << "the device silently replaced the requested FillMode";
}

#endif  // CNA_BACKEND_HEADLESS
