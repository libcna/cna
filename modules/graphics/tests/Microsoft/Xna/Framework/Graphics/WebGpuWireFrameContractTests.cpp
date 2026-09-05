// SPDX-License-Identifier: MS-PL

// ============================================================================
// WebGPU's FillMode::WireFrame contract, measured end to end.
//
// THE HISTORY, because this file has now recorded three different answers and each one was true
// when it was written:
//
//   * Before WEBGPU-115 the renderer reported the capability true, accepted the request, built and
//     natively submitted a distinct pipeline for it, and returned SOLID geometry. The two frames
//     were byte-identical (total=18176 interior=1089/1089 under both fill modes). That is an
//     affirmative false claim through a public capability query, which is the one shape such a
//     query exists to prevent.
//   * WEBGPU-115 replaced it with a deterministic refusal, on the stated grounds that "wgpu-native
//     exposes no polygon mode, so a wireframe request cannot reach any native pipeline state".
//   * plans/plan_webgpu.md WEBGPU-153 disproved the premise rather than the refusal. A wireframe
//     never needed a polygon mode: the reference renderer has always produced one by expanding
//     each triangle's three edges into a 32-bit line-list index buffer, and that route works
//     natively AND in the browser, where WebGPU genuinely has no polygon mode at all. (The pinned
//     wgpu-native does in fact carry `WGPUPrimitiveStateExtras::polygonMode` behind the
//     native-only `WGPUNativeFeature_PolygonModeLine`, so even the API claim was wrong -- but it
//     is not what this renderer uses, precisely because it is native-only.)
//
// THE CONTRACT THIS FILE NOW MEASURES:
//
//     SupportsCapability(WireFrame) == true         backed by pixels, not inherited
//     a WireFrame polygon draw                      accepted, no exception
//     queued draw commands                          +1        (one line-list command)
//     pipeline caches                               +1        (topology is part of the key)
//     native draw issues                            +1        (no extra pass, retry or frame)
//     uncaptured native errors                      +0
//     the target                                    interior EMPTY, all three edges present
//     the same frame under Solid                    interior FULL -- a different picture
//     the next Solid draw                           renders exactly
//
// The expansion happens at QUEUE time, so what the replay receives is an ordinary 32-bit indexed
// line-list command and no Issue* path has a wireframe branch at all.
//
// The geometry is REMED-GFX-209's asymmetric triangle, shared through WireFrameTriangleOracle.hpp
// rather than copied, so these readings are directly comparable with the per-renderer contract
// suite's own. What this file adds over that shared suite is the NATIVE cardinality: how many
// commands, pipelines, passes, submits and validation errors the wireframe request actually costs.
// ============================================================================

// plans/plan_runtimerenderer.md RTR-P9-9: a compile-time guard, because this suite needs the WebGPU
// renderer's own headers. Widened from the DEFAULT-renderer macro to "compiled into this build",
// so a multi-renderer build holding WebGPU without selecting it still compiles these tests; each
// test then checks at runtime that WebGPU is the ACTIVE renderer.
#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "WireFrameTriangleOracle.hpp"

namespace
{
    using namespace CnaTest::WireFrameOracle;   // NOLINT(google-build-using-namespace)
    using CNA::GraphicsCapability;
    using CNA::Internal::Renderers::WebGPU::WebGPURenderer;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    /// Every cardinality this ticket has to account for, read from the renderer's own CNAEXT
    /// diagnostics. All seven are cumulative since device creation, so a difference between two
    /// readings is exactly what happened in between -- nothing here is reset by a flush.
    struct Counters
    {
        std::size_t queuedCommands = 0;
        std::size_t nativeDraws = 0;
        std::size_t coloredPipelines = 0;
        std::size_t instancedPipelines = 0;
        std::size_t renderPasses = 0;
        std::size_t queueSubmits = 0;
        std::size_t uncapturedErrors = 0;

        static Counters Read(const WebGPURenderer& renderer)
        {
            Counters c;
            c.queuedCommands = renderer.GetQueuedDrawCommandCountEXT();
            c.nativeDraws = renderer.GetNativeDrawIssueCountEXT();
            c.coloredPipelines = renderer.GetColoredPipelineCacheSizeEXT();
            c.instancedPipelines = renderer.GetInstancedPipelineCacheSizeEXT();
            c.renderPasses = renderer.GetRenderPassCountEXT();
            c.queueSubmits = renderer.GetQueueSubmitCountEXT();
            c.uncapturedErrors = renderer.GetUncapturedErrorCountEXT();
            return c;
        }

        [[nodiscard]] std::string Describe() const
        {
            std::ostringstream os;
            os << "cmd=" << queuedCommands << " nativeDraw=" << nativeDraws
               << " coloredPipe=" << coloredPipelines << " instPipe=" << instancedPipelines
               << " pass=" << renderPasses << " submit=" << queueSubmits
               << " uncaptured=" << uncapturedErrors;
            return os.str();
        }
    };

    WebGPURenderer& RendererOf(GraphicsDevice& device)
    {
        auto* renderer = dynamic_cast<WebGPURenderer*>(&device.GetRenderer());
        // This whole file is gated on CNA_RENDERER_WEBGPU, so the cast cannot legitimately fail.
        if (renderer == nullptr)
            throw std::runtime_error("CNA_RENDERER_WEBGPU build has no WebGPURenderer");
        return *renderer;
    }

    /// One route, measured at the three points that matter: before the public draw call, straight
    /// after it (so "was a command queued?" is separable from "was anything submitted?"), and after
    /// the target has been unbound and read back (so the flush is included).
    struct RouteRun
    {
        Counters before;
        Counters afterDraw;
        Counters afterFlush;
        /// Pre-filled with the clear colour so a run that never reached a readback still
        /// describes a legal frame instead of indexing an empty buffer.
        Frame frame = ClearFrame();
        bool accepted = false;
        std::string rejection;
        /// Growth of the cache belonging to THIS route's own draw family, which for most routes is
        /// the Colored3D one but is the Instanced3D one for the instanced route.
        std::size_t familyPipelinesBuilt = 0;

        [[nodiscard]] std::size_t QueuedByDraw() const
        {
            return afterDraw.queuedCommands - before.queuedCommands;
        }
        [[nodiscard]] std::size_t ColoredPipelinesBuilt() const
        {
            return afterFlush.coloredPipelines - before.coloredPipelines;
        }
        [[nodiscard]] std::size_t NativeDraws() const
        {
            return afterFlush.nativeDraws - before.nativeDraws;
        }
        [[nodiscard]] std::size_t Submits() const
        {
            return afterFlush.queueSubmits - before.queueSubmits;
        }
        [[nodiscard]] std::size_t Passes() const
        {
            return afterFlush.renderPasses - before.renderPasses;
        }
        [[nodiscard]] std::size_t UncapturedErrors() const
        {
            return afterFlush.uncapturedErrors - before.uncapturedErrors;
        }
    };

    /// Renders the shared triangle through the ordinary non-indexed route with @p fill, taking a
    /// counter reading at each of the three points above.
    RouteRun RunOrdinaryRoute(GraphicsDevice& device, FillMode fill)
    {
        RouteRun run;
        WebGPURenderer& renderer = RendererOf(device);
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        try
        {
            const std::array<VertexPositionColor, 3> verts = TriangleVertices();
            VertexBuffer vb(device, PositionColorDeclaration(),
                            static_cast<int>(verts.size()), BufferUsage::None);
            vb.SetData(verts.data(), static_cast<int>(verts.size()));

            ApplyFixtureState(device, fill);
            BasicEffect effect(device);
            ApplyFixtureEffect(effect);

            device.SetRenderTarget(&target);
            device.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            device.Clear(Color(kClear[0], kClear[1], kClear[2], kClear[3]));
            effect.Apply();
            device.SetVertexBuffer(&vb);

            // The window opens here, one statement before the public draw: everything above is
            // resource setup, and the deferred Clear queues no draw command of its own.
            run.before = Counters::Read(renderer);
            try
            {
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                run.accepted = true;
            }
            catch (const std::exception& e)
            {
                run.rejection = e.what();
            }
            run.afterDraw = Counters::Read(renderer);

            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(nullptr);
            ReadTarget(target, run.frame);
            run.afterFlush = Counters::Read(renderer);
        }
        catch (const std::exception& e)
        {
            // Setup itself failed -- reported as a refusal with no counter window at all, which
            // reads differently from a refused draw and cannot be mistaken for one.
            run.rejection = std::string("setup: ") + e.what();
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(nullptr);
        }
        return run;
    }

    /// A RouteRun as the shared oracle's own Result, so this file's counter-carrying runs can be
    /// judged by `ExpectSolidTriangle` rather than by a second copy of its expectations.
    [[nodiscard]] Result AsOracleResult(const RouteRun& run)
    {
        Result result;
        result.frame = run.frame;
        result.rendered = run.accepted;
        result.rejection = run.rejection;
        return result;
    }

    /// What a wireframe frame must look like: the interior empty, every edge present, and nothing
    /// in the frame but ink and clear. Shared by every arm below so they cannot drift apart, and
    /// deliberately the same four measurements the cross-renderer suite makes through the oracle.
    void ExpectWireFrameFrame(const Frame& frame, const char* what)
    {
        // 1. THE INTERIOR IS EMPTY -- the one measurement that separates a wireframe from a fill.
        EXPECT_EQ(0, frame.LitIn(kInterior))
            << what << " filled " << frame.LitIn(kInterior)
            << " interior pixels: that is a solid fill, not a wireframe (" << frame.Describe() << ')';
        // 2. EVERY EDGE IS PRESENT. The probes are disjoint, so a dropped edge cannot hide behind
        //    the surviving two, and the failure names which one went missing.
        for (std::size_t i = 0; i < kEdgeProbes.size(); ++i)
        {
            EXPECT_GE(frame.LitIn(kEdgeProbes[i]), 8)
                << what << " is missing edge " << kEdgeNames[i] << " (" << frame.Describe() << ')';
            EXPECT_TRUE(Frame::NearInk(frame.FirstLitIn(kEdgeProbes[i])))
                << what << " edge " << kEdgeNames[i] << " is "
                << Describe(frame.FirstLitIn(kEdgeProbes[i])) << ", not the ink colour";
        }
        // 3. IT IS NOT A DROPPED DRAW.
        EXPECT_GT(frame.LitTotal(), 0) << what << " rendered nothing at all";
        // 4. NOTHING BUT INK AND CLEAR: a retry, a second draw or a blend leaves a third colour.
        EXPECT_TRUE(frame.EveryLitPixelIsInk())
            << what << " produced a lit pixel that is neither ink nor clear";
    }

    void PrintRun(const char* label, const RouteRun& run)
    {
        std::cout << "[WEBGPU-115] " << label << ": "
                  << (run.accepted ? "ACCEPTED" : "REJECTED -- \"" + run.rejection + '"')
                  << " | queuedByDraw=" << run.QueuedByDraw()
                  << " coloredPipelines+" << run.ColoredPipelinesBuilt()
                  << " nativeDraws=" << run.NativeDraws()
                  << " passes=" << run.Passes()
                  << " submits=" << run.Submits()
                  << " uncaptured=" << run.UncapturedErrors()
                  << " | " << run.frame.Describe() << std::endl;
    }
}   // namespace

// ---------------------------------------------------------------------------
// 1. THE CAPABILITY QUERY. The public contract for "can this renderer do X", and the reason the
//    silent substitution below is a false claim rather than an undocumented gap.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, CapabilityQueryAnswersForWireFrame)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::WireFrame); });
    const bool reported = gd.SupportsCapability(GraphicsCapability::WireFrame);
    std::cout << "[WEBGPU-153] SupportsCapability(WireFrame) == "
              << (reported ? "true" : "false") << std::endl;

    // WEBGPU-153: true, and backed by the pixels every arm below measures rather than by an
    // inherited default. The claim a capability query makes has to be one the renderer can keep.
    EXPECT_TRUE(reported)
        << "WebGPU under-reports WireFrame -- the edge-expansion implementation is still here "
           "while the capability says the renderer cannot do it";

    // The surrounding capability answers must not move with it. OcclusionQuery is true
    // (WEBGPU-84), MultipleRenderTargets too (WEBGPU-85/86/87), and MultiStreamVertexInput became
    // true in WEBGPU-172, which built one WGPUVertexBufferLayout per resolved stream on every stock
    // family, ordinary and instanced. It is asserted here rather than deleted because this check
    // exists to catch a capability moving as a SIDE EFFECT of a change to another one.
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::ThreeD));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::DepthStencilBuffer));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::OcclusionQuery));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::CustomEffects));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::Texture3D));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::MultiStreamVertexInput));

    // WEBGPU-172: and the width the capability is worth. GetMaxVertexStreams() is the device's own
    // maxVertexBuffers less the slot reserved for the neutral record, clamped to the resolver's
    // table -- never a constant, and never larger than what a draw can actually bind.
    const CNA::RendererLimitValue maxStreams =
        gd.GetRendererCapabilityProfileEXT().GetLimit(CNA::RendererLimit::MaxVertexStreams);
    std::cout << "[WEBGPU-172] MaxVertexStreams == " << maxStreams.value
              << (maxStreams.known ? "" : " (unknown)") << std::endl;
    EXPECT_TRUE(maxStreams.known);
    EXPECT_GE(maxStreams.value, 2u)
        << "a renderer that claims MultiStreamVertexInput must accept at least two streams";
    EXPECT_LE(maxStreams.value, 16u) << "XNA 4.0 HiDef binds at most 16 vertex streams";
}

// ---------------------------------------------------------------------------
// 2. THE FULL PATH, counted. Capability -> RasterizerState -> public draw -> queued command ->
//    pipeline key -> native pipeline -> render pass -> queue submit.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, WireFrameDrawQueuesExactlyOneLineListCommand)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    const RouteRun run = RunOrdinaryRoute(gd, FillMode::WireFrame);
    PrintRun("ordinary-nonindexed wireframe", run);

    ASSERT_TRUE(run.accepted)
        << "WebGPU refused a WireFrame draw: " << run.rejection;
    ExpectWireFrameFrame(run.frame, "the ordinary-nonindexed wireframe draw");

    // The cost is EXACTLY one ordinary draw. The expansion happens at queue time and rewrites the
    // command in place, so it must not cost a second command, a second pass or a second submit --
    // and a renderer that re-drew the triangle once per edge would fail here while still producing
    // a picture that looks right.
    EXPECT_EQ(1u, run.QueuedByDraw())
        << "a wireframe draw queued " << run.QueuedByDraw() << " commands -- "
        << run.afterDraw.Describe();
    EXPECT_EQ(1u, run.NativeDraws())
        << "a wireframe draw issued " << run.NativeDraws() << " native draws -- "
        << run.afterFlush.Describe();
    EXPECT_LE(run.Passes(), 1u) << run.afterFlush.Describe();
    EXPECT_LE(run.Submits(), 1u) << run.afterFlush.Describe();

    // One extra pipeline, not more: the topology is part of the cache key, so the line-list variant
    // is a sibling of the solid one rather than a new key per draw.
    EXPECT_EQ(1u, run.ColoredPipelinesBuilt())
        << "the wireframe draw built " << run.ColoredPipelinesBuilt()
        << " pipelines -- " << run.afterFlush.Describe();

    // A line topology takes no depth bias in WebGPU, so a pipeline that carried the caller's one
    // through would fail validation rather than render. This is where that would surface.
    EXPECT_EQ(0u, run.UncapturedErrors())
        << "the wireframe draw produced native validation errors -- " << run.afterFlush.Describe();
}

// ---------------------------------------------------------------------------
// 3. THE OUTPUT. The single measurement that makes this a silent wrong result rather than a
//    missing feature: WireFrame and Solid are the same picture, byte for byte.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, WireFrameAndSolidAreDifferentPicturesAndSolidIsUnaffected)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    const RouteRun solid = RunOrdinaryRoute(gd, FillMode::Solid);
    PrintRun("ordinary-nonindexed solid", solid);
    const RouteRun wire = RunOrdinaryRoute(gd, FillMode::WireFrame);
    PrintRun("ordinary-nonindexed wireframe", wire);
    const RouteRun recovered = RunOrdinaryRoute(gd, FillMode::Solid);
    PrintRun("ordinary-nonindexed solid-recovery", recovered);

    ASSERT_TRUE(solid.accepted) << "WebGPU refused an ordinary Solid draw: " << solid.rejection;
    ASSERT_TRUE(wire.accepted) << "WebGPU refused a WireFrame draw: " << wire.rejection;
    ASSERT_TRUE(recovered.accepted)
        << "WebGPU refused a Solid draw after a WireFrame one: " << recovered.rejection;

    ExpectSolidTriangle(AsOracleResult(solid));
    ExpectWireFrameFrame(wire.frame, "the wireframe draw");

    // THE MEASUREMENT THIS FILE EXISTS FOR. Before WEBGPU-115 these two frames were byte-identical
    // and the renderer claimed the capability anyway; they must now differ by an order of
    // magnitude. This triangle's edges are ~600 pixels against an interior of 18176.
    EXPECT_NE(solid.frame.pixels == wire.frame.pixels, true)
        << "WireFrame and Solid are the same picture again -- the silent substitution is back";
    EXPECT_LT(wire.frame.LitTotal() * 4, solid.frame.LitTotal())
        << "WireFrame covered " << wire.frame.LitTotal() << " pixels against Solid's "
        << solid.frame.LitTotal() << " -- not a measurably smaller figure";

    // And the wireframe draw leaves the solid one exactly as it was.
    EXPECT_TRUE(recovered.frame.pixels == solid.frame.pixels)
        << "Solid output changed across a WireFrame draw -- " << recovered.frame.Describe()
        << " vs " << solid.frame.Describe();
    EXPECT_EQ(1u, recovered.QueuedByDraw()) << recovered.afterDraw.Describe();
    EXPECT_EQ(1u, recovered.NativeDraws()) << recovered.afterFlush.Describe();
    EXPECT_EQ(1u, recovered.Submits()) << recovered.afterFlush.Describe();
    // The recovery reuses the pipeline the first Solid draw built: the wireframe draw added the
    // line-list variant beside it rather than replacing it.
    EXPECT_EQ(0u, recovered.ColoredPipelinesBuilt()) << recovered.afterFlush.Describe();
    EXPECT_EQ(0u, recovered.UncapturedErrors()) << recovered.afterFlush.Describe();
}

// ---------------------------------------------------------------------------
// 4. THE EXCEPTION TYPE. "Deterministic" means a caller can catch it by name and fall back, so the
//    type is part of the contract, not just the fact that something was thrown.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, WireFrameDrawThrowsNothingAndRepeatsIdenticallyOnOneTarget)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Color,
                          DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    const std::array<VertexPositionColor, 3> verts = TriangleVertices();
    VertexBuffer vb(gd, PositionColorDeclaration(), static_cast<int>(verts.size()),
                    BufferUsage::None);
    vb.SetData(verts.data(), static_cast<int>(verts.size()));

    ApplyFixtureState(gd, FillMode::WireFrame);
    BasicEffect effect(gd);
    ApplyFixtureEffect(effect);

    gd.SetRenderTarget(&target);
    gd.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
    gd.Clear(Color(kClear[0], kClear[1], kClear[2], kClear[3]));
    effect.Apply();
    gd.SetVertexBuffer(&vb);

    // WEBGPU-115 asserted System::NotSupportedException here. There is nothing left to throw: the
    // request is served, not refused.
    EXPECT_NO_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1));
    // Repeating it is idempotent -- the expansion holds no state of its own, so a second call
    // produces a second identical line-list command rather than accumulating anything.
    EXPECT_NO_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1));

    gd.SetVertexBuffer(nullptr);
    gd.SetRenderTarget(nullptr);

    Frame frame;
    ReadTarget(target, frame);
    std::cout << "[WEBGPU-153] two wireframe draws on one target: " << frame.Describe()
              << std::endl;
    // Two identical wireframe draws over each other are still the same wireframe: the interior
    // stays empty (a pair that had promoted to a fill would show it here) and the edges stay ink.
    ExpectWireFrameFrame(frame, "two stacked wireframe draws");

    // And selecting Solid on the same device, without recreating anything, still fills.
    gd.SetRenderTarget(&target);
    gd.Clear(Color(kClear[0], kClear[1], kClear[2], kClear[3]));
    ApplyFixtureState(gd, FillMode::Solid);
    effect.Apply();
    gd.SetVertexBuffer(&vb);
    EXPECT_NO_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1));
    gd.SetVertexBuffer(nullptr);
    gd.SetRenderTarget(nullptr);

    Frame solidFrame;
    ReadTarget(target, solidFrame);
    EXPECT_EQ(kInteriorArea, solidFrame.LitIn(kInterior))
        << "the Solid draw after two wireframe draws did not fill the interior -- "
        << solidFrame.Describe();
    EXPECT_TRUE(solidFrame.EveryLitPixelIsInk())
        << "a wireframe draw left a stray pixel behind -- " << solidFrame.Describe();
}

// ===========================================================================
// 5. THE ROUTE MATRIX.
//
// The guard sits at five entry points, not eleven, so the interesting question is not "does each
// Queue*Draw() check?" but "does every public route actually reach one of those five?". Each route
// below is run TWICE on the same device -- once with FillMode::Solid, once with WireFrame -- and
// only the pair is conclusive: the Solid leg proves the route is real and reaches the GPU, the
// WireFrame leg proves it is refused. A route that silently did nothing would fail the Solid leg,
// so "refused" can never be confused with "never ran".
// ===========================================================================
namespace
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Graphics::IndexBuffer;
    using Microsoft::Xna::Framework::Graphics::IndexElementSize;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

    /// One public draw route. `Setup` runs before the counter window opens, so nothing it creates
    /// or binds can be mistaken for work the measured draw did; `Draw` is the single public call
    /// the window brackets.
    struct Route
    {
        Route() = default;
        Route(const Route&) = delete;
        Route& operator=(const Route&) = delete;
        virtual ~Route() = default;
        [[nodiscard]] virtual const char* Name() const = 0;
        virtual void Setup(GraphicsDevice& device) = 0;
        virtual void Draw(GraphicsDevice& device) = 0;
        /// Which pipeline cache this route's family lands in, so the "+0 pipelines" assertion is
        /// made against the cache the accepted leg actually grows.
        [[nodiscard]] virtual std::size_t PipelineCacheSize(const WebGPURenderer& b) const
        {
            return b.GetColoredPipelineCacheSizeEXT();
        }
    };

    /// The shared triangle, optionally preceded by @p pad decoy vertices so a nonzero vertexStart /
    /// baseVertex has something to skip over.
    std::vector<VertexPositionColor> PaddedTriangle(int pad)
    {
        const std::array<VertexPositionColor, 3> tri = TriangleVertices();
        std::vector<VertexPositionColor> out(static_cast<std::size_t>(pad));
        out.insert(out.end(), tri.begin(), tri.end());
        return out;
    }

    /// A VertexBuffer holding @p pad decoys then the triangle.
    std::unique_ptr<VertexBuffer> MakeTriangleBuffer(GraphicsDevice& device, int pad,
                                                     std::vector<VertexPositionColor>& keep)
    {
        keep = PaddedTriangle(pad);
        auto vb = std::make_unique<VertexBuffer>(device, PositionColorDeclaration(),
                                                 static_cast<int>(keep.size()), BufferUsage::None);
        vb->SetData(keep.data(), static_cast<int>(keep.size()));
        return vb;
    }

    // ---- ordinary VertexBuffer routes -------------------------------------------------------
    struct OrdinaryNonIndexed : Route
    {
        int pad;
        std::vector<VertexPositionColor> verts;
        std::unique_ptr<VertexBuffer> vb;
        std::unique_ptr<BasicEffect> effect;
        explicit OrdinaryNonIndexed(int padVertices) : pad(padVertices) {}
        [[nodiscard]] const char* Name() const override
        {
            return pad == 0 ? "ordinary-nonindexed" : "ordinary-nonindexed vertexStart>0";
        }
        void Setup(GraphicsDevice& device) override
        {
            vb = MakeTriangleBuffer(device, pad, verts);
            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->Apply();
            device.SetVertexBuffer(vb.get());
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawPrimitives(PrimitiveType::TriangleList, pad, 1);
        }
    };

    struct OrdinaryIndexed : Route
    {
        bool wide;
        int baseVertex;
        int startIndex;
        std::vector<VertexPositionColor> verts;
        std::unique_ptr<VertexBuffer> vb;
        std::unique_ptr<IndexBuffer> ib;
        std::unique_ptr<BasicEffect> effect;
        std::string name;
        OrdinaryIndexed(bool thirtyTwoBit, int base, int start)
            : wide(thirtyTwoBit), baseVertex(base), startIndex(start)
        {
            name = std::string("ordinary-indexed-") + (wide ? "32" : "16");
            if (baseVertex != 0) name += " baseVertex>0";
            if (startIndex != 0) name += " startIndex>0";
        }
        [[nodiscard]] const char* Name() const override { return name.c_str(); }
        void Setup(GraphicsDevice& device) override
        {
            vb = MakeTriangleBuffer(device, baseVertex, verts);
            // `startIndex` decoy indices, then the triangle's own three.
            const int total = startIndex + 3;
            ib = std::make_unique<IndexBuffer>(
                device, wide ? IndexElementSize::ThirtyTwoBits : IndexElementSize::SixteenBits,
                total, BufferUsage::None);
            if (wide)
            {
                std::vector<std::uint32_t> idx(static_cast<std::size_t>(total), 0u);
                for (int i = 0; i < 3; ++i)
                    idx[static_cast<std::size_t>(startIndex + i)] = static_cast<std::uint32_t>(i);
                ib->SetData(idx.data(), total);
            }
            else
            {
                std::vector<std::uint16_t> idx(static_cast<std::size_t>(total), 0u);
                for (int i = 0; i < 3; ++i)
                    idx[static_cast<std::size_t>(startIndex + i)] = static_cast<std::uint16_t>(i);
                ib->SetData(idx.data(), total);
            }
            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->Apply();
            device.SetVertexBuffer(vb.get());
            device.SetIndexBuffer(ib.get());
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, baseVertex, 0, 3,
                                         startIndex, 1);
        }
    };

    // ---- DrawUser* routes ---------------------------------------------------------------------
    struct UserTypedNonIndexed : Route
    {
        std::vector<VertexPositionColor> verts;
        std::unique_ptr<BasicEffect> effect;
        [[nodiscard]] const char* Name() const override { return "user-nonindexed (typed)"; }
        void Setup(GraphicsDevice& device) override
        {
            verts = PaddedTriangle(0);
            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->Apply();
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 1,
                                      PositionColorDeclaration());
        }
    };

    struct UserTypedIndexed : Route
    {
        std::vector<VertexPositionColor> verts;
        std::array<std::uint16_t, 3> idx{0, 1, 2};
        std::unique_ptr<BasicEffect> effect;
        [[nodiscard]] const char* Name() const override { return "user-indexed (typed, 16-bit)"; }
        void Setup(GraphicsDevice& device) override
        {
            verts = PaddedTriangle(0);
            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->Apply();
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, verts.data(), 0, 3,
                                             idx.data(), 0, 1);
        }
    };

    /// The raw `const void*` overloads, which are the ONLY callers of the renderer's
    /// DrawColoredPrimitives / DrawIndexedColoredPrimitives entry points -- a different pair of
    /// guarded entry points from every route above.
    struct UserRawNonIndexed : Route
    {
        std::vector<VertexPositionColor> verts;
        std::unique_ptr<BasicEffect> effect;
        [[nodiscard]] const char* Name() const override { return "user-nonindexed (raw void*)"; }
        void Setup(GraphicsDevice& device) override
        {
            verts = PaddedTriangle(0);
            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->Apply();
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList,
                                      static_cast<const void*>(verts.data()), 0, 1);
        }
    };

    struct UserRawIndexed : Route
    {
        std::vector<VertexPositionColor> verts;
        std::array<std::uint16_t, 3> idx{0, 1, 2};
        std::unique_ptr<BasicEffect> effect;
        [[nodiscard]] const char* Name() const override { return "user-indexed (raw void*)"; }
        void Setup(GraphicsDevice& device) override
        {
            verts = PaddedTriangle(0);
            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->Apply();
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                             static_cast<const void*>(verts.data()), 0, 3,
                                             static_cast<const void*>(idx.data()), 0, 1);
        }
    };

    // ---- a second effect family ---------------------------------------------------------------
    /// BasicEffect with a texture on a stride-24 buffer, which dispatches to QueueTexturedDraw and
    /// its own pipeline cache instead of the Colored3D one every route above uses.
    struct TexturedFamily : Route
    {
        std::vector<VertexPositionColorTexture> verts;
        std::unique_ptr<VertexBuffer> vb;
        std::unique_ptr<Texture2D> texture;
        std::unique_ptr<BasicEffect> effect;
        [[nodiscard]] const char* Name() const override { return "textured effect family"; }
        [[nodiscard]] std::size_t PipelineCacheSize(const WebGPURenderer&) const override
        {
            // The textured family has no EXT accessor of its own; the Colored3D cache is asserted
            // instead, and must not grow either -- a refused draw creates no pipeline anywhere.
            return 0;
        }
        void Setup(GraphicsDevice& device) override
        {
            const std::array<VertexPositionColor, 3> tri = TriangleVertices();
            verts.clear();
            for (const VertexPositionColor& v : tri)
                verts.emplace_back(v.Position, v.Color, Vector2(0.5f, 0.5f));
            const VertexDeclaration decl(
                24,
                {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                 VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                 VertexElement(16, VertexElementFormat::Vector2,
                               VertexElementUsage::TextureCoordinate, 0)});
            vb = std::make_unique<VertexBuffer>(device, decl, 3, BufferUsage::None);
            vb->SetData(verts.data(), 3);

            texture = std::make_unique<Texture2D>(device, 1, 1);
            const Color white(255, 255, 255, 255);
            texture->SetData(&white, 1);

            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->setTextureEnabledProperty(true);
            effect->setTextureProperty(texture.get());
            effect->Apply();
            device.SetVertexBuffer(vb.get());
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        }
    };

    // ---- the instanced route ------------------------------------------------------------------
    struct InstancedRoute : Route
    {
        std::vector<VertexPositionColor> verts;
        std::array<std::uint16_t, 3> idx{0, 1, 2};
        std::unique_ptr<VertexBuffer> vb;
        std::unique_ptr<IndexBuffer> ib;
        std::unique_ptr<VertexBuffer> instances;
        std::unique_ptr<BasicEffect> effect;
        [[nodiscard]] const char* Name() const override { return "instanced"; }
        [[nodiscard]] std::size_t PipelineCacheSize(const WebGPURenderer& b) const override
        {
            return b.GetInstancedPipelineCacheSizeEXT();
        }
        void Setup(GraphicsDevice& device) override
        {
            verts = PaddedTriangle(0);
            vb = std::make_unique<VertexBuffer>(device, PositionColorDeclaration(), 3,
                                                BufferUsage::None);
            vb->SetData(verts.data(), 3);
            ib = std::make_unique<IndexBuffer>(device, IndexElementSize::SixteenBits, 3,
                                               BufferUsage::None);
            ib->SetData(idx.data(), 3);

            const VertexDeclaration instanceDecl(
                64,
                {VertexElement(0, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 1),
                 VertexElement(16, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 2),
                 VertexElement(32, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 3),
                 VertexElement(48, VertexElementFormat::Vector4,
                               VertexElementUsage::TextureCoordinate, 4)});
            instances = std::make_unique<VertexBuffer>(device, instanceDecl, 1, BufferUsage::None);
            const Matrix identity = Matrix::getIdentityProperty();
            instances->SetDataRaw(&identity, 1, static_cast<int>(sizeof(Matrix)));

            effect = std::make_unique<BasicEffect>(device);
            ApplyFixtureEffect(*effect);
            effect->Apply();
            device.SetVertexBuffers({VertexBufferBinding(vb.get(), 0, 0),
                                     VertexBufferBinding(instances.get(), 0, 1)});
            device.SetIndexBuffer(ib.get());
        }
        void Draw(GraphicsDevice& device) override
        {
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1);
        }
    };

    /// Runs @p route once under @p fill, bracketing exactly the public draw call.
    RouteRun RunRoute(GraphicsDevice& device, FillMode fill, Route& route)
    {
        RouteRun run;
        WebGPURenderer& renderer = RendererOf(device);
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        const std::size_t pipelinesBefore = route.PipelineCacheSize(renderer);
        std::size_t pipelinesAfter = pipelinesBefore;
        try
        {
            ApplyFixtureState(device, fill);
            device.SetRenderTarget(&target);
            device.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            device.Clear(Color(kClear[0], kClear[1], kClear[2], kClear[3]));
            route.Setup(device);

            run.before = Counters::Read(renderer);
            try
            {
                route.Draw(device);
                run.accepted = true;
            }
            catch (const std::exception& e)
            {
                run.rejection = e.what();
            }
            run.afterDraw = Counters::Read(renderer);

            device.SetVertexBuffers({});
            device.SetIndexBuffer(nullptr);
            device.SetRenderTarget(nullptr);
            ReadTarget(target, run.frame);
            run.afterFlush = Counters::Read(renderer);
            pipelinesAfter = route.PipelineCacheSize(renderer);
        }
        catch (const std::exception& e)
        {
            run.rejection = std::string("setup: ") + e.what();
            device.SetVertexBuffers({});
            device.SetIndexBuffer(nullptr);
            device.SetRenderTarget(nullptr);
        }
        run.familyPipelinesBuilt = pipelinesAfter - pipelinesBefore;
        return run;
    }
}   // namespace

TEST(WebGpuWireFrameContract, EveryPublicDrawRouteWireframesAndAcceptsSolid)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    std::vector<std::unique_ptr<Route>> routes;
    routes.emplace_back(std::make_unique<OrdinaryNonIndexed>(0));
    routes.emplace_back(std::make_unique<OrdinaryNonIndexed>(3));           // vertexStart > 0
    routes.emplace_back(std::make_unique<OrdinaryIndexed>(false, 0, 0));    // 16-bit
    routes.emplace_back(std::make_unique<OrdinaryIndexed>(true, 0, 0));     // 32-bit
    routes.emplace_back(std::make_unique<OrdinaryIndexed>(false, 3, 0));    // baseVertex > 0
    routes.emplace_back(std::make_unique<OrdinaryIndexed>(false, 0, 3));    // startIndex > 0
    routes.emplace_back(std::make_unique<UserTypedNonIndexed>());
    routes.emplace_back(std::make_unique<UserTypedIndexed>());
    routes.emplace_back(std::make_unique<UserRawNonIndexed>());
    routes.emplace_back(std::make_unique<UserRawIndexed>());
    routes.emplace_back(std::make_unique<TexturedFamily>());
    routes.emplace_back(std::make_unique<InstancedRoute>());

    // The route list is the point of this test, not a detail of it: the expansion is applied per
    // draw FAMILY, so a family that was missed would silently fill while its neighbours wireframe.
    // vertexStart, baseVertex and startIndex each appear because each one changes how the source
    // index sequence is addressed, which is exactly what the expansion has to get right.
    for (const std::unique_ptr<Route>& route : routes)
    {
        SCOPED_TRACE(route->Name());
        GraphicsDevice gd;

        // Solid first: this leg is the proof that the route exists and reaches the GPU at all.
        const RouteRun solid = RunRoute(gd, FillMode::Solid, *route);
        PrintRun((std::string(route->Name()) + " solid").c_str(), solid);
        ASSERT_TRUE(solid.accepted)
            << route->Name() << " refused an ordinary Solid draw: " << solid.rejection;
        EXPECT_EQ(1u, solid.QueuedByDraw()) << solid.afterDraw.Describe();
        EXPECT_EQ(1u, solid.NativeDraws()) << solid.afterFlush.Describe();
        EXPECT_GT(solid.frame.LitTotal(), 0)
            << route->Name() << " rendered nothing under Solid, so its WireFrame leg would prove "
                                "nothing -- " << solid.frame.Describe();

        // Then WireFrame, on a fresh device so no state from the Solid leg can carry over.
        GraphicsDevice wireDevice;
        const RouteRun wire = RunRoute(wireDevice, FillMode::WireFrame, *route);
        PrintRun((std::string(route->Name()) + " wireframe").c_str(), wire);
        ASSERT_TRUE(wire.accepted)
            << route->Name() << " refused a WireFrame draw: " << wire.rejection;
        EXPECT_EQ(1u, wire.QueuedByDraw())
            << route->Name() << " queued " << wire.QueuedByDraw() << " commands -- "
            << wire.afterDraw.Describe();
        EXPECT_EQ(1u, wire.NativeDraws())
            << route->Name() << " issued " << wire.NativeDraws() << " native draws -- "
            << wire.afterFlush.Describe();
        EXPECT_EQ(0u, wire.UncapturedErrors())
            << route->Name() << " produced native validation errors -- "
            << wire.afterFlush.Describe();

        // The interior is empty and the frame is smaller than Solid's. Stated per route, because
        // "some routes wireframe and the rest quietly fill" is the defect this test exists to
        // catch and a whole-suite total would hide it.
        EXPECT_EQ(0, wire.frame.LitIn(kInterior))
            << route->Name() << " filled the triangle interior under FillMode::WireFrame -- "
            << wire.frame.Describe();
        EXPECT_GT(wire.frame.LitTotal(), 0)
            << route->Name() << " rendered nothing under FillMode::WireFrame -- "
            << wire.frame.Describe();
        EXPECT_LT(wire.frame.LitTotal() * 4, solid.frame.LitTotal())
            << route->Name() << " WireFrame covered " << wire.frame.LitTotal()
            << " pixels against Solid's " << solid.frame.LitTotal() << " -- "
            << wire.frame.Describe();
    }
}

// ---------------------------------------------------------------------------
// 6. STATE TRANSITIONS AND REPETITION on one device: WireFrame -> Solid, and
//    Solid -> refused WireFrame -> Solid, with repeated refusals in between.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, AlternatingFillModesNeverLeaveStaleState)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    OrdinaryNonIndexed route(0);

    const RouteRun wire1 = RunRoute(gd, FillMode::WireFrame, route);
    PrintRun("alternate wireframe-1", wire1);
    const RouteRun solid1 = RunRoute(gd, FillMode::Solid, route);
    PrintRun("alternate solid-2", solid1);
    const RouteRun wire2 = RunRoute(gd, FillMode::WireFrame, route);
    PrintRun("alternate wireframe-3", wire2);
    const RouteRun wire3 = RunRoute(gd, FillMode::WireFrame, route);
    PrintRun("alternate wireframe-4", wire3);
    const RouteRun solid2 = RunRoute(gd, FillMode::Solid, route);
    PrintRun("alternate solid-5", solid2);

    ASSERT_TRUE(wire1.accepted) << wire1.rejection;
    ASSERT_TRUE(wire2.accepted) << wire2.rejection;
    ASSERT_TRUE(wire3.accepted) << wire3.rejection;
    ASSERT_TRUE(solid1.accepted) << solid1.rejection;
    ASSERT_TRUE(solid2.accepted) << solid2.rejection;

    // Every wireframe frame is the same picture, whatever ran before it. A pipeline cache keyed on
    // topology could return a solid pipeline to a wireframe draw (or the reverse) after an
    // alternation; that would show up here and nowhere else.
    ExpectWireFrameFrame(wire1.frame, "the first wireframe draw");
    ExpectWireFrameFrame(wire2.frame, "the wireframe draw after a Solid one");
    ExpectWireFrameFrame(wire3.frame, "the second consecutive wireframe draw");
    EXPECT_TRUE(wire1.frame.pixels == wire2.frame.pixels)
        << "wireframe output drifted across a Solid draw -- " << wire1.frame.Describe() << " then "
        << wire2.frame.Describe();
    EXPECT_TRUE(wire2.frame.pixels == wire3.frame.pixels)
        << "wireframe output drifted between consecutive draws -- " << wire2.frame.Describe()
        << " then " << wire3.frame.Describe();

    // And every Solid frame is the same picture, before and after any number of wireframe draws.
    EXPECT_TRUE(solid1.frame.pixels == solid2.frame.pixels)
        << "Solid output drifted across WireFrame draws -- " << solid1.frame.Describe()
        << " then " << solid2.frame.Describe();
    EXPECT_EQ(kInteriorArea, solid2.frame.LitIn(kInterior)) << solid2.frame.Describe();

    // The second consecutive wireframe draw reuses the line-list pipeline the first one built:
    // alternation must not make the cache grow without bound.
    EXPECT_EQ(1u, wire3.QueuedByDraw());
    EXPECT_EQ(1u, wire3.NativeDraws());
    EXPECT_EQ(0u, wire3.ColoredPipelinesBuilt())
        << "a repeated wireframe draw built another pipeline -- " << wire3.afterFlush.Describe();
}

// ---------------------------------------------------------------------------
// 7. LIFETIME. A refusal must not retain the resources the draw would have referenced, and must
//    not stop them being replaced or the device being torn down.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, WireFrameRetainsNothingAndSurvivesResourceReplacement)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    {
        // A wireframe draw whose vertex buffer then goes out of scope while the device lives on.
        // The expansion copies the indices it derives into the command, so nothing here may
        // outlive the buffers it read.
        OrdinaryNonIndexed doomed(0);
        const RouteRun wire = RunRoute(gd, FillMode::WireFrame, doomed);
        PrintRun("lifetime wireframe", wire);
        ASSERT_TRUE(wire.accepted) << wire.rejection;
        ExpectWireFrameFrame(wire.frame, "the wireframe draw whose buffers then died");
    }
    // The buffers that draw referenced are gone. If the expansion had retained a reference to the
    // source index buffer rather than copying out of it, the flush below would replay a dangling one.
    OrdinaryNonIndexed replacement(0);
    const RouteRun recovered = RunRoute(gd, FillMode::Solid, replacement);
    PrintRun("lifetime replacement solid", recovered);
    ASSERT_TRUE(recovered.accepted) << recovered.rejection;
    EXPECT_EQ(kInteriorArea, recovered.frame.LitIn(kInterior)) << recovered.frame.Describe();
    EXPECT_EQ(0u, recovered.UncapturedErrors()) << recovered.afterFlush.Describe();

    // A trailing wireframe draw, then teardown: the destructor drains the queue, and a command
    // holding a native handle past the device's lifetime would surface as an uncaptured error.
    OrdinaryNonIndexed trailing(0);
    const RouteRun last = RunRoute(gd, FillMode::WireFrame, trailing);
    PrintRun("lifetime trailing wireframe", last);
    ASSERT_TRUE(last.accepted) << last.rejection;
    EXPECT_EQ(1u, last.QueuedByDraw()) << last.afterDraw.Describe();
    EXPECT_EQ(0u, last.UncapturedErrors()) << last.afterFlush.Describe();
}

// ---------------------------------------------------------------------------
// 8. NATIVE VALIDATION. The counter above says the uncaptured-error callback never fired; this
//    asks wgpu-native itself, through a validation error scope wrapped around the whole sequence.
// ---------------------------------------------------------------------------
namespace
{
    struct ErrorScopeState
    {
        bool completed = false;
        WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
        WGPUErrorType type = WGPUErrorType_Unknown;
        std::string message;
    };

    void OnErrorScope(WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message,
                      void* userdata1, void*)
    {
        auto& state = *static_cast<ErrorScopeState*>(userdata1);
        state.status = status;
        state.type = type;
        if (message.data != nullptr)
        {
            if (message.length == WGPU_STRLEN)
                state.message = message.data;
            else
                state.message.assign(message.data, message.length);
        }
        state.completed = true;
    }

    void PopAndExpectClean(WebGPURenderer& renderer, const char* what)
    {
        ErrorScopeState state;
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnErrorScope;
        callback.userdata1 = &state;
        wgpuDevicePopErrorScope(renderer.Device(), callback);
        for (int attempt = 0; attempt < 10000 && !state.completed; ++attempt)
            wgpuInstanceProcessEvents(renderer.Instance());

        ASSERT_TRUE(state.completed) << what << ": wgpu-native did not complete the error scope";
        ASSERT_EQ(WGPUPopErrorScopeStatus_Success, state.status)
            << what << ": status=" << static_cast<int>(state.status) << '\n' << state.message;
        EXPECT_EQ(WGPUErrorType_NoError, state.type)
            << what << ": type=" << static_cast<int>(state.type) << '\n' << state.message;
        EXPECT_TRUE(state.message.empty())
            << what << ": wgpu-native returned a message for a clean scope:\n" << state.message;
    }
}   // namespace

TEST(WebGpuWireFrameContract, WireFrameAndRecoveryAreNativelyClean)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    WebGPURenderer& renderer = RendererOf(gd);
    const std::size_t uncapturedBefore = renderer.GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer.Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer.Device(), WGPUErrorFilter_Validation);

    OrdinaryNonIndexed route(0);
    const RouteRun wire = RunRoute(gd, FillMode::WireFrame, route);
    PrintRun("validation wireframe", wire);
    const RouteRun recovered = RunRoute(gd, FillMode::Solid, route);
    PrintRun("validation recovery", recovered);

    ASSERT_TRUE(wire.accepted) << wire.rejection;
    ASSERT_TRUE(recovered.accepted) << recovered.rejection;
    ExpectWireFrameFrame(wire.frame, "the wireframe draw under a validation scope");
    EXPECT_EQ(kInteriorArea, recovered.frame.LitIn(kInterior)) << recovered.frame.Describe();

    // wgpu-native's own verdict, not just this renderer's error counter. The line-list pipeline is
    // where a stale depth bias or a mismatched index format would be caught, and this is the arm
    // that asks the driver rather than inferring it from pixels.
    PopAndExpectClean(renderer, "validation scope");
    PopAndExpectClean(renderer, "out-of-memory scope");
    EXPECT_EQ(uncapturedBefore, renderer.GetUncapturedErrorCountEXT())
        << "the wireframe/recovery sequence produced uncaptured native errors";
}

// ---------------------------------------------------------------------------
// 9. THE TOPOLOGY BOUNDARY. A fill mode describes how a POLYGON's interior is rasterized, so a line
//    or point list has nothing for it to select and Solid/WireFrame are the same request. The
//    expansion must therefore leave those draws completely alone -- expanding a line list into
//    "edges" would double every line and rewrite a draw that was already correct.
//
//    The claim is MEASURED, not reasoned about: each non-polygon topology is drawn under both fill
//    modes and the two frames must be byte-identical, while each polygon topology must differ.
//
//    This boundary has now been got wrong in both directions, which is why it is tested from both:
//    WEBGPU-115's first guard REFUSED a WireFrame point-list draw that every other renderer renders
//    (found by PointListPrimitiveTest.PointListIsNotAffectedByTriangleCulling), and an expansion
//    that forgot the same check would silently rewrite one.
// ---------------------------------------------------------------------------
namespace
{
    /// Draws @p primitive from the shared triangle's three vertices under @p fill, returning the
    /// frame or the refusal. Line and point topologies consume the same three positions, so the
    /// two fill modes are directly comparable per topology.
    RouteRun RunTopology(GraphicsDevice& device, FillMode fill, PrimitiveType primitive,
                         int primitiveCount)
    {
        RouteRun run;
        WebGPURenderer& renderer = RendererOf(device);
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        try
        {
            const std::array<VertexPositionColor, 3> verts = TriangleVertices();
            VertexBuffer vb(device, PositionColorDeclaration(), 3, BufferUsage::None);
            vb.SetData(verts.data(), 3);

            ApplyFixtureState(device, fill);
            BasicEffect effect(device);
            ApplyFixtureEffect(effect);

            device.SetRenderTarget(&target);
            device.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
            device.Clear(Color(kClear[0], kClear[1], kClear[2], kClear[3]));
            effect.Apply();
            device.SetVertexBuffer(&vb);

            run.before = Counters::Read(renderer);
            try
            {
                device.DrawPrimitives(primitive, 0, primitiveCount);
                run.accepted = true;
            }
            catch (const std::exception& e)
            {
                run.rejection = e.what();
            }
            run.afterDraw = Counters::Read(renderer);

            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(nullptr);
            ReadTarget(target, run.frame);
            run.afterFlush = Counters::Read(renderer);
        }
        catch (const std::exception& e)
        {
            run.rejection = std::string("setup: ") + e.what();
            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(nullptr);
        }
        return run;
    }

    struct TopologyCase
    {
        const char* name;
        PrimitiveType primitive;
        int primitiveCount;
        bool polygon;
    };
}   // namespace

TEST(WebGpuWireFrameContract, OnlyPolygonTopologiesAreExpanded)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    const std::array<TopologyCase, 5> cases{
        TopologyCase{"TriangleList", PrimitiveType::TriangleList, 1, true},
        TopologyCase{"TriangleStrip", PrimitiveType::TriangleStrip, 1, true},
        TopologyCase{"LineList", PrimitiveType::LineList, 1, false},
        TopologyCase{"LineStrip", PrimitiveType::LineStrip, 2, false},
        TopologyCase{"PointListEXT", PrimitiveType::PointListEXT, 3, false},
    };

    for (const TopologyCase& c : cases)
    {
        SCOPED_TRACE(c.name);
        GraphicsDevice gd;
        const RouteRun solid = RunTopology(gd, FillMode::Solid, c.primitive, c.primitiveCount);
        PrintRun((std::string(c.name) + " solid").c_str(), solid);
        GraphicsDevice wireDevice;
        const RouteRun wire =
            RunTopology(wireDevice, FillMode::WireFrame, c.primitive, c.primitiveCount);
        PrintRun((std::string(c.name) + " wireframe").c_str(), wire);

        ASSERT_TRUE(solid.accepted) << c.name << " refused a Solid draw: " << solid.rejection;
        EXPECT_GT(solid.frame.LitTotal(), 0)
            << c.name << " rendered nothing under Solid -- " << solid.frame.Describe();
        ASSERT_TRUE(wire.accepted)
            << c.name << " refused a WireFrame draw: " << wire.rejection;
        EXPECT_EQ(0u, wire.UncapturedErrors()) << wire.afterFlush.Describe();

        if (c.polygon)
        {
            // A polygon topology is expanded, so the two frames must NOT match.
            EXPECT_FALSE(wire.frame.pixels == solid.frame.pixels)
                << c.name << " rendered identically under both fill modes -- the expansion did not "
                             "reach this topology (" << wire.frame.Describe() << ')';
            EXPECT_EQ(0, wire.frame.LitIn(kInterior))
                << c.name << " filled the interior under FillMode::WireFrame -- "
                << wire.frame.Describe();
        }
        else
        {
            // A non-polygon topology has no interior to leave empty, so WireFrame selects nothing
            // and the output must be EXACTLY what Solid produces -- byte for byte, because an
            // expansion that touched it would double every line or drop every point.
            EXPECT_TRUE(wire.frame.pixels == solid.frame.pixels)
                << c.name << " rendered differently under WireFrame than under Solid -- "
                << wire.frame.Describe() << " vs " << solid.frame.Describe();
            EXPECT_EQ(1u, wire.QueuedByDraw()) << wire.afterDraw.Describe();
            EXPECT_EQ(1u, wire.NativeDraws()) << wire.afterFlush.Describe();
        }
    }
}

// ---------------------------------------------------------------------------
// 10. THE MULTI-STREAM WIREFRAME. plans/plan_webgpu.md WEBGPU-154/172.
//
// WEBGPU-154 recorded a defect in the reference renderer: EasyGL gates its own edge expansion on
// `!multiStream`, so once a draw binds several VertexBufferBindings its wireframe request is
// silently served as a solid fill while the capability still says true. WEBGPU-172 turned this
// renderer's own MultiStreamVertexInput capability on, which is exactly the moment that defect
// becomes possible to clone.
//
// It is not cloned, and this test is the measurement rather than the argument: the expansion
// rewrites INDICES only and never reads the vertex data, so the number of bound streams cannot
// reach it. The same triangle is drawn twice -- once from one packed stride-16 buffer, once split
// into a position-only (stride 12) and a colour-only (stride 4) pair -- and both must produce the
// same wireframe, with an empty interior.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, AMultiStreamDrawWireframesLikeASingleStreamOne)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    ASSERT_TRUE(gd.SupportsCapability(GraphicsCapability::MultiStreamVertexInput))
        << "WEBGPU-172 turned this on; without it this test measures nothing";

    const auto runSplit = [&gd](FillMode fill, Frame& out) {
        RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        const std::array<VertexPositionColor, 3> verts = TriangleVertices();

        // The same three vertices, split by semantic across two bindings. Neither stride is a
        // layout any renderer recognises alone, so a dropped second stream cannot draw this.
        std::array<float, 9> positions{};
        std::array<std::uint32_t, 3> colors{};
        for (std::size_t i = 0; i < verts.size(); ++i)
        {
            positions[i * 3 + 0] = verts[i].Position.X;
            positions[i * 3 + 1] = verts[i].Position.Y;
            positions[i * 3 + 2] = verts[i].Position.Z;
            colors[i] = verts[i].Color.getPackedValueProperty();
        }
        const VertexDeclaration positionOnly(12, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
        const VertexDeclaration colorOnly(4, {
            VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
        VertexBuffer positionVb(gd, positionOnly, 3, BufferUsage::None);
        positionVb.SetDataRaw(positions.data(), 3, 12);
        VertexBuffer colorVb(gd, colorOnly, 3, BufferUsage::None);
        colorVb.SetDataRaw(colors.data(), 3, 4);

        ApplyFixtureState(gd, fill);
        BasicEffect effect(gd);
        ApplyFixtureEffect(effect);

        gd.SetRenderTarget(&target);
        gd.setScissorRectangleProperty(Rectangle(0, 0, kSize, kSize));
        gd.Clear(Color(kClear[0], kClear[1], kClear[2], kClear[3]));
        effect.Apply();
        gd.SetVertexBuffers({VertexBufferBinding(&positionVb), VertexBufferBinding(&colorVb)});
        EXPECT_NO_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1));
        gd.SetVertexBuffer(nullptr);
        gd.SetRenderTarget(nullptr);
        ReadTarget(target, out);
    };

    Frame splitSolid = ClearFrame();
    Frame splitWire = ClearFrame();
    runSplit(FillMode::Solid, splitSolid);
    runSplit(FillMode::WireFrame, splitWire);

    // The split draw renders at all -- without this, everything below would pass on an empty frame.
    EXPECT_GT(splitSolid.LitTotal(), 0)
        << "the split-vertex solid draw rendered nothing -- " << splitSolid.Describe();
    ExpectWireFrameFrame(splitWire, "the split-vertex wireframe draw");
    EXPECT_LT(splitWire.LitTotal() * 4, splitSolid.LitTotal())
        << "a multi-stream WireFrame draw covered " << splitWire.LitTotal()
        << " pixels against Solid's " << splitSolid.LitTotal()
        << " -- the silent solid fill this test exists to catch";

    // And it is the SAME wireframe the one-buffer draw produces, byte for byte: the streams
    // changed where the bytes came from, not which pixels the edges cover.
    const RouteRun single = RunOrdinaryRoute(gd, FillMode::WireFrame);
    ASSERT_TRUE(single.accepted) << single.rejection;
    EXPECT_TRUE(splitWire.pixels == single.frame.pixels)
        << "the split-vertex wireframe differs from the single-buffer one -- "
        << splitWire.Describe() << " vs " << single.frame.Describe();
}

#endif  // CNA_RENDERER_WEBGPU
