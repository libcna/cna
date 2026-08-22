// SPDX-License-Identifier: MS-PL
// SAMPLE-001: verifies the Direct3D 9 pixel-center convention used by XNA 4.0.
//
// The original Microsoft Primitives sample draws each star as the exact screen-space triangle
// (x,y), (x+1,y), (x,y+1). The real XNA executable covers one pixel for that triangle. OpenGL's
// different pixel-center and top-left fill conventions used to drop it completely in EasyGL.
// This regression test retains a larger control triangle so a failure cannot be mistaken for a
// broken BasicEffect or readback path.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kBackground(0, 0, 0, 255);
    const Color kTiny(255, 0, 0, 255);
    const Color kControl(0, 255, 0, 255);

    bool Matches(const Color& color, const Color& expected)
    {
        constexpr int tolerance = 8;
        const auto close = [](int a, int b) { return std::abs(a - b) <= tolerance; };
        return close(color.getRProperty(), expected.getRProperty())
            && close(color.getGProperty(), expected.getGProperty())
            && close(color.getBProperty(), expected.getBProperty());
    }
}

class XnaPixelCenterTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(kBackground);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(
            Matrix::CreateOrthographicOffCenter(0.0f, static_cast<float>(kSize),
                                                static_cast<float>(kSize), 0.0f, 0.0f, 1.0f));
        effect.Apply();

        const VertexPositionColor triangles[] = {
            {Vector3(16.0f, 16.0f, 0.0f), kTiny},
            {Vector3(17.0f, 16.0f, 0.0f), kTiny},
            {Vector3(16.0f, 17.0f, 0.0f), kTiny},
            {Vector3(32.0f, 32.0f, 0.0f), kControl},
            {Vector3(48.0f, 32.0f, 0.0f), kControl},
            {Vector3(32.0f, 48.0f, 0.0f), kControl},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangles, 0, 2);

        std::vector<Color> frame(static_cast<std::size_t>(kSize) * kSize, kBackground);
        const Rectangle full(0, 0, kSize, kSize);
        device.GetBackBufferData(&full, frame.data(), 0, static_cast<int>(frame.size()));

        int tinyPixels = 0;
        int controlPixels = 0;
        for (const Color& pixel : frame)
        {
            tinyPixels += Matches(pixel, kTiny) ? 1 : 0;
            controlPixels += Matches(pixel, kControl) ? 1 : 0;
        }

        const bool tinyVisible = tinyPixels >= 1;
        const bool controlVisible = controlPixels >= 32;
        std::printf("[%s] XNA 1x1 triangle: %d covered pixel(s), expected at least 1\n",
                    tinyVisible ? "PASS" : "FAIL", tinyPixels);
        std::printf("[%s] BasicEffect control triangle: %d covered pixel(s), expected at least 32\n",
                    controlVisible ? "PASS" : "FAIL", controlPixels);

        result_ = tinyVisible && controlVisible ? 0 : 1;
        Exit();
    }

public:
    XnaPixelCenterTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    XnaPixelCenterTest game;
    game.Run();
    return game.getResult();
}
