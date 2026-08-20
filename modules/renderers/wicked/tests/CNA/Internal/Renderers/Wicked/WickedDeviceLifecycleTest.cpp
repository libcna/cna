// SPDX-License-Identifier: MS-PL
//
// plans/plan_wicked.md WICKED-78: GraphicsDevice teardown must leave no live GPU/VMA allocation.
//
// At the pinned Wicked Engine revision the Vulkan device destructor left its three null images
// alive, so VMA's "Some allocations were not freed" assertion aborted the whole process on any
// teardown that actually reached vmaDestroyAllocator -- which meant every shared test that
// constructs a device and never draws. A draw made teardown LOOK clean for the opposite reason:
// the engine's pool-allocated command lists were never freed, and the first per-frame linear
// allocation pinned the allocation handler (VmaAllocator, VkDevice, VkInstance) forever, hiding
// the assertion behind a whole-device leak. cmake/patches/wicked-device-teardown.patch releases
// both in the destructor.
//
// These tests are deliberately plain device lifecycles. The regression signal is the process
// surviving them: pre-fix, the first bare teardown aborts the binary in vk_mem_alloc.h. Each
// TEST constructs and destroys its own device, so the file as a whole is also the repeated
// create/destroy proof.

#include <array>
#include <cstring>
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

namespace
{
    struct V16
    {
        float px, py, pz;
        std::uint8_t r, g, b, a;
    };

    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16, {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                 VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
    }

    void DrawOneTriangle(GraphicsDevice& device)
    {
        constexpr int kSize = 32;
        RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        std::array<V16, 3> vertices{};
        vertices[0] = {-0.6f, -0.6f, 0.5f, 255, 255, 255, 255};
        vertices[1] = {0.6f, -0.6f, 0.5f, 255, 255, 255, 255};
        vertices[2] = {0.0f, 0.7f, 0.5f, 255, 255, 255, 255};
        VertexDeclaration declaration = PositionColorDeclaration();
        VertexBuffer buffer(device, declaration, 3, BufferUsage::None);
        buffer.SetDataRaw(vertices.data(), 3, 16);

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAlphaProperty(1.0f);

        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Transparent);
        const Rectangle region(0, 0, kSize, kSize);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
    }
}

/// The exact pre-fix aborting shape: a device that is constructed, renders nothing, and is
/// destroyed. Most shared tests have this shape, which is why the corpus could not run at all.
TEST(WickedDeviceLifecycleTest, BareDeviceTearsDownCleanly)
{
    GraphicsDevice device;
}

/// The second pre-fix aborting shape: one capability query, no draw.
TEST(WickedDeviceLifecycleTest, QueryOnlyDeviceTearsDownCleanly)
{
    GraphicsDevice device;
    EXPECT_TRUE(device.SupportsCapability(GraphicsCapability::ThreeD));
}

/// Back-to-back full device cycles inside one test body, on top of the per-test cycles the rest
/// of this file already performs.
TEST(WickedDeviceLifecycleTest, RepeatedCreateDestroyCyclesSurvive)
{
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        GraphicsDevice device;
        EXPECT_TRUE(device.SupportsCapability(GraphicsCapability::ThreeD)) << "cycle " << cycle;
    }
}

/// Disposing resources explicitly before the device goes away must be safe and idempotent.
TEST(WickedDeviceLifecycleTest, ExplicitDisposalBeforeDeviceTeardownIsSafe)
{
    GraphicsDevice device;
    {
        VertexDeclaration declaration = PositionColorDeclaration();
        VertexBuffer buffer(device, declaration, 4, BufferUsage::None);
        Texture2D texture(device, 8, 8);
        RenderTarget2D target(device, 16, 16, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        buffer.Dispose();
        texture.Dispose();
        target.Dispose();
        // A second Dispose must be a no-op, not a double release.
        buffer.Dispose();
        texture.Dispose();
        target.Dispose();
    }
}

/// The draw-then-teardown shape that pre-fix looked clean for the wrong reason (the engine's
/// pooled command lists pinned the allocator forever). Post-fix it must still exit cleanly --
/// now because teardown genuinely completes, which the sanitizer lane corroborates.
TEST(WickedDeviceLifecycleTest, DrawingDeviceTearsDownCleanly)
{
    GraphicsDevice device;
    DrawOneTriangle(device);
}

/// Public wrappers created per XNA's ownership contract -- resources destroyed before their
/// device, here by scope order -- across a device that both queried and drew.
TEST(WickedDeviceLifecycleTest, MixedWorkloadDeviceTearsDownCleanly)
{
    GraphicsDevice device;
    EXPECT_TRUE(device.SupportsCapability(GraphicsCapability::ThreeD));
    DrawOneTriangle(device);
    EXPECT_TRUE(device.SupportsCapability(GraphicsCapability::DepthStencilBuffer));
}
