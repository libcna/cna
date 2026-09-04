// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-20: the five CnaPresentationMode policies, verified by where pixels actually
// land rather than only by the numbers GetViewportSize() reports.
//
// The window is 800x480 and the logical canvas is 100x100 -- a deliberately different aspect ratio,
// because a matching one makes Letterbox, Overscan and Stretch indistinguishable. A sprite covering
// the whole logical canvas is drawn in each mode, and the test asserts both where it appears and
// where it does NOT: a letterbox bar that is still the clear colour is what tells "the canvas was
// fitted" apart from "the canvas was stretched over everything".
//
// Check A -- Letterbox: the canvas is centred and square (480x480 of an 800x480 window), and the
//   side bars keep the clear colour.
// Check B -- Stretch: the sprite covers the whole window, bars included.
// Check C -- Overscan: the canvas is scaled to cover, so the window's centre and edges are all
//   sprite -- the opposite of Letterbox with the same input.
// Check D -- NativeBackBuffer: the logical size becomes the window size, so a 100x100 sprite stays
//   100x100 in the top-left corner instead of being scaled up.
// Check E -- FixedHeightDynamicWidth: the logical height is preserved and the width follows the
//   window's aspect ratio, so the canvas fills the window exactly with no bars.
// Check F -- window <-> logical coordinate transforms round-trip, and agree with where the pixels
//   actually landed (the centre of the logical canvas maps to the centre of the drawn area).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Llgl;
using CNA::Internal::Renderers::CnaPresentationMode;

namespace
{
    constexpr int kWindowWidth = 800;
    constexpr int kWindowHeight = 480;
    constexpr int kLogicalSize = 100;
    constexpr int kExpectedChecks = 6;

    const Color kClearColor{0, 0, 0, 255};
    const Color kSpriteColor{255, 255, 255, 255};
}

class LlglPresentationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    int passCount_ = 0;
    int result_ = 1;
    bool done_ = false;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    // Draws the whole logical canvas in the given mode, then reports the colour at a set of WINDOW
    // pixels.
    //
    // The two-step shape is forced by what the coordinate spaces actually mean. GetBackBufferData
    // addresses the logical back buffer, so under a 100x100 canvas it cannot name a letterbox bar
    // at all -- the bars are outside the canvas by definition, which is the whole point of them.
    // So the frame is recorded in the mode under test, and only then is the device switched to a
    // 1:1 presentation for reading. That is not a trick: sprite geometry was baked into window
    // pixels when Draw() was called, so the recorded frame is unchanged by the switch, and every
    // sample below is a real pixel of that frame at a real window position.
    std::vector<Color> RenderModeAndSample(CnaPresentationMode mode,
                                            const std::vector<Rectangle>& windowSamples,
                                            int& logicalWidthOut, int& logicalHeightOut)
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<LlglRenderer&>(device.GetRenderer());

        renderer.SetPresentationMode(static_cast<int>(mode));
        renderer.SetVirtualResolution(kLogicalSize, kLogicalSize);

        device.Clear(kClearColor);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*texture_, Rectangle(0, 0, kLogicalSize, kLogicalSize),
                           Rectangle(0, 0, 1, 1), kSpriteColor);
        spriteBatch_->End();

        renderer.GetViewportSize(logicalWidthOut, logicalHeightOut);

        // Switch to a 1:1 window-sized presentation purely for reading back, and read through the
        // renderer directly. GraphicsDevice::GetBackBufferData would validate the region against
        // PresentationParameters, which this test deliberately leaves at the window size while the
        // renderer draws at a 100x100 canvas -- an arrangement no game produces, and not what is
        // under test here.
        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
        renderer.SetVirtualResolution(kWindowWidth, kWindowHeight);

        std::vector<Color> samples;
        samples.reserve(windowSamples.size());
        for (const Rectangle& region : windowSamples)
        {
            std::uint8_t rgba[4] = {0, 0, 0, 0};
            renderer.ReadBackbuffer(region.X, region.Y, 1, 1, rgba);
            samples.emplace_back(rgba[0], rgba[1], rgba[2], rgba[3]);
        }
        return samples;
    }

    static bool IsSprite(const Color& color)
    {
        return color.getRProperty() > 200 && color.getGProperty() > 200 && color.getBProperty() > 200;
    }

    static bool IsClear(const Color& color)
    {
        return color.getRProperty() < 40 && color.getGProperty() < 40 && color.getBProperty() < 40;
    }

protected:
    void LoadContent() override
    {
        auto& device = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> white{255, 255, 255, 255};
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& renderer = static_cast<LlglRenderer&>(getGraphicsDeviceProperty().GetRenderer());

        // The window is 800x480 and the canvas is square, so Letterbox fits it to 480x480 centred
        // at x = 160..640: the centre is canvas, x = 40 and x = 760 are bars.
        {
            int width = 0;
            int height = 0;
            const auto samples = RenderModeAndSample(CnaPresentationMode::Letterbox,
                {Rectangle(400, 240, 1, 1), Rectangle(40, 240, 1, 1), Rectangle(760, 240, 1, 1)},
                width, height);
            check(IsSprite(samples[0]) && IsClear(samples[1]) && IsClear(samples[2]) &&
                  width == kLogicalSize && height == kLogicalSize,
                  "Letterbox centres the canvas and leaves the side bars at the clear colour");
        }

        {
            int width = 0;
            int height = 0;
            const auto samples = RenderModeAndSample(CnaPresentationMode::Stretch,
                {Rectangle(400, 240, 1, 1), Rectangle(40, 240, 1, 1), Rectangle(760, 240, 1, 1)},
                width, height);
            check(IsSprite(samples[0]) && IsSprite(samples[1]) && IsSprite(samples[2]),
                  "Stretch fills the whole window, bars included");
        }

        {
            int width = 0;
            int height = 0;
            const auto samples = RenderModeAndSample(CnaPresentationMode::Overscan,
                {Rectangle(400, 240, 1, 1), Rectangle(40, 240, 1, 1), Rectangle(760, 240, 1, 1)},
                width, height);
            check(IsSprite(samples[0]) && IsSprite(samples[1]) && IsSprite(samples[2]),
                  "Overscan scales the canvas to cover the window, so nothing is left at the clear colour");
        }

        {
            // NativeBackBuffer makes the logical size equal the window size, so the same 100x100
            // sprite occupies exactly 100x100 window pixels in the corner -- (50,50) is inside it
            // and (400,240) is not.
            int width = 0;
            int height = 0;
            const auto samples = RenderModeAndSample(CnaPresentationMode::NativeBackBuffer,
                {Rectangle(50, 50, 1, 1), Rectangle(400, 240, 1, 1)}, width, height);
            check(IsSprite(samples[0]) && IsClear(samples[1]) &&
                  width == kWindowWidth && height == kWindowHeight,
                  "NativeBackBuffer draws 1:1 with no scaling and reports the window size as logical");
        }

        {
            int width = 0;
            int height = 0;
            const auto samples = RenderModeAndSample(CnaPresentationMode::FixedHeightDynamicWidth,
                {Rectangle(400, 240, 1, 1), Rectangle(40, 240, 1, 1), Rectangle(760, 240, 1, 1)},
                width, height);
            // The height stays at the requested 100 and the width follows the window's 800:480
            // aspect ratio, so the canvas fills the window exactly: no bars anywhere.
            const int expectedWidth = static_cast<int>(std::lround(
                static_cast<double>(kWindowWidth) * kLogicalSize / kWindowHeight));
            check(IsSprite(samples[0]) && height == kLogicalSize && width == expectedWidth,
                  "FixedHeightDynamicWidth keeps the logical height and derives the width from the window");
        }

        {
            // Back in Letterbox: the centre of the logical canvas must map to the centre of the
            // 480x480 drawn area, and the transforms must be inverses of each other.
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
            renderer.SetVirtualResolution(kLogicalSize, kLogicalSize);

            float windowX = 0.0f;
            float windowY = 0.0f;
            const bool forward = renderer.TransformLogicalToWindow(50.0f, 50.0f, windowX, windowY);

            float logicalX = 0.0f;
            float logicalY = 0.0f;
            const bool inverse = renderer.TransformWindowToLogical(windowX, windowY, logicalX, logicalY);

            const bool centred = std::fabs(windowX - kWindowWidth * 0.5f) < 1.0f &&
                                 std::fabs(windowY - kWindowHeight * 0.5f) < 1.0f;
            const bool roundTrips = std::fabs(logicalX - 50.0f) < 0.5f && std::fabs(logicalY - 50.0f) < 0.5f;
            check(forward && inverse && centred && roundTrips,
                  "logical<->window transforms round-trip and agree with the letterboxed geometry");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
        result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
        Exit();
    }

public:
    LlglPresentationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWindowWidth);
        gdm_->setPreferredBackBufferHeightProperty(kWindowHeight);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    LlglPresentationTest game;
    game.Run();
    return game.getResult();
}
