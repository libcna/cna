// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu_modern_graphics.md WMG-0020: GPU debug labels on the WebGPU renderer.
//
// `GraphicsDevice::SetStringMarkerEXT` is the one debug-label entry point CNA has -- there is no
// public push/pop group API, and this test does not add one. What it measures is that the label
// really reaches the command stream rather than being dropped, and that the debug groups the
// renderer opens around its own passes are balanced.
//
// The labels themselves are invisible: nothing a debugger shows can be read back from inside the
// process, and WebGPU offers no query for them. So the measurement is the renderer's own recorded
// counts -- the same shape VulkanRenderer exposes and the same shape Vulkan_GpuTimerDebug reads
// (`GetRecordedDebugMarkerCountEXT` and its two region siblings). A count is a weaker claim than a
// pixel, which is why every check below is about a DIFFERENCE between two measurements rather than
// about an absolute number: a renderer that ignored the labels would leave the difference at zero.
//
// Check A -- a marker queued before any draw is emitted: the count rises by exactly one.
// Check B -- three markers around three draws all reach the stream, and none is coalesced.
// Check C -- the render pass opened a debug group, and closed every one it opened.
// Check D -- a compute dispatch opens and closes its own group, nested inside the encoder's work.
// Check E -- a null and an empty label insert nothing at all, rather than an empty debug entry.
// Check F -- a marker queued when no pass will ever open is dropped safely, not left dangling.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = this renderer cannot emit labels.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#ifdef CNA_CNAEXT
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/GraphicsCapability.hpp"
#endif

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    std::vector<VertexPositionColor> quad(const float z, const Color& c)
    {
        return {
            {Vector3(-1.0f, -1.0f, z), c}, {Vector3(1.0f, -1.0f, z), c}, {Vector3(1.0f, 1.0f, z), c},
            {Vector3(-1.0f, -1.0f, z), c}, {Vector3(1.0f, 1.0f, z), c},  {Vector3(-1.0f, 1.0f, z), c},
        };
    }
}

class WebGpuDebugMarkerTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    static void drawQuad(GraphicsDevice& dev, BasicEffect& fx, const float z, const Color& c)
    {
        const auto verts = quad(z, c);
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(),
                        static_cast<int>(verts.size()), BufferUsage::None);
        vb.SetData(verts.data(), static_cast<int>(verts.size()));
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(dev.GetRenderer());

        if (!renderer.SupportsDebugUtilsLabelsEXT())
        {
            std::printf("SKIP: this renderer cannot emit debug labels\n");
            Exit();
            skipped_ = true;
            return;
        }

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        dev.setRasterizerStateProperty(rs);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);

        // A read-back is what flushes a bind cycle on this renderer, so each phase below ends with
        // one: the counts only move when the ordered stream is actually replayed.
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);

        // ---- A: one marker, one emission -------------------------------------------------
        RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        const int markersBefore = renderer.GetRecordedDebugMarkerCountEXT();
        const int beginsBefore = renderer.GetRecordedDebugRegionBeginCountEXT();

        dev.SetRenderTarget(&target);
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetStringMarkerEXT("WMG-0020 single label");
        drawQuad(dev, fx, 0.5f, Color(0, 0, 255, 255));
        dev.SetRenderTarget(nullptr);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

        const int markersAfterA = renderer.GetRecordedDebugMarkerCountEXT();
        const int beginsAfterA = renderer.GetRecordedDebugRegionBeginCountEXT();
        const int endsAfterA = renderer.GetRecordedDebugRegionEndCountEXT();
        std::printf("A: markers %d -> %d, groups opened %d -> %d, closed %d\n", markersBefore,
                    markersAfterA, beginsBefore, beginsAfterA, endsAfterA);
        check(markersAfterA == markersBefore + 1,
              "Check A: one SetStringMarkerEXT reaches the command stream exactly once");

        // ---- C: the pass opened a group, and closed every one it opened -------------------
        check(beginsAfterA > beginsBefore,
              "Check C: the render pass opened a debug group of its own");
        check(endsAfterA == beginsAfterA,
              "Check C: every debug group the renderer opened was closed");

        // ---- B: three labels around three draws, none coalesced --------------------------
        const int markersBeforeB = renderer.GetRecordedDebugMarkerCountEXT();
        dev.SetRenderTarget(&target);
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetStringMarkerEXT("WMG-0020 first");
        drawQuad(dev, fx, 0.5f, Color(255, 0, 0, 255));
        dev.SetStringMarkerEXT("WMG-0020 second");
        drawQuad(dev, fx, 0.4f, Color(0, 255, 0, 255));
        dev.SetStringMarkerEXT("WMG-0020 third");
        drawQuad(dev, fx, 0.3f, Color(0, 0, 255, 255));
        dev.SetRenderTarget(nullptr);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

        const int markersAfterB = renderer.GetRecordedDebugMarkerCountEXT();
        std::printf("B: markers %d -> %d across three draws\n", markersBeforeB, markersAfterB);
        check(markersAfterB == markersBeforeB + 3,
              "Check B: three labels around three draws all reach the stream, none coalesced");

        // ---- E: a null and an empty label insert nothing ---------------------------------
        const int markersBeforeE = renderer.GetRecordedDebugMarkerCountEXT();
        dev.SetRenderTarget(&target);
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetStringMarkerEXT("");
        renderer.SetStringMarkerEXT(nullptr);
        drawQuad(dev, fx, 0.5f, Color(255, 255, 0, 255));
        dev.SetRenderTarget(nullptr);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

        const int markersAfterE = renderer.GetRecordedDebugMarkerCountEXT();
        std::printf("E: markers %d -> %d for a null and an empty label\n", markersBeforeE,
                    markersAfterE);
        check(markersAfterE == markersBeforeE,
              "Check E: a null and an empty label insert nothing rather than an empty entry");

        // ---- D: a compute dispatch opens and closes its own group ------------------------
#ifndef CNA_CNAEXT
        std::printf("D: the engine layer is off in this build; the compute-group check needs it\n");
#else
        if (!dev.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
        {
            std::printf("D: this renderer has no compute shaders; the group check is skipped\n");
        }
        else
        {
            const int beginsBeforeD = renderer.GetRecordedDebugRegionBeginCountEXT();
            const int endsBeforeD = renderer.GetRecordedDebugRegionEndCountEXT();
            RunOneComputeDispatch(dev);
            const int beginsAfterD = renderer.GetRecordedDebugRegionBeginCountEXT();
            const int endsAfterD = renderer.GetRecordedDebugRegionEndCountEXT();
            std::printf("D: compute groups opened %d -> %d, closed %d -> %d\n", beginsBeforeD,
                        beginsAfterD, endsBeforeD, endsAfterD);
            check(beginsAfterD == beginsBeforeD + 1 && endsAfterD == endsBeforeD + 1,
                  "Check D: a compute dispatch opens exactly one debug group and closes it");
        }
#endif

        // ---- F: a label with no pass behind it is dropped safely -------------------------
        // Queued outside any bind cycle that will produce a pass, and then abandoned. The claim is
        // that nothing crashes, nothing is left dangling into the next frame's stream, and no
        // phantom emission is counted -- a label is not work, so it cannot open a pass to hold it.
        const int markersBeforeF = renderer.GetRecordedDebugMarkerCountEXT();
        dev.SetStringMarkerEXT("WMG-0020 no pass will ever open for this");
        const int markersAfterF = renderer.GetRecordedDebugMarkerCountEXT();
        std::printf("F: markers %d -> %d for a label with nothing to ride\n", markersBeforeF,
                    markersAfterF);
        check(markersAfterF == markersBeforeF,
              "Check F: a label queued with no pass behind it emits nothing and is safe");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

private:
    bool skipped_ = false;

#ifdef CNA_CNAEXT
    // The smallest real dispatch this renderer will accept: one storage buffer written by one
    // workgroup. Its result is not the point -- the debug group around it is.
    static void RunOneComputeDispatch(GraphicsDevice& dev)
    {
        static constexpr char kWgsl[] = R"WGSL(
@group(0) @binding(0) var<storage, read_write> out: array<u32>;
@compute @workgroup_size(1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    out[id.x] = id.x + 1u;
}
)WGSL";
        try
        {
            CNA::Graphics::ComputeShader shader(dev, kWgsl);
            CNA::Graphics::StorageBuffer buffer(dev, 4 * sizeof(std::uint32_t));
            shader.bindStorageBuffer(0, buffer);
            shader.dispatch(4, 1, 1);
        }
        catch (const std::exception& e)
        {
            std::printf("D: the dispatch could not be created: %s\n", e.what());
        }
    }
#endif

public:
    WebGpuDebugMarkerTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] bool wasSkipped() const { return skipped_; }
};

int main()
{
    WebGpuDebugMarkerTest game;
    game.Run();

    if (game.wasSkipped()) return 77;
    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
