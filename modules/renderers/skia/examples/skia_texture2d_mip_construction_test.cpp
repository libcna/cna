// SPDX-License-Identifier: MS-PL
// SKIA-126: public Texture2D mip construction, properties, zeroed levels and lifecycle.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>

using CNA::Internal::Renderers::Skia::SkiaRenderer;
using CNA::Internal::Renderers::Skia::SkiaResourceCounters;
using CNA::Internal::Renderers::Skia::SkiaResourceStats;
using CNA::Internal::Renderers::Skia::SkiaTextureRenderer;
using CNA::Internal::Graphics::ImageData;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    [[nodiscard]] bool SameStats(const SkiaResourceStats& left,
                                 const SkiaResourceStats& right)
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

    template <typename F>
    [[nodiscard]] bool Throws(F&& operation)
    {
        try { operation(); }
        catch (const std::exception&) { return true; }
        return false;
    }
}

class SkiaTexture2DMipConstructionTest final : public Game
{
public:
    SkiaTexture2DMipConstructionTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(1);
        graphics_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<SkiaRenderer*>(&device.GetRenderer());
        Check(renderer != nullptr, "test is running through the Skia graphics renderer");
        if (!renderer)
        {
            Exit();
            return;
        }

        const SkiaResourceStats baseline = renderer->GetResourceStatsEXT();
        {
            Texture2D texture(device, 7, 5, true, SurfaceFormat::Color);
            Check(texture.getWidthProperty() == 7 && texture.getHeightProperty() == 5
                      && texture.getBoundsProperty().Width == 7
                      && texture.getBoundsProperty().Height == 5
                      && texture.getFormatProperty() == SurfaceFormat::Color
                      && texture.getLevelCountProperty() == 3,
                  "odd NPOT public properties report the complete 7x5, 3x2, 1x1 chain");
            const SkiaResourceStats live = renderer->GetResourceStatsEXT();
            Check(live.textureRenderers == baseline.textureRenderers + 1u
                      && live.mipChains2D == baseline.mipChains2D + 1u
                      && live.mipChain2DStorageBytes
                            == baseline.mipChain2DStorageBytes + 168u,
                  "public construction accounts one exact complete RGBA8 chain");

            texture.Dispose();
            Check(SameStats(renderer->GetResourceStatsEXT(), baseline),
                  "explicit public disposal releases mip storage and image views immediately");
        }
        Check(SameStats(renderer->GetResourceStatsEXT(), baseline),
              "disposed Texture2D destruction does not double-release counters");

        {
            Texture2D vertical(device, 1, 9, true, SurfaceFormat::Color);
            Texture2D horizontal(device, 9, 1, true, SurfaceFormat::Color);
            Texture2D unit(device, 1, 1, true, SurfaceFormat::Color);
            Texture2D flat(device, 7, 5, false, SurfaceFormat::Color);
            Check(vertical.getLevelCountProperty() == 4
                      && horizontal.getLevelCountProperty() == 4
                      && unit.getLevelCountProperty() == 1
                      && flat.getLevelCountProperty() == 1,
                  "one-dimensional, unit, and non-mipped public level counts are exact");
        }
        Check(SameStats(renderer->GetResourceStatsEXT(), baseline),
              "scoped public mip textures return every resource counter to baseline");

        const int maximumAxis = device.GetMaxTextureDimension();
        {
            Texture2D maximumWidth(device, maximumAxis, 1, true, SurfaceFormat::Color);
            Check(maximumAxis == 16384 && maximumWidth.getLevelCountProperty() == 15,
                  "maximum legal one-dimensional axis allocates all 15 levels through 1x1");
        }
        Check(SameStats(renderer->GetResourceStatsEXT(), baseline),
              "maximum-axis legal chain releases its bounded allocation");

        Check(Throws([&] {
                  Texture2D invalid(device, 0, 4, true, SurfaceFormat::Color);
                  (void)invalid;
              })
                  && Throws([&] {
                      Texture2D invalid(device, maximumAxis + 1, 1, true,
                                        SurfaceFormat::Color);
                      (void)invalid;
                  })
                  && SameStats(renderer->GetResourceStatsEXT(), baseline),
              "zero and over-maximum public dimensions reject without publishing resources");

        auto directCounters = std::make_shared<SkiaResourceCounters>();
        ImageData data;
        data.width = 7;
        data.height = 5;
        data.mipLevels = 3;
        data.pixels.assign(7u * 5u * 4u, 0x7Bu);
        {
            SkiaTextureRenderer direct(data, directCounters);
            const auto& chain = direct.MipChainEXT();
            const bool levelZeroCopied = std::all_of(
                chain.LevelData(0), chain.LevelData(0) + chain.Level(0).bytes,
                [](std::uint8_t value) { return value == 0x7Bu; });
            bool descendantsZero = true;
            for (int level = 1; level < chain.LevelCount(); ++level)
            {
                descendantsZero = descendantsZero && std::all_of(
                    chain.LevelData(level), chain.LevelData(level) + chain.Level(level).bytes,
                    [](std::uint8_t value) { return value == 0u; });
            }
            Check(direct.MipLevelCountEXT() == 3 && levelZeroCopied && descendantsZero,
                  "renderer copies authored level zero and zero-initializes every descendant");
        }
        Check(directCounters->GetStats().textureRenderers == 0u
                  && directCounters->GetStats().mipChains2D == 0u
                  && directCounters->GetStats().mipChain2DStorageBytes == 0u,
              "direct renderer destruction releases image and chain accounting");

        data.width = 8;
        data.height = 8;
        data.mipLevels = 2;
        data.pixels.assign(8u * 8u * 4u, 0u);
        Check(Throws([&] { SkiaTextureRenderer incomplete(data, directCounters); })
                  && directCounters->GetStats().textureRenderers == 0u
                  && directCounters->GetStats().mipChains2D == 0u,
              "incomplete internal level counts reject without allocating or accounting");

        Exit();
    }

    void Draw(const GameTime&) override {}

private:
    void Check(bool pass, const char* label)
    {
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label);
        if (!pass) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    int failures_ = 0;
};

int main()
{
    SkiaTexture2DMipConstructionTest game;
    game.Run();
    return game.Result();
}
