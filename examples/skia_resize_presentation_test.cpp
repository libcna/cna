// SPDX-License-Identifier: MS-PL
// SKIA-71: public resize/fullscreen/presentation regression with live 2D resources.
//
// The raster backend recreates only its backbuffer surface on GraphicsDevice::Reset(). A live
// RenderTarget2D must remain valid (and remain the active canvas until explicitly unbound), while
// the already-created SpriteBatch must draw correctly into both that target and the replacement
// backbuffer. Fullscreen is intentionally checked as a stored PresentationParameters contract:
// virtual X11 displays may legitimately decline the actual window-mode switch.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kInitialWidth = 8;
    constexpr int kInitialHeight = 6;
    constexpr int kResizedWidth = 12;
    constexpr int kResizedHeight = 8;
    const Color kBlack(0, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kRed(255, 0, 0, 255);

    [[nodiscard]] bool Same(Color actual, Color expected)
    {
        return actual.getRProperty() == expected.getRProperty()
            && actual.getGProperty() == expected.getGProperty()
            && actual.getBProperty() == expected.getBProperty()
            && actual.getAProperty() == expected.getAProperty();
    }
}

class SkiaResizePresentationTest final : public Game
{
public:
    SkiaResizePresentationTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kInitialWidth);
        graphics_->setPreferredBackBufferHeightProperty(kInitialHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        source_ = std::make_unique<Texture2D>(device, 1, 1, false, SurfaceFormat::Color);
        source_->SetData(&kRed, 1);
        target_ = std::make_unique<RenderTarget2D>(device, 3, 2, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0,
                                                   RenderTargetUsage::PreserveContents);

        device.DeviceResetting += [this](System::Object* sender, const System::EventArgs&)
        {
            auto* device = static_cast<GraphicsDevice*>(sender);
            ++resettingCount_;
            resettingWidths_.push_back(
                device->getPresentationParametersProperty().getBackBufferWidthProperty());
            sequence_.push_back('R');
        };
        device.DeviceReset += [this](System::Object* sender, const System::EventArgs&)
        {
            auto* device = static_cast<GraphicsDevice*>(sender);
            ++resetCount_;
            resetWidths_.push_back(
                device->getPresentationParametersProperty().getBackBufferWidthProperty());
            sequence_.push_back('r');
        };
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(target_.get());
        device.Clear(kBlue);
        DrawSprite(*source_, Rectangle(1, 0, 1, 1));

        // Reset while a target is actually active. The target is a separate SkSurface and must
        // survive the backbuffer replacement together with the already-created SpriteBatch.
        graphics_->setPreferredBackBufferWidthProperty(kResizedWidth);
        graphics_->setPreferredBackBufferHeightProperty(kResizedHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::Stretch);
        graphics_->ApplyChanges();

        Check(resettingCount_ == 1 && resetCount_ == 1 && sequence_ == "Rr",
              "resize raises one ordered DeviceResetting/DeviceReset pair with live resources");
        Check(resettingWidths_.size() == 1 && resettingWidths_[0] == kInitialWidth,
              "DeviceResetting observes the old backbuffer width");
        Check(resetWidths_.size() == 1 && resetWidths_[0] == kResizedWidth,
              "DeviceReset observes the resized backbuffer width");
        Check(device.getPresentationParametersProperty().getBackBufferWidthProperty() == kResizedWidth
                  && device.getPresentationParametersProperty().getBackBufferHeightProperty() == kResizedHeight,
              "resize stores the requested presentation dimensions");
        Check(graphics_->getPreferredPresentationModeProperty() == PresentationMode::Stretch,
              "resize applies the requested presentation mode");

        std::vector<Color> targetPixels(6, kBlack);
        target_->GetData(targetPixels.data(), 0, static_cast<int>(targetPixels.size()));
        Check(Same(targetPixels[0], kBlue) && Same(targetPixels[1], kRed)
                  && Same(targetPixels[5], kBlue),
              "active RenderTarget2D content survives a backbuffer resize");

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(kBlack);
        DrawSprite(*target_, Rectangle(0, 0, 3, 2));
        CheckBackbufferPixel(device, 0, 0, kBlue,
                             "replacement backbuffer samples the pre-resize RenderTarget2D");
        CheckBackbufferPixel(device, 1, 0, kRed,
                             "replacement backbuffer receives SpriteBatch output after resize");
        CheckBackbufferPixel(device, kResizedWidth - 1, kResizedHeight - 1, kBlack,
                             "replacement backbuffer has the requested new dimensions");

        // SDL/Xvfb is allowed to reject the actual fullscreen transition. The public stored
        // parameter, reset event sequence, and live SpriteBatch path must nonetheless be sound.
        graphics_->setIsFullScreenProperty(true);
        graphics_->ApplyChanges();
        Check(device.getPresentationParametersProperty().getIsFullScreenProperty(),
              "fullscreen request is retained in PresentationParameters");
        Check(resettingCount_ == 2 && resetCount_ == 2 && sequence_ == "RrRr",
              "fullscreen request raises a second ordered reset pair");
        device.Clear(kBlack);
        DrawSprite(*source_, Rectangle(kResizedWidth - 1, kResizedHeight - 1, 1, 1));
        CheckBackbufferPixel(device, kResizedWidth - 1, kResizedHeight - 1, kRed,
                             "live SpriteBatch draws after fullscreen presentation reset");

        graphics_->setIsFullScreenProperty(false);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->ApplyChanges();
        Check(!device.getPresentationParametersProperty().getIsFullScreenProperty(),
              "windowed presentation request is retained after fullscreen reset");
        Check(graphics_->getPreferredPresentationModeProperty() == PresentationMode::NativeBackBuffer,
              "presentation mode can return to NativeBackBuffer with live resources");
        Check(resettingCount_ == 3 && resetCount_ == 3 && sequence_ == "RrRrRr",
              "restoring windowed presentation raises a third ordered reset pair");
        device.Clear(kBlack);
        DrawSprite(*target_, Rectangle(0, 0, 3, 2));
        CheckBackbufferPixel(device, 1, 0, kRed,
                             "RenderTarget2D and SpriteBatch remain usable after all presentation resets");
        Exit();
    }

private:
    void DrawSprite(Texture2D& texture, const Rectangle& destination)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(texture, destination, Color::White);
        spriteBatch_->End();
    }

    void CheckBackbufferPixel(GraphicsDevice& device, int x, int y, Color expected, const char* label)
    {
        Color actual(0, 0, 0, 0);
        const Rectangle pixel(x, y, 1, 1);
        device.GetBackBufferData(&pixel, &actual, 0, 1);
        Check(Same(actual, expected), label);
    }

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> source_;
    std::unique_ptr<RenderTarget2D> target_;
    std::vector<int> resettingWidths_;
    std::vector<int> resetWidths_;
    std::string sequence_;
    int resettingCount_ = 0;
    int resetCount_ = 0;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaResizePresentationTest game;
    game.Run();
    return game.Result();
}
