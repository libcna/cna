// SPDX-License-Identifier: MS-PL

// plans/plan_cabi.md CABI-28: coverage for the ContentLost contract on the default-pool resources.
//
// This file exists because the contract had none. CABI-15 made the event real -- CNA raises it on
// the renderers whose API can actually lose a device -- and nothing anywhere asserted a single part
// of it: not that the flag is set, not that the event fires, and not that the flag is ever cleared
// again. It was not, for render targets: `ClearContentLostEXT()` was declared and never called, so
// a target that lost its content reported "lost" for the rest of its life. A flag that is set and
// never cleared is a different untruth from one that never rises, not a smaller one.
//
// The device is asked whether it can bind a render target at all before any binding case runs, so
// the renderers with no off-screen path skip rather than fail.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    /// Whether this renderer has an off-screen path at all. Families that keep
    /// IGraphicsRenderer's null CreateRenderTarget2D() refuse the binding outright.
    [[nodiscard]] bool CanBindRenderTargets(GraphicsDevice& device)
    {
        try
        {
            RenderTarget2D probe(device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None);
            device.SetRenderTarget(&probe);
            device.SetRenderTarget(nullptr);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

TEST(ContentLostTest, AFreshRenderTargetHasNotLostItsContent)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    EXPECT_FALSE(target.getIsContentLostProperty());
}

TEST(ContentLostTest, NotifySetsTheFlagAndRaisesTheEventOnce)
{
    GraphicsDevice gd;
    RenderTarget2D target(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);

    int raised = 0;
    target.ContentLost += [&raised](System::Object*, const System::EventArgs&) { ++raised; };

    target.NotifyContentLostEXT();

    EXPECT_TRUE(target.getIsContentLostProperty());
    EXPECT_EQ(raised, 1);
}

// The half that was missing entirely: binding the target for writing ends the lost state.
TEST(ContentLostTest, BindingARenderTargetClearsTheLostFlag)
{
    GraphicsDevice gd;
    if (!CanBindRenderTargets(gd))
        GTEST_SKIP() << "this renderer has no off-screen render target path";

    RenderTarget2D target(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    target.NotifyContentLostEXT();
    ASSERT_TRUE(target.getIsContentLostProperty());

    gd.SetRenderTarget(&target);
    EXPECT_FALSE(target.getIsContentLostProperty());
    gd.SetRenderTarget(nullptr);
}

// The placement of that clear is deliberate: after the renderer has accepted the binding, not
// beside the per-binding validation. A binding that throws never wrote anything, so it must leave
// the flag exactly as it found it. Moving the clear earlier passes every other case in this file
// and fails this one.
TEST(ContentLostTest, ABindingThatThrowsDoesNotClearTheLostFlag)
{
    GraphicsDevice gd;
    if (!CanBindRenderTargets(gd))
        GTEST_SKIP() << "this renderer has no off-screen render target path";

    RenderTarget2D small(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    RenderTarget2D large(gd, 8, 8, false, SurfaceFormat::Color, DepthFormat::None);

    small.NotifyContentLostEXT();
    ASSERT_TRUE(small.getIsContentLostProperty());

    // Mismatched dimensions are refused; a renderer with no MRT refuses the pair earlier, for its
    // own reason. Either way the call throws and nothing was bound.
    EXPECT_ANY_THROW(gd.SetRenderTargets(
        {RenderTargetBinding(&small), RenderTargetBinding(&large)}));

    EXPECT_TRUE(small.getIsContentLostProperty())
        << "a binding that threw must not have cleared the flag";
}

// The dynamic-buffer half of the same contract. Wired since CABI-15 and never asserted.
TEST(ContentLostTest, WritingADynamicVertexBufferClearsTheLostFlag)
{
    GraphicsDevice gd;
    DynamicVertexBuffer buffer(gd, VertexPositionColor::getVertexDeclarationStatic(), 3,
                               BufferUsage::WriteOnly);

    buffer.NotifyContentLostEXT();
    ASSERT_TRUE(buffer.getIsContentLostProperty());

    const std::vector<VertexPositionColor> vertices = {
        VertexPositionColor(Vector3(0.0f, 0.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(1.0f, 0.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color::White),
    };
    // Non-empty on purpose: the shared empty-upload branch returns before the clear.
    buffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()),
                   SetDataOptions::Discard);

    EXPECT_FALSE(buffer.getIsContentLostProperty());
}
