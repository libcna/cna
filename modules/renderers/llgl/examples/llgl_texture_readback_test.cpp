// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-19: byte-exact round-trip through the LLGL renderer's own texture upload and
// readback path.
//
// This deliberately drives ITextureRenderer directly rather than going through Texture2D. The
// public Texture2D::GetData() answers from its own CPU-side pixel shadow whenever it has one, so a
// test written against the public API would pass without the GPU ever being asked anything -- it
// would prove the shadow works, not the renderer. The renderer path this test does exercise is the
// one RenderTarget2D readback will depend on (LLGL-26).
//
// Check A -- a full-surface upload reads back byte-for-byte identical.
// Check B -- a sub-rectangle reads back exactly the texels of that rectangle, in row order.
// Check C -- UpdatePixels() replaces level 0's content, and the new content reads back.
// Check D -- UpdatePixelsLevel() writes a specific mip level, and that level reads back
//   independently of level 0 (which must keep its own, different content).
// Check E -- a destination buffer too small for the requested region is refused, not partially
//   filled: the shared layer treats false as "read nothing", so a renderer that wrote half a buffer
//   and returned true would hand the caller fabricated pixels.
// Check F -- a mip level the texture does not have is refused.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/Llgl/LlglRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::Llgl;
using CNA::Internal::Graphics::ImageData;

namespace
{
    constexpr int kExpectedChecks = 6;

    // 4x4 RGBA8 where every texel is distinct and derivable from its coordinates, so a swapped
    // row, a transposed axis or an off-by-one offset all produce a different value rather than a
    // coincidentally matching one.
    std::vector<std::uint8_t> MakeGradientPixels(int width, int height, std::uint8_t seed)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height) * 4u, 0);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
                pixels[offset + 0] = static_cast<std::uint8_t>(seed + x * 16);
                pixels[offset + 1] = static_cast<std::uint8_t>(seed + y * 16);
                pixels[offset + 2] = static_cast<std::uint8_t>(seed + x * 4 + y);
                pixels[offset + 3] = 255;
            }
        }
        return pixels;
    }
}

class LlglTextureReadbackTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& renderer = static_cast<LlglRenderer&>(getGraphicsDeviceProperty().GetRenderer());

        // --- Check A: full-surface round-trip -------------------------------------------------
        ImageData image;
        image.width = 4;
        image.height = 4;
        image.mipLevels = 2;
        image.pixels = MakeGradientPixels(4, 4, 10);

        auto texture = renderer.CreateTexture(image);
        std::vector<std::uint8_t> readback(4 * 4 * 4, 0);
        bool ok = texture->GetData(0, 0, 0, 4, 4, readback.data(), static_cast<int>(readback.size()));
        check(ok && readback == image.pixels, "a full-surface upload reads back byte-for-byte");

        // --- Check B: sub-rectangle ------------------------------------------------------------
        std::vector<std::uint8_t> subRegion(2 * 2 * 4, 0);
        ok = texture->GetData(0, 1, 1, 2, 2, subRegion.data(), static_cast<int>(subRegion.size()));
        bool subMatches = ok;
        for (int y = 0; y < 2 && subMatches; ++y)
        {
            for (int x = 0; x < 2 && subMatches; ++x)
            {
                const std::size_t source = (static_cast<std::size_t>(y + 1) * 4 + (x + 1)) * 4;
                const std::size_t destination = (static_cast<std::size_t>(y) * 2 + x) * 4;
                for (int channel = 0; channel < 4; ++channel)
                {
                    if (subRegion[destination + channel] != image.pixels[source + channel])
                        subMatches = false;
                }
            }
        }
        check(subMatches, "a sub-rectangle reads back exactly its own texels, in row order");

        // --- Check C: UpdatePixels replaces level 0 --------------------------------------------
        const std::vector<std::uint8_t> replacement = MakeGradientPixels(4, 4, 200);
        texture->UpdatePixels(replacement.data(), 4 * 4);
        std::fill(readback.begin(), readback.end(), 0);
        ok = texture->GetData(0, 0, 0, 4, 4, readback.data(), static_cast<int>(readback.size()));
        check(ok && readback == replacement, "UpdatePixels() replaces level 0 and the new content reads back");

        // --- Check D: UpdatePixelsLevel writes one specific level ------------------------------
        const std::vector<std::uint8_t> mipPixels = MakeGradientPixels(2, 2, 90);
        texture->UpdatePixelsLevel(1, mipPixels.data(), 2, 2);
        std::vector<std::uint8_t> mipReadback(2 * 2 * 4, 0);
        ok = texture->GetData(1, 0, 0, 2, 2, mipReadback.data(), static_cast<int>(mipReadback.size()));
        const bool mipIsOwnContent = ok && mipReadback == mipPixels;

        std::fill(readback.begin(), readback.end(), 0);
        const bool levelZeroUntouched =
            texture->GetData(0, 0, 0, 4, 4, readback.data(), static_cast<int>(readback.size())) &&
            readback == replacement;
        check(mipIsOwnContent && levelZeroUntouched,
              "UpdatePixelsLevel() writes one level, and level 0 keeps its own different content");

        // --- Check E/F: refusals ---------------------------------------------------------------
        std::vector<std::uint8_t> tooSmall(4 * 4 * 4 - 1, 0);
        check(!texture->GetData(0, 0, 0, 4, 4, tooSmall.data(), static_cast<int>(tooSmall.size())),
              "a destination buffer too small for the region is refused, not partially filled");

        check(!texture->GetData(7, 0, 0, 4, 4, readback.data(), static_cast<int>(readback.size())),
              "a mip level the texture does not have is refused");

        std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
        result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
        Exit();
    }

public:
    LlglTextureReadbackTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    LlglTextureReadbackTest game;
    game.Run();
    return game.getResult();
}
