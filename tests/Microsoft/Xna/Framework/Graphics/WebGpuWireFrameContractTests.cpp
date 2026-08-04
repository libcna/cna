// SPDX-License-Identifier: MS-PL

// ============================================================================
// WEBGPU-115 -- WebGPU's FillMode::WireFrame contract, measured end to end.
//
// wgpu-native has no polygon-mode API at all: `WGPUPrimitiveState` has no field a wireframe fill
// could reach. What the backend did with the request was therefore the whole question, and this
// file answers it by measurement rather than by reading the source or the documentation.
//
// The reading this file was written against, taken on the pre-fix tree:
//
//     SupportsCapability(WireFrame) == true          (inherited IGraphicsBackend default)
//     ApplyRasterizerState           accepted        (no throw, no warning, no log)
//     queued draw commands           +1
//     Colored3D pipeline cache       +1              (`wireframe` is folded into the key)
//     native draw issues             +1
//     render passes / queue submits  +1 / +1
//     target                         total=18176 interior=1089/1089 AB=298 BC=310 CA=329
//     the same frame under Solid     total=18176 interior=1089/1089 AB=298 BC=310 CA=329
//
// The two frames are BYTE-IDENTICAL. So the runtime made an affirmative false claim through the
// public capability query, accepted the request, built and natively submitted a distinct pipeline
// for it, and returned solid geometry -- with no exception, no capability rejection and no
// diagnostic anywhere on the path. That is the defect, and every assertion below states it as the
// CURRENT behaviour so the record is a measurement and not a description.
//
// The geometry is REMED-GFX-209's asymmetric triangle, shared through WireFrameTriangleOracle.hpp
// rather than copied, so these readings are directly comparable with the per-backend contract
// suite's own.
// ============================================================================

#ifdef CNA_BACKEND_WEBGPU

#include <cstddef>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "WireFrameTriangleOracle.hpp"

namespace
{
    using namespace CnaTest::WireFrameOracle;   // NOLINT(google-build-using-namespace)
    using CNA::GraphicsCapability;
    using CNA::Internal::Backends::WebGPU::WebGPUGraphicsBackend;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    /// Every cardinality this ticket has to account for, read from the backend's own NOXNA
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

        static Counters Read(const WebGPUGraphicsBackend& backend)
        {
            Counters c;
            c.queuedCommands = backend.GetQueuedDrawCommandCountEXT();
            c.nativeDraws = backend.GetNativeDrawIssueCountEXT();
            c.coloredPipelines = backend.GetColoredPipelineCacheSizeEXT();
            c.instancedPipelines = backend.GetInstancedPipelineCacheSizeEXT();
            c.renderPasses = backend.GetRenderPassCountEXT();
            c.queueSubmits = backend.GetQueueSubmitCountEXT();
            c.uncapturedErrors = backend.GetUncapturedErrorCountEXT();
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

    WebGPUGraphicsBackend& BackendOf(GraphicsDevice& device)
    {
        auto* backend = dynamic_cast<WebGPUGraphicsBackend*>(&device.GetBackend());
        // This whole file is gated on CNA_BACKEND_WEBGPU, so the cast cannot legitimately fail.
        if (backend == nullptr)
            throw std::runtime_error("CNA_BACKEND_WEBGPU build has no WebGPUGraphicsBackend");
        return *backend;
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
        WebGPUGraphicsBackend& backend = BackendOf(device);
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
            run.before = Counters::Read(backend);
            try
            {
                device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
                run.accepted = true;
            }
            catch (const std::exception& e)
            {
                run.rejection = e.what();
            }
            run.afterDraw = Counters::Read(backend);

            device.SetVertexBuffer(nullptr);
            device.SetRenderTarget(nullptr);
            ReadTarget(target, run.frame);
            run.afterFlush = Counters::Read(backend);
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
// 1. THE CAPABILITY QUERY. The public contract for "can this backend do X", and the reason the
//    silent substitution below is a false claim rather than an undocumented gap.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, CapabilityQueryAnswersForWireFrame)
{
    GraphicsDevice gd;
    EXPECT_NO_THROW({ (void)gd.SupportsCapability(GraphicsCapability::WireFrame); });
    const bool reported = gd.SupportsCapability(GraphicsCapability::WireFrame);
    std::cout << "[WEBGPU-115] SupportsCapability(WireFrame) == "
              << (reported ? "true" : "false") << std::endl;

    // PRE-FIX: WebGPUGraphicsBackend does not override SupportsCapability at all, so this is
    // IGraphicsBackend's permissive default -- a `true` that is inherited, never asserted, and
    // contradicted by every pixel the renderer produces.
    EXPECT_TRUE(reported)
        << "WebGPU no longer claims WireFrame support. If WEBGPU-115's capability override landed, "
           "this arm and the two below must move to the rejection contract";

    // The surrounding capability answers must not move with it. MultiStreamVertexInput is the one
    // entry whose shared default is false (REMED-GFX-201); everything else is genuinely supported.
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::ThreeD));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::DepthStencilBuffer));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::MultipleRenderTargets));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::OcclusionQuery));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::CustomEffects));
    EXPECT_TRUE(gd.SupportsCapability(GraphicsCapability::Texture3D));
    EXPECT_FALSE(gd.SupportsCapability(GraphicsCapability::MultiStreamVertexInput));
}

// ---------------------------------------------------------------------------
// 2. THE FULL PATH, counted. Capability -> RasterizerState -> public draw -> queued command ->
//    pipeline key -> native pipeline -> render pass -> queue submit.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, WireFrameDrawIsQueuedPipelinedAndNativelySubmitted)
{
    GraphicsDevice gd;
    const RouteRun run = RunOrdinaryRoute(gd, FillMode::WireFrame);
    PrintRun("ordinary-nonindexed wireframe", run);

    // PRE-FIX: accepted at every stage. Not one of these is zero.
    ASSERT_TRUE(run.accepted) << "WebGPU refused a WireFrame draw: " << run.rejection;
    EXPECT_EQ(1u, run.QueuedByDraw()) << run.afterDraw.Describe();
    EXPECT_EQ(1u, run.ColoredPipelinesBuilt())
        << "the `wireframe` bit is folded into Make3DPipelineKey, so the request builds its own "
           "WGPURenderPipeline -- " << run.afterFlush.Describe();
    EXPECT_EQ(1u, run.NativeDraws()) << run.afterFlush.Describe();
    EXPECT_EQ(1u, run.Passes()) << run.afterFlush.Describe();
    EXPECT_EQ(1u, run.Submits()) << run.afterFlush.Describe();
    // The native layer never complains, because nothing invalid was ever asked of it: the request
    // simply evaporated between the pipeline key and WGPUPrimitiveState.
    EXPECT_EQ(0u, run.UncapturedErrors()) << run.afterFlush.Describe();

    // And the target was mutated -- fully, as a solid triangle.
    EXPECT_EQ(kInteriorArea, run.frame.LitIn(kInterior)) << run.frame.Describe();
}

// ---------------------------------------------------------------------------
// 3. THE OUTPUT. The single measurement that makes this a silent wrong result rather than a
//    missing feature: WireFrame and Solid are the same picture, byte for byte.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, WireFrameOutputIsByteIdenticalToSolid)
{
    GraphicsDevice gd;
    const RouteRun solid = RunOrdinaryRoute(gd, FillMode::Solid);
    PrintRun("ordinary-nonindexed solid", solid);
    const RouteRun wire = RunOrdinaryRoute(gd, FillMode::WireFrame);
    PrintRun("ordinary-nonindexed wireframe", wire);

    ASSERT_TRUE(solid.accepted) << "WebGPU refused an ordinary Solid draw: " << solid.rejection;
    ASSERT_TRUE(wire.accepted) << "WebGPU refused a WireFrame draw: " << wire.rejection;

    // PRE-FIX: identical. A wireframe would empty the interior and light only the three edges.
    EXPECT_EQ(kInteriorArea, wire.frame.LitIn(kInterior))
        << "WebGPU no longer fills the interior under FillMode::WireFrame -- "
        << wire.frame.Describe();
    EXPECT_TRUE(wire.frame.pixels == solid.frame.pixels)
        << "WireFrame output now differs from Solid -- " << wire.frame.Describe() << " vs "
        << solid.frame.Describe();
}

#endif  // CNA_BACKEND_WEBGPU
