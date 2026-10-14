// SPDX-License-Identifier: MS-PL
// GraphicsDevice.Reset with a larger back buffer grows the real rendering surface, not only the
// reported PresentationParameters and Viewport.
//
// PresentationLifecycleTests.cpp checks the reported side of a Reset (PresentationParameters, the
// Viewport). This program checks the surface behind it: the frame after a Reset from 32x16 to
// 64x32, a clear and a sprite drawn entirely outside the OLD bounds must both be read back from the
// framebuffer, and the whole new back buffer must be readable in one GetBackBufferData call.
// The post-reset checks run on the next frame, the way a game resets from Update and draws next.
//
// Runs with PresentationMode::NativeBackBuffer so logical and physical pixels map 1:1.
//
// First written for the retired SDL_RENDERER renderer (Task 711).
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kOldWidth = 32;
    constexpr int kOldHeight = 16;
    constexpr int kNewWidth = 64;
    constexpr int kNewHeight = 32;

    const Color kBefore(255, 0, 255, 255);
    const Color kClear(0, 255, 255, 255);
    const Color kSprite(255, 255, 0, 255);

    bool colourMatch(const Color& got, const Color& want, int tol = 16)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tol
            && std::abs(got.getGProperty() - want.getGProperty()) <= tol
            && std::abs(got.getBProperty() - want.getBProperty()) <= tol;
    }
}

class BackBufferResetResizeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             white_;

    int frame_  = 0;
    int result_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (!ok) result_ = 1;
    }

    void checkPixel(GraphicsDevice& dev, int x, int y, const Color& want, const char* label)
    {
        Color got(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        dev.GetBackBufferData(&region, &got, 0, 1);
        check(colourMatch(got, want),
              std::string(label) + ": got=(" + std::to_string(got.getRProperty()) + "," +
                  std::to_string(got.getGProperty()) + "," + std::to_string(got.getBProperty()) +
                  ") want=(" + std::to_string(want.getRProperty()) + "," +
                  std::to_string(want.getGProperty()) + "," + std::to_string(want.getBProperty()) + ")");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        white_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color white[1] = {Color::White};
        white_->SetData(white, 1);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        ++frame_;

        if (frame_ == 1)
        {
            check(dev.getPresentationParametersProperty().getBackBufferWidthProperty() == kOldWidth &&
                      dev.getPresentationParametersProperty().getBackBufferHeightProperty() == kOldHeight,
                  "the initial PresentationParameters report the configured 32x16 back buffer");
            dev.Clear(kBefore);
            checkPixel(dev, kOldWidth - 2, kOldHeight - 2, kBefore,
                       "a pixel near the ORIGINAL edge reads the clear colour before the reset");

            PresentationParameters larger = dev.getPresentationParametersProperty();
            larger.setBackBufferWidthProperty(kNewWidth);
            larger.setBackBufferHeightProperty(kNewHeight);
            dev.Reset(larger);

            check(dev.getPresentationParametersProperty().getBackBufferWidthProperty() == kNewWidth &&
                      dev.getPresentationParametersProperty().getBackBufferHeightProperty() == kNewHeight,
                  "PresentationParameters report the new 64x32 back buffer after Reset");
            check(dev.getViewportProperty().getWidthProperty() == kNewWidth &&
                      dev.getViewportProperty().getHeightProperty() == kNewHeight,
                  "the Viewport covers the new 64x32 back buffer after Reset");
            return;
        }

        if (frame_ == 2)
        {
            dev.Clear(kClear);
            // Entirely outside the old 32x16 bounds on both axes.
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                       const_cast<SamplerState*>(&SamplerState::PointClamp),
                       nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
            sb_->Draw(*white_, Rectangle(40, 20, 24, 12), kSprite);
            sb_->End();

            checkPixel(dev, kNewWidth - 4, kNewHeight - 4, kSprite,
                       "a sprite drawn beyond the old bounds reaches the grown surface");
            checkPixel(dev, kOldWidth + 4, 4, kClear,
                       "the clear reaches a pixel beyond the old width");
            checkPixel(dev, 4, kOldHeight + 4, kClear,
                       "the clear reaches a pixel beyond the old height");
            checkPixel(dev, 4, 4, kClear, "the clear still covers the old area");

            std::vector<Color> whole(static_cast<std::size_t>(kNewWidth * kNewHeight), Color(0, 0, 0, 0));
            try
            {
                dev.GetBackBufferData(whole.data(), static_cast<int>(whole.size()));
                const Color corner = whole[static_cast<std::size_t>((kNewHeight - 1) * kNewWidth + kNewWidth - 1)];
                check(colourMatch(corner, kSprite),
                      "a whole-back-buffer read returns all 64x32 pixels, the last one inside the sprite");
            }
            catch (const std::exception& e)
            {
                check(false, std::string("reading the whole new back buffer threw: ") + e.what());
            }
            Exit();
        }
    }

public:
    BackBufferResetResizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GetBackBufferData is HiDef-only, as in XNA 4.0.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kOldWidth);
        gdm_->setPreferredBackBufferHeightProperty(kOldHeight);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("backbuffer_reset_resize"))
        return CNA::Examples::Conformance::kSkipExitCode;
    BackBufferResetResizeTest game;
    game.Run();
    return game.getResult();
}
