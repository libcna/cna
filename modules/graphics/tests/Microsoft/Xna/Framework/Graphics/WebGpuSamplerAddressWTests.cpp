// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-160 -- `SamplerState.AddressW` reaches the native sampler.
//
// WHY THIS TEST COUNTS SAMPLERS INSTEAD OF READING PIXELS. `AddressW` is the third addressing axis:
// it is observable in output only where a renderer samples a VOLUME texture, and this renderer has
// no public route that does -- `IEffectRenderer::BindTexture3D` is unimplemented on WebGPU, and
// extending `ShaderEffect`'s volume binding is explicitly outside this phase (see the plan's
// out-of-scope table). So the row's own "three modes, three different readbacks" cannot be met
// here yet, and `WEBGPU-160` stays 🟨 for exactly that reason rather than being called done.
//
// What CAN be measured is that the state now arrives: `GetOrCreateSlotSampler` builds its cache key
// and its `WGPUSamplerDescriptor` from `addressW` as well as U and V, so three draws differing ONLY
// in `AddressW` must produce three distinct native samplers. Before the fix they produced one --
// the value was in neither the key nor the descriptor, and `addressModeW` was hardcoded to
// `ClampToEdge`. That is a real, discriminating measurement of the plumbing at the level the
// plumbing lives, and it fails if the state is dropped again.
//
// It also guards the thing the fix could plausibly have broken: the 32-bit key was exactly full
// (filter, U, V, anisotropy, one byte each), so folding a fifth field into it would have ALIASED
// two genuinely different samplers onto one entry. The repeat leg below is what would catch that.

#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace
{
    using CNA::Internal::Renderers::WebGPU::WebGPURenderer;
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    constexpr int kSize = 32;

    WebGPURenderer& RendererOf(GraphicsDevice& device)
    {
        auto* renderer = dynamic_cast<WebGPURenderer*>(&device.GetRenderer());
        if (renderer == nullptr)
            throw std::runtime_error("CNA_RENDERER_WEBGPU build has no WebGPURenderer");
        return *renderer;
    }

    /// One textured draw, which is what actually makes the renderer materialise a slot sampler.
    /// The pixels are irrelevant here -- the measurement is the cache -- but the draw has to be a
    /// real one, because a sampler that is never requested is never created.
    void DrawTexturedQuad(GraphicsDevice& device, Texture2D& texture)
    {
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        struct Vertex { float x, y, z; float u, v; };
        // Triangle-STRIP order (TL, BL, TR, BR), not ring order: a strip fed a ring draws a
        // different second triangle. The pixels are not asserted here, but a half-covered quad
        // would still be a misleading thing to leave in a test.
        const std::array<Vertex, 4> verts{
            Vertex{-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
            Vertex{-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            Vertex{ 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
            Vertex{ 1.0f, -1.0f, 0.0f, 1.0f, 1.0f}};
        VertexBuffer vb(device,
                        VertexDeclaration(20,
                            {VertexElement(0, VertexElementFormat::Vector3,
                                           VertexElementUsage::Position, 0),
                             VertexElement(12, VertexElementFormat::Vector2,
                                           VertexElementUsage::TextureCoordinate, 0)}),
                        static_cast<int>(verts.size()), BufferUsage::None);
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), 20);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);

        device.setBlendStateProperty(BlendState::Opaque);
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        effect.Apply();
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        // The readback is what forces the deferred frame to be replayed, which is where the
        // sampler is actually created.
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
    }

    /// A SamplerState identical in every respect but the W axis.
    SamplerState WithAddressW(TextureAddressMode w)
    {
        SamplerState state;
        state.setFilterProperty(TextureFilter::Point);
        state.setAddressUProperty(TextureAddressMode::Clamp);
        state.setAddressVProperty(TextureAddressMode::Clamp);
        state.setAddressWProperty(w);
        return state;
    }
}

// ---------------------------------------------------------------------------
// Three draws differing ONLY in AddressW must create three distinct native samplers. Before
// WEBGPU-160 they created one: the value reached neither the cache key nor the descriptor.
// ---------------------------------------------------------------------------
TEST(WebGpuSamplerAddressW, EachAddressWModeProducesItsOwnNativeSampler)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device;
    WebGPURenderer& renderer = RendererOf(device);
    Texture2D texture = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});

    const std::array<TextureAddressMode, 3> modes{
        TextureAddressMode::Wrap, TextureAddressMode::Clamp, TextureAddressMode::Mirror};

    const std::size_t before = renderer.GetSlotSamplerCacheSizeEXT();
    for (const TextureAddressMode mode : modes)
    {
        device.getSamplerStatesProperty()[0] = WithAddressW(mode);
        DrawTexturedQuad(device, texture);
    }
    const std::size_t afterThree = renderer.GetSlotSamplerCacheSizeEXT();
    std::cout << "[WEBGPU-160] slot sampler cache: " << before << " -> " << afterThree
              << " after Wrap/Clamp/Mirror in W" << std::endl;

    EXPECT_EQ(3u, afterThree - before)
        << "three SamplerStates differing only in AddressW produced "
        << (afterThree - before) << " native samplers instead of 3 -- AddressW is missing from the "
           "cache key, the descriptor, or both, and the value is being discarded again";

    // The repeat leg. The 32-bit key was exactly full before this task, so a fifth field folded
    // into it would have aliased two different samplers onto one entry -- which this catches, and
    // which the count above would not.
    for (const TextureAddressMode mode : modes)
    {
        device.getSamplerStatesProperty()[0] = WithAddressW(mode);
        DrawTexturedQuad(device, texture);
    }
    const std::size_t afterRepeat = renderer.GetSlotSamplerCacheSizeEXT();
    std::cout << "[WEBGPU-160] slot sampler cache after repeating all three: " << afterRepeat
              << std::endl;
    EXPECT_EQ(afterThree, afterRepeat)
        << "repeating the same three SamplerStates created " << (afterRepeat - afterThree)
        << " further samplers -- the cache key is not stable for a repeated state";
}

// ---------------------------------------------------------------------------
// AddressW is carried INDEPENDENTLY of the other axes. IGraphicsRenderer splits ApplySamplerState
// and ApplySamplerAddressW into two calls precisely so a renderer can adopt the W axis on its own,
// and GraphicsDevice drives them separately -- so the surviving value must not depend on the order
// the two arrive in. A setter that reset W would make this test's second draw reuse the first's
// sampler.
// ---------------------------------------------------------------------------
TEST(WebGpuSamplerAddressW, ApplySamplerStateDoesNotResetTheWAxis)
{
    // plans/plan_runtimerenderer.md RTR-P9-9: this is WebGPU's own contract.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device;
    WebGPURenderer& renderer = RendererOf(device);
    Texture2D texture = Texture2D::CreateFromPixels(
        device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});

    // Establish Mirror in W, then change ONLY the filter through the other call. If W survived,
    // the second draw's sampler differs from the first's in filter alone and a third sampler is
    // never needed; if W were reset to Clamp, the two draws would differ in two fields.
    device.getSamplerStatesProperty()[0] = WithAddressW(TextureAddressMode::Mirror);
    DrawTexturedQuad(device, texture);
    const std::size_t afterMirror = renderer.GetSlotSamplerCacheSizeEXT();

    SamplerState linearMirror = WithAddressW(TextureAddressMode::Mirror);
    linearMirror.setFilterProperty(TextureFilter::Linear);
    device.getSamplerStatesProperty()[0] = linearMirror;
    DrawTexturedQuad(device, texture);
    const std::size_t afterLinear = renderer.GetSlotSamplerCacheSizeEXT();

    // Now go back to the FIRST state exactly. It must hit the cache rather than create a fourth
    // sampler, which it can only do if its W axis is still Mirror.
    device.getSamplerStatesProperty()[0] = WithAddressW(TextureAddressMode::Mirror);
    DrawTexturedQuad(device, texture);
    const std::size_t afterReturn = renderer.GetSlotSamplerCacheSizeEXT();

    std::cout << "[WEBGPU-160] cache after Mirror/Point=" << afterMirror
              << ", Mirror/Linear=" << afterLinear << ", back to Mirror/Point=" << afterReturn
              << std::endl;
    EXPECT_EQ(1u, afterLinear - afterMirror)
        << "changing only the filter created " << (afterLinear - afterMirror) << " samplers";
    EXPECT_EQ(afterLinear, afterReturn)
        << "returning to the first SamplerState created a new sampler -- ApplySamplerState reset "
           "the W axis, so the state that came back is not the state that left";
}

#endif  // CNA_RENDERER_WEBGPU
