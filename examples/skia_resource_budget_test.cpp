// SPDX-License-Identifier: MS-PL
// SKIA-74/SKIA-110: prove bounded raster snapshots, live-resource counter release/reuse, and
// repeated presenter reconstruction while a target and its immutable sampling snapshot are live.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::SkiaResourceStats;

namespace
{
    constexpr int kCycles = 64;
    const Color kBlack(0, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kRed(255, 0, 0, 255);
}

class SkiaResourceBudgetTest final : public Game
{
public:
    SkiaResourceBudgetTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* backend = dynamic_cast<SkiaGraphicsBackend*>(&device.GetBackend());
        Check(backend != nullptr, "GraphicsDevice exposes Skia debug resource counters");
        if (!backend)
        {
            Exit();
            return;
        }

        Check(IsEmpty(backend->GetResourceStatsEXT()), "resource counters start without hidden images or targets");
        auto texture = std::make_unique<Texture2D>(device, 1, 1, false, SurfaceFormat::Color);
        texture->SetData(&kRed, 1);
        const SkiaResourceStats textureStats = backend->GetResourceStatsEXT();
        Check(textureStats.textureBackends == 1 && textureStats.textureImageViews == 2
                  && textureStats.textureImageBytes == 8,
              "one Texture2D owns exactly two bounded alpha-labelled SkImages");

        auto target = std::make_unique<RenderTarget2D>(device, 2, 2, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0,
                                                       RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(target.get());
        device.Clear(kBlue);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        DrawTarget(*target);
        DrawTarget(*target);
        SkiaResourceStats targetStats = backend->GetResourceStatsEXT();
        Check(targetStats.renderTargets == 1 && targetStats.targetSnapshots == 1
                  && targetStats.targetSurfaceBytes == 16 && targetStats.targetSnapshotBytes == 16,
              "repeated sampling retains one bounded target snapshot");

        device.SetRenderTarget(target.get());
        device.Clear(kRed);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(backend->GetResourceStatsEXT().targetSnapshots == 0,
              "target Clear invalidates its old immutable sampling snapshot");
        DrawTarget(*target);
        Check(backend->GetResourceStatsEXT().targetSnapshots == 1,
              "sampling after a target write recreates exactly one current snapshot");
        target.reset();
        Check(backend->GetResourceStatsEXT().renderTargets == 0
                  && backend->GetResourceStatsEXT().targetSnapshots == 0,
              "target destruction releases its surface and cached snapshot");

        int lostCount = 0;
        int resettingCount = 0;
        int resetCount = 0;
        device.DeviceLost += [&lostCount](System::Object*, const System::EventArgs&) { ++lostCount; };
        device.DeviceResetting += [&resettingCount](System::Object*, const System::EventArgs&)
        {
            ++resettingCount;
        };
        device.DeviceReset += [&resetCount](System::Object*, const System::EventArgs&) { ++resetCount; };

        bool boundedAcrossCycles = true;
        bool recoveredAcrossCycles = true;
        for (int cycle = 0; cycle < kCycles; ++cycle)
        {
            auto transient = std::make_unique<RenderTarget2D>(device, 2, 2, false, SurfaceFormat::Color,
                                                               DepthFormat::None, 0,
                                                               RenderTargetUsage::PreserveContents);
            const Color expected = (cycle & 1) == 0 ? kBlue : kRed;
            device.SetRenderTarget(transient.get());
            device.Clear(expected);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            DrawTarget(*transient);
            const SkiaResourceStats live = backend->GetResourceStatsEXT();
            boundedAcrossCycles = boundedAcrossCycles && live.renderTargets == 1
                && live.targetSnapshots == 1 && live.targetSurfaceBytes == 16
                && live.targetSnapshotBytes == 16;

            // Reconstruct SDL's presentation renderer/texture while both the mutable target and
            // immutable SkImage snapshot are live. Reuse the snapshot after recovery so stats alone
            // cannot hide a stale Skia or backend pointer.
            backend->DebugSimulateContextLoss();
            device.Clear(kBlack);
            DrawTarget(*transient);
            device.Present();
            Color actual(0, 0, 0, 0);
            const Rectangle sample(0, 0, 1, 1);
            device.GetBackBufferData(&sample, &actual, 0, 1);
            recoveredAcrossCycles = recoveredAcrossCycles && SameStats(
                live, backend->GetResourceStatsEXT()) && actual == expected;

            transient.reset();
            const SkiaResourceStats released = backend->GetResourceStatsEXT();
            boundedAcrossCycles = boundedAcrossCycles && released.renderTargets == 0
                && released.targetSnapshots == 0 && released.targetSurfaceBytes == 0
                && released.targetSnapshotBytes == 0;
        }
        Check(boundedAcrossCycles, "64 target/snapshot cycles release and reuse a bounded cache");
        Check(recoveredAcrossCycles && lostCount == 0 && resettingCount == kCycles
                  && resetCount == kCycles,
              "64 paired presenter recreations preserve live targets, snapshots, pixels, and reset events");

        texture.reset();
        Check(IsEmpty(backend->GetResourceStatsEXT()), "all debug resource counters return to zero after release");
        Exit();
    }

private:
    [[nodiscard]] static bool SameStats(const SkiaResourceStats& left,
                                        const SkiaResourceStats& right)
    {
        return left.textureBackends == right.textureBackends
            && left.textureImageViews == right.textureImageViews
            && left.cpuTextureCubeBackends == right.cpuTextureCubeBackends
            && left.cpuTexture3DBackends == right.cpuTexture3DBackends
            && left.renderTargets == right.renderTargets
            && left.renderTargetCubes == right.renderTargetCubes
            && left.targetSnapshots == right.targetSnapshots
            && left.mipChains2D == right.mipChains2D
            && left.textureImageBytes == right.textureImageBytes
            && left.cpuTextureStorageBytes == right.cpuTextureStorageBytes
            && left.targetSurfaceBytes == right.targetSurfaceBytes
            && left.cubeTargetStorageBytes == right.cubeTargetStorageBytes
            && left.targetSnapshotBytes == right.targetSnapshotBytes
            && left.mipChain2DStorageBytes == right.mipChain2DStorageBytes;
    }

    [[nodiscard]] static bool IsEmpty(const SkiaResourceStats& stats)
    {
        return stats.textureBackends == 0 && stats.textureImageViews == 0 && stats.renderTargets == 0
            && stats.cpuTextureCubeBackends == 0 && stats.cpuTexture3DBackends == 0
            && stats.renderTargetCubes == 0
            && stats.targetSnapshots == 0 && stats.mipChains2D == 0
            && stats.textureImageBytes == 0
            && stats.cpuTextureStorageBytes == 0
            && stats.targetSurfaceBytes == 0 && stats.cubeTargetStorageBytes == 0
            && stats.targetSnapshotBytes == 0 && stats.mipChain2DStorageBytes == 0;
    }

    void DrawTarget(Texture2D& target)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(target, Rectangle(0, 0, 2, 2), Color::White);
        spriteBatch_->End();
    }

    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaResourceBudgetTest game;
    game.Run();
    return game.Result();
}
