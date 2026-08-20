// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-18: real Texture2D mip level support for the OpenGL4 graphics renderer --
// Texture2D::SetData(level, ...) for level>0 previously reached
// OpenGL4TextureRenderer::UpdatePixelsLevel(), which was unoverridden (inherited no-op default),
// so any mip level beyond 0 was silently discarded. GL_TEXTURE_MAX_LEVEL also defaulted to GL's
// own 1000, so a texture with any mip levels genuinely uploaded was still "incomplete" once
// sampled with a mip-aware filter.
//
// Methodology matches this project's own established mip-filter test family (Task 298 --
// easygl_texture_mip_filter_effect_test.cpp/vulkan/bgfx ports): a 128x128 mipMap texture has 8
// levels (128,64,32,16,8,4,2,1). Levels 0-2 are solid RED; levels 3-7 are solid GREEN. Drawn via
// SpriteBatch (which correctly threads TextureFilter through to ApplySamplerState, unlike the
// direct-3D-draw path's still-hardcoded sampler -- see plans/plan_opengl4.md's own GL4-13 "Remaining
// work" note) at a tiny on-screen size, forcing the GPU's automatic LOD calculation well into the
// GREEN range.
//
// Check A -- TextureFilter::LinearMipPoint at a tiny (8x8px) destination size samples GREEN: a
//   real high mip level was genuinely selected and its genuinely-uploaded content sampled, not
//   just "didn't throw".
// Check B -- TextureFilter::Point at the SAME tiny size samples RED: this renderer deliberately
//   maps Point to a non-mip-aware GL_NEAREST (matching EasyGLRenderer's own identical,
//   documented choice) -- Point never mip-selects regardless of minification, a known behavior
//   this check asserts, not a bug.
// Check C -- the SAME mip texture sampled at a NORMAL (non-minified, 1:1) destination size with
//   LinearMipPoint reads RED (level 0's own content) -- proves level 0's upload still works
//   correctly alongside the higher levels now being real.
// Check D -- an ordinary, non-mipMap (single-level) texture sampled with a mip-aware filter
//   (LinearMipPoint) at normal size does NOT render as solid black -- the classic GL "incomplete
//   mipmap" failure mode a missing GL_TEXTURE_MAX_LEVEL clamp produces, now prevented for the
//   overwhelmingly common non-mipped case.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWindowSize = 64;
    constexpr int kMipTexSize = 128;

    bool IsRed(const Color& c) { return c.getRProperty() >= 200 && c.getGProperty() <= 40; }
    // Color::Green in this codebase is real XNA's (0,128,0), not (0,255,0) (Lime is the
    // pure-green one) -- the mip texture is filled with Color::Green itself, so this checks
    // against that same real value, not an assumed pure-green.
    bool IsGreen(const Color& c) { return c.getGProperty() >= 100 && c.getGProperty() <= 160 && c.getRProperty() <= 40; }
    bool IsBlack(const Color& c)
    {
        return c.getRProperty() <= 10 && c.getGProperty() <= 10 && c.getBProperty() <= 10;
    }

    SamplerState MakeSampler(TextureFilter filter)
    {
        SamplerState ss;
        ss.setFilterProperty(filter);
        ss.setAddressUProperty(TextureAddressMode::Clamp);
        ss.setAddressVProperty(TextureAddressMode::Clamp);
        return ss;
    }
}

class OpenGL4MipmapTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    // Draws the whole texture into a destSize x destSize square at the window's top-left corner
    // under the given filter, and returns the pixel at its centre.
    Color DrawAndSample(SpriteBatch& sb, Texture2D& tex, TextureFilter filter, int destSize)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color::Black);

        SamplerState ss = MakeSampler(filter);
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &ss, nullptr, nullptr);
        sb.Draw(tex, Rectangle(0, 0, destSize, destSize), Rectangle(0, 0, tex.getWidthProperty(), tex.getHeightProperty()), Color::White);
        sb.End();

        const Rectangle reg(destSize / 2, destSize / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        // Levels 0-2 (128,64,32) solid RED; levels 3-7 (16,8,4,2,1) solid GREEN.
        Texture2D mipTex(dev, kMipTexSize, kMipTexSize, /*mipMap=*/true, SurfaceFormat::Color);
        for (int level = 0; level <= 7; ++level)
        {
            const int dim = kMipTexSize >> level;
            const bool red = level <= 2;
            std::vector<Color> px(static_cast<std::size_t>(dim) * dim,
                                  red ? Color::Red : Color::Green);
            mipTex.SetData(level, nullptr, px.data(), 0, static_cast<int>(px.size()));
        }

        const Color mipAware = DrawAndSample(sb, mipTex, TextureFilter::LinearMipPoint, 8);
        Check(IsGreen(mipAware),
              "Check A: LinearMipPoint at 8x8px (128->1 texture) samples GREEN (high mip level genuinely selected)");

        const Color pointOnly = DrawAndSample(sb, mipTex, TextureFilter::Point, 8);
        Check(IsRed(pointOnly),
              "Check B: Point at the same tiny size samples RED (documented: Point never mip-selects on this renderer)");

        const Color level0 = DrawAndSample(sb, mipTex, TextureFilter::LinearMipPoint, kWindowSize);
        Check(IsRed(level0),
              "Check C: LinearMipPoint at normal (non-minified) size samples RED (level 0's own upload still correct)");

        std::vector<std::uint8_t> plainPixels(4 * 4 * 4);
        for (std::size_t i = 0; i < 16; ++i)
        {
            plainPixels[i * 4 + 0] = 200; plainPixels[i * 4 + 1] = 100;
            plainPixels[i * 4 + 2] = 50;  plainPixels[i * 4 + 3] = 255;
        }
        Texture2D plainTex = Texture2D::CreateFromPixels(dev, 4, 4, plainPixels);
        const Color plainMipAware = DrawAndSample(sb, plainTex, TextureFilter::LinearMipPoint, kWindowSize);
        Check(!IsBlack(plainMipAware),
              "Check D: a single-level texture sampled with a mip-aware filter is not solid black (GL_TEXTURE_MAX_LEVEL clamp works)");
    }

public:
    OpenGL4MipmapTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWindowSize);
        gdm_->setPreferredBackBufferHeightProperty(kWindowSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4MipmapTest>();
}
