// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-41: MAGNUM renderer integration smoke test.
//
// Exercises the four paths that need a live OpenGL context and therefore cannot be covered by the
// GTest suite: clear + back-buffer readback, a SpriteBatch textured quad, a 3D coloured primitive
// draw, and a render target round trip. Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool ChannelsMatch(const Color& actual, int r, int g, int b)
    {
        return actual.getRProperty() == r && actual.getGProperty() == g
            && actual.getBProperty() == b;
    }

    void Report(const char* name, bool passed, const Color& actual, const char* expected)
    {
        std::printf("[%s] %s: pixel=(%d,%d,%d,%d), expected %s\n",
                    passed ? "PASS" : "FAIL", name,
                    actual.getRProperty(), actual.getGProperty(),
                    actual.getBProperty(), actual.getAProperty(), expected);
    }
}

class MagnumSmokeTest : public Game
{
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<BasicEffect> effect_;
    Texture2D redTexture_;
    bool done_ = false;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        // The 3D draw below goes through GraphicsDevice::DrawPrimitives, which refuses a draw with
        // no effect applied -- the vertex colours are what this stage is checking, so lighting and
        // texturing stay off and only VertexColorEnabled is on.
        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        effect_->setLightingEnabledProperty(false);
        effect_->setTextureEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::getIdentityProperty());

        const std::vector<uint8_t> pixels = {255, 0, 0, 255};
        redTexture_ = Texture2D::CreateFromPixels(device, 1, 1, pixels);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& viewport = device.getViewportProperty();
        const int width = viewport.getWidthProperty();
        const int height = viewport.getHeightProperty();
        const Rectangle centre(width / 2, height / 2, 1, 1);

        bool allPassed = true;

        // 1. Clear + readback.
        device.Clear(Color(0, 0, 255, 255));
        device.SetDepthTestEnabled(false);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        bool passed = ChannelsMatch(pixel, 0, 0, 255);
        Report("Clear", passed, pixel, "(0,0,255,*)");
        allPassed = allPassed && passed;

        // 2. SpriteBatch textured quad.
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch_->Begin();
        spriteBatch_->Draw(redTexture_, Rectangle(0, 0, width, height),
                           Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        pixel = Color(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        passed = ChannelsMatch(pixel, 255, 0, 0);
        Report("SpriteBatch", passed, pixel, "(255,0,0,*)");
        allPassed = allPassed && passed;

        // 3. 3D coloured primitive covering the whole clip volume.
        device.Clear(Color(0, 0, 0, 255));
        VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(), 6,
                                  BufferUsage::None);
        const Color green(0, 255, 0, 255);
        const std::array<VertexPositionColor, 6> vertices = {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), green),
            VertexPositionColor(Vector3(-1.0f,  1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), green),
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 1.0f, -1.0f, 0.0f), green),
        };
        vertexBuffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));
        effect_->Apply();
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        pixel = Color(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        passed = ChannelsMatch(pixel, 0, 255, 0);
        Report("ColoredPrimitives", passed, pixel, "(0,255,0,*)");
        allPassed = allPassed && passed;

        // 4. Render target round trip: clear a target, read it back, then restore the back buffer.
        RenderTarget2D target(device, 64, 64);
        device.SetRenderTarget(&target);
        device.Clear(Color(255, 255, 0, 255));
        device.SetRenderTarget(nullptr);
        std::array<Color, 1> targetPixels = {Color(0, 0, 0, 0)};
        const Rectangle targetRegion(32, 32, 1, 1);
        target.GetData(0, &targetRegion, targetPixels.data(), 0, 1);
        passed = ChannelsMatch(targetPixels[0], 255, 255, 0);
        Report("RenderTarget2D", passed, targetPixels[0], "(255,255,0,*)");
        allPassed = allPassed && passed;

        result_ = allPassed ? 0 : 1;
        Exit();
    }

public:
    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    MagnumSmokeTest game;
    game.Run();
    return game.GetResult();
}
