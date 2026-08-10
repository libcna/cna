// SPDX-License-Identifier: MS-PL
// REMED-GFX-212, structural half: making the Vulkan instanced route honour
// `BasicEffect.VertexColorEnabled` must cost nothing a caller can observe.
//
// The pixel fixture (tests/Microsoft/Xna/Framework/Graphics/InstancedVertexColorTests.cpp) proves
// WHICH colour each route produces. It cannot see what the correction cost, and several
// wrong-but-passing implementations exist:
//
//   * put VertexColorEnabled in the pipeline cache key   -> one pipeline variant per setting,
//                                                           and a rebuild on every toggle;
//   * key the geometry layout on MakeExt3DKey's bucket   -> a position-only declaration and a
//     rather than the raw stride                            position+colour one share a pipeline;
//   * build a fresh pipeline per draw                    -> unbounded cache growth;
//   * expand the colour with a second draw or submit     -> extra submits/frames/waits.
//
// Every count below is read from the live renderer's own cumulative EXT counters before and after a
// known public sequence, so each expectation is an exact number rather than a trend.
//
// WHAT FLUSHES, stated because a boundary that overclaims is worse than none: this renderer defers
// its 3D draws and `SetRenderTarget(nullptr)` only closes the segment -- nothing is submitted until
// something demands the result. Every cycle below therefore ends with the SAME one-pixel
// `RenderTarget2D::GetData`, which is the harness's flush trigger and not part of what any sequence
// costs: it appears identically in all of them, so a delta between two sequences measures only what
// the sequence itself added. That is why this file asserts equalities BETWEEN sequences (a
// VertexColorEnabled=true cycle costs exactly what a false one costs; four draws in a cycle cost
// what one does) rather than claiming an absolute "no readback".
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
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
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kRT = 64;

    /// One quad: four vertices, six indices, two triangles. The geometry is irrelevant here --
    /// only the cardinality is.
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

    /// Everything one measured sequence cost.
    struct Cost
    {
        std::size_t instancedPipelines = 0;
        std::size_t allPipelines = 0;
        std::uint64_t frameSubmits = 0;
        std::uint64_t fenceWaits = 0;
        std::uint64_t presents = 0;
    };

    Cost operator-(const Cost& after, const Cost& before)
    {
        return Cost{after.instancedPipelines - before.instancedPipelines,
                    after.allPipelines - before.allPipelines,
                    after.frameSubmits - before.frameSubmits,
                    after.fenceWaits - before.fenceWaits,
                    after.presents - before.presents};
    }

    class VulkanInstancedVertexColorCardinalityTest
    {
    public:
        void Run()
        {
            GraphicsDevice dev;
            if (!dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
            {
                std::printf("SKIP: renderer does not support 3D rendering\n");
                return;
            }
            auto& renderer = static_cast<VulkanRenderer&>(dev.GetRenderer());

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

            const auto snapshot = [&renderer] {
                Cost cost;
                cost.instancedPipelines = renderer.GetInstancedPipelineCacheSizeEXT();
                cost.allPipelines = renderer.GetGraphicsPipelineCacheEntryCountEXT();
                cost.frameSubmits = renderer.GetFrameSubmitCountEXT();
                cost.fenceWaits = renderer.GetFrameFenceWaitCountEXT();
                cost.presents = renderer.GetPresentCountEXT();
                return cost;
            };

            /// One render-target cycle drawing @p draws instanced quads from @p vb, toggling
            /// VertexColorEnabled per draw when @p alternate is set.
            const auto cycle = [&](VertexBuffer& vb, int draws, bool vertexColorEnabled,
                                   bool alternate) {
                RenderTarget2D target(dev, kRT, kRT, false, SurfaceFormat::Color,
                                      DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
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
                // The flush trigger -- see this file's header. One pixel, identical in every
                // sequence, so it cancels out of every delta this file compares.
                Color pixel = Color::Black;
                const Rectangle region(0, 0, 1, 1);
                target.GetData(0, &region, &pixel, 0, 1);
            };

            const auto measure = [&](const char* label, auto&& body) {
                const Cost before = snapshot();
                body();
                const Cost delta = snapshot() - before;
                std::printf(
                    "  %-46s instancedPipelines=+%zu allPipelines=+%zu submits=+%llu waits=+%llu "
                    "presents=+%llu\n",
                    label, delta.instancedPipelines, delta.allPipelines,
                    static_cast<unsigned long long>(delta.frameSubmits),
                    static_cast<unsigned long long>(delta.fenceWaits),
                    static_cast<unsigned long long>(delta.presents));
                return delta;
            };

            std::printf("REMED-GFX-212 Vulkan instanced vertex-colour cardinality\n");

            // 1. The first stride-16 instanced draw builds exactly one Instanced3D variant.
            const Cost first =
                measure("1 draw, VertexColorEnabled=false (cold)",
                        [&] { cycle(vb16, 1, false, false); });
            Check("cold stride-16 draw builds exactly 1 Instanced3D variant",
                  first.instancedPipelines == 1);
            Check("a render-target cycle presents nothing", first.presents == 0);

            // 2. The SAME draw with VertexColorEnabled=true must reuse it. This is the whole
            //    structural claim: the setting travels in the push constant, not the key.
            const Cost toggled = measure("1 draw, VertexColorEnabled=true (warm)",
                                         [&] { cycle(vb16, 1, true, false); });
            Check("toggling VertexColorEnabled builds NO new Instanced3D variant",
                  toggled.instancedPipelines == 0);
            Check("toggling VertexColorEnabled builds NO new pipeline at all",
                  toggled.allPipelines == 0);
            Check("toggling VertexColorEnabled costs exactly what leaving it off costs",
                  toggled.frameSubmits == first.frameSubmits &&
                      toggled.fenceWaits == first.fenceWaits &&
                      toggled.presents == first.presents);

            // 3. Alternating the setting four times inside one cycle likewise.
            const Cost alternating = measure("4 draws, alternating false/true in one cycle",
                                             [&] { cycle(vb16, 4, false, true); });
            Check("alternating VertexColorEnabled builds no variant",
                  alternating.instancedPipelines == 0);
            Check("four draws in one cycle cost no extra submit than one draw does",
                  alternating.frameSubmits == first.frameSubmits);
            Check("four draws in one cycle cost no extra queue wait than one draw does",
                  alternating.fenceWaits == first.fenceWaits);

            // 4. A stride-24 declaration must NOT reuse the stride-16 pipeline -- its vertex input
            //    layout differs (arrayStride and the colour element's offset are the same, but the
            //    binding stride is not), which is what FoldPerVertexStrideIntoKey guarantees.
            const Cost stride24 = measure("1 draw, stride 24 (cold)",
                                          [&] { cycle(vb24, 1, true, false); });
            Check("a stride-24 declaration builds its OWN Instanced3D variant",
                  stride24.instancedPipelines == 1);

            // 5. Repeating everything must add nothing: the cache is bounded by the real state
            //    dimensions, not by draw count.
            const Cost repeat = measure("repeat every sequence above", [&] {
                cycle(vb16, 1, false, false);
                cycle(vb16, 1, true, false);
                cycle(vb16, 4, false, true);
                cycle(vb24, 1, true, false);
            });
            Check("repeating every sequence builds no further variant",
                  repeat.instancedPipelines == 0);
            Check("repeating every sequence builds no further pipeline at all",
                  repeat.allPipelines == 0);
            Check("repeating every sequence presents nothing", repeat.presents == 0);
            Check("repeating a sequence costs exactly what it cost the first time",
                  repeat.frameSubmits == first.frameSubmits + toggled.frameSubmits +
                                         alternating.frameSubmits + stride24.frameSubmits);

            // 6. Two distinct declarations, two variants, and nothing else in the whole run.
            Check("the whole run built exactly 2 Instanced3D variants",
                  renderer.GetInstancedPipelineCacheSizeEXT() == 2);

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
    VulkanInstancedVertexColorCardinalityTest test;
    test.Run();
    return test.Result();
}
