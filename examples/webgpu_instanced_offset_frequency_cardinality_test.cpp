// SPDX-License-Identifier: MS-PL
// REMED-GFX-211 / REMED-GFX-213, structural half: honouring both bindings' VertexOffset and every
// legal InstanceFrequency must cost nothing a caller can observe.
//
// The pixel fixture (tests/Microsoft/Xna/Framework/Graphics/InstancedDrawMultiStreamTests.cpp,
// InstancedDrawRangeTests.cpp) proves WHICH records each stream supplies. This file proves the
// mechanism did not pay for it, because several wrong-but-passing implementations exist:
//
//   * put the offsets or the divisor in the pipeline cache key -> one variant per offset/frequency;
//   * expand a divisor with a second draw or a second pass     -> extra draws, passes and submits;
//   * upload one GPU buffer per logical instance               -> buffer count scaling with
//                                                                instanceCount rather than draws;
//   * read the expanded data back to verify it                 -> a queue wait per frame;
//   * bind the instance buffer at a native byte offset that is
//     not the copy's own base                                  -> a validation error.
//
// Every CNA-side count is read from the live backend and every native count from wgpu-native's own
// wgpuGenerateReport(), before and after a known public sequence, so each expectation is an exact
// number or an exact equality between two sequences rather than a trend.
//
// What the native counts do and do not measure, stated because a boundary that overclaims is worse
// than none: this backend releases a queued draw's transient buffers inside the same flush that
// creates them, so a delta taken across a whole render-target cycle is a NET LIVE count, not a
// created count. It therefore proves that no offset, no frequency and no repeat leaves a resource
// behind -- the persistent-growth half of the contract -- while the "no second draw, pass, submit
// or pipeline variant" half is carried by the backend's own counters below, which are cumulative.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp"

#if __has_include(<webgpu/wgpu.h>)
#include <webgpu/wgpu.h>
#define CNA_HAVE_WGPU_NATIVE_REPORT 1
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::WebGPU::WebGPUGraphicsBackend;

namespace
{
    constexpr int kBBW = 64;
    constexpr int kBBH = 64;
    constexpr int kRT  = 32;

    /// Three vertices, one triangle -- the geometry is irrelevant here, only the cardinality is.
    constexpr int kVertexCount = 12;

    /// Instance records the matrix buffer holds. Deliberately far more than any draw below asks
    /// for, so a nonzero instance VertexOffset always has somewhere real to point.
    constexpr int kInstanceRecords = 32;

    const Color kBlack(0, 0, 0, 255);

    /// The per-instance world transform, one mat4 per record, exactly as the instanced route's
    /// stock declaration describes it.
    struct InstanceMatrix
    {
        std::array<float, 16> m;
    };

    VertexDeclaration InstanceMatrixDeclaration()
    {
        return VertexDeclaration(
            64,
            {
                VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::Position, 1),
                VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::Position, 2),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::Position, 3),
                VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::Position, 4),
            });
    }

    InstanceMatrix Identity()
    {
        return InstanceMatrix{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    }

    /// Everything one measured sequence cost, CNA-side and native-side together.
    struct Cost
    {
        std::size_t passes = 0;
        std::size_t submits = 0;
        std::size_t coloredPipelines = 0;
        std::size_t instancedPipelines = 0;
        std::size_t nativeBuffers = 0;
        std::size_t nativePipelines = 0;
        std::size_t nativeCommandBuffers = 0;
    };
}

class WebGpuInstancedOffsetFrequencyCardinalityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> fx_;
    std::unique_ptr<VertexBuffer> mesh_;
    std::unique_ptr<VertexBuffer> matrices_;
    std::unique_ptr<IndexBuffer> indices_;
    int phase_ = 0;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void checkCount(std::size_t got, std::size_t want, const std::string& label)
    {
        check(got == want, got == want
                  ? label + " = " + std::to_string(got)
                  : label + ": expected " + std::to_string(want) + ", got " + std::to_string(got));
    }

    static std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                DepthFormat::Depth24Stencil8, 0,
                                                RenderTargetUsage::DiscardContents);
    }

    /// wgpu-native's own live-object registry. A render pipeline per frequency, a buffer retained
    /// per logical instance, or a command buffer left alive by a divisor would appear here
    /// regardless of what the backend's own counters say. These are LIVE counts -- see this file's
    /// header for exactly what a delta over one cycle does and does not prove.
    static Cost NativeCost(WebGPUGraphicsBackend& backend, Cost cost)
    {
#ifdef CNA_HAVE_WGPU_NATIVE_REPORT
        WGPUGlobalReport report{};
        wgpuGenerateReport(backend.Instance(), &report);
        cost.nativeBuffers = report.hub.buffers.numAllocated;
        cost.nativePipelines = report.hub.renderPipelines.numAllocated;
        cost.nativeCommandBuffers = report.hub.commandBuffers.numAllocated;
#else
        (void)backend;
#endif
        return cost;
    }

    Cost Snapshot(WebGPUGraphicsBackend& backend)
    {
        Cost cost;
        cost.passes = backend.GetRenderPassCountEXT();
        cost.submits = backend.GetQueueSubmitCountEXT();
        cost.coloredPipelines = backend.GetColoredPipelineCacheSizeEXT();
        cost.instancedPipelines = backend.GetInstancedPipelineCacheSizeEXT();
        return NativeCost(backend, cost);
    }

    static Cost Delta(const Cost& before, const Cost& after)
    {
        Cost d;
        d.passes = after.passes - before.passes;
        d.submits = after.submits - before.submits;
        d.coloredPipelines = after.coloredPipelines - before.coloredPipelines;
        d.instancedPipelines = after.instancedPipelines - before.instancedPipelines;
        d.nativeBuffers = after.nativeBuffers - before.nativeBuffers;
        d.nativePipelines = after.nativePipelines - before.nativePipelines;
        d.nativeCommandBuffers = after.nativeCommandBuffers - before.nativeCommandBuffers;
        return d;
    }

    static void Print(const char* label, const Cost& d)
    {
        std::printf("[ MEASURE  ] REMED-GFX-211/213 WebGPU %s: passes=%zu submits=%zu "
                    "coloredPipelines=+%zu instancedPipelines=+%zu nativeBuffers=%zu "
                    "nativePipelines=+%zu nativeCommandBuffers=%zu\n",
                    label, d.passes, d.submits, d.coloredPipelines, d.instancedPipelines,
                    d.nativeBuffers, d.nativePipelines, d.nativeCommandBuffers);
        std::fflush(stdout);
    }

    /// One render-target cycle of @p drawCount instanced draws issued under the given binding
    /// state, and everything it cost. The baseline is taken INSIDE the cycle: binding a target
    /// flushes whatever was pending, which is not what this file is measuring.
    Cost CycleCost(GraphicsDevice& dev, WebGPUGraphicsBackend& backend,
                   int perVertexOffset, int instanceOffset, int frequency,
                   int instanceCount, int drawCount)
    {
        auto rt = MakeTarget(dev);
        dev.SetRenderTarget(rt.get());
        const Cost before = Snapshot(backend);

        dev.Clear(kBlack);
        dev.SetVertexBuffers({
            VertexBufferBinding(mesh_.get(), perVertexOffset, 0),
            VertexBufferBinding(matrices_.get(), instanceOffset, frequency),
        });
        dev.SetIndexBuffer(indices_.get());
        for (int draw = 0; draw < drawCount; ++draw)
        {
            fx_->Apply();
            dev.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, 3, 0, 1, instanceCount);
        }
        dev.SetVertexBuffers({});
        dev.SetIndexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        return Delta(before, Snapshot(backend));
    }

    void Draw(const GameTime&) override
    {
        if (phase_ == 0) { phase_ = 1; return; }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        auto& dev = getGraphicsDeviceProperty();
        auto& backend = static_cast<WebGPUGraphicsBackend&>(dev.GetBackend());
        std::printf("REMED-GFX-211/213 instanced offset+frequency cardinality -- WEBGPU\n");
#ifndef CNA_HAVE_WGPU_NATIVE_REPORT
        std::printf("NOTE: wgpu.h was not found, so native live-object counts read 0 and only the "
                    "CNA-side counters below are decisive.\n");
#endif
        const std::size_t errors0 = backend.GetUncapturedErrorCountEXT();

        // Warm-up: builds the one Instanced3D pipeline variant these draws need and gets the frame
        // past its first swapchain acquisition, so every delta below measures the sequence under
        // test rather than one-off setup.
        CycleCost(dev, backend, 0, 0, 1, 4, 4);

        // ---- C1: the same frame at frequency 1 and at frequency 2 ----
        const Cost fourAtOne = CycleCost(dev, backend, 0, 0, 1, 6, 4);
        const Cost fourAtTwo = CycleCost(dev, backend, 0, 0, 2, 6, 4);
        Print("4 draws x 6 instances @frequency=1", fourAtOne);
        Print("4 draws x 6 instances @frequency=2", fourAtTwo);

        checkCount(fourAtTwo.passes, fourAtOne.passes,
                   "C1 frequency 2 render passes match frequency 1");
        checkCount(fourAtTwo.submits, fourAtOne.submits,
                   "C1 frequency 2 queue submits match frequency 1");
        checkCount(fourAtTwo.nativeBuffers, fourAtOne.nativeBuffers,
                   "C1 frequency 2 left the same live native buffer count as frequency 1");
        checkCount(fourAtTwo.nativeCommandBuffers, fourAtOne.nativeCommandBuffers,
                   "C1 frequency 2 native command buffers match frequency 1");
        checkCount(fourAtTwo.instancedPipelines, 0,
                   "C1 frequency 2 built no new Instanced3D pipeline variant");
        checkCount(fourAtTwo.nativePipelines, 0,
                   "C1 frequency 2 built no new native render pipeline");

        // ---- C2: the per-draw cost itself, isolated from the fixed cost of a cycle ----
        const Cost eightAtOne = CycleCost(dev, backend, 0, 0, 1, 6, 8);
        const Cost eightAtTwo = CycleCost(dev, backend, 0, 0, 2, 6, 8);
        Print("8 draws x 6 instances @frequency=1", eightAtOne);
        Print("8 draws x 6 instances @frequency=2", eightAtTwo);

        checkCount(eightAtOne.nativeBuffers - fourAtOne.nativeBuffers,
                   eightAtTwo.nativeBuffers - fourAtTwo.nativeBuffers,
                   "C2 four more public draws left the same live native buffer count at either frequency");
        checkCount(eightAtTwo.passes, fourAtTwo.passes,
                   "C2 doubling the draws in one cycle opened no extra render pass");
        checkCount(eightAtTwo.submits, fourAtTwo.submits,
                   "C2 doubling the draws in one cycle cost no extra queue submit");

        // ---- C3: the divisor must not allocate per LOGICAL instance ----
        // Twelve instances at frequency 2 consume six source records and produce twelve
        // destination ones. If either count reached a GPU allocation, this cycle would cost more
        // native buffers than the six-instance one it is compared against.
        const Cost twelveAtTwo = CycleCost(dev, backend, 0, 0, 2, 12, 4);
        Print("4 draws x 12 instances @frequency=2", twelveAtTwo);
        checkCount(twelveAtTwo.nativeBuffers, fourAtTwo.nativeBuffers,
                   "C3 doubling the instance count left no additional live native buffer");
        checkCount(twelveAtTwo.instancedPipelines, 0,
                   "C3 doubling the instance count built no new pipeline variant");

        // ---- C4: neither binding's VertexOffset is part of any cache key ----
        const Cost offsets = CycleCost(dev, backend, 3, 5, 1, 6, 4);
        const Cost offsetsAndFrequency = CycleCost(dev, backend, 6, 2, 3, 6, 4);
        Print("4 draws, offsets (3,5) @frequency=1", offsets);
        Print("4 draws, offsets (6,2) @frequency=3", offsetsAndFrequency);

        checkCount(offsets.instancedPipelines, 0,
                   "C4 nonzero VertexOffsets built no new Instanced3D pipeline variant");
        checkCount(offsetsAndFrequency.instancedPipelines, 0,
                   "C4 nonzero VertexOffsets at frequency 3 built no new pipeline variant");
        checkCount(offsets.nativePipelines, 0,
                   "C4 nonzero VertexOffsets built no new native render pipeline");
        checkCount(offsetsAndFrequency.nativePipelines, 0,
                   "C4 offsets at frequency 3 built no new native render pipeline");
        checkCount(offsets.passes, fourAtOne.passes,
                   "C4 nonzero VertexOffsets opened the same number of render passes");
        checkCount(offsets.submits, fourAtOne.submits,
                   "C4 nonzero VertexOffsets cost the same number of queue submits");
        checkCount(offsetsAndFrequency.nativeBuffers, fourAtOne.nativeBuffers,
                   "C4 offsets plus frequency 3 left the same live native buffer count");

        // ---- C5: repeating the whole sequence must not grow anything ----
        const Cost repeat = CycleCost(dev, backend, 3, 5, 1, 6, 4);
        Print("4 draws, offsets (3,5) @frequency=1 (repeat)", repeat);
        checkCount(repeat.instancedPipelines, 0,
                   "C5 repeating an offset sequence built no further pipeline variant");
        checkCount(repeat.nativeBuffers, offsets.nativeBuffers,
                   "C5 repeating an offset sequence left the same live native buffer count");
        checkCount(repeat.passes, offsets.passes,
                   "C5 repeating an offset sequence opened the same render passes");

        // ---- C6: WebGPU validation stayed silent through every offset and frequency ----
        const std::size_t errors = backend.GetUncapturedErrorCountEXT() - errors0;
        check(errors == 0, errors == 0
                  ? "C6 WebGPU validation stayed silent through every offset and frequency"
                  : "C6 WebGPU reported " + std::to_string(errors) + " uncaptured errors");

        std::printf("%d/%d checks passed on WEBGPU\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();

        std::vector<VertexPositionColor> mesh;
        mesh.reserve(kVertexCount);
        for (int i = 0; i < kVertexCount; ++i)
        {
            const float x = -0.9f + 0.1f * static_cast<float>(i);
            mesh.push_back(VertexPositionColor(Vector3(x, -0.9f, 0.5f), Color(255, 0, 0, 255)));
        }
        mesh_ = std::make_unique<VertexBuffer>(
            dev, VertexPositionColor::getVertexDeclarationStatic(), kVertexCount,
            BufferUsage::None);
        mesh_->SetData(mesh.data(), kVertexCount);

        std::vector<InstanceMatrix> records(kInstanceRecords, Identity());
        matrices_ = std::make_unique<VertexBuffer>(
            dev, InstanceMatrixDeclaration(), kInstanceRecords, BufferUsage::None);
        matrices_->SetDataRaw(records.data(), kInstanceRecords,
                              static_cast<int>(sizeof(InstanceMatrix)));

        std::vector<std::uint16_t> ids(kVertexCount);
        for (int i = 0; i < kVertexCount; ++i)
            ids[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(i);
        indices_ = std::make_unique<IndexBuffer>(
            dev, IndexElementSize::SixteenBits, kVertexCount, BufferUsage::None);
        indices_->SetData(ids.data(), kVertexCount);

        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->setLightingEnabledProperty(false);
        fx_->setTextureEnabledProperty(false);
        fx_->VertexColorEnabled = true;

        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    WebGpuInstancedOffsetFrequencyCardinalityTest test;
    test.Run();
    return test.Result();
}
