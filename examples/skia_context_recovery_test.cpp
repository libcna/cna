// SPDX-License-Identifier: MS-PL
// SKIA-16/SKIA-28/SKIA-65: reset the SDL presenter without losing CPU-raster Skia resources.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"
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
using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::SkiaResourceStats;

namespace
{
    constexpr int kBackbufferSize = 8;
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

    [[nodiscard]] bool SameStats(const SkiaResourceStats& left, const SkiaResourceStats& right)
    {
        return left.textureBackends == right.textureBackends
            && left.textureImageViews == right.textureImageViews
            && left.cpuTextureCubeBackends == right.cpuTextureCubeBackends
            && left.cpuTexture3DBackends == right.cpuTexture3DBackends
            && left.renderTargets == right.renderTargets
            && left.renderTargetCubes == right.renderTargetCubes
            && left.targetSnapshots == right.targetSnapshots
            && left.textureImageBytes == right.textureImageBytes
            && left.cpuTextureStorageBytes == right.cpuTextureStorageBytes
            && left.targetSurfaceBytes == right.targetSurfaceBytes
            && left.cubeTargetStorageBytes == right.cubeTargetStorageBytes
            && left.targetSnapshotBytes == right.targetSnapshotBytes;
    }
}

class SkiaContextRecoveryTest final : public Game
{
public:
    SkiaContextRecoveryTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        graphics_->setPreferredBackBufferHeightProperty(kBackbufferSize);
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
        target_ = std::make_unique<RenderTarget2D>(device, 2, 2, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0,
                                                   RenderTargetUsage::PreserveContents);

        device.DeviceLost += [this](System::Object*, const System::EventArgs&) { ++lostCount_; };
        device.DeviceResetting += [this](System::Object*, const System::EventArgs&)
        {
            ++resettingCount_;
            sequence_.push_back('R');
        };
        device.DeviceReset += [this](System::Object*, const System::EventArgs&)
        {
            ++resetCount_;
            sequence_.push_back('r');
        };
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* backend = dynamic_cast<SkiaGraphicsBackend*>(&device.GetBackend());
        Check(backend != nullptr, "GraphicsDevice owns SkiaGraphicsBackend for presentation recovery");
        if (!backend)
        {
            Exit();
            return;
        }

        device.SetRenderTarget(target_.get());
        device.Clear(kBlue);
        DrawSprite(*source_, Rectangle(1, 0, 1, 1));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.Clear(kBlack);
        DrawSprite(*target_, Rectangle(3, 0, 2, 2));
        const SkiaResourceStats beforeRecovery = backend->GetResourceStatsEXT();
        Check(beforeRecovery.textureBackends == 1 && beforeRecovery.textureImageViews == 2
                  && beforeRecovery.renderTargets == 1 && beforeRecovery.targetSnapshots == 1,
              "live source, target, and cached target snapshot exist before presenter recovery");

        // The debug seam rebuilds only SDL's renderer/streaming texture. It must emit a reset
        // pair, not DeviceLost, because Skia's selected CPU-raster images never became invalid.
        backend->DebugSimulateContextLoss();
        Check(lostCount_ == 0 && resettingCount_ == 1 && resetCount_ == 1 && sequence_ == "Rr",
              "presenter recovery raises exactly DeviceResetting then DeviceReset, without DeviceLost");
        Check(device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal,
              "GraphicsDevice returns to Normal after synchronous presenter recovery");
        Check(SameStats(beforeRecovery, backend->GetResourceStatsEXT()),
              "recovery leaves CPU-raster texture, surface, and snapshot ownership bounded and intact");

        std::vector<Color> targetPixels(4, kBlack);
        target_->GetData(targetPixels.data(), 0, static_cast<int>(targetPixels.size()));
        Check(Same(targetPixels[0], kBlue) && Same(targetPixels[1], kRed)
                  && Same(targetPixels[2], kBlue) && Same(targetPixels[3], kBlue),
              "RenderTarget2D readback survives the rebuilt SDL presenter");

        device.Clear(kBlack);
        DrawSprite(*source_, Rectangle(0, 0, 1, 1));
        DrawSprite(*target_, Rectangle(3, 0, 2, 2));
        CheckBackbufferPixel(device, 0, 0, kRed,
                             "Texture2D draws through the rebuilt SDL presenter");
        CheckBackbufferPixel(device, 3, 0, kBlue,
                             "cached RenderTarget2D snapshot remains sampleable after presenter recovery");
        CheckBackbufferPixel(device, 4, 0, kRed,
                             "RenderTarget2D content remains exact after presenter recovery");

        device.Present();
        CheckBackbufferPixel(device, 4, 0, kRed,
                             "rebuilt SDL presenter displays without changing the raster backbuffer");

        backend->DebugRestoreContext();
        Check(lostCount_ == 0 && resettingCount_ == 2 && resetCount_ == 2 && sequence_ == "RrRr",
              "DebugRestoreContext performs a second synchronous presenter reset pair");
        Check(SameStats(beforeRecovery, backend->GetResourceStatsEXT()),
              "DebugRestoreContext also preserves CPU-raster resource ownership");
        device.Present();
        CheckBackbufferPixel(device, 4, 0, kRed,
                             "second presenter recovery still displays the existing raster backbuffer");
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
    std::string sequence_;
    int lostCount_ = 0;
    int resettingCount_ = 0;
    int resetCount_ = 0;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaContextRecoveryTest game;
    game.Run();
    return game.Result();
}
