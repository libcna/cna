// SPDX-License-Identifier: MS-PL
// ISSUE-156: the default sprite projection preserves XNA's zero-to-one Z interval.
// Prepared witness; run before repairing SDL_GPU and do not infer coverage from compilation.
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

class SpriteDepthProjectionTest : public testing::Test
{
protected:
    static GraphicsDevice MakeDevice()
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(256);
        parameters.setBackBufferHeightProperty(256);
        parameters.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::HiDef, parameters);
    }
    GraphicsDevice device = MakeDevice();
    Texture2D texture{device, 1, 1};
    void SetUp() override
    {
        CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3, SdlGpu);
        RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
        const Color white = Color::White;
        texture.SetData(&white, 1);
        device.Clear(Color::Black);
    }
    Color Center()
    {
        const Rectangle area(128, 128, 1, 1);
        Color pixel = Color::Transparent;
        device.GetBackBufferData(&area, &pixel, 0, 1);
        return pixel;
    }
    void Draw(SpriteBatch& batch, Color color, float depth)
    {
        batch.Draw(texture, Rectangle(16, 16, 224, 224), std::nullopt, color,
                   0.0f, Vector2::Zero, SpriteEffects::None, depth);
    }
    void Sprite(Color color, float depth, const Matrix& transform = Matrix::getIdentityProperty())
    {
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque,
                    &SamplerState::PointClamp, &DepthStencilState::Default,
                    &RasterizerState::CullNone, nullptr, transform);
        Draw(batch, color, depth);
        batch.End();
    }
    void Geometry(Color color, float depth)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const VertexPositionColor vertices[] = {
            {Vector3(-1, 1, depth), color}, {Vector3(-1, -1, depth), color},
            {Vector3(1, -1, depth), color}, {Vector3(-1, 1, depth), color},
            {Vector3(1, -1, depth), color}, {Vector3(1, 1, depth), color}
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }
};

TEST_F(SpriteDepthProjectionTest, ImmediateNearDepthWinsRegardlessOfSubmissionOrder)
{
    Sprite(Color::Red, 0.1f);
    Sprite(Color::Blue, 0.9f);
    EXPECT_EQ(Center(), Color::Red);
    device.Clear(Color::Black);
    Sprite(Color::Blue, 0.9f);
    Sprite(Color::Red, 0.1f);
    EXPECT_EQ(Center(), Color::Red);
}

TEST_F(SpriteDepthProjectionTest, FrontToBackSortingRespectsDepthAndNoDepthControl)
{
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::FrontToBack, &BlendState::Opaque,
                &SamplerState::PointClamp, &DepthStencilState::None, &RasterizerState::CullNone);
    Draw(batch, Color::Blue, 0.9f);
    Draw(batch, Color::Red, 0.1f);
    batch.End();
    EXPECT_EQ(Center(), Color::Blue);
    device.Clear(Color::Black);
    batch.Begin(SpriteSortMode::FrontToBack, &BlendState::Opaque,
                &SamplerState::PointClamp, &DepthStencilState::Default, &RasterizerState::CullNone);
    Draw(batch, Color::Blue, 0.9f);
    Draw(batch, Color::Red, 0.1f);
    batch.End();
    EXPECT_EQ(Center(), Color::Red);
}

TEST_F(SpriteDepthProjectionTest, SpriteAndGeometryShareTheSameZeroToOneInterval)
{
    Geometry(Color::Blue, 0.3f);
    Sprite(Color::Red, 0.2f);
    EXPECT_EQ(Center(), Color::Red);
    device.Clear(Color::Black);
    Geometry(Color::Blue, 0.3f);
    Sprite(Color::Red, 0.35f);
    EXPECT_EQ(Center(), Color::Blue);
    device.Clear(Color::Black);
    Geometry(Color::Blue, 0.7f);
    Sprite(Color::Red, 0.8f);
    EXPECT_EQ(Center(), Color::Blue);
    device.Clear(Color::Black);
    Geometry(Color::Blue, 0.7f);
    Sprite(Color::Red, 0.65f);
    EXPECT_EQ(Center(), Color::Red);
}

TEST_F(SpriteDepthProjectionTest, ZTranslationUsesTheSameDepthIntervalAsGeometry)
{
    Geometry(Color::Blue, 0.5f);
    Matrix transform = Matrix::getIdentityProperty();
    transform.M43 = 0.6f;
    Sprite(Color::Red, 0.1f, transform);
    EXPECT_EQ(Center(), Color::Blue);
}

TEST_F(SpriteDepthProjectionTest, ZScaleUsesTheSameDepthIntervalAsGeometry)
{
    Geometry(Color::Blue, 0.3f);
    Matrix transform = Matrix::getIdentityProperty();
    transform.M33 = 0.5f;
    Sprite(Color::Red, 0.3f, transform);
    EXPECT_EQ(Center(), Color::Red);
}
} // namespace
