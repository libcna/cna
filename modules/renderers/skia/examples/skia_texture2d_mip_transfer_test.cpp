// SPDX-License-Identifier: MS-PL
// SKIA-127: exact public and renderer Texture2D transfers across every mip level.

#include "CNA/Internal/Renderers/Skia/SkiaTextureRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using CNA::Internal::Renderers::Skia::SkiaTextureRenderer;
using CNA::Internal::Graphics::ImageData;
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

    template <typename E, typename F>
    [[nodiscard]] bool Throws(F&& operation)
    {
        try { operation(); }
        catch (const E&) { return true; }
        catch (...) { return false; }
        return false;
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
                    (seed + x * 31 + y * 7) & 0xFF,
                    (seed * 3 + x * 11 + y * 37) & 0xFF,
                    (seed * 5 + x * 19 + y * 13) & 0xFF,
                    17 + ((seed + x * 29 + y * 41) % 220));
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<std::uint8_t> ToBytes(const std::vector<Color>& colors)
    {
        std::vector<std::uint8_t> bytes(colors.size() * 4u);
        for (std::size_t i = 0; i < colors.size(); ++i)
        {
            bytes[i * 4u + 0u] = colors[i].getRProperty();
            bytes[i * 4u + 1u] = colors[i].getGProperty();
            bytes[i * 4u + 2u] = colors[i].getBProperty();
            bytes[i * 4u + 3u] = colors[i].getAProperty();
        }
        return bytes;
    }
}

class SkiaTexture2DMipTransferTest final : public Game
{
public:
    SkiaTexture2DMipTransferTest()
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
        Texture2D texture(device, 7, 5, true, SurfaceFormat::Color);

        std::vector<Color> level0 = Pattern(7, 5, 3);
        std::vector<Color> level1 = Pattern(3, 2, 67);
        std::vector<Color> level2 = Pattern(1, 1, 149);
        const std::vector<Color> expectedLevel0 = level0;
        std::vector<Color> expectedLevel1 = level1;
        const std::vector<Color> expectedLevel2 = level2;
        texture.SetData(0, nullptr, level0.data(), 0, static_cast<int>(level0.size()));
        texture.SetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));
        texture.SetData(2, nullptr, level2.data(), 0, static_cast<int>(level2.size()));

        std::fill(level0.begin(), level0.end(), Color(1, 2, 3, 4));
        std::fill(level1.begin(), level1.end(), Color(5, 6, 7, 8));
        std::fill(level2.begin(), level2.end(), Color(9, 10, 11, 12));

        std::vector<Color> read0(35, Color::Transparent);
        std::vector<Color> read1(6, Color::Transparent);
        std::vector<Color> read2(1, Color::Transparent);
        texture.GetData(0, nullptr, read0.data(), 0, 35);
        texture.GetData(1, nullptr, read1.data(), 0, 6);
        texture.GetData(2, nullptr, read2.data(), 0, 1);
        Check(SameColors(read0, expectedLevel0)
                  && SameColors(read1, expectedLevel1)
                  && SameColors(read2, expectedLevel2),
              "full uploads/readbacks preserve arbitrary translucent bytes in all NPOT levels");

        const Rectangle patchRect(1, 0, 2, 1);
        const std::vector<Color> patch = {
            Color(200, 201, 202, 203),
            Color(31, 63, 95, 127),
            Color(129, 97, 65, 33),
        };
        texture.SetData(1, &patchRect, patch.data(), 1, 2);
        expectedLevel1[1] = patch[1];
        expectedLevel1[2] = patch[2];
        read1.assign(6, Color::Transparent);
        texture.GetData(1, nullptr, read1.data(), 0, 6);
        Check(SameColors(read1, expectedLevel1),
              "partial level-1 upload changes its addressed row and preserves untouched texels");

        const Color guard(222, 111, 77, 13);
        std::vector<Color> guarded(10, guard);
        texture.GetData(1, &patchRect, guarded.data(), 3, 6);
        Check(Same(guarded[3], patch[1]) && Same(guarded[4], patch[2])
                  && std::all_of(guarded.begin(), guarded.begin() + 3,
                                 [&](Color value) { return Same(value, guard); })
                  && std::all_of(guarded.begin() + 5, guarded.end(),
                                 [&](Color value) { return Same(value, guard); }),
              "partial GetData honors destination startIndex/excess capacity and touches no guards");

        const Rectangle level0PatchRect(5, 3, 2, 2);
        const std::vector<Color> level0Patch = {
            Color(10, 20, 30, 40), Color(50, 60, 70, 80),
            Color(90, 100, 110, 120), Color(130, 140, 150, 160),
        };
        texture.SetData(0, &level0PatchRect, level0Patch.data(), 0, 4);
        std::vector<Color> level0PatchRead(4, Color::Transparent);
        texture.GetData(0, &level0PatchRect, level0PatchRead.data(), 0, 4);
        Check(SameColors(level0PatchRead, level0Patch),
              "partial level-zero path remains byte-exact after migrating storage to the chain");

        std::vector<Color> invalidDestination(8, guard);
        const Rectangle outside(2, 1, 2, 2);
        Check(Throws<std::out_of_range>([&] {
                  texture.SetData(3, nullptr, patch.data(), 0, 1);
              })
                  && Throws<std::out_of_range>([&] {
                      texture.GetData(-1, nullptr, invalidDestination.data(), 0, 1);
                  })
                  && Throws<std::out_of_range>([&] {
                      texture.SetData(1, &outside, patch.data(), 0, 3);
                  })
                  && Throws<std::out_of_range>([&] {
                      texture.GetData(1, &patchRect, invalidDestination.data(), 0, 1);
                  })
                  && std::all_of(invalidDestination.begin(), invalidDestination.end(),
                                 [&](Color value) { return Same(value, guard); }),
              "invalid levels, rectangles and short reads reject before touching caller output");

        ImageData image;
        image.width = 7;
        image.height = 5;
        image.mipLevels = 3;
        image.pixels.assign(7u * 5u * 4u, 0u);
        SkiaTextureRenderer direct(image);
        std::vector<std::uint8_t> rendererLevel1 = ToBytes(expectedLevel1);
        const std::vector<std::uint8_t> expectedRendererLevel1 = rendererLevel1;
        direct.UpdatePixelsLevel(1, rendererLevel1.data(), 3, 2);
        std::fill(rendererLevel1.begin(), rendererLevel1.end(), 0xEEu);

        std::array<std::uint8_t, 16> region{};
        const bool regionRead = direct.GetData(1, 1, 0, 2, 2,
                                                region.data(), region.size());
        std::array<std::uint8_t, 16> expectedRegion{};
        for (int row = 0; row < 2; ++row)
        {
            std::copy_n(expectedRendererLevel1.data()
                            + (static_cast<std::size_t>(row) * 3u + 1u) * 4u,
                        8u, expectedRegion.data() + static_cast<std::size_t>(row) * 8u);
        }
        Check(regionRead && region == expectedRegion,
              "renderer owns caller bytes and reads a level-1 sub-rectangle with exact row pitch");

        bool wrongDimensionsRejected = false;
        try { direct.UpdatePixelsLevel(1, expectedRendererLevel1.data(), 2, 3); }
        catch (const std::runtime_error&) { wrongDimensionsRejected = true; }
        std::vector<std::uint8_t> fullRendererLevel1(expectedRendererLevel1.size());
        const bool unchangedRead = direct.GetData(1, 0, 0, 3, 2,
                                                   fullRendererLevel1.data(),
                                                   fullRendererLevel1.size());
        Check(wrongDimensionsRejected && unchangedRead
                  && fullRendererLevel1 == expectedRendererLevel1,
              "wrong upload dimensions reject atomically and preserve the complete level");

        region.fill(0xA5u);
        Check(!direct.GetData(3, 0, 0, 1, 1, region.data(), region.size())
                  && !direct.GetData(1, 2, 1, 2, 1, region.data(), region.size())
                  && !direct.GetData(1, 0, 0, 0, 1, region.data(), region.size())
                  && std::all_of(region.begin(), region.end(),
                                 [](std::uint8_t value) { return value == 0xA5u; }),
              "renderer invalid level/region/empty reads return false without touching output");

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
    SkiaTexture2DMipTransferTest game;
    game.Run();
    return game.Result();
}
