// SPDX-License-Identifier: MS-PL
// SKIA-128: deterministic odd/NPOT mip generation and authored-level invalidation.

#include "CNA/Internal/Renderers/Skia/SkiaTextureRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using CNA::Internal::Renderers::Skia::SkiaTextureRenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
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
}

class InspectableTexture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureRenderer* SkiaRenderer() const
    {
        return dynamic_cast<SkiaTextureRenderer*>(GetRendererRaw());
    }
};

class SkiaTexture2DMipGenerationTest final : public Game
{
public:
    SkiaTexture2DMipGenerationTest()
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
        InspectableTexture2D texture(device, 7, 5, true, SurfaceFormat::Color);
        SkiaTextureRenderer* renderer = texture.SkiaRenderer();

        std::vector<Color> initialLevel1(6, Color(19, 23, 29, 31));
        texture.GetData(1, nullptr, initialLevel1.data(), 0, 6);
        Check(renderer != nullptr
                  && std::all_of(initialLevel1.begin(), initialLevel1.end(),
                                 [](Color value) { return Same(value, Color::Transparent); })
                  && renderer->MipGenerationCountEXT(1) == 0u
                  && renderer->MipGenerationCountEXT(2) == 0u,
              "new checked descendant levels are defined zeroes but have not been generated");

        std::vector<Color> level0 = Pattern(7, 5, 5);
        texture.SetData(0, nullptr, level0.data(), 0, static_cast<int>(level0.size()));
        std::vector<Color> expectedLevel1 = BoxMip(level0, 7, 5, 3, 2);
        std::vector<Color> expectedLevel2 = BoxMip(expectedLevel1, 3, 2, 1, 1);
        std::vector<Color> level1Read(6, Color::Transparent);
        std::vector<Color> level2Read(1, Color::Transparent);
        texture.GetData(1, nullptr, level1Read.data(), 0, 6);
        texture.GetData(2, nullptr, level2Read.data(), 0, 1);
        Check(SameColors(level1Read, expectedLevel1)
                  && SameColors(level2Read, expectedLevel2)
                  && renderer->MipGenerationCountEXT(1) == 1u
                  && renderer->MipGenerationCountEXT(2) == 1u,
              "level-zero upload generates the complete odd 7x5 -> 3x2 -> 1x1 chain exactly once");

        const Rectangle oddEdge(6, 4, 1, 1);
        const Color oddEdgeValue(251, 7, 199, 113);
        texture.SetData(0, &oddEdge, &oddEdgeValue, 0, 1);
        level0[34] = oddEdgeValue;
        expectedLevel1 = BoxMip(level0, 7, 5, 3, 2);
        expectedLevel2 = BoxMip(expectedLevel1, 3, 2, 1, 1);
        texture.GetData(1, nullptr, level1Read.data(), 0, 6);
        texture.GetData(2, nullptr, level2Read.data(), 0, 1);
        Check(SameColors(level1Read, expectedLevel1)
                  && SameColors(level2Read, expectedLevel2)
                  && renderer->MipGenerationCountEXT(1) == 2u
                  && renderer->MipGenerationCountEXT(2) == 2u,
              "partial level-zero update invalidates descendants and the final odd edge contributes once");

        const Rectangle level1PatchRect(2, 1, 1, 1);
        const Color level1Patch(3, 241, 127, 61);
        texture.SetData(1, &level1PatchRect, &level1Patch, 0, 1);
        expectedLevel1[5] = level1Patch;
        expectedLevel2 = BoxMip(expectedLevel1, 3, 2, 1, 1);
        texture.GetData(1, nullptr, level1Read.data(), 0, 6);
        texture.GetData(2, nullptr, level2Read.data(), 0, 1);
        Check(SameColors(level1Read, expectedLevel1)
                  && SameColors(level2Read, expectedLevel2)
                  && renderer->MipGenerationCountEXT(1) == 2u
                  && renderer->MipGenerationCountEXT(2) == 3u,
              "partial first-authored mip seeds untouched texels from generated bytes and rebuilds descendants");

        const std::vector<Color> replacementLevel0 = Pattern(7, 5, 173);
        texture.SetData(replacementLevel0.data(), static_cast<int>(replacementLevel0.size()));
        texture.GetData(1, nullptr, level1Read.data(), 0, 6);
        texture.GetData(2, nullptr, level2Read.data(), 0, 1);
        Check(SameColors(level1Read, expectedLevel1)
                  && SameColors(level2Read, expectedLevel2)
                  && renderer->MipGenerationCountEXT(1) == 2u
                  && renderer->MipGenerationCountEXT(2) == 3u,
              "later level-zero upload stops at the explicit level-1 ownership barrier");

        const Color explicitLevel2(211, 53, 97, 37);
        texture.SetData(2, nullptr, &explicitLevel2, 0, 1);
        const std::vector<Color> replacementLevel1 = Pattern(3, 2, 91);
        texture.SetData(1, nullptr, replacementLevel1.data(), 0,
                        static_cast<int>(replacementLevel1.size()));
        texture.GetData(2, nullptr, level2Read.data(), 0, 1);
        Check(Same(level2Read[0], explicitLevel2)
                  && renderer->MipGenerationCountEXT(2) == 3u,
              "explicit final mip survives a later parent upload without a redundant rebuild");

        Check(!renderer->HasDefinedMipLevel(-1) && !renderer->HasDefinedMipLevel(3)
                  && renderer->HasDefinedMipLevel(0) && renderer->HasDefinedMipLevel(2),
              "defined-level query accepts exactly the allocated chain");

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
    SkiaTexture2DMipGenerationTest game;
    game.Run();
    return game.Result();
}
