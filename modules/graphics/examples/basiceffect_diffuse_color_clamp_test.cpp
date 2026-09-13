// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-197 (finding F-36's CNA-wide sibling) -- Direct3D 9 saturates `oD0`, and
// `BasicEffect`'s unlit path writes `DiffuseColor` straight into it.
//
// `plans/plan_fx.md` FX-123 measured that saturation for the vertex-LIT programs and `VULKAN-196`
// measured it for `EnvironmentMapEffect`'s `oD1`. The same rule governs `vout.Diffuse : COLOR0`,
// which the unlit path fills with `DiffuseColor` -- and with `VertexColorEnabled`, with
// `vin.Color * DiffuseColor`. `BasicEffect.DiffuseColor` has no clamp in its setter
// (`BasicEffect.cs:117`), so a game can hand the shader a value above 1.
//
// Measured on the real XNA 4.0 runtime by `spikes/xna-diffuse-color-clamp-spike/`, which is what
// this test encodes rather than a reading of the semantic:
//
//   DiffuseColor 0.5 -> (50,50,50)   1.0 -> (100,100,100)   2.0 -> (100,100,100)   3.0 -> (100,100,100)
//
// Renderer-agnostic on purpose, and its legs are RELATIONAL: what matters is that 2.0 and 3.0 land
// where 1.0 does, not the absolute value, which depends on the base the shader started from.
//
// Three shader shapes, because one clamp in one shader is not the finding:
//
//   T   textured, no vertex colour        -- the plain tint path (textured3d)
//   TV  textured AND vertex-coloured      -- the modulated path (colored_textured3d)
//
// Each is measured at 0.5 (the positive control: without it, "2.0 == 1.0" cannot be told apart from
// "DiffuseColor does nothing here"), at 1.0, at 2.0 and at 3.0. A GREY base, never white: with a
// white base every answer saturates at the output and no leg could fail -- the trap FX-123's own
// test records. Both work because the texture multiplies in the FRAGMENT stage, AFTER the clamp.
//
// A colour-only quad has no such post-multiply, so a FLAT one cannot distinguish the two orders at
// all -- the output write saturates either way. That case needs the third leg, and it is the one
// FX-123's argument is actually about:
//
//   G   a colour GRADIENT, no texture     -- vertices at 100% and 20%, sampled at the midpoint
//
// with the same probe's measurement of it:  DiffuseColor 1.0 -> (153,153,153), 2.0 -> (178,178,178).
// 178 is the midpoint of saturate(2.0)=255 and saturate(0.4)=102. Interpolating first and saturating
// afterwards gives 1.2 at the midpoint and clips to 255, so this leg separates the two orders by
// 77 levels.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kN = 64;
/// Grey, so that 1.0 and an unclamped 2.0 are two different eight-bit answers.
const Color kGrey(100, 100, 100, 255);
} // namespace

class BasicEffectDiffuseColorClampTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    static bool Same(const Color& a, const Color& b, int tol = 4)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color ReadCentre(GraphicsDevice& dev)
    {
        const Rectangle reg(kN / 2, kN / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    void PrepareEffect(BasicEffect& fx, float diffuse)
    {
        fx.setLightingEnabledProperty(false);
        fx.setDiffuseColorProperty(Vector3(diffuse, diffuse, diffuse));
        fx.setAlphaProperty(1.0f);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

    /// T -- textured, no vertex colour.
    Color RenderTextured(GraphicsDevice& dev, Texture2D& tex, float diffuse)
    {
        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 0.0f) },
        };
        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        PrepareEffect(fx, diffuse);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setVertexColorEnabledProperty(false);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return ReadCentre(dev);
    }

    /// G -- a colour gradient, no texture: left edge white, right edge 20% grey, sampled at the
    /// centre. The interpolator is the only thing between the vertex colour and the pixel, which is
    /// what makes the two clamp orders separable without a texture to multiply afterwards.
    Color RenderGradient(GraphicsDevice& dev, float diffuse)
    {
        const Color a(255, 255, 255, 255);
        const Color b(51, 51, 51, 255);
        const VertexPositionColor quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), a },
            { Vector3(-1.0f, -1.0f, 0.0f), a },
            { Vector3( 1.0f, -1.0f, 0.0f), b },
            { Vector3(-1.0f,  1.0f, 0.0f), a },
            { Vector3( 1.0f, -1.0f, 0.0f), b },
            { Vector3( 1.0f,  1.0f, 0.0f), b },
        };
        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        PrepareEffect(fx, diffuse);
        fx.setTextureEnabledProperty(false);
        fx.setVertexColorEnabledProperty(true);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return ReadCentre(dev);
    }

    /// TV -- textured and vertex-coloured. White vertices, so the texture is the only base and the
    /// arithmetic stays the same as T; what changes is which program runs.
    Color RenderTexturedVertexColoured(GraphicsDevice& dev, Texture2D& tex, float diffuse)
    {
        const Color w(255, 255, 255, 255);
        const VertexPositionColorTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), w, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), w, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), w, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), w, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), w, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), w, Vector2(1.0f, 0.0f) },
        };
        dev.Clear(Color(0, 0, 0, 255));
        BasicEffect fx(dev);
        PrepareEffect(fx, diffuse);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setVertexColorEnabledProperty(true);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return ReadCentre(dev);
    }

    template <typename F>
    void Family(const char* tag, F&& render)
    {
        const Color half  = render(0.5f);
        const Color one   = render(1.0f);
        const Color two   = render(2.0f);
        const Color three = render(3.0f);
        const std::string t(tag);
        check(!Same(half, one),
              t + "-control DiffuseColor is live: 0.5 gives " + Text(half) + " and 1.0 gives " +
                  Text(one));
        check(Same(one, two),
              t + " DiffuseColor 2.0 saturates to 1.0, as D3D9 saturates oD0: 1.0 gives " +
                  Text(one) + " and 2.0 gives " + Text(two));
        check(Same(one, three),
              t + " and so does 3.0: " + Text(three) + " (want " + Text(one) + ")");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        Texture2D tex(dev, 1, 1, false, SurfaceFormat::Color);
        tex.SetData(&kGrey, 1);

        Family("T ", [&](float d) { return RenderTextured(dev, tex, d); });
        Family("TV", [&](float d) { return RenderTexturedVertexColoured(dev, tex, d); });

        // G -- the gradient. Absolute values here, because they are the real XNA runtime's
        // (spikes/xna-diffuse-color-clamp-spike/), and a relational leg cannot express the point:
        // both orders agree at 1.0 and differ only where the product crosses 1.
        const Color g1 = RenderGradient(dev, 1.0f);
        const Color g2 = RenderGradient(dev, 2.0f);
        check(Same(g1, Color(153, 153, 153, 255), 6),
              "G-control gradient midpoint at DiffuseColor 1.0 is XNA's " + Text(g1) +
                  " (want (153,153,153); this leg pins the geometry, not the clamp)");
        check(Same(g2, Color(178, 178, 178, 255), 6),
              "G gradient midpoint at DiffuseColor 2.0 is XNA's " + Text(g2) +
                  " (want (178,178,178), the midpoint of saturate(2.0)=255 and saturate(0.4)=102; "
                  "interpolating the raw product first gives 1.2 and clips to 255)");
        check(g2.getRProperty() < 250,
              "G' and it is not clipped: " + Text(g2) +
                  " (255 would mean the clamp happened after the interpolator, or not at all)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    BasicEffectDiffuseColorClampTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kN);
        gdm_->setPreferredBackBufferHeightProperty(kN);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectDiffuseColorClampTest game;
    game.Run();
    return game.getResult();
}
