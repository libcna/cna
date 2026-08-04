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
// The two frames were BYTE-IDENTICAL. So the runtime made an affirmative false claim through the
// public capability query, accepted the request, built and natively submitted a distinct pipeline
// for it, and returned solid geometry -- with no exception, no capability rejection and no
// diagnostic anywhere on the path. The commit that added this file asserts exactly those numbers;
// it is the A/B evidence, and `git show` on it is where the old behaviour lives.
//
// THE CONTRACT THIS FILE NOW MEASURES. A backend must not report a capability as supported while
// silently substituting a different rendering mode, so:
//
//     SupportsCapability(WireFrame) == false        asserted by the backend, not inherited
//     a RasterizerState carrying WireFrame          still a legal state operation
//     the first draw that would consume it          throws System::NotSupportedException
//     queued draw commands                          +0
//     pipeline caches                               +0
//     native draw issues                            +0
//     queue submits attributable to the draw        +0
//     the target                                    unchanged -- clear colour only
//     the next Solid draw                           renders exactly
//
// Rejection is at DRAW time, not at ApplyRasterizerState: a state setter cannot know whether a
// draw will follow or which route it would take, which is the same reasoning REMED-GFX-DECL-GUARD
// applied to SetVertexDeclaration.
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
#include "System/NotSupportedException.hpp"
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

    /// The refusal's own contract: a catchable System::NotSupportedException whose message names
    /// what was refused and where to ask about it. A guard that fires with someone else's message
    /// is a different guard.
    void ExpectWireFrameRejection(const std::string& message)
    {
        EXPECT_NE(std::string::npos, message.find("WireFrame"))
            << "the refusal does not name FillMode::WireFrame: \"" << message << '"';
        EXPECT_NE(std::string::npos, message.find("WebGPU"))
            << "the refusal does not name the backend: \"" << message << '"';
        EXPECT_NE(std::string::npos, message.find("SupportsCapability"))
            << "the refusal does not point at the capability query: \"" << message << '"';
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

    // WEBGPU-115: false, and asserted by WebGPUGraphicsBackend::SupportsCapability rather than
    // inherited from IGraphicsBackend's permissive default. The renderer has no polygon mode to
    // back a `true` with, and the query is the public contract for that fact.
    EXPECT_FALSE(reported)
        << "WebGPU claims WireFrame support again -- the capability override is gone, and the "
           "draws below will silently render solid geometry instead of refusing";

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
TEST(WebGpuWireFrameContract, WireFrameDrawIsRefusedBeforeAnythingIsQueued)
{
    GraphicsDevice gd;
    const RouteRun run = RunOrdinaryRoute(gd, FillMode::WireFrame);
    PrintRun("ordinary-nonindexed wireframe", run);

    ASSERT_FALSE(run.accepted)
        << "WebGPU accepted a WireFrame draw again and produced " << run.frame.Describe();
    ExpectWireFrameRejection(run.rejection);

    // Nothing downstream of the guard ran. Each of these was 1 before the fix.
    EXPECT_EQ(0u, run.QueuedByDraw())
        << "a refused draw appended a command to drawOrder_ -- " << run.afterDraw.Describe();
    EXPECT_EQ(0u, run.ColoredPipelinesBuilt())
        << "a refused draw grew the Colored3D pipeline cache -- " << run.afterFlush.Describe();
    EXPECT_EQ(0u, run.NativeDraws())
        << "a refused draw reached the render-pass encoder -- " << run.afterFlush.Describe();
    EXPECT_EQ(0u, run.UncapturedErrors())
        << "the refusal produced native validation errors -- " << run.afterFlush.Describe();

    // No extra work either: the guard adds no retry, no dummy draw, no second frame. The one
    // remaining pass and submit belong to the ordered Clear this fixture issued before the draw,
    // which the refusal must neither cancel nor duplicate.
    EXPECT_LE(run.Passes(), 1u) << run.afterFlush.Describe();
    EXPECT_LE(run.Submits(), 1u) << run.afterFlush.Describe();

    // The target the refused draw was aimed at still holds the clear colour and nothing else.
    ExpectClearOnly(run.frame, "the refused WireFrame draw");
}

// ---------------------------------------------------------------------------
// 3. THE OUTPUT. The single measurement that makes this a silent wrong result rather than a
//    missing feature: WireFrame and Solid are the same picture, byte for byte.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, SolidRendersExactlyAfterARefusedWireFrameDraw)
{
    GraphicsDevice gd;
    const RouteRun solid = RunOrdinaryRoute(gd, FillMode::Solid);
    PrintRun("ordinary-nonindexed solid", solid);
    const RouteRun wire = RunOrdinaryRoute(gd, FillMode::WireFrame);
    PrintRun("ordinary-nonindexed wireframe", wire);
    const RouteRun recovered = RunOrdinaryRoute(gd, FillMode::Solid);
    PrintRun("ordinary-nonindexed solid-recovery", recovered);

    ASSERT_TRUE(solid.accepted) << "WebGPU refused an ordinary Solid draw: " << solid.rejection;
    ASSERT_FALSE(wire.accepted) << "WebGPU accepted a WireFrame draw -- " << wire.frame.Describe();
    ASSERT_TRUE(recovered.accepted)
        << "WebGPU refused a Solid draw after a refused WireFrame one: " << recovered.rejection;

    // The refusal is not the Solid picture, and the Solid picture is unchanged by it.
    ExpectClearOnly(wire.frame, "the refused WireFrame draw");
    EXPECT_TRUE(recovered.frame.pixels == solid.frame.pixels)
        << "Solid output changed across a refused WireFrame draw -- " << recovered.frame.Describe()
        << " vs " << solid.frame.Describe();

    // And recovery costs exactly one ordinary draw: no extra frame, Present, wait, retry or dummy.
    EXPECT_EQ(1u, recovered.QueuedByDraw()) << recovered.afterDraw.Describe();
    EXPECT_EQ(1u, recovered.NativeDraws()) << recovered.afterFlush.Describe();
    EXPECT_EQ(1u, recovered.Submits()) << recovered.afterFlush.Describe();
    // The recovery reuses the pipeline the first Solid draw built -- the refused draw left no
    // WireFrame variant behind for it to collide with, and built none of its own.
    EXPECT_EQ(0u, recovered.ColoredPipelinesBuilt()) << recovered.afterFlush.Describe();
    EXPECT_EQ(0u, recovered.UncapturedErrors()) << recovered.afterFlush.Describe();
}

// ---------------------------------------------------------------------------
// 4. THE EXCEPTION TYPE. "Deterministic" means a caller can catch it by name and fall back, so the
//    type is part of the contract, not just the fact that something was thrown.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, RefusalIsACatchableNotSupportedException)
{
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

    EXPECT_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
                 System::NotSupportedException);

    // Repeating the refused draw is idempotent -- the guard holds no state of its own, so the
    // second call fails exactly like the first rather than leaking through or aborting.
    EXPECT_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
                 System::NotSupportedException);

    // Selecting Solid on the same device, without recreating anything, is all recovery takes.
    ApplyFixtureState(gd, FillMode::Solid);
    effect.Apply();
    EXPECT_NO_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1));

    gd.SetVertexBuffer(nullptr);
    gd.SetRenderTarget(nullptr);

    Frame frame;
    ReadTarget(target, frame);
    std::cout << "[WEBGPU-115] refuse, refuse, then Solid on one target: " << frame.Describe()
              << std::endl;
    EXPECT_EQ(kInteriorArea, frame.LitIn(kInterior))
        << "the Solid draw after two refusals did not fill the interior -- " << frame.Describe();
    EXPECT_TRUE(frame.EveryLitPixelIsInk())
        << "a refused draw left a pixel behind -- " << frame.Describe();
}

#endif  // CNA_BACKEND_WEBGPU
