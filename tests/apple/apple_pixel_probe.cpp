// plans/plan_apple_m4.md AM4-037: what the one-frame smoke app cannot say -- that a frame put the
// right pixels where they belong. Each frame clears to a known colour, draws a SpriteBatch quad and,
// on a renderer with 3D, a BasicEffect triangle, then reads the three spots back through
// GetBackBufferData before presenting. The first frames of an iOS app can run before its view is
// on screen, with nothing to draw into yet, so on iOS the probe waits up to kMaxWarmupFrames frames
// for the first that reads back correctly; a macOS window exists before the first frame, so there
// the first frame must already be correct. Once a frame is correct, kConfirmFrames more must be too,
// so a renderer that is right only now and then does not pass on a lucky frame. The verdict goes to
// stdout, where `simctl launch --console` and a macOS shell can both see it.
#include "CNA/Platform/Entrypoint.hpp"

#include <TargetConditionals.h>

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
#if TARGET_OS_IPHONE
    constexpr int kMaxWarmupFrames = 120;
#else
    constexpr int kMaxWarmupFrames = 1;
#endif
    constexpr int kConfirmFrames = 10;

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
        /// The first frame that read back correctly, 0 while none has.
        int firstGoodFrame = 0;
        /// The first frame after firstGoodFrame that did not, 0 while none has.
        int firstRelapseFrame = 0;
        std::string relapseReport;

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
            if (ok && firstGoodFrame == 0) firstGoodFrame = frames;
            if (!ok && firstGoodFrame != 0 && firstRelapseFrame == 0)
            {
                firstRelapseFrame = frames;
                relapseReport = report;
            }
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
        while (probe.firstGoodFrame == 0 && probe.frames < kMaxWarmupFrames);
        if (probe.firstGoodFrame != 0)
        {
            for (int i = 0; i < kConfirmFrames; ++i) probe.RunOneFrame();
        }
        const bool passed = probe.firstGoodFrame != 0 && probe.firstRelapseFrame == 0;
        std::printf("CNA_APPLE_PIXELS %s first_good_frame=%d confirmed_frames=%d\n", probe.report.c_str(),
                    probe.firstGoodFrame, probe.firstGoodFrame != 0 ? kConfirmFrames : 0);
        if (probe.firstRelapseFrame != 0)
            std::printf("CNA_APPLE_PIXELS relapse %s\n", probe.relapseReport.c_str());
        std::puts(passed ? "CNA_APPLE_PIXELS_OK" : "CNA_APPLE_PIXELS_FAILED");
        std::fflush(stdout);
        return passed ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "CNA_APPLE_PIXELS_FAILED: %s\n", error.what());
        return 1;
    }
}
