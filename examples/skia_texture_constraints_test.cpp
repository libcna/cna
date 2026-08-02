// SPDX-License-Identifier: MS-PL
// Public Texture2D refusal matrix for the SKIA raster backend. Color, the three SKIA-135 packed
// formats, and the two SKIA-136 colour formats have dedicated positive fixtures; every other
// format remains a creation-time refusal, while valid dimensions reach the declared device limit.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SkiaTextureConstraintsTest final : public Game
{
public:
    SkiaTextureConstraintsTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(16);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();

        Texture2D onePixel(device, 1, 1, false, SurfaceFormat::Color);
        const Color expected(23, 127, 231, 91);
        onePixel.SetData(&expected, 1);
        Color actual(0, 0, 0, 0);
        onePixel.GetData(&actual, 0, 1);
        Check(Same(actual, expected), "1x1 Color texture round-trips exact straight-alpha CPU bytes");

        Texture2D npot(device, 3, 5, false, SurfaceFormat::Color);
        std::vector<Color> npotPixels;
        npotPixels.reserve(15);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 3; ++x)
                npotPixels.emplace_back(x * 80, y * 45, 123, 255);
        npot.SetData(npotPixels.data(), static_cast<int>(npotPixels.size()));
        std::vector<Color> npotReadback(npotPixels.size(), Color(0, 0, 0, 0));
        npot.GetData(npotReadback.data(), 0, static_cast<int>(npotReadback.size()));
        Check(npotReadback == npotPixels, "3x5 NPOT Color texture round-trips exact bytes");

        for (const FormatCase& formatCase : UnsupportedFormats())
        {
            Check(Throws([&](GraphicsDevice& d) {
                      Texture2D texture(d, 2, 2, false, formatCase.format);
                      (void)texture;
                  }, device),
                  std::string(formatCase.name) + " is rejected before backend allocation");
        }

        const int maxDimension = device.GetMaxTextureDimension();
        Check(maxDimension > 0, "backend reports a positive maximum texture dimension");
        Check(Throws([&](GraphicsDevice& d) {
                  Texture2D texture(d, maxDimension + 1, 1, false, SurfaceFormat::Color);
                  (void)texture;
              }, device),
              "dimension one above the reported maximum is rejected before allocation");
        Check(Throws([](GraphicsDevice& d) { Texture2D texture(d, 0, 1, false, SurfaceFormat::Color); (void)texture; }, device),
              "zero texture width is rejected");

        // Exercise a comfortably bounded but high single-axis size; it catches arithmetic and
        // row-stride assumptions without asking the test host to reserve a maximum-sized image.
        const int largeWidth = std::min(maxDimension, 4096);
        Texture2D large(device, largeWidth, 1, false, SurfaceFormat::Color);
        Check(large.getWidthProperty() == largeWidth && large.getHeightProperty() == 1,
              "large valid single-axis Color texture is created without dimension clamping");
        Exit();
    }

private:
    static bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    struct FormatCase
    {
        SurfaceFormat format;
        const char* name;
    };

    static const std::array<FormatCase, 21>& UnsupportedFormats()
    {
        static constexpr std::array formats{
            FormatCase{SurfaceFormat::Bgra5551, "Bgra5551"},
            FormatCase{SurfaceFormat::Dxt1, "Dxt1"},
            FormatCase{SurfaceFormat::Dxt3, "Dxt3"},
            FormatCase{SurfaceFormat::Dxt5, "Dxt5"},
            FormatCase{SurfaceFormat::NormalizedByte2, "NormalizedByte2"},
            FormatCase{SurfaceFormat::NormalizedByte4, "NormalizedByte4"},
            FormatCase{SurfaceFormat::Rg32, "Rg32"},
            FormatCase{SurfaceFormat::Rgba64, "Rgba64"},
            FormatCase{SurfaceFormat::Alpha8, "Alpha8"},
            FormatCase{SurfaceFormat::Single, "Single"},
            FormatCase{SurfaceFormat::Vector2, "Vector2"},
            FormatCase{SurfaceFormat::Vector4, "Vector4"},
            FormatCase{SurfaceFormat::HalfSingle, "HalfSingle"},
            FormatCase{SurfaceFormat::HalfVector2, "HalfVector2"},
            FormatCase{SurfaceFormat::HalfVector4, "HalfVector4"},
            FormatCase{SurfaceFormat::HdrBlendable, "HdrBlendable"},
            FormatCase{SurfaceFormat::Dxt5SrgbEXT, "Dxt5SrgbEXT"},
            FormatCase{SurfaceFormat::Bc7EXT, "Bc7EXT"},
            FormatCase{SurfaceFormat::Bc7SrgbEXT, "Bc7SrgbEXT"},
            FormatCase{SurfaceFormat::ByteEXT, "ByteEXT"},
            FormatCase{SurfaceFormat::UShortEXT, "UShortEXT"},
        };
        return formats;
    }

    template <typename Callable>
    static bool Throws(Callable&& callable, GraphicsDevice& device)
    {
        try { callable(device); }
        catch (const std::exception&) { return true; }
        return false;
    }

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    bool done_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaTextureConstraintsTest game;
    game.Run();
    return game.Result();
}
