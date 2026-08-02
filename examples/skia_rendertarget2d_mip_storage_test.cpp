// SPDX-License-Identifier: MS-PL
// SKIA-131: stable, isolated RenderTarget2D surfaces and exact shadows at every mip level.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/NotSupportedException.hpp"

#include "include/core/SkImage.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::SkiaRenderTargetBackend;
using CNA::Internal::Backends::Skia::SkiaResourceStats;
using CNA::Internal::Backends::Skia::SkiaSourceAlphaConvention;

namespace
{
    const Color kBlack(0, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kYellow(255, 255, 0, 255);

    [[nodiscard]] bool SameStats(const SkiaResourceStats& a, const SkiaResourceStats& b)
    {
        return a.textureBackends == b.textureBackends
            && a.textureImageViews == b.textureImageViews
            && a.cpuTextureCubeBackends == b.cpuTextureCubeBackends
            && a.cpuTexture3DBackends == b.cpuTexture3DBackends
            && a.renderTargets == b.renderTargets
            && a.renderTargetCubes == b.renderTargetCubes
            && a.targetSnapshots == b.targetSnapshots
            && a.mipChains2D == b.mipChains2D
            && a.textureImageBytes == b.textureImageBytes
            && a.cpuTextureStorageBytes == b.cpuTextureStorageBytes
            && a.targetSurfaceBytes == b.targetSurfaceBytes
            && a.cubeTargetStorageBytes == b.cubeTargetStorageBytes
            && a.targetSnapshotBytes == b.targetSnapshotBytes
            && a.mipChain2DStorageBytes == b.mipChain2DStorageBytes;
    }

    [[nodiscard]] bool SameColors(const std::vector<Color>& a, const std::vector<Color>& b)
    {
        return a == b;
    }
}

class SkiaRenderTarget2DMipStorageTest final : public Game
{
public:
    SkiaRenderTarget2DMipStorageTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(16);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1, false, SurfaceFormat::Color);
        white_->SetData(&kWhite, 1);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* graphicsBackend = dynamic_cast<SkiaGraphicsBackend*>(&device.GetBackend());
        Check(graphicsBackend != nullptr, "Skia graphics backend exposes resource accounting");
        if (!graphicsBackend)
        {
            Exit();
            return;
        }
        const SkiaResourceStats baseline = graphicsBackend->GetResourceStatsEXT();

        {
            auto counters = std::make_shared<CNA::Internal::Backends::Skia::SkiaResourceCounters>();
            bool rejected = false;
            try
            {
                SkiaRenderTargetBackend overBudget(
                    8192, 8192, true, {}, counters, false);
                (void)overBudget;
            }
            catch (const System::NotSupportedException&)
            {
                rejected = true;
            }
            const SkiaResourceStats failed = counters->GetStats();
            Check(rejected && failed.renderTargets == 0u && failed.targetSurfaceBytes == 0u
                      && failed.mipChains2D == 0u && failed.mipChain2DStorageBytes == 0u,
                  "over-budget surface plus shadow rejects before allocation or accounting");
        }

        {
            RenderTarget2D target(device, 8, 8, true, SurfaceFormat::Color, DepthFormat::None,
                                  0, RenderTargetUsage::PreserveContents);
            auto* backend = dynamic_cast<SkiaRenderTargetBackend*>(target.GetRenderTargetBackend());
            Check(backend != nullptr && target.getLevelCountProperty() == 4
                      && backend->MipLevelCountEXT() == 4,
                  "8x8 mip target exposes the complete four-level chain");
            if (!backend)
            {
                Exit();
                return;
            }

            const SkiaResourceStats live = graphicsBackend->GetResourceStatsEXT();
            Check(backend->SurfaceStorageBytesEXT() == 340u
                      && backend->MipChainEXT().StorageBytes() == 340u
                      && live.renderTargets == baseline.renderTargets + 1u
                      && live.targetSurfaceBytes == baseline.targetSurfaceBytes + 340u
                      && live.mipChains2D == baseline.mipChains2D + 1u
                      && live.mipChain2DStorageBytes == baseline.mipChain2DStorageBytes + 340u,
                  "surface and exact-shadow storage account every mip exactly once");

            std::vector<Color> levelZero(64, kRed);
            std::vector<Color> levelOne(16, kBlack);
            for (int i = 0; i < 16; ++i)
                levelOne[static_cast<std::size_t>(i)] = Color(
                    static_cast<std::uint8_t>(9 + i * 7),
                    static_cast<std::uint8_t>(201 - i * 5),
                    static_cast<std::uint8_t>(31 + i * 3),
                    static_cast<std::uint8_t>(17 + i * 11));
            std::vector<Color> levelTwo(4, kBlue);
            std::vector<Color> levelThree(1, kYellow);
            target.SetData(0, nullptr, levelZero.data(), 0, 64);
            target.SetData(1, nullptr, levelOne.data(), 0, 16);
            target.SetData(2, nullptr, levelTwo.data(), 0, 4);
            target.SetData(3, nullptr, levelThree.data(), 0, 1);

            std::vector<Color> publicLevelOne(16, kBlack);
            target.GetData(1, nullptr, publicLevelOne.data(), 0, 16);
            std::vector<std::uint8_t> backendLevelOne(64u, 0u);
            const bool backendRead = backend->GetData(
                1, 0, 0, 4, 4, backendLevelOne.data(),
                static_cast<int>(backendLevelOne.size()));
            bool backendExact = backendRead;
            for (int i = 0; i < 16 && backendExact; ++i)
            {
                backendExact = backendLevelOne[static_cast<std::size_t>(i) * 4u + 0u]
                                   == levelOne[static_cast<std::size_t>(i)].getRProperty()
                    && backendLevelOne[static_cast<std::size_t>(i) * 4u + 1u]
                                   == levelOne[static_cast<std::size_t>(i)].getGProperty()
                    && backendLevelOne[static_cast<std::size_t>(i) * 4u + 2u]
                                   == levelOne[static_cast<std::size_t>(i)].getBProperty()
                    && backendLevelOne[static_cast<std::size_t>(i) * 4u + 3u]
                                   == levelOne[static_cast<std::size_t>(i)].getAProperty();
            }
            Check(SameColors(publicLevelOne, levelOne) && backendExact,
                  "translucent level-1 upload/readback preserves exact canonical bytes");

            const Rectangle patchRegion(2, 1, 1, 1);
            const Color patch(3, 5, 7, 9);
            target.SetData(1, &patchRegion, &patch, 0, 1);
            levelOne[6] = patch;
            target.GetData(1, nullptr, publicLevelOne.data(), 0, 16);
            Check(SameColors(publicLevelOne, levelOne),
                  "partial higher-level upload changes only its addressed texel");

            const auto levelTwoSnapshot = backend->SnapshotMipLevelEXT(
                2, SkiaSourceAlphaConvention::Premultiplied);
            const auto levelTwoSnapshotAgain = backend->SnapshotMipLevelEXT(
                2, SkiaSourceAlphaConvention::Straight);
            Check(levelTwoSnapshot && levelTwoSnapshot->width() == 2
                      && levelTwoSnapshot->height() == 2
                      && levelTwoSnapshot.get() == levelTwoSnapshotAgain.get(),
                  "snapshot cache identifies and reuses the requested 2x2 level");
            const SkiaResourceStats snapshotTwoStats = graphicsBackend->GetResourceStatsEXT();
            const auto levelThreeSnapshot = backend->SnapshotMipLevelEXT(
                3, SkiaSourceAlphaConvention::Premultiplied);
            const SkiaResourceStats snapshotThreeStats = graphicsBackend->GetResourceStatsEXT();
            Check(levelThreeSnapshot && levelThreeSnapshot->width() == 1
                      && levelThreeSnapshot->height() == 1
                      && levelThreeSnapshot.get() != levelTwoSnapshot.get()
                      && snapshotTwoStats.targetSnapshots == baseline.targetSnapshots + 1u
                      && snapshotTwoStats.targetSnapshotBytes == baseline.targetSnapshotBytes + 16u
                      && snapshotThreeStats.targetSnapshots == baseline.targetSnapshots + 1u
                      && snapshotThreeStats.targetSnapshotBytes == baseline.targetSnapshotBytes + 4u,
                  "switching snapshot levels keeps one bounded non-aliasing cache entry");

            device.setViewportProperty(Viewport(3, 4, 2, 2));
            device.setScissorRectangleProperty(Rectangle(5, 6, 1, 1));
            device.SetRenderTarget(&target);
            Check(device.getViewportProperty().getWidthProperty() == 8
                      && device.getViewportProperty().getHeightProperty() == 8
                      && device.getScissorRectangleProperty() == Rectangle(0, 0, 8, 8),
                  "binding a mip target resets viewport and scissor to level zero");
            device.Clear(kBlack);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                const_cast<SamplerState*>(&SamplerState::PointClamp),
                                nullptr, nullptr);
            spriteBatch_->Draw(*white_, Rectangle(0, 0, 2, 2), Color::White);
            spriteBatch_->End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::vector<Color> renderedLevelZero(64, kRed);
            target.GetData(0, nullptr, renderedLevelZero.data(), 0, 64);
            target.GetData(1, nullptr, publicLevelOne.data(), 0, 16);
            std::vector<Color> publicLevelTwo(4, kBlack);
            std::vector<Color> publicLevelThree(1, kBlack);
            target.GetData(2, nullptr, publicLevelTwo.data(), 0, 4);
            target.GetData(3, nullptr, publicLevelThree.data(), 0, 1);
            const std::vector<Color> renderedLevelOne{
                kWhite, kBlack, kBlack, kBlack,
                kBlack, kBlack, kBlack, kBlack,
                kBlack, kBlack, kBlack, kBlack,
                kBlack, kBlack, kBlack, kBlack};
            const std::vector<Color> renderedLevelTwo{
                Color(64, 64, 64, 255), kBlack, kBlack, kBlack};
            const std::vector<Color> renderedLevelThree{
                Color(16, 16, 16, 255)};
            Check(renderedLevelZero[0] == kWhite && renderedLevelZero[9] == kWhite
                      && renderedLevelZero[2] == kBlack && renderedLevelZero[63] == kBlack,
                  "Clear and SpriteBatch draw mutate the bound level-zero surface");
            Check(SameColors(publicLevelOne, renderedLevelOne)
                      && SameColors(publicLevelTwo, renderedLevelTwo)
                      && SameColors(publicLevelThree, renderedLevelThree),
                  "level-zero rendering deterministically replaces every descendant without aliasing");

            device.SetRenderTarget(&target);
            Color preserved(0, 0, 0, 0);
            const Rectangle preservedPixel(0, 0, 1, 1);
            device.GetBackBufferData(&preservedPixel, &preserved, 0, 1);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(preserved == kWhite,
                  "PreserveContents rebind keeps rendered level-zero pixels");

            device.Clear(kBlack);
            SamplerState pointMipPoint;
            pointMipPoint.setFilterProperty(TextureFilter::Point);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                &pointMipPoint, nullptr, nullptr);
            spriteBatch_->Draw(target, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 8, 8),
                               Color::White);
            spriteBatch_->End();
            Color sampled(0, 0, 0, 0);
            device.GetBackBufferData(&preservedPixel, &sampled, 0, 1);
            Check(sampled == renderedLevelThree[0],
                  "public minification samples the regenerated 1x1 target mip");
        }

        Check(SameStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "mip target destruction returns surfaces, chain, and snapshot to baseline");

        {
            RenderTarget2D discard(device, 8, 8, true, SurfaceFormat::Color, DepthFormat::None,
                                   0, RenderTargetUsage::DiscardContents);
            std::vector<Color> high(16, kGreen);
            discard.SetData(1, nullptr, high.data(), 0, 16);
            device.SetRenderTarget(&discard);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Color> levelZero(64, kWhite);
            std::vector<Color> retainedHigh(16, kBlack);
            discard.GetData(0, nullptr, levelZero.data(), 0, 64);
            discard.GetData(1, nullptr, retainedHigh.data(), 0, 16);
            Check(levelZero.front() == kBlack && levelZero.back() == kBlack
                      && std::all_of(retainedHigh.begin(), retainedHigh.end(),
                                     [](Color value) { return value == kBlack; }),
                  "DiscardContents replaces level zero and regenerates its higher storage");
        }
        Check(SameStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "discard target release also returns every counter to baseline");

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
    std::unique_ptr<Texture2D> white_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaRenderTarget2DMipStorageTest game;
    game.Run();
    return game.Result();
}
