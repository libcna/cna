// SPDX-License-Identifier: MS-PL
// SKIA-72: distinguish logical raster/readback coordinates from physical window/output scaling.
//
// The test deliberately makes a 40x30 logical Letterbox backbuffer live in a 120x60 window. It
// validates the known 20-pixel horizontal letterbox offset in *window* coordinates, asks the
// active SDL renderer for its physical output size, and leaves that measured ratio diagnostic:
// Xvfb commonly reports 1x; a real high-DPI display can report a larger output. In either case,
// Skia must use SDL_RenderCoordinates{From,To}Window rather than mixing window points with output
// pixels. Public GetBackBufferData must remain in the 40x30 logical raster coordinate system.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kLogicalWidth = 40;
    constexpr int kLogicalHeight = 30;
    constexpr int kWindowWidth = 120;
    constexpr int kWindowHeight = 60;
    constexpr int kMaxWaitFrames = 300;
    const Color kBlue(0, 0, 255, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool Near(float actual, float expected)
    {
        return std::fabs(actual - expected) <= 0.01f;
    }

    [[nodiscard]] bool Same(Color actual, Color expected)
    {
        return actual.getRProperty() == expected.getRProperty()
            && actual.getGProperty() == expected.getGProperty()
            && actual.getBProperty() == expected.getBProperty()
            && actual.getAProperty() == expected.getAProperty();
    }
}

class SkiaDisplayScaleTest final : public Game
{
public:
    SkiaDisplayScaleTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kLogicalWidth);
        graphics_->setPreferredBackBufferHeightProperty(kLogicalHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::Letterbox);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        pixel_ = std::make_unique<Texture2D>(device, 1, 1, false, SurfaceFormat::Color);
        pixel_->SetData(&kRed, 1);

        window_ = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
        Check(window_ != nullptr, "Skia display-scale diagnostic has a real SDL window");
        if (window_ && !SDL_SetWindowSize(window_, kWindowWidth, kWindowHeight))
        {
            Check(false, "SDL accepts the requested diagnostic window size");
            Exit();
        }
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;

        int windowWidth = 0;
        int windowHeight = 0;
        SDL_GetWindowSize(window_, &windowWidth, &windowHeight);
        if (windowWidth != kWindowWidth || windowHeight != kWindowHeight)
        {
            if (++waitFrames_ < kMaxWaitFrames)
                return;
            Check(false, "timed out waiting for the diagnostic window size");
            finished_ = true;
            Exit();
            return;
        }

        finished_ = true;
        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<CNA::Internal::Renderers::Skia::SkiaRenderer*>(&device.GetRenderer());
        Check(renderer != nullptr, "GraphicsDevice owns SkiaRenderer");
        if (!renderer)
        {
            Exit();
            return;
        }

        SDL_Renderer* nativeRenderer = SDL_GetRenderer(window_);
        int outputWidth = 0;
        int outputHeight = 0;
        Check(nativeRenderer != nullptr
                  && SDL_GetRenderOutputSize(nativeRenderer, &outputWidth, &outputHeight),
              "Skia SDL renderer reports its physical output size");
        Check(outputWidth > 0 && outputHeight > 0,
              "physical renderer output dimensions are positive");
        std::printf("[INFO] logical=%dx%d window=%dx%d output=%dx%d scale=(%.3f,%.3f)\n",
                    kLogicalWidth, kLogicalHeight, windowWidth, windowHeight, outputWidth, outputHeight,
                    static_cast<double>(outputWidth) / windowWidth,
                    static_cast<double>(outputHeight) / windowHeight);

        float logicalX = -1.0f;
        float logicalY = -1.0f;
        Check(renderer->TransformWindowToLogical(20.0f, 0.0f, logicalX, logicalY)
                  && Near(logicalX, 0.0f) && Near(logicalY, 0.0f),
              "window letterbox origin maps to logical origin without output-pixel scaling");
        Check(renderer->TransformWindowToLogical(60.0f, 30.0f, logicalX, logicalY)
                  && Near(logicalX, 20.0f) && Near(logicalY, 15.0f),
              "window centre maps to logical centre with the letterbox offset");

        float windowX = -1.0f;
        float windowY = -1.0f;
        Check(renderer->TransformLogicalToWindow(0.0f, 0.0f, windowX, windowY)
                  && Near(windowX, 20.0f) && Near(windowY, 0.0f),
              "logical origin maps back to the window letterbox origin");
        Check(renderer->TransformLogicalToWindow(20.0f, 15.0f, windowX, windowY)
                  && Near(windowX, 60.0f) && Near(windowY, 30.0f),
              "logical centre maps back to the window centre");

        device.Clear(kBlue);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*pixel_, Rectangle(kLogicalWidth - 1, kLogicalHeight - 1, 1, 1), Color::White);
        spriteBatch_->End();

        Color redPixel(0, 0, 0, 0);
        const Rectangle logicalCorner(kLogicalWidth - 1, kLogicalHeight - 1, 1, 1);
        device.GetBackBufferData(&logicalCorner, &redPixel, 0, 1);
        Check(Same(redPixel, kRed), "GetBackBufferData reads the logical raster corner, not output pixels");
        Color bluePixel(0, 0, 0, 0);
        const Rectangle logicalOrigin(0, 0, 1, 1);
        device.GetBackBufferData(&logicalOrigin, &bluePixel, 0, 1);
        Check(Same(bluePixel, kBlue), "logical raster origin remains independently readable");
        Exit();
    }

private:
    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> pixel_;
    SDL_Window* window_ = nullptr;
    int waitFrames_ = 0;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaDisplayScaleTest game;
    game.Run();
    return game.Result();
}
