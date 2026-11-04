// SPDX-License-Identifier: MS-PL

// C ABI CABI-28, revised by TQ-035: coverage for the ContentLost contract on the default-pool
// resources.
//
// The flag must be set by a reported loss and cleared again exactly where XNA clears it. For a
// render target that is (Microsoft.Xna.Framework.Graphics.dll):
//   - GraphicsDevice::VerifyCanDraw IL_0011-003e, which every draw entry point calls right after
//     its argument checks (DrawPrimitives IL_0050, DrawIndexedPrimitives IL_0067,
//     DrawInstancedPrimitives IL_00ab, DrawUserPrimitives<T> IL_00af, DrawUserIndexedPrimitives<T>
//     IL_0107) and before its own refusals (CannotDrawNoShader IL_020d, CannotDrawNoData IL_01ee);
//     SpriteBatch reaches it through DrawIndexedPrimitives (PlatformRenderBatch IL_02eb-02fb);
//   - GraphicsDevice::Clear IL_01e4-020f, after the native clear succeeded;
//   - Texture2D/TextureCube::CopyData after a completed SetData (pinned in Texture2DTests.cpp).
// Those are the only callers of IDynamicGraphicsResource.SetContentLost(false) in the assembly;
// SetRenderTarget(s) is not one of them. The owner adopted XNA's rule on 2026-11-04, reversing
// CABI-28's clear-on-bind.
//
// The device is asked whether it can bind a render target at all before any binding case runs, so
// the renderers with no off-screen path skip rather than fail.

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdint>
#include <optional>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::ClearOptions;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::EffectPass;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetBinding;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
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

    /// One triangle across the whole target, in clip space.
    [[nodiscard]] std::vector<VertexPositionColor> Triangle()
    {
        return {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::White),
            VertexPositionColor(Vector3(3.0f, -1.0f, 0.0f), Color::White),
            VertexPositionColor(Vector3(-1.0f, 3.0f, 0.0f), Color::White),
        };
    }

    /// A 4x4 target that is bound, then reported lost -- the state every clearing case starts in.
    class BoundLostTargetTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;

        void SetUp() override
        {
            if (!CanBindRenderTargets(gd))
                GTEST_SKIP() << "this renderer has no off-screen render target path";
            target.emplace(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
            gd.SetRenderTarget(&*target);
            target->NotifyContentLostEXT();
            ASSERT_TRUE(target->getIsContentLostProperty());
        }

        void TearDown() override
        {
            if (target)
                gd.SetRenderTarget(nullptr);
        }

        [[nodiscard]] bool Requires3D()
        {
            return gd.SupportsCapability(CNA::GraphicsCapability::ThreeD);
        }

        std::optional<RenderTarget2D> target;
    };
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

// TQ-035 replaces CABI-28's "BindingARenderTargetClearsTheLostFlag": XNA's SetRenderTargets has
// no SetContentLost call, so neither binding nor unbinding nor rebinding ends the lost state.
TEST(ContentLostTest, BindingARenderTargetLeavesTheLostFlagSet)
{
    GraphicsDevice gd;
    if (!CanBindRenderTargets(gd))
        GTEST_SKIP() << "this renderer has no off-screen render target path";

    RenderTarget2D target(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    target.NotifyContentLostEXT();
    ASSERT_TRUE(target.getIsContentLostProperty());

    gd.SetRenderTarget(&target);
    EXPECT_TRUE(target.getIsContentLostProperty()) << "binding";
    gd.SetRenderTarget(nullptr);
    EXPECT_TRUE(target.getIsContentLostProperty()) << "unbinding";
    gd.SetRenderTargets({RenderTargetBinding(&target)});
    EXPECT_TRUE(target.getIsContentLostProperty()) << "binding through SetRenderTargets";
    gd.SetRenderTarget(nullptr);
}

// A refused binding writes nothing either, so it too leaves the flag as it found it.
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

// XNA Clear IL_01e4-020f: every overload ends the bound target's lost state.
TEST_F(BoundLostTargetTest, ClearOfTheBoundTargetClearsTheLostFlag)
{
    gd.Clear(Color::CornflowerBlue);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "Clear(Color)";

    target->NotifyContentLostEXT();
    gd.Clear(ClearOptions::Target, Color::Black, 1.0f, 0);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "Clear(ClearOptions, Color, float, int)";

    target->NotifyContentLostEXT();
    gd.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "CNAEXT Clear(float, float, float, float)";
}

// XNA Clear IL_01d1-01db throws CannotClearNullDepth before IL_01e4 is reached.
TEST_F(BoundLostTargetTest, AClearThatThrowsLeavesTheLostFlagSet)
{
    EXPECT_THROW(gd.Clear(ClearOptions::DepthBuffer, Color::Black, 1.0f, 0),
                 System::InvalidOperationException);
    EXPECT_TRUE(target->getIsContentLostProperty())
        << "the target has no depth buffer, so the clear was refused and wrote nothing";
}

// XNA's loop covers currentRenderTargets only: a target that is not bound keeps its lost state,
// and a Clear of the back buffer touches no render target at all.
TEST_F(BoundLostTargetTest, ClearLeavesTargetsThatAreNotBoundAlone)
{
    RenderTarget2D other(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    other.NotifyContentLostEXT();

    gd.Clear(Color::Black);
    EXPECT_FALSE(target->getIsContentLostProperty());
    EXPECT_TRUE(other.getIsContentLostProperty()) << "not bound when the Clear ran";

    gd.SetRenderTarget(nullptr);
    gd.Clear(Color::Black);
    EXPECT_TRUE(other.getIsContentLostProperty()) << "a back-buffer Clear";
}

// Clear IL_01e6-020d loops over every bound target, so all MRT members are cleared. Reach allows
// one target (SetRenderTargets IL_0088-00ac), so this needs a HiDef device.
TEST(ContentLostTest, ClearEndsTheLostStateOfEveryBoundTarget)
{
    GraphicsDevice gd(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                      PresentationParameters::CreateWindowedEXT(800, 480));
    RenderTarget2D first(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    RenderTarget2D second(gd, 4, 4, false, SurfaceFormat::Color, DepthFormat::None);
    try
    {
        gd.SetRenderTargets({RenderTargetBinding(&first), RenderTargetBinding(&second)});
    }
    catch (...)
    {
        GTEST_SKIP() << "this renderer cannot bind two render targets";
    }
    first.NotifyContentLostEXT();
    second.NotifyContentLostEXT();

    gd.Clear(Color::Black);
    EXPECT_FALSE(first.getIsContentLostProperty());
    EXPECT_FALSE(second.getIsContentLostProperty());
    gd.SetRenderTarget(nullptr);  // the targets may not be destroyed while bound
}

// A cube face binds the cube itself (RenderTargetHelper.pTexture), and Clear clears it.
TEST(ContentLostTest, ClearOfABoundCubeFaceClearsTheCubesLostFlag)
{
    GraphicsDevice gd;
    std::optional<RenderTargetCube> cube;
    try
    {
        cube.emplace(gd, 4, false, SurfaceFormat::Color, DepthFormat::None);
        gd.SetRenderTarget(&*cube, CubeMapFace::PositiveX);
    }
    catch (...)
    {
        GTEST_SKIP() << "this renderer cannot bind a cube render target";
    }
    cube->NotifyContentLostEXT();
    EXPECT_TRUE(cube->getIsContentLostProperty()) << "binding alone does not clear";

    gd.Clear(Color::Black);
    EXPECT_FALSE(cube->getIsContentLostProperty());
    gd.SetRenderTarget(nullptr);
}

// A complete user-primitive draw into the bound target ends its lost state (VerifyCanDraw).
TEST_F(BoundLostTargetTest, DrawingIntoTheBoundTargetClearsTheLostFlag)
{
    if (!Requires3D())
        GTEST_SKIP() << "this renderer has no 3D draw path";
    BasicEffect effect(gd);
    effect.VertexColorEnabled = true;
    const std::vector<VertexPositionColor> vertices = Triangle();
    for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
    {
        pass.Apply();
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1,
                              VertexPositionColor::getVertexDeclarationStatic());
    }
    EXPECT_FALSE(target->getIsContentLostProperty());
}

// The argument checks precede VerifyCanDraw (DrawUserPrimitives<T> IL_0041-0050 before IL_00af;
// DrawPrimitives IL_0017-0026 before IL_0050), so a draw refused for its arguments clears nothing.
TEST_F(BoundLostTargetTest, ADrawRefusedForItsArgumentsLeavesTheLostFlagSet)
{
    if (!Requires3D())
        GTEST_SKIP() << "this renderer has no 3D draw path";
    BasicEffect effect(gd);
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
    const std::vector<VertexPositionColor> vertices = Triangle();
    EXPECT_THROW(gd.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 0,
                                       VertexPositionColor::getVertexDeclarationStatic()),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 0),
                 System::ArgumentOutOfRangeException);
    EXPECT_TRUE(target->getIsContentLostProperty());
}

// VerifyCanDraw clears first (IL_0011-003e) and only then refuses a draw with no shader
// (CannotDrawNoShader IL_020d-0217) or no vertex data (CannotDrawNoData IL_01ee-01f8). So even a
// draw that XNA refuses for the device's state has already ended the lost state -- on every draw
// entry point, which this checks one by one with nothing applied and nothing bound.
TEST_F(BoundLostTargetTest, ADrawRefusedForTheDeviceStateHasAlreadyClearedTheLostFlag)
{
    if (!Requires3D())
        GTEST_SKIP() << "this renderer has no 3D draw path";
    const std::vector<VertexPositionColor> vertices = Triangle();

    EXPECT_THROW(gd.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 1,
                                       VertexPositionColor::getVertexDeclarationStatic()),
                 System::InvalidOperationException);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "DrawUserPrimitives";

    target->NotifyContentLostEXT();
    EXPECT_THROW(gd.DrawPrimitives(PrimitiveType::TriangleList, 0, 1),
                 System::InvalidOperationException);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "DrawPrimitives";

    target->NotifyContentLostEXT();
    EXPECT_THROW(gd.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1),
                 System::InvalidOperationException);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "DrawIndexedPrimitives";

    target->NotifyContentLostEXT();
    EXPECT_THROW(gd.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1, 1),
                 System::InvalidOperationException);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "DrawInstancedPrimitives";

    const std::vector<std::uint16_t> indices{0, 1, 2};
    target->NotifyContentLostEXT();
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 3,
                                              indices.data(), 0, 1,
                                              VertexPositionColor::getVertexDeclarationStatic()),
                 System::InvalidOperationException);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "DrawUserIndexedPrimitives";
}

// SpriteBatch.End IL_0038-0042 flushes only a non-empty queue; every flushed batch is a
// DrawIndexedPrimitives (PlatformRenderBatch IL_02eb-02fb) and so a VerifyCanDraw.
TEST_F(BoundLostTargetTest, ASpriteBatchClearsTheLostFlagOnlyWhenItDrawsSomething)
{
    Texture2D texture(gd, 1, 1);
    SpriteBatch batch(gd);

    batch.Begin();
    batch.End();
    EXPECT_TRUE(target->getIsContentLostProperty()) << "an empty batch draws nothing";

    batch.Begin();
    batch.Draw(texture, Vector2(0.0f, 0.0f), Color::White);
    EXPECT_TRUE(target->getIsContentLostProperty()) << "a deferred sprite is drawn at End";
    batch.End();
    EXPECT_FALSE(target->getIsContentLostProperty()) << "deferred batch";

    // Immediate mode draws inside Draw (XNA InternalDraw -> RenderBatch).
    target->NotifyContentLostEXT();
    batch.Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend);
    batch.Draw(texture, Vector2(0.0f, 0.0f), Color::White);
    EXPECT_FALSE(target->getIsContentLostProperty()) << "immediate sprite";
    batch.End();
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
