// SPDX-License-Identifier: MS-PL
//
// Bounded row-stride unit audit for the Skia texture path.
//
// The units in play are deliberately distinct and are named here rather than assumed:
//
//   texels     -- the public width/height a caller declares;
//   bytes/texel-- format-dependent (Alpha8 = 1, Bgr565 = 2, Color = 4, Vector4 = 16);
//   row bytes  -- width * bytes/texel for CNA's own tightly packed storage;
//   stride     -- the row pitch of whatever buffer the copy is reading from or writing to.
//
// Every width below is 13, so the level-0 row is 13, 26, 52 and 208 bytes respectively -- none of
// which is a multiple of 4, 8 or 16 for the narrow formats. Any code that rounds a row up to an
// alignment, or reads a Skia-preferred rowBytes where CNA's packed pitch was meant, shifts every
// row after the first by a constant and is visible as a diagonal shear.
//
// Mip level 1 is 6 x 3 (13 >> 1 == 6), so its rows are 6, 12, 24 and 96 bytes -- a *different*
// misalignment from level 0, which is what makes the two levels an independent pair rather than
// one check run twice.
//
// This is verified independently rather than by reusing another renderer's staging logic: Skia's
// storage is CNA-owned and tightly packed, whereas a mapped GPU allocation has a driver-chosen
// pitch, so the two cannot share a correctness argument. The GetData legs prove the CPU shadow;
// the drawn leg proves the sampling image Skia builds over that same storage, which is the half a
// byte-exact readback cannot see on its own.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 13;   // prime; no row is 4/8/16-aligned in the narrow formats
    constexpr int kHeight = 7;   // prime; level 1 is 6 x 3, a different misalignment
    constexpr int kLevel1Width = 6;
    constexpr int kLevel1Height = 3;

    /// A value that varies with BOTH axes, so a whole-row shift is distinguishable from a
    /// whole-image offset: row-major x+y alone would alias under a one-texel row shear.
    [[nodiscard]] std::uint8_t Pattern(int x, int y, int channel)
    {
        return static_cast<std::uint8_t>((x * 17 + y * 61 + channel * 5) & 0xFF);
    }

    /// The packed word for a PackedVector element, widened to its own storage width so the
    /// 1-byte and 2-byte formats share one generator without either losing a channel.
    template <typename T>
    [[nodiscard]] auto PackedFor(int x, int y, int channel, int bytes)
        -> decltype(std::declval<const T&>().getPackedValueProperty())
    {
        using Packed = decltype(std::declval<const T&>().getPackedValueProperty());
        Packed value = static_cast<Packed>(Pattern(x, y, channel));
        if (bytes > 1)
            value = static_cast<Packed>(value | (static_cast<Packed>(Pattern(x, y, channel + 1)) << 8));
        return value;
    }
}

class SkiaTextureRowStrideTest final : public Game
{
public:
    SkiaTextureRowStrideTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();

        RunByteWidth<PackedVector::Alpha8>(device, SurfaceFormat::Alpha8, "Alpha8", 1);
        RunByteWidth<PackedVector::Bgr565>(device, SurfaceFormat::Bgr565, "Bgr565", 2);
        RunColor(device);
        RunDrawnRows(device);

        std::printf("=== %d checks, %d failed ===\n", checks_, failures_);
        Exit();
    }

private:
    /// Level 0 and level 1 exact round trips for a format whose row bytes are not a multiple of
    /// four at either level. `T` is the format's public PackedVector transfer element.
    template <typename T>
    void RunByteWidth(GraphicsDevice& device, SurfaceFormat format, const char* name, int bytes)
    {
        Texture2D texture(device, kWidth, kHeight, true, format);

        std::vector<T> level0(static_cast<std::size_t>(kWidth) * kHeight);
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
                level0[static_cast<std::size_t>(y) * kWidth + x].setPackedValueProperty(
                    PackedFor<T>(x, y, 0, bytes));
        texture.SetData(0, nullptr, level0.data(), 0, static_cast<int>(level0.size()));

        std::vector<T> readback(level0.size());
        texture.GetData(0, nullptr, readback.data(), 0, static_cast<int>(readback.size()));
        Check(readback == level0,
              std::string(name) + " level 0: " + std::to_string(kWidth) + " texels x "
              + std::to_string(bytes) + " bytes = " + std::to_string(kWidth * bytes)
              + "-byte rows round trip exactly");

        // A sub-rectangle that starts mid-row and spans a row boundary: its own transfer pitch is
        // narrower than the level's, so a copy that reused the level pitch reads the wrong texels.
        const Rectangle window(3, 1, 7, 4);
        std::vector<T> partial(static_cast<std::size_t>(window.Width)
                               * window.Height);
        texture.GetData(0, &window, partial.data(), 0, static_cast<int>(partial.size()));
        bool exact = true;
        for (int y = 0; y < window.Height; ++y)
            for (int x = 0; x < window.Width; ++x)
                exact = exact && partial[static_cast<std::size_t>(y) * window.Width + x]
                        == level0[static_cast<std::size_t>(y + window.Y) * kWidth
                                  + x + window.X];
        Check(exact, std::string(name) + " level 0: a 7-texel sub-rectangle keeps its own "
                     "transfer pitch, not the level pitch");

        std::vector<T> level1(static_cast<std::size_t>(kLevel1Width) * kLevel1Height);
        for (int y = 0; y < kLevel1Height; ++y)
            for (int x = 0; x < kLevel1Width; ++x)
                level1[static_cast<std::size_t>(y) * kLevel1Width + x].setPackedValueProperty(
                    PackedFor<T>(x, y, 2, bytes));
        texture.SetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));

        std::vector<T> readback1(level1.size());
        texture.GetData(1, nullptr, readback1.data(), 0, static_cast<int>(readback1.size()));
        Check(readback1 == level1,
              std::string(name) + " level 1: " + std::to_string(kLevel1Width * bytes)
              + "-byte rows round trip exactly at a different misalignment");
    }

    void RunColor(GraphicsDevice& device)
    {
        Texture2D texture(device, kWidth, kHeight, true, SurfaceFormat::Color);
        std::vector<Color> level0(static_cast<std::size_t>(kWidth) * kHeight, Color(0, 0, 0, 0));
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
                level0[static_cast<std::size_t>(y) * kWidth + x] =
                    Color(Pattern(x, y, 0), Pattern(x, y, 1), Pattern(x, y, 2), 255);
        texture.SetData(level0.data(), static_cast<int>(level0.size()));

        std::vector<Color> readback(level0.size(), Color(0, 0, 0, 0));
        texture.GetData(readback.data(), static_cast<int>(readback.size()));
        Check(readback == level0, "Color level 0: 52-byte rows round trip exactly");
    }

    /// The half a byte-exact readback cannot see: the sampling image Skia builds over the same
    /// packed storage. Drawn 1:1 into a 13 x 7 backbuffer, a row-pitch mismatch shears the image
    /// diagonally, so every texel is compared, not a corner sample.
    void RunDrawnRows(GraphicsDevice& device)
    {
        Texture2D texture(device, kWidth, kHeight, false, SurfaceFormat::Color);
        std::vector<Color> pixels(static_cast<std::size_t>(kWidth) * kHeight, Color(0, 0, 0, 0));
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
                pixels[static_cast<std::size_t>(y) * kWidth + x] =
                    Color(Pattern(x, y, 0), Pattern(x, y, 1), Pattern(x, y, 2), 255);
        texture.SetData(pixels.data(), static_cast<int>(pixels.size()));

        SpriteBatch batch(device);
        device.Clear(Color(0, 0, 0, 255));
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                    const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        batch.Draw(texture, Vector2(0.0f, 0.0f), Color::White);
        batch.End();

        std::vector<Color> shown(pixels.size(), Color(0, 0, 0, 0));
        device.GetBackBufferData(shown.data(), static_cast<int>(shown.size()));

        int firstBadX = -1;
        int firstBadY = -1;
        for (int y = 0; y < kHeight && firstBadY < 0; ++y)
            for (int x = 0; x < kWidth; ++x)
                if (!(shown[static_cast<std::size_t>(y) * kWidth + x] == pixels[static_cast<std::size_t>(y) * kWidth + x]))
                {
                    firstBadX = x;
                    firstBadY = y;
                    break;
                }
        Check(firstBadY < 0,
              firstBadY < 0
                  ? "sampled image: all 91 texels of a 13-wide draw land on their own row"
                  : "sampled image: first divergence at (" + std::to_string(firstBadX) + ","
                        + std::to_string(firstBadY) + ") -- a row-pitch shear");
    }

    void Check(bool condition, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaTextureRowStrideTest game;
    game.Run();
    return game.Result();
}
