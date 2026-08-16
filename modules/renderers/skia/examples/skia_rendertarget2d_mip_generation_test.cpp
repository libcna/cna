// SPDX-License-Identifier: MS-PL
// SKIA-132: deterministic RenderTarget2D mip invalidation and resolve barriers.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaRenderTargetBinding.hpp"
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
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "include/core/SkImage.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::Skia::SkiaRenderer;
using CNA::Internal::Renderers::Skia::SkiaRenderTargetRenderer;
using CNA::Internal::Renderers::Skia::SkiaRenderTargetBinding;
using CNA::Internal::Renderers::Skia::SkiaResourceStats;
using CNA::Internal::Renderers::Skia::SkiaSourceAlphaConvention;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBlack(0, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kWhite(255, 255, 255, 255);
    const Color kYellow(255, 255, 0, 255);

    [[nodiscard]] bool Same(Color left, Color right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }

    [[nodiscard]] bool SameColors(const std::vector<Color>& left,
                                  const std::vector<Color>& right)
    {
        return left.size() == right.size()
            && std::equal(left.begin(), left.end(), right.begin(), Same);
    }

    [[nodiscard]] bool AllColor(const std::vector<Color>& values, Color expected)
    {
        return std::all_of(values.begin(), values.end(), [expected](Color value)
        {
            return Same(value, expected);
        });
    }

    [[nodiscard]] bool SameStats(const SkiaResourceStats& left, const SkiaResourceStats& right)
    {
        return left.textureRenderers == right.textureRenderers
            && left.textureImageViews == right.textureImageViews
            && left.cpuTextureCubeRenderers == right.cpuTextureCubeRenderers
            && left.cpuTexture3DRenderers == right.cpuTexture3DRenderers
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

    [[nodiscard]] std::vector<Color> Pattern(int width, int height, int seed)
    {
        std::vector<Color> result;
        result.reserve(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                result.emplace_back(
                    (seed + x * 37 + y * 17) & 0xFF,
                    (seed * 3 + x * 13 + y * 43) & 0xFF,
                    (seed * 7 + x * 29 + y * 11) & 0xFF,
                    9 + ((seed + x * 31 + y * 47) % 240));
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<Color> BoxMip(const std::vector<Color>& source,
                                             int sourceWidth, int sourceHeight,
                                             int targetWidth, int targetHeight)
    {
        std::vector<Color> target;
        target.reserve(static_cast<std::size_t>(targetWidth) * targetHeight);
        for (int y = 0; y < targetHeight; ++y)
        {
            const int sourceY0 = static_cast<int>(
                static_cast<std::int64_t>(y) * sourceHeight / targetHeight);
            const int sourceY1 = static_cast<int>(
                static_cast<std::int64_t>(y + 1) * sourceHeight / targetHeight);
            for (int x = 0; x < targetWidth; ++x)
            {
                const int sourceX0 = static_cast<int>(
                    static_cast<std::int64_t>(x) * sourceWidth / targetWidth);
                const int sourceX1 = static_cast<int>(
                    static_cast<std::int64_t>(x + 1) * sourceWidth / targetWidth);
                unsigned int sums[4] = {};
                unsigned int count = 0u;
                for (int sourceY = sourceY0; sourceY < sourceY1; ++sourceY)
                {
                    for (int sourceX = sourceX0; sourceX < sourceX1; ++sourceX)
                    {
                        const Color value = source[static_cast<std::size_t>(sourceY) * sourceWidth
                                                   + sourceX];
                        sums[0] += value.getRProperty();
                        sums[1] += value.getGProperty();
                        sums[2] += value.getBProperty();
                        sums[3] += value.getAProperty();
                        ++count;
                    }
                }
                target.emplace_back(
                    static_cast<int>((sums[0] + count / 2u) / count),
                    static_cast<int>((sums[1] + count / 2u) / count),
                    static_cast<int>((sums[2] + count / 2u) / count),
                    static_cast<int>((sums[3] + count / 2u) / count));
            }
        }
        return target;
    }

    class ForeignRenderTarget final : public IRenderTargetRenderer
    {
    public:
        [[nodiscard]] int GetWidth() const override { return 7; }
        [[nodiscard]] int GetHeight() const override { return 5; }

        void BindAsRenderTarget() override {}
        void UnbindAsRenderTarget() override {}
    };
}

class SkiaRenderTarget2DMipGenerationTest final : public Game
{
public:
    SkiaRenderTarget2DMipGenerationTest()
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
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        fill_ = std::make_unique<Texture2D>(device, 1, 1, false, SurfaceFormat::Color);
        fill_->SetData(&kWhite, 1);
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* graphicsRenderer = dynamic_cast<SkiaRenderer*>(&device.GetRenderer());
        Check(graphicsRenderer != nullptr, "Skia renderer exposes the target resolve test seams");
        if (!graphicsRenderer)
        {
            Exit();
            return;
        }
        const SkiaResourceStats baseline = graphicsRenderer->GetResourceStatsEXT();

        {
            RenderTarget2D target(device, 7, 5, true, SurfaceFormat::Color, DepthFormat::None,
                                  0, RenderTargetUsage::PreserveContents);
            auto* renderer = dynamic_cast<SkiaRenderTargetRenderer*>(target.GetRenderTargetRenderer());
            Check(renderer != nullptr && renderer->MipLevelCountEXT() == 3,
                  "odd 7x5 target exposes the complete 7x5 -> 3x2 -> 1x1 chain");
            if (!renderer)
            {
                Exit();
                return;
            }

            std::vector<Color> level0 = Pattern(7, 5, 5);
            target.SetData(0, nullptr, level0.data(), 0, static_cast<int>(level0.size()));
            std::vector<Color> expectedLevel1 = BoxMip(level0, 7, 5, 3, 2);
            std::vector<Color> expectedLevel2 = BoxMip(expectedLevel1, 3, 2, 1, 1);
            std::vector<Color> level1(6, Color::Transparent);
            std::vector<Color> level2(1, Color::Transparent);
            target.GetData(1, nullptr, level1.data(), 0, 6);
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(SameColors(level1, expectedLevel1) && SameColors(level2, expectedLevel2)
                      && renderer->MipGenerationCountEXT(1) == 1u
                      && renderer->MipGenerationCountEXT(2) == 1u,
                  "level-zero SetData eagerly generates the complete odd chain exactly once");

            expectedLevel1 = Pattern(3, 2, 91);
            target.SetData(1, nullptr, expectedLevel1.data(), 0, 6);
            expectedLevel2 = BoxMip(expectedLevel1, 3, 2, 1, 1);
            target.GetData(1, nullptr, level1.data(), 0, 6);
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(SameColors(level1, expectedLevel1) && SameColors(level2, expectedLevel2)
                      && renderer->MipGenerationCountEXT(1) == 1u
                      && renderer->MipGenerationCountEXT(2) == 2u,
                  "higher-level SetData replaces its level and regenerates only descendants");

            const Rectangle patchRect(2, 1, 1, 1);
            const Color patch(3, 241, 127, 61);
            target.SetData(1, &patchRect, &patch, 0, 1);
            expectedLevel1[5] = patch;
            expectedLevel2 = BoxMip(expectedLevel1, 3, 2, 1, 1);
            target.GetData(1, nullptr, level1.data(), 0, 6);
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(SameColors(level1, expectedLevel1) && SameColors(level2, expectedLevel2)
                      && renderer->MipGenerationCountEXT(2) == 3u,
                  "partial target mip upload seeds untouched texels from live renderer bytes");

            level0 = Pattern(7, 5, 173);
            target.SetData(level0.data(), static_cast<int>(level0.size()));
            expectedLevel1 = BoxMip(level0, 7, 5, 3, 2);
            expectedLevel2 = BoxMip(expectedLevel1, 3, 2, 1, 1);
            target.GetData(1, nullptr, level1.data(), 0, 6);
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(SameColors(level1, expectedLevel1) && SameColors(level2, expectedLevel2)
                      && renderer->MipGenerationCountEXT(1) == 2u
                      && renderer->MipGenerationCountEXT(2) == 4u,
                  "later parent upload replaces prior explicit target descendants like EasyGL resolve");

            device.SetRenderTarget(&target);
            device.Clear(kBlack);
            fill_->SetData(&kGreen, 1);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                const_cast<SamplerState*>(&SamplerState::PointClamp),
                                nullptr, nullptr);
            spriteBatch_->Draw(*fill_, Rectangle(0, 0, 7, 5), Color::White);
            spriteBatch_->Draw(*fill_, Rectangle(0, 0, 7, 5), Color::White);
            spriteBatch_->End();
            Check(renderer->MipLevelDirtyEXT(1) && renderer->MipLevelDirtyEXT(2)
                      && renderer->MipGenerationCountEXT(1) == 2u
                      && renderer->MipGenerationCountEXT(2) == 4u,
                  "multiple writes in one pass only mark descendants dirty");
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            target.GetData(1, nullptr, level1.data(), 0, 6);
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(AllColor(level1, kGreen) && AllColor(level2, kGreen)
                      && renderer->MipGenerationCountEXT(1) == 3u
                      && renderer->MipGenerationCountEXT(2) == 5u,
                  "unbind regenerates each dirty descendant exactly once");

            device.SetRenderTarget(&target);
            device.Clear(kRed);
            std::vector<Color> renderedLevel0(35, Color::Transparent);
            target.GetData(0, nullptr, renderedLevel0.data(), 0, 35);
            const std::uint64_t readBarrierLevel1 = renderer->MipGenerationCountEXT(1);
            const std::uint64_t readBarrierLevel2 = renderer->MipGenerationCountEXT(2);
            target.GetData(2, nullptr, level2.data(), 0, 1);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(AllColor(renderedLevel0, kRed) && AllColor(level2, kRed)
                      && readBarrierLevel1 == 4u && readBarrierLevel2 == 6u
                      && renderer->MipGenerationCountEXT(1) == readBarrierLevel1
                      && renderer->MipGenerationCountEXT(2) == readBarrierLevel2,
                  "active-target readback resolves once and later readback/unbind do no duplicate work");

            device.SetRenderTarget(&target);
            device.Clear(kBlue);
            const auto resolvedSnapshot = renderer->SnapshotMipLevelEXT(
                2, SkiaSourceAlphaConvention::Premultiplied);
            const std::uint64_t snapshotBarrierLevel1 = renderer->MipGenerationCountEXT(1);
            const std::uint64_t snapshotBarrierLevel2 = renderer->MipGenerationCountEXT(2);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Check(resolvedSnapshot && resolvedSnapshot->width() == 1
                      && resolvedSnapshot->height() == 1
                      && snapshotBarrierLevel1 == 5u && snapshotBarrierLevel2 == 7u
                      && renderer->MipGenerationCountEXT(1) == snapshotBarrierLevel1
                      && renderer->MipGenerationCountEXT(2) == snapshotBarrierLevel2,
                  "sampling snapshot is the same one-shot resolve barrier as readback and unbind");

            device.SetRenderTarget(&target);
            device.Clear(kGreen);
            const std::uint64_t beforeFailedBind1 = renderer->MipGenerationCountEXT(1);
            const std::uint64_t beforeFailedBind2 = renderer->MipGenerationCountEXT(2);
            ForeignRenderTarget foreignType;
            bool foreignTypeRejected = false;
            try
            {
                graphicsRenderer->SetRenderTarget2D(&foreignType);
            }
            catch (const std::exception&)
            {
                foreignTypeRejected = true;
            }
            auto foreignBinding = std::make_shared<SkiaRenderTargetBinding>();
            SkiaRenderTargetRenderer foreignDevice(2, 2, true, foreignBinding, {}, true);
            bool foreignDeviceRejected = false;
            try
            {
                graphicsRenderer->SetRenderTarget2D(&foreignDevice);
            }
            catch (const std::exception&)
            {
                foreignDeviceRejected = true;
            }
            Check(foreignTypeRejected && foreignDeviceRejected
                      && renderer->MipLevelDirtyEXT(1) && renderer->MipLevelDirtyEXT(2)
                      && renderer->MipGenerationCountEXT(1) == beforeFailedBind1
                      && renderer->MipGenerationCountEXT(2) == beforeFailedBind2,
                  "failed foreign and cross-device binds preserve the active dirty chain");
            device.Clear(kYellow);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(AllColor(level2, kYellow)
                      && renderer->MipGenerationCountEXT(1) == beforeFailedBind1 + 1u
                      && renderer->MipGenerationCountEXT(2) == beforeFailedBind2 + 1u,
                  "the prior target remains selected and resolves normally after failed binds");

            const auto cachedBeforeRecovery = renderer->SnapshotMipLevelEXT(
                2, SkiaSourceAlphaConvention::Premultiplied);
            device.SetRenderTarget(&target);
            device.Clear(kBlue);
            const SkiaResourceStats beforeRecovery = graphicsRenderer->GetResourceStatsEXT();
            const std::uint64_t beforeRecoveryLevel1 = renderer->MipGenerationCountEXT(1);
            const std::uint64_t beforeRecoveryLevel2 = renderer->MipGenerationCountEXT(2);
            graphicsRenderer->DebugSimulateContextLoss();
            Check(cachedBeforeRecovery && SameStats(beforeRecovery,
                                                    graphicsRenderer->GetResourceStatsEXT())
                      && renderer->MipLevelDirtyEXT(1) && renderer->MipLevelDirtyEXT(2)
                      && renderer->MipGenerationCountEXT(1) == beforeRecoveryLevel1
                      && renderer->MipGenerationCountEXT(2) == beforeRecoveryLevel2,
                  "presenter recovery preserves a live dirty target without forcing a resolve");
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            target.GetData(1, nullptr, level1.data(), 0, 6);
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(AllColor(level1, kBlue) && AllColor(level2, kBlue)
                      && renderer->MipGenerationCountEXT(1) == beforeRecoveryLevel1 + 1u
                      && renderer->MipGenerationCountEXT(2) == beforeRecoveryLevel2 + 1u,
                  "target descendants remain exact after presenter recovery and later resolve");

            device.SetRenderTarget(&target);
            const std::uint64_t beforeSelfSampleLevel1 = renderer->MipGenerationCountEXT(1);
            const std::uint64_t beforeSelfSampleLevel2 = renderer->MipGenerationCountEXT(2);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                const_cast<SamplerState*>(&SamplerState::PointClamp),
                                nullptr, nullptr);
            spriteBatch_->Draw(target, Rectangle(0, 0, 7, 5), Rectangle(0, 0, 7, 5),
                               Color::White);
            spriteBatch_->End();
            Check(renderer->MipLevelDirtyEXT(1) && renderer->MipLevelDirtyEXT(2)
                      && renderer->MipGenerationCountEXT(1) == beforeSelfSampleLevel1
                      && renderer->MipGenerationCountEXT(2) == beforeSelfSampleLevel2,
                  "self-sampling resolves its source before marking the destination pass dirty");
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            target.GetData(2, nullptr, level2.data(), 0, 1);
            Check(AllColor(level2, kBlue)
                      && renderer->MipGenerationCountEXT(1) == beforeSelfSampleLevel1 + 1u
                      && renderer->MipGenerationCountEXT(2) == beforeSelfSampleLevel2 + 1u,
                  "self-sampling draw receives one later descendant regeneration");

            device.Clear(kBlack);
            SamplerState pointMipPoint;
            pointMipPoint.setFilterProperty(TextureFilter::Point);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                &pointMipPoint, nullptr, nullptr);
            spriteBatch_->Draw(target, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 7, 5),
                               Color::White);
            spriteBatch_->End();
            Color sampled(0, 0, 0, 0);
            const Rectangle sampledPixel(0, 0, 1, 1);
            device.GetBackBufferData(&sampledPixel, &sampled, 0, 1);
            Check(Same(sampled, kBlue),
                  "public minification samples the recovered generated final mip");
        }

        Check(SameStats(graphicsRenderer->GetResourceStatsEXT(), baseline),
              "generated target chain, surfaces, and snapshot return every counter to baseline");
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
    std::unique_ptr<Texture2D> fill_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaRenderTarget2DMipGenerationTest game;
    game.Run();
    return game.Result();
}
