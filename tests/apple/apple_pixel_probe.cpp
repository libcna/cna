// plans/plan_apple_m4.md AM4-037: what the one-frame smoke app cannot say -- that a frame put the
// right pixels where they belong. Each frame clears to a known colour, draws a SpriteBatch quad and,
// on a renderer with 3D, a BasicEffect triangle, then reads the three spots back through
// GetBackBufferData before presenting. The first frames of an iOS app can run before its view is
// on screen, with nothing to draw into yet, so the probe runs up to kMaxFrames frames and reports
// the first that read back correctly. The verdict goes to stdout, where `simctl launch --console`
// and a macOS shell can both see it.
#include "CNA/Platform/Entrypoint.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBackground(10, 20, 30, 255);
    const Color kSprite(255, 0, 0, 255);
    const Color kTriangle(0, 255, 0, 255);
    constexpr int kMaxFrames = 120;

    bool Near(const Color& a, const Color& b)
    {
        const auto close = [](int x, int y) { return std::abs(x - y) <= 8; };
        return close(a.getRProperty(), b.getRProperty()) && close(a.getGProperty(), b.getGProperty()) &&
               close(a.getBProperty(), b.getBProperty());
    }

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + ")";
    }

    class ApplePixelProbe final : public Game
    {
    public:
        ApplePixelProbe()
        {
            graphics_ = std::make_unique<GraphicsDeviceManager>(this);
            // GetBackBufferData is HiDef-only, as in XNA 4.0.
            graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            graphics_->setSupportedOrientationsProperty(DisplayOrientation::LandscapeLeft);
        }

        bool ok = false;
        int frames = 0;
        std::string report;

    protected:
        void Draw(const GameTime& gameTime) override
        {
            ++frames;
            auto& device = getGraphicsDeviceProperty();
            const int w = device.getPresentationParametersProperty().getBackBufferWidthProperty();
            const int h = device.getPresentationParametersProperty().getBackBufferHeightProperty();

            device.Clear(kBackground);

            Texture2D white(device, 1, 1);
            const Color whitePixel = Color::White;
            white.SetData(&whitePixel, 0, 1);
            SpriteBatch batch(device);
            batch.Begin();
            batch.Draw(white, Rectangle(w / 8, h / 4, w / 4, h / 2), kSprite);
            batch.End();

            const bool threeD = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
            if (threeD)
            {
                device.setBlendStateProperty(BlendState::Opaque);
                device.setDepthStencilStateProperty(DepthStencilState::None);
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                BasicEffect effect(device);
                effect.VertexColorEnabled = true;
                effect.setProjectionProperty(Matrix::CreateOrthographicOffCenter(
                    0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 0.0f, 1.0f));
                effect.Apply();
                const VertexPositionColor triangle[] = {
                    {Vector3(w * 0.55f, h * 0.2f, 0.0f), kTriangle},
                    {Vector3(w * 0.95f, h * 0.5f, 0.0f), kTriangle},
                    {Vector3(w * 0.55f, h * 0.8f, 0.0f), kTriangle},
                };
                device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle, 0, 1);
            }

            const auto sample = [&device](int x, int y)
            {
                Color pixel;
                const Rectangle one(x, y, 1, 1);
                device.GetBackBufferData(&one, &pixel, 0, 1);
                return pixel;
            };
            const Color background = sample(w / 2, h / 16);
            const Color sprite = sample(w / 4, h / 2);
            const Color triangle = threeD ? sample(static_cast<int>(w * 0.65f), h / 2) : kTriangle;

            ok = Near(background, kBackground) && Near(sprite, kSprite) && Near(triangle, kTriangle);
            report = "renderer=" + std::string(device.GetGraphicsRendererName()) + " frame=" +
                     std::to_string(frames) + " backbuffer=" +
                     std::to_string(w) + "x" + std::to_string(h) + " background=" + Describe(background) +
                     " sprite=" + Describe(sprite) +
                     (threeD ? " triangle=" + Describe(triangle) : std::string(" triangle=not-drawn(no 3D)"));
            Game::Draw(gameTime);
        }

    private:
        std::unique_ptr<GraphicsDeviceManager> graphics_;
    };
}

int main(int /*argc*/, char* /*argv*/[])
{
    try
    {
        ApplePixelProbe probe;
        do probe.RunOneFrame();
        while (!probe.ok && probe.frames < kMaxFrames);
        std::printf("CNA_APPLE_PIXELS %s\n", probe.report.c_str());
        std::puts(probe.ok ? "CNA_APPLE_PIXELS_OK" : "CNA_APPLE_PIXELS_FAILED");
        std::fflush(stdout);
        return probe.ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "CNA_APPLE_PIXELS_FAILED: %s\n", error.what());
        return 1;
    }
}
