// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-201 (finding F-37) -- the same 36-byte layout as
// `basiceffect_lit_vertex_color_perpixel_test.cpp`, with lighting OFF.
//
// `VULKAN-200` gave the lit family a colour input for Position+Normal+Colour+TexCoord, the layout
// XNA's stock `ModelProcessor` emits for a mesh with a colour channel. It required
// `LightingEnabled`, so the unlit twin of the same record stayed refused -- and it was refused for
// a reason rather than by oversight: routing it through the coloured LIT shaders' unlit branch
// would have worked on the first run and been quietly wrong, because that branch does not clamp
// `inColor * DiffuseColor` at the vertex the way `colored_textured3d` has since `VULKAN-197`. Two
// unlit coloured draws would then disagree about Direct3D 9's `oD0` saturate purely by stride.
//
// This test pins BOTH halves of the answer, and the second is the one a flat quad cannot see:
//
//   A  the record draws at all, and its colour is the vertex colour -- not a refusal, and not the
//      diffuse tint alone.
//   B  a colour GRADIENT with `DiffuseColor = 2` reaches the SAME answer the 16-byte and 24-byte
//      records reach, because that is the whole point of routing it to the same program. The
//      render-target write saturates either way on a flat quad, so only two vertices that disagree
//      can separate "saturate then interpolate" from "interpolate then saturate".
//
// Leg B's expected value is the real XNA 4.0 runtime's, measured by
// `spikes/xna-diffuse-color-clamp-spike/`: left edge white, right edge 20% grey,
// `DiffuseColor = 2` gives a midpoint of **(178,178,178)** -- the midpoint of `saturate(2.0) = 255`
// and `saturate(0.4) = 102` -- against **255** for a renderer that interpolates the raw product.
// That measurement was taken on a 16-byte `VertexPositionColor` record with no texture; it applies
// here unchanged because the only difference is a multiply by a white texture in the fragment
// stage, which cannot move the value. The 1.0 row is the geometry control at (153,153,153).
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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kN = 64;
const Color kWhite(255, 255, 255, 255);
const Vector3 kNormal(0.0f, 0.0f, 1.0f);

/// (0.4, 0.8, 0.2) in eight bits.
const Color kVertexColor(102, 204, 51, 255);

/// Position + Normal + Colour + TextureCoordinate -- the stock ModelProcessor's colour-carrying
/// record. The Normal is present and, with lighting off, carries no meaning: that is exactly why
/// the unlit program may ignore it, and why ignoring it is not the silent drop VULKAN-199 refused.
struct LitColourVertex
{
    Vector3      position;
    Vector3      normal;
    unsigned int color;   ///< packed BGRA, as VertexElementFormat::Color is
    Vector2      uv;
};
static_assert(sizeof(LitColourVertex) == 36, "the layout under test is the 36-byte one");
} // namespace

class BasicEffectUnlitVertexColor36ByteTest final : public Game
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

    static bool Same(const Color& a, const Color& b, int tol = 6)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color ReadCentre(GraphicsDevice& dev)
    {
        const Rectangle region(kN / 2, kN / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

    /// One unlit draw of the 36-byte record, with the two corner colours and a diffuse tint.
    Color Draw36(GraphicsDevice& dev, Texture2D& tex, Color left, Color right, float diffuse)
    {
        const VertexDeclaration declaration{
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
            VertexElement(28, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        };
        const unsigned int l = left.getPackedValueProperty();
        const unsigned int r = right.getPackedValueProperty();
        const LitColourVertex quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, l, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), kNormal, l, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, r, Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, l, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, r, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), kNormal, r, Vector2(1.0f, 0.0f) },
        };
        VertexBuffer buffer(dev, declaration, 6, BufferUsage::WriteOnly);
        buffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(LitColourVertex)));

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(false);
        fx.setVertexColorEnabledProperty(true);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setAlphaProperty(1.0f);
        fx.setDiffuseColorProperty(Vector3(diffuse, diffuse, diffuse));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(&buffer);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        return ReadCentre(dev);
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);

        Texture2D tex(dev, 1, 1, false, SurfaceFormat::Color);
        tex.SetData(&kWhite, 1);

        // A -- the record draws, and its colour is the vertex colour.
        const Color flat = Draw36(dev, tex, kVertexColor, kVertexColor, 1.0f);
        check(Same(flat, kVertexColor),
              "A an UNLIT 36-byte Position+Normal+Colour+TexCoord record draws its vertex colour: " +
                  Text(flat) + " (want " + Text(kVertexColor) + ")");

        // B -- the gradient, which is the only thing that can see the clamp order.
        const Color white(255, 255, 255, 255);
        const Color dark(51, 51, 51, 255);
        const Color g1 = Draw36(dev, tex, white, dark, 1.0f);
        const Color g2 = Draw36(dev, tex, white, dark, 2.0f);
        check(Same(g1, Color(153, 153, 153, 255)),
              "B-control gradient midpoint at DiffuseColor 1.0 is XNA's " + Text(g1) +
                  " (want (153,153,153); this leg pins the geometry, not the clamp)");
        check(Same(g2, Color(178, 178, 178, 255)),
              "B gradient midpoint at DiffuseColor 2.0 is XNA's " + Text(g2) +
                  " (want (178,178,178), the midpoint of saturate(2.0)=255 and saturate(0.4)=102; "
                  "this is the same answer the 16- and 24-byte records give, which is the point)");
        check(g2.getRProperty() < 250,
              "B' and it is not clipped: " + Text(g2) +
                  " (255 would mean this record reached a program that does not clamp at the "
                  "vertex, i.e. a different answer by stride)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    BasicEffectUnlitVertexColor36ByteTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kN);
        gdm_->setPreferredBackBufferHeightProperty(kN);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BasicEffectUnlitVertexColor36ByteTest game;
    game.Run();
    return game.getResult();
}
