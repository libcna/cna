// SPDX-License-Identifier: MS-PL
// REMED-GFX-212, structural half: making the WebGPU instanced route honour
// `BasicEffect.VertexColorEnabled` must cost nothing a caller can observe.
//
// The pixel fixture (tests/Microsoft/Xna/Framework/Graphics/InstancedVertexColorTests.cpp) proves
// WHICH colour each route produces. It cannot see what the correction cost, and several
// wrong-but-passing implementations exist:
//
//   * put VertexColorEnabled in the render-pipeline cache key -> one variant per setting, rebuilt
//                                                                on every toggle;
//   * let a position-only and a position+colour declaration share a key -> the wrong layout;
//   * build a fresh pipeline or shader module per draw        -> unbounded growth;
//   * expand the colour with a second draw, pass or submit    -> extra passes and submits.
//
// CNA-side counts come from the backend's own cumulative EXT counters and native counts from
// wgpu-native's `wgpuGenerateReport()` live-object registry, before and after a known public
// sequence, so each expectation is an exact number rather than a trend.
//
// What the native counts do and do not measure, stated because a boundary that overclaims is worse
// than none: this backend releases a queued draw's transient buffers inside the same flush that
// creates them, so a delta taken across a whole render-target cycle is a NET LIVE count, not a
// created count. `nativePipelines` is the one that matters here and is genuinely cumulative for
// this file's purpose -- a render pipeline is retained by the cache for the device's lifetime.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
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
#include "CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp"

#if __has_include(<webgpu/wgpu.h>)
#include <webgpu/wgpu.h>
#define CNA_HAVE_WGPU_NATIVE_REPORT 1
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::WebGPU::WebGPUGraphicsBackend;

namespace
{
    constexpr int kRT = 64;
    constexpr int kVertexCount = 4;
    constexpr int kIndexCount = 6;
    constexpr int kPrimitiveCount = 2;

    struct PackedVertex
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(PackedVertex) == 16);

    struct PackedTexVertex
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
        float u, v;
    };
    static_assert(sizeof(PackedTexVertex) == 24);

    VertexDeclaration PackedDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    VertexDeclaration PackedTexDeclaration()
    {
        return VertexDeclaration(
            24,
            {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                VertexElement(16, VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
            });
    }

    VertexDeclaration MatrixDeclaration()
    {
        return VertexDeclaration(
            64,
            {
                VertexElement(0, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 1),
                VertexElement(16, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 2),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 3),
                VertexElement(48, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 4),
            });
    }

    struct MatrixRecord
    {
        std::array<float, 16> m;
    };

    MatrixRecord Identity()
    {
        return MatrixRecord{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    }

    struct Cost
    {
        std::size_t passes = 0;
        std::size_t submits = 0;
        std::size_t instancedPipelines = 0;
        std::size_t nativePipelines = 0;
        std::size_t uncapturedErrors = 0;
    };

    Cost Snapshot(WebGPUGraphicsBackend& backend)
    {
        Cost cost;
        cost.passes = backend.GetRenderPassCountEXT();
        cost.submits = backend.GetQueueSubmitCountEXT();
        cost.instancedPipelines = backend.GetInstancedPipelineCacheSizeEXT();
        cost.uncapturedErrors = backend.GetUncapturedErrorCountEXT();
#ifdef CNA_HAVE_WGPU_NATIVE_REPORT
        WGPUGlobalReport report{};
        wgpuGenerateReport(backend.Instance(), &report);
        cost.nativePipelines = report.hub.renderPipelines.numAllocated;
#endif
        return cost;
    }

    Cost Delta(const Cost& before, const Cost& after)
    {
        return Cost{after.passes - before.passes,
                    after.submits - before.submits,
                    after.instancedPipelines - before.instancedPipelines,
                    after.nativePipelines - before.nativePipelines,
                    after.uncapturedErrors - before.uncapturedErrors};
    }

    class WebGpuInstancedVertexColorCardinalityTest
    {
    public:
        void Run()
        {
            GraphicsDevice dev;
            if (!dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
            {
                std::printf("SKIP: backend does not support 3D rendering\n");
                return;
            }
            auto& backend = static_cast<WebGPUGraphicsBackend&>(dev.GetBackend());

            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.setBlendStateProperty(BlendState::Opaque);

            const std::array<PackedVertex, kVertexCount> quad16{
                PackedVertex{-0.5f, 0.5f, 0.5f, 255, 128, 64, 255},
                PackedVertex{0.5f, 0.5f, 0.5f, 255, 128, 64, 255},
                PackedVertex{0.5f, -0.5f, 0.5f, 255, 128, 64, 255},
                PackedVertex{-0.5f, -0.5f, 0.5f, 255, 128, 64, 255}};
            const std::array<PackedTexVertex, kVertexCount> quad24{
                PackedTexVertex{-0.5f, 0.5f, 0.5f, 255, 128, 64, 255, 0.f, 0.f},
                PackedTexVertex{0.5f, 0.5f, 0.5f, 255, 128, 64, 255, 1.f, 0.f},
                PackedTexVertex{0.5f, -0.5f, 0.5f, 255, 128, 64, 255, 1.f, 1.f},
                PackedTexVertex{-0.5f, -0.5f, 0.5f, 255, 128, 64, 255, 0.f, 1.f}};
            const std::array<std::uint16_t, kIndexCount> indices{0, 1, 2, 0, 2, 3};
            const std::array<MatrixRecord, 1> instances{Identity()};

            VertexBuffer vb16(dev, PackedDeclaration(), kVertexCount, BufferUsage::None);
            vb16.SetDataRaw(quad16.data(), kVertexCount, 16);
            VertexBuffer vb24(dev, PackedTexDeclaration(), kVertexCount, BufferUsage::None);
            vb24.SetDataRaw(quad24.data(), kVertexCount, 24);
            VertexBuffer instanceVb(dev, MatrixDeclaration(), 1, BufferUsage::None);
            instanceVb.SetDataRaw(instances.data(), 1, 64);
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, kIndexCount, BufferUsage::None);
            ib.SetData(indices.data(), kIndexCount);
            dev.SetIndexBuffer(&ib);

            BasicEffect effect(dev);

            const auto cycle = [&](VertexBuffer& vb, int draws, bool vertexColorEnabled,
                                   bool alternate) {
                RenderTarget2D target(dev, kRT, kRT, false, SurfaceFormat::Color,
                                      DepthFormat::Depth24Stencil8, 0,
                                      RenderTargetUsage::DiscardContents);
                dev.SetVertexBuffers({VertexBufferBinding(&vb, 0, 0),
                                      VertexBufferBinding(&instanceVb, 0, 1)});
                dev.SetRenderTarget(&target);
                dev.Clear(Color::Black);
                for (int i = 0; i < draws; ++i)
                {
                    effect.VertexColorEnabled =
                        alternate ? ((i % 2) == 0 ? vertexColorEnabled : !vertexColorEnabled)
                                  : vertexColorEnabled;
                    effect.setLightingEnabledProperty(false);
                    effect.setTextureEnabledProperty(false);
                    effect.setFogEnabledProperty(false);
                    effect.setDiffuseColorProperty(Vector3(0.5f, 1.0f, 0.25f));
                    effect.setAlphaProperty(1.0f);
                    effect.Apply();
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, kVertexCount, 0,
                                                kPrimitiveCount, 1);
                }
                dev.SetRenderTarget(nullptr);
            };

            const auto measure = [&](const char* label, auto&& body) {
                const Cost before = Snapshot(backend);
                body();
                const Cost d = Delta(before, Snapshot(backend));
                std::printf("  %-46s passes=+%zu submits=+%zu instancedPipelines=+%zu "
                            "nativePipelines=+%zu uncapturedErrors=+%zu\n",
                            label, d.passes, d.submits, d.instancedPipelines, d.nativePipelines,
                            d.uncapturedErrors);
                std::fflush(stdout);
                return d;
            };

            std::printf("REMED-GFX-212 WebGPU instanced vertex-colour cardinality\n");

            // Warm-up, deliberately through the ORDINARY route so the Instanced3D cache is still
            // cold when the first measured cycle runs. The very first render-target cycle after
            // device creation also flushes the device's own initial backbuffer pass, which is
            // device start-up cost and not what any sequence below costs; drawing one ordinary
            // quad first moves it out of the measured window without touching instancedPipelines_.
            {
                RenderTarget2D warmup(dev, kRT, kRT, false, SurfaceFormat::Color,
                                      DepthFormat::Depth24Stencil8, 0,
                                      RenderTargetUsage::DiscardContents);
                dev.SetVertexBuffers({VertexBufferBinding(&vb16, 0, 0)});
                dev.SetRenderTarget(&warmup);
                dev.Clear(Color::Black);
                effect.VertexColorEnabled = true;
                effect.setLightingEnabledProperty(false);
                effect.setTextureEnabledProperty(false);
                effect.setFogEnabledProperty(false);
                effect.setDiffuseColorProperty(Vector3(0.5f, 1.0f, 0.25f));
                effect.setAlphaProperty(1.0f);
                effect.Apply();
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, kVertexCount, 0,
                                          kPrimitiveCount);
                dev.SetRenderTarget(nullptr);
            }
            Check("the warm-up ordinary draw built no Instanced3D variant",
                  backend.GetInstancedPipelineCacheSizeEXT() == 0);

            const Cost first = measure("1 draw, VertexColorEnabled=false (cold)",
                                       [&] { cycle(vb16, 1, false, false); });
            Check("cold stride-16 draw builds exactly 1 Instanced3D variant",
                  first.instancedPipelines == 1);
            Check("a 1-draw render-target cycle costs exactly 1 render pass", first.passes == 1);
            Check("a 1-draw render-target cycle costs exactly 1 queue submit", first.submits == 1);

            const Cost toggled = measure("1 draw, VertexColorEnabled=true (warm)",
                                         [&] { cycle(vb16, 1, true, false); });
            Check("toggling VertexColorEnabled builds NO new Instanced3D variant",
                  toggled.instancedPipelines == 0);
            Check("toggling VertexColorEnabled builds NO new native render pipeline",
                  toggled.nativePipelines == 0);
            Check("toggling VertexColorEnabled adds no pass", toggled.passes == 1);
            Check("toggling VertexColorEnabled adds no submit", toggled.submits == 1);

            const Cost alternating = measure("4 draws, alternating false/true in one cycle",
                                             [&] { cycle(vb16, 4, false, true); });
            Check("alternating VertexColorEnabled builds no variant",
                  alternating.instancedPipelines == 0);
            Check("four draws in one cycle still cost exactly 1 pass", alternating.passes == 1);
            Check("four draws in one cycle still cost exactly 1 submit", alternating.submits == 1);

            const Cost stride24 = measure("1 draw, stride 24 (cold)",
                                          [&] { cycle(vb24, 1, true, false); });
            Check("a stride-24 declaration builds its OWN Instanced3D variant",
                  stride24.instancedPipelines == 1);

            const Cost repeat = measure("repeat every sequence above", [&] {
                cycle(vb16, 1, false, false);
                cycle(vb16, 1, true, false);
                cycle(vb16, 4, false, true);
                cycle(vb24, 1, true, false);
            });
            Check("repeating every sequence builds no further variant",
                  repeat.instancedPipelines == 0);
            Check("repeating every sequence builds no further native render pipeline",
                  repeat.nativePipelines == 0);
            Check("repeating four cycles costs exactly four passes", repeat.passes == 4);
            Check("repeating four cycles costs exactly four submits", repeat.submits == 4);

            Check("the whole run built exactly 2 Instanced3D variants",
                  backend.GetInstancedPipelineCacheSizeEXT() == 2);
            Check("WebGPU validation stayed silent across the whole matrix",
                  backend.GetUncapturedErrorCountEXT() == 0);

            std::printf("%d/%d checks passed\n", passed_, passed_ + failed_);
        }

        [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }

    private:
        void Check(const char* what, bool ok)
        {
            std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
            if (ok)
                ++passed_;
            else
                ++failed_;
        }

        int passed_ = 0;
        int failed_ = 0;
    };
}   // namespace

int main()
{
    WebGpuInstancedVertexColorCardinalityTest test;
    test.Run();
    return test.Result();
}
