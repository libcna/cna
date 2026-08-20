// SPDX-License-Identifier: MS-PL
// plans/plan_direct2d.md D2D-129: the 2D half of the cross-renderer diagnostic. Issues one fixed corpus
// of PUBLIC 2D commands and dumps the resulting 64x64 RGBA8 backbuffer, in exactly the format
// cross_renderer_diagnostic_compare.cpp already reads -- so the existing comparator judges this
// too and no second comparator exists to drift.
//
// The existing cross_renderer_diagnostic_scene.cpp cannot serve here: it draws a BasicEffect
// triangle through a VertexBuffer, and Direct2D is a permanently 2D-only renderer that rejects
// every one of those calls. This file is deliberately renderer-agnostic in the same way -- no
// #ifdef, no renderer-specific include -- and is built once per renderer so the SAME source
// produces one dump per renderer.
//
// What the corpus deliberately does NOT contain, and why, is the whitelist of known differences.
// It is kept narrow on purpose: every construct below is one where two conforming 2D renderers may
// legitimately disagree pixel-for-pixel, so including it would force a tolerance so wide that real
// regressions would hide inside it.
//
//   * Linear filtering of a magnified sprite -- the exact filter kernel and its edge behavior are
//     unspecified; every sprite here is point-sampled at an integer scale.
//   * Mip selection and mip-linear blending -- Direct2D has authored levels and no implicit chain,
//     EasyGL has a GL sampler chain; the selection rule is not a shared contract.
//   * Anisotropic filtering, MSAA, Additive blending -- capability-dependent and reported false by
//     Direct2D.
//   * Rotation by a non-right angle -- the rasterized coverage of a rotated quad edge is
//     implementation defined.
//   * Non-opaque destinations -- the backbuffer's alpha handling at presentation differs; the
//     corpus composites onto an opaque cleared background only.
//
// See docs/direct2d-easygl-differential.md for how to run it and for the tolerance rationale.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class CrossRenderer2DCorpus : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::string outputPath_;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        SpriteBatch sprites(device);
        SamplerState point = SamplerState::PointClamp;
        RasterizerState noScissor;
        noScissor.setScissorTestEnableProperty(false);

        // A 2x2 atlas with four distinct opaque colours: every sampling case below can be read
        // back to an exact expected texel, and a transposed or mirrored fetch is visible.
        auto atlas = Texture2D::CreateFromPixels(
            device, 2, 2,
            std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255,
                                      0, 0, 255, 255, 255, 255, 0, 255});
        // A straight-alpha texel for the NonPremultiplied row and its premultiplied twin for the
        // AlphaBlend row: the two rows must land on the same colour if both conventions are
        // honoured, which is the single most common cross-renderer disagreement.
        auto straightHalf = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 128, 0, 128});
        auto premultipliedHalf = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{128, 64, 0, 128});

        device.setRasterizerStateProperty(noScissor);
        device.Clear(Color(20, 20, 20, 255));

        // Row 1 (y 0..15): whole-atlas blit at an integer 8x scale, then the same with each flip.
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &noScissor);
        sprites.Draw(atlas, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 2, 2), Color::White);
        sprites.Draw(atlas, Rectangle(16, 0, 16, 16), Rectangle(0, 0, 2, 2), Color::White,
                     0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        sprites.Draw(atlas, Rectangle(32, 0, 16, 16), Rectangle(0, 0, 2, 2), Color::White,
                     0.0f, Vector2::Zero, SpriteEffects::FlipVertically, 0.0f);
        // A single-texel crop proves the source rectangle addresses the atlas, not its origin.
        sprites.Draw(atlas, Rectangle(48, 0, 16, 16), Rectangle(1, 1, 1, 1), Color::White);
        sprites.End();

        // Row 2 (y 16..31): tinting. Opaque so only the multiply itself is under test.
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &noScissor);
        sprites.Draw(atlas, Rectangle(0, 16, 16, 16), Rectangle(0, 0, 1, 1), Color(128, 255, 255, 255));
        sprites.Draw(atlas, Rectangle(16, 16, 16, 16), Rectangle(1, 0, 1, 1), Color(255, 128, 255, 255));
        sprites.Draw(atlas, Rectangle(32, 16, 16, 16), Rectangle(0, 1, 1, 1), Color(255, 255, 128, 255));
        sprites.Draw(atlas, Rectangle(48, 16, 16, 16), Rectangle(1, 1, 1, 1), Color(64, 64, 64, 255));
        sprites.End();

        // Row 3 (y 32..47): the two alpha conventions over the same opaque background. Both halves
        // must produce the same composited colour.
        sprites.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &noScissor);
        sprites.Draw(premultipliedHalf, Rectangle(0, 32, 32, 16), Rectangle(0, 0, 1, 1), Color::White);
        sprites.End();
        sprites.Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr,
                      &noScissor);
        sprites.Draw(straightHalf, Rectangle(32, 32, 32, 16), Rectangle(0, 0, 1, 1), Color::White);
        sprites.End();

        // Row 4 (y 48..63): a render-target round trip and a scissor rectangle. The target is
        // filled with two blocks, unbound, then blitted back at 1:1.
        RenderTarget2D target(device, 32, 16, false, SurfaceFormat::Color, DepthFormat::None);
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 0, 255));
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &noScissor);
        sprites.Draw(atlas, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 1, 1), Color::White);
        sprites.Draw(atlas, Rectangle(16, 0, 16, 16), Rectangle(1, 1, 1, 1), Color::White);
        sprites.End();
        device.SetRenderTarget(nullptr);
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &noScissor);
        sprites.Draw(target, Rectangle(0, 48, 32, 16), Rectangle(0, 0, 32, 16), Color::White);
        sprites.End();

        RasterizerState withScissor = noScissor;
        withScissor.setScissorTestEnableProperty(true);
        device.setScissorRectangleProperty(Rectangle(40, 52, 16, 8));
        device.setRasterizerStateProperty(withScissor);
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &withScissor);
        sprites.Draw(atlas, Rectangle(32, 48, 32, 16), Rectangle(0, 1, 1, 1), Color::White);
        sprites.End();
        device.setRasterizerStateProperty(noScissor);

        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);
        device.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));

        std::FILE* file = std::fopen(outputPath_.c_str(), "wb");
        if (file == nullptr)
        {
            std::fprintf(stderr, "cross_renderer_2d_corpus: failed to open '%s' for writing\n",
                         outputPath_.c_str());
            Exit();
            return;
        }
        // Same byte order as cross_renderer_diagnostic_scene: R,G,B,A per pixel through Color's
        // accessors, independent of Color's own packed layout.
        std::vector<std::uint8_t> rgba(pixels.size() * 4u);
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            rgba[i * 4 + 0] = pixels[i].getRProperty();
            rgba[i * 4 + 1] = pixels[i].getGProperty();
            rgba[i * 4 + 2] = pixels[i].getBProperty();
            rgba[i * 4 + 3] = pixels[i].getAProperty();
        }
        std::fwrite(rgba.data(), 1, rgba.size(), file);
        std::fclose(file);
        std::printf("wrote %zu bytes to %s\n", rgba.size(), outputPath_.c_str());
        result_ = 0;
        Exit();
    }

public:
    explicit CrossRenderer2DCorpus(std::string outputPath) : outputPath_(std::move(outputPath))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main(int argc, char** argv)
{
    const std::string outputPath = argc > 1 ? argv[1] : "corpus2d_dump.rgba";
    CrossRenderer2DCorpus game(outputPath);
    game.Run();
    return game.getResult();
}
