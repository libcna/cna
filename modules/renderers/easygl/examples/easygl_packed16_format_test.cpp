// SPDX-License-Identifier: MS-PL
// REMED-GFX-244: the three packed 16-bit SurfaceFormats GraphicsProfile.Reach permits --
// Bgr565, Bgra5551 and Bgra4444 -- uploaded through their typed SetData overloads and then
// SAMPLED in a real draw, which is the bar IGL-71 set: storage alone is not promotion.
//
// The channel order is the whole difficulty. XNA packs Bgr565 as R:11 G:5 B:0, which is exactly
// what GL_UNSIGNED_SHORT_5_6_5 reads, so that one is handed over untouched. The two with alpha are
// packed A R G B with alpha in the HIGH bits, while GL's 5_5_5_1 and 4_4_4_4 put it in the LOW
// ones, so the renderer rotates each texel left by one channel width before upload. A rotation is
// a pure permutation -- if it were wrong in either direction the colours below would come back
// channel-swapped rather than merely imprecise, which is what makes this test able to tell.
//
// Full-intensity primaries are chosen deliberately: 31/31, 63/63 and 15/15 all expand back to
// exactly 255, so a correct result is EXACT and no tolerance hides a swap. Alpha likewise is 0 or 1
// at the extremes of a 1-bit and a 4-bit channel.
//
// It has to be a DRAW, and that was measured rather than assumed. Texture2D::GetData round-trips
// all three formats exactly even when the renderer's rotation is deliberately removed and the
// texture therefore samples with its channels swapped -- the typed readback is served from this
// renderer's CPU-side copy and never asks the GPU what it stored. A readback-based test here would
// pass for a completely wrong upload.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// The four texels every leg uses, as straight RGBA. Each survives 4-, 5- and 6-bit
    /// quantization exactly, so an exact comparison is legitimate.
    struct Texel { float r, g, b, a; };
    constexpr std::array<Texel, 4> kTexels{{
        {1.0f, 0.0f, 0.0f, 1.0f},   // red
        {0.0f, 1.0f, 0.0f, 1.0f},   // green
        {0.0f, 0.0f, 1.0f, 1.0f},   // blue
        {1.0f, 1.0f, 1.0f, 1.0f},   // white
    }};
}

class Packed16FormatTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ok ? ++pass_ : ++fail_;
    }

    /// Uploads @p texture, draws it 1:1 through SpriteBatch and returns the 2x2 readback.
    std::vector<Color> DrawAndRead(GraphicsDevice& dev, Texture2D& texture)
    {
        RenderTarget2D rt(dev, 2, 2, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(13, 17, 19, 255));
        {
            SamplerState point = SamplerState::PointClamp;
            SpriteBatch batch(dev);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            batch.Draw(texture, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 2, 2),
                       Color(255, 255, 255, 255));
            batch.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(4, Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), 0, 4);
        return pixels;
    }

    /// @p make turns one texel into the packed type this leg is about, since the three packed
    /// constructors do not share a signature (Bgr565 has no alpha channel to take).
    template <typename Packed, typename Make>
    void RunLeg(GraphicsDevice& dev, const char* name, SurfaceFormat format, Make make)
    {
        std::unique_ptr<Texture2D> texture;
        try
        {
            texture = std::make_unique<Texture2D>(dev, 2, 2, false, format);
        }
        catch (const std::exception& e)
        {
            check(false, std::string(name) + ": construction threw: " + e.what());
            return;
        }

        std::array<Packed, 4> data{};
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = make(kTexels[i]);
        texture->SetData(data.data(), static_cast<int>(data.size()));

        const std::vector<Color> pixels = DrawAndRead(dev, *texture);

        int wrong = 0;
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            const Color expected(static_cast<int>(kTexels[i].r * 255.0f + 0.5f),
                                 static_cast<int>(kTexels[i].g * 255.0f + 0.5f),
                                 static_cast<int>(kTexels[i].b * 255.0f + 0.5f), 255);
            const Color& got = pixels[i];
            if (got.getRProperty() != expected.getRProperty() ||
                got.getGProperty() != expected.getGProperty() ||
                got.getBProperty() != expected.getBProperty())
            {
                ++wrong;
                std::printf("        %s texel %zu: expected (%d,%d,%d) got (%d,%d,%d)\n",
                            name, i, expected.getRProperty(), expected.getGProperty(),
                            expected.getBProperty(), got.getRProperty(), got.getGProperty(),
                            got.getBProperty());
            }
        }
        check(wrong == 0, std::string(name) +
                              ": every one of the 4 texels samples back to its own colour "
                              "(mismatches=" + std::to_string(wrong) + ")");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        RunLeg<PackedVector::Bgr565>(dev, "A Bgr565", SurfaceFormat::Bgr565,
            [](const Texel& t) { return PackedVector::Bgr565(t.r, t.g, t.b); });
        RunLeg<PackedVector::Bgra5551>(dev, "B Bgra5551", SurfaceFormat::Bgra5551,
            [](const Texel& t) { return PackedVector::Bgra5551(t.r, t.g, t.b, t.a); });
        RunLeg<PackedVector::Bgra4444>(dev, "C Bgra4444", SurfaceFormat::Bgra4444,
            [](const Texel& t) { return PackedVector::Bgra4444(t.r, t.g, t.b, t.a); });

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    Packed16FormatTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    Packed16FormatTest game;
    game.Run();
    return game.getResult();
}
