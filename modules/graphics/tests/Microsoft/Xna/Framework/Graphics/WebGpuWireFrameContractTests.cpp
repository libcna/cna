// SPDX-License-Identifier: MS-PL

// ============================================================================
// WEBGPU-115 -- WebGPU's FillMode::WireFrame contract, measured end to end.
//
// wgpu-native has no polygon-mode API at all: `WGPUPrimitiveState` has no field a wireframe fill
// could reach. What the renderer did with the request was therefore the whole question, and this
// file answers it by measurement rather than by reading the source or the documentation.
//
// The reading this file was written against, taken on the pre-fix tree:
//
//     SupportsCapability(WireFrame) == true          (inherited IGraphicsRenderer default)
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
// THE CONTRACT THIS FILE NOW MEASURES. A renderer must not report a capability as supported while
// silently substituting a different rendering mode, so:
//
//     SupportsCapability(WireFrame) == false        asserted by the renderer, not inherited
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
// rather than copied, so these readings are directly comparable with the per-renderer contract
// suite's own.
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

    /// The refusal's own contract: a catchable System::NotSupportedException whose message names
    /// what was refused and where to ask about it. A guard that fires with someone else's message
    /// is a different guard.
    void ExpectWireFrameRejection(const std::string& message)
    {
        EXPECT_NE(std::string::npos, message.find("WireFrame"))
            << "the refusal does not name FillMode::WireFrame: \"" << message << '"';
        EXPECT_NE(std::string::npos, message.find("WebGPU"))
            << "the refusal does not name the renderer: \"" << message << '"';
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
    std::cout << "[WEBGPU-115] SupportsCapability(WireFrame) == "
              << (reported ? "true" : "false") << std::endl;

    // WEBGPU-115: false, and asserted by WebGPURenderer::SupportsCapability rather than
    // inherited from IGraphicsRenderer's permissive default. The renderer has no polygon mode to
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
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
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

TEST(WebGpuWireFrameContract, EveryPublicDrawRouteRefusesWireFrameAndAcceptsSolid)
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
        ASSERT_FALSE(wire.accepted)
            << route->Name() << " accepted a WireFrame draw and produced " << wire.frame.Describe();
        ExpectWireFrameRejection(wire.rejection);
        EXPECT_EQ(0u, wire.QueuedByDraw())
            << route->Name() << " queued a command for a refused draw -- "
            << wire.afterDraw.Describe();
        EXPECT_EQ(0u, wire.NativeDraws())
            << route->Name() << " reached the render-pass encoder -- " << wire.afterFlush.Describe();
        EXPECT_EQ(0u, wire.familyPipelinesBuilt)
            << route->Name() << " grew its family's pipeline cache -- " << wire.afterFlush.Describe();
        EXPECT_EQ(0u, wire.ColoredPipelinesBuilt())
            << route->Name() << " grew the Colored3D pipeline cache -- "
            << wire.afterFlush.Describe();
        EXPECT_EQ(0u, wire.UncapturedErrors())
            << route->Name() << " produced native validation errors -- "
            << wire.afterFlush.Describe();
        ExpectClearOnly(wire.frame, route->Name());
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

    EXPECT_FALSE(wire1.accepted);
    EXPECT_FALSE(wire2.accepted);
    EXPECT_FALSE(wire3.accepted);
    ASSERT_TRUE(solid1.accepted) << solid1.rejection;
    ASSERT_TRUE(solid2.accepted) << solid2.rejection;

    // Every refusal is identical -- the guard carries no state that a repetition could change.
    EXPECT_EQ(wire1.rejection, wire2.rejection);
    EXPECT_EQ(wire2.rejection, wire3.rejection);
    ExpectClearOnly(wire1.frame, "the first refused draw");
    ExpectClearOnly(wire2.frame, "the second refused draw");
    ExpectClearOnly(wire3.frame, "the third refused draw");

    // And every Solid frame is the same picture, before and after any number of refusals.
    EXPECT_TRUE(solid1.frame.pixels == solid2.frame.pixels)
        << "Solid output drifted across refused WireFrame draws -- " << solid1.frame.Describe()
        << " then " << solid2.frame.Describe();
    EXPECT_EQ(kInteriorArea, solid2.frame.LitIn(kInterior)) << solid2.frame.Describe();
    // Two consecutive refusals cost nothing at all: no pipeline, no command, no native draw.
    EXPECT_EQ(0u, wire3.QueuedByDraw());
    EXPECT_EQ(0u, wire3.NativeDraws());
    EXPECT_EQ(0u, wire3.ColoredPipelinesBuilt());
}

// ---------------------------------------------------------------------------
// 7. LIFETIME. A refusal must not retain the resources the draw would have referenced, and must
//    not stop them being replaced or the device being torn down.
// ---------------------------------------------------------------------------
TEST(WebGpuWireFrameContract, RefusalRetainsNothingAndSurvivesResourceReplacement)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    {
        // Refuse a draw whose vertex buffer then goes out of scope while the device lives on.
        OrdinaryNonIndexed doomed(0);
        const RouteRun refused = RunRoute(gd, FillMode::WireFrame, doomed);
        PrintRun("lifetime refused", refused);
        EXPECT_FALSE(refused.accepted);
        ExpectClearOnly(refused.frame, "the refused draw");
    }
    // The buffers the refused draw referenced are gone. If the refusal had queued a command that
    // held them, the flush below would replay a dangling reference.
    OrdinaryNonIndexed replacement(0);
    const RouteRun recovered = RunRoute(gd, FillMode::Solid, replacement);
    PrintRun("lifetime replacement solid", recovered);
    ASSERT_TRUE(recovered.accepted) << recovered.rejection;
    EXPECT_EQ(kInteriorArea, recovered.frame.LitIn(kInterior)) << recovered.frame.Describe();
    EXPECT_EQ(0u, recovered.UncapturedErrors()) << recovered.afterFlush.Describe();

    // A trailing refusal, then teardown: the destructor drains the queue, and a command left there
    // by a refused draw would release native handles after the device is gone.
    OrdinaryNonIndexed trailing(0);
    const RouteRun last = RunRoute(gd, FillMode::WireFrame, trailing);
    PrintRun("lifetime trailing refusal", last);
    EXPECT_FALSE(last.accepted);
    EXPECT_EQ(0u, last.QueuedByDraw()) << last.afterDraw.Describe();
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

TEST(WebGpuWireFrameContract, RefusalAndRecoveryAreNativelyClean)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice gd;
    WebGPURenderer& renderer = RendererOf(gd);
    const std::size_t uncapturedBefore = renderer.GetUncapturedErrorCountEXT();
    wgpuDevicePushErrorScope(renderer.Device(), WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(renderer.Device(), WGPUErrorFilter_Validation);

    OrdinaryNonIndexed route(0);
    const RouteRun refused = RunRoute(gd, FillMode::WireFrame, route);
    PrintRun("validation refused", refused);
    const RouteRun recovered = RunRoute(gd, FillMode::Solid, route);
    PrintRun("validation recovery", recovered);

    EXPECT_FALSE(refused.accepted);
    ASSERT_TRUE(recovered.accepted) << recovered.rejection;
    EXPECT_EQ(kInteriorArea, recovered.frame.LitIn(kInterior)) << recovered.frame.Describe();

    PopAndExpectClean(renderer, "validation scope");
    PopAndExpectClean(renderer, "out-of-memory scope");
    EXPECT_EQ(uncapturedBefore, renderer.GetUncapturedErrorCountEXT())
        << "the refusal/recovery sequence produced uncaptured native errors";
}

// ---------------------------------------------------------------------------
// 9. THE TOPOLOGY BOUNDARY. A fill mode describes how a POLYGON's interior is rasterized, so a
//    line or point list has nothing for it to select and Solid/WireFrame are the same request.
//    This renderer substitutes nothing there, so refusing those draws would delete a draw that is
//    already correct -- an over-wide guard, not a safety property. The claim is MEASURED here, not
//    reasoned about: each non-polygon topology is drawn under both fill modes and the two frames
//    must be byte-identical.
//
//    This is what PointListPrimitiveTest.PointListIsNotAffectedByTriangleCulling found: the first
//    version of the guard refused a WireFrame point-list draw that every other renderer renders,
//    identically, under either fill mode.
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

TEST(WebGpuWireFrameContract, OnlyPolygonTopologiesAreRefused)
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

        if (c.polygon)
        {
            EXPECT_FALSE(wire.accepted)
                << c.name << " is a polygon topology and must be refused -- "
                << wire.frame.Describe();
            ExpectClearOnly(wire.frame, c.name);
        }
        else
        {
            // Accepted, and -- the part that justifies accepting it -- the output is exactly what
            // Solid produces. Nothing was substituted, so there is nothing to refuse.
            ASSERT_TRUE(wire.accepted)
                << c.name << " has no polygon interior, so WireFrame selects nothing and the draw "
                             "must not be refused: " << wire.rejection;
            EXPECT_TRUE(wire.frame.pixels == solid.frame.pixels)
                << c.name << " rendered differently under WireFrame than under Solid -- "
                << wire.frame.Describe() << " vs " << solid.frame.Describe();
            EXPECT_EQ(1u, wire.QueuedByDraw()) << wire.afterDraw.Describe();
            EXPECT_EQ(1u, wire.NativeDraws()) << wire.afterFlush.Describe();
            EXPECT_EQ(0u, wire.UncapturedErrors()) << wire.afterFlush.Describe();
        }
    }
}

#endif  // CNA_RENDERER_WEBGPU
