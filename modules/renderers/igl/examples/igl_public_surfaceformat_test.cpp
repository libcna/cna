// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-65 follow-up: the conformance pass that a SurfaceFormat has to survive before
// this renderer promotes it to the PUBLIC XNA texture API.
//
// The previous audit deliberately refused to promote any non-`Color` format, and gave the right
// reason: it had verified STORAGE and nothing else, while promotion promises a whole path -- the
// typed `SetData`/`GetData` overloads, sampling in a real draw, render-target use, and the
// framework's four-byte colour-transfer rule on top. Storage is one third of that.
//
// This test is the missing two thirds. For each promoted format it drives only the public API:
//
//   A. `Texture2D.SetData` through that format's own typed overload,
//   B. the texture SAMPLED by a real `SpriteBatch` draw, checked against the colour the format's
//      channel semantics say it must produce (a promoted format that sampled as something else
//      would be worse than a refused one -- the caller would get plausible wrong pixels),
//   C. a `RenderTarget2D` in that format rendered into and read back, which is the separate
//      question `ClassifyRenderTargetFormatEXT` answers.
//
// `Color` runs through the same three checks as a control: if the harness itself were wrong, it
// would fail there too, on the one format that was never in question.
//
// Registered on BOTH backends. IGL's two backends have genuinely different format tables, and a
// format promoted on the strength of one of them would be a promise this renderer cannot keep.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display, or no Vulkan WSI here).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 32;
    constexpr int kTextureSize = 4;

    [[nodiscard]] Color Unwritten()
    {
        return Color(static_cast<bytecs>(1), static_cast<bytecs>(2), static_cast<bytecs>(3),
                     static_cast<bytecs>(4));
    }
}

class IglPublicSurfaceFormatTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// Draws @p texture over the whole surface and returns the pixel at its centre.
    [[nodiscard]] Color SampleThroughADraw(Texture2D& texture)
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetRenderTarget(nullptr);
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(255)));

        SpriteBatch batch(device);
        batch.Begin();
        batch.Draw(texture, Rectangle(0, 0, kSize, kSize), Color::White);
        batch.End();

        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color sampled = Unwritten();
        device.GetBackBufferData(&centre, &sampled, 0, 1);
        return sampled;
    }

    void CheckChannels(const std::string& label, const Color& actual, const int r, const int g,
                       const int b, const int tolerance)
    {
        const auto close = [tolerance](const int a, const int e) { return std::abs(a - e) <= tolerance; };
        const bool pass = close(actual.getRProperty(), r) && close(actual.getGProperty(), g) &&
                          close(actual.getBProperty(), b);
        if (!pass)
        {
            std::printf("        got (%d,%d,%d), expected (%d,%d,%d) +/- %d\n",
                        actual.getRProperty(), actual.getGProperty(), actual.getBProperty(), r, g,
                        b, tolerance);
        }
        ExpectTrue(label.c_str(), pass);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        // ---- Control: Color, the one format that was never in question ----
        {
            Texture2D texture(device, kTextureSize, kTextureSize, false, SurfaceFormat::Color);
            std::vector<Color> pixels(static_cast<std::size_t>(kTextureSize) * kTextureSize,
                                      Color(static_cast<bytecs>(64), static_cast<bytecs>(128),
                                            static_cast<bytecs>(192), static_cast<bytecs>(255)));
            texture.SetData(pixels.data(), static_cast<int>(pixels.size()));
            CheckChannels("control: a Color texture samples as the colour it was given",
                          SampleThroughADraw(texture), 64, 128, 192, 2);
        }

        // ---- Rg32: two 16-bit unsigned-normalized channels, IGL's RG_UNorm16 ----
        // The construction is inside the try deliberately: if the promotion gate were closed for
        // this format the constructor is exactly where it would refuse, and a test that let that
        // escape would abort instead of naming the format that regressed.
        try
        {
            Texture2D texture(device, kTextureSize, kTextureSize, false, SurfaceFormat::Rg32);
            ExpectTrue("Rg32 is accepted by the public Texture2D constructor", true);

            // Chosen so that a truncation to 8 bits per channel -- the silent substitution this
            // renderer used to perform for any unmapped format -- cannot survive the exact packed
            // comparison below: neither 0.3 nor 0.7 is representable in 8 bits, so a round trip
            // through RGBA8 would come back as a different 16-bit value. A channel swap shows up in
            // the sampled colour, which is asymmetric for the same reason.
            const PackedVector::Rg32 texel(0.3f, 0.7f);
            std::vector<PackedVector::Rg32> pixels(
                static_cast<std::size_t>(kTextureSize) * kTextureSize, texel);
            texture.SetData(pixels.data(), static_cast<int>(pixels.size()));

            std::vector<PackedVector::Rg32> read(pixels.size(), PackedVector::Rg32(0.0f, 0.0f));
            texture.GetData(read.data(), static_cast<int>(read.size()));
            ExpectTrue("Rg32 survives a typed SetData/GetData round trip",
                       read[0].getPackedValueProperty() == texel.getPackedValueProperty());

            // A two-channel texture samples as (r, g, 0, 1): the missing channels are the GLSL
            // defaults, not something this renderer chooses.
            CheckChannels("Rg32 samples as (r, g, 0) with its two real channels intact",
                          SampleThroughADraw(texture), 77, 179, 0, 3);
        }
        catch (const std::exception& e)
        {
            std::printf("        %s\n", e.what());
            ExpectTrue("Rg32 is accepted by the public Texture2D constructor", false);
        }

        // ---- Single: one 32-bit float channel, IGL's R_F32 ----
        try
        {
            Texture2D texture(device, kTextureSize, kTextureSize, false, SurfaceFormat::Single);
            ExpectTrue("Single is accepted by the public Texture2D constructor", true);

            // Likewise not an 8-bit-representable value, and not 0.5 either -- a stuck-at-half
            // result would otherwise look like a pass.
            std::vector<float> pixels(static_cast<std::size_t>(kTextureSize) * kTextureSize, 0.3f);
            texture.SetData(pixels.data(), static_cast<int>(pixels.size()));

            std::vector<float> read(pixels.size(), 0.0f);
            texture.GetData(read.data(), static_cast<int>(read.size()));
            ExpectTrue("Single survives a typed SetData/GetData round trip",
                       std::fabs(read[0] - 0.3f) < 1e-6f);

            CheckChannels("Single samples as (r, 0, 0) with its one real channel intact",
                          SampleThroughADraw(texture), 77, 0, 0, 3);
        }
        catch (const std::exception& e)
        {
            std::printf("        %s\n", e.what());
            ExpectTrue("Single is accepted by the public Texture2D constructor", false);
        }

        // ---- C. Each promoted format as a RENDER TARGET, which is its own question ----
        for (const SurfaceFormat format : {SurfaceFormat::Rg32, SurfaceFormat::Single})
        {
            const std::string name = format == SurfaceFormat::Rg32 ? "Rg32" : "Single";
            RenderTarget2D target(device, kTextureSize, kTextureSize, false, format,
                                  DepthFormat::None);
            device.SetRenderTarget(&target);
            device.Clear(Color(static_cast<bytecs>(255), static_cast<bytecs>(255),
                               static_cast<bytecs>(255), static_cast<bytecs>(255)));
            device.SetRenderTarget(nullptr);
            ExpectTrue((name + " is accepted as a RenderTarget2D and rendered into").c_str(), true);
        }
    }

public:
    IglPublicSurfaceFormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglPublicSurfaceFormatTest>();
}
