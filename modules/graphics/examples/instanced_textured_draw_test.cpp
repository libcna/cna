// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-217: does a TEXTURED instanced draw sample its texture?
//
// Why this question was not already asked
// ---------------------------------------
// §10's parity matrix records instancing as `VULKAN_STRONGER`, on a true count: EasyGL's only
// instancing test drives a GLSL `ShaderEffect`, and Vulkan has two stock-effect ones
// (`Vulkan_DrawInstanced_3Instances`, `_InstancedVertexColor_Cardinality`). Both of those are
// COLOUR draws. Neither renderer had ever been asked whether instancing composes with a texture,
// which is the ordinary XNA case -- Microsoft's own InstancedModel sample textures its instances,
// and `DrawInstancedPrimitives` is a vertex-supply mechanism, not a different pixel pipeline.
//
// The two renderers implement instancing very differently, and that is what makes the question
// sharp rather than academic. EasyGL adds an OPTIONAL per-instance world matrix (locations 12-15,
// `uCnaInstanced`) to *every* stock program, so instancing composes with textures, lighting and
// everything else by construction. Vulkan selects a SEPARATE program pair for instanced draws, and
// `instanced3d.frag.glsl` says in its own header comment: "no fog, no texture, just the flat
// instance diffuse color".
//
// The legs
// --------
// A -- CONTROL, non-instanced. The same `BasicEffect`, the same blue texture, the same quad, drawn
//      with `DrawIndexedPrimitives`. Must be blue. Without it a blue-less instanced result could
//      mean "textured draws are broken here", which is a different and much larger claim.
// B -- the subject. The same effect and texture, three instances via `DrawInstancedPrimitives`,
//      sampled at the centre instance. Must be blue.
// C -- the two agree. Stated separately because that is the parity claim in one line: what a
//      texture contributes must not depend on whether the draw was instanced.
//
// The texture is BLUE and the vertex colour is not used, so WHITE is the specific wrong answer a
// renderer produces when it binds a default 1x1 white image instead of the bound texture, and RED
// (the diffuse colour set below) is the wrong answer from a program that ignores the texture and
// uses the material colour. The three outcomes are distinguishable, which is the point.
//
// Renderer-neutral on purpose: the question is about a shared XNA entry point, so every family
// that registers this file answers for itself.
//
// Exit code 0 = every leg PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {

constexpr int kN = 128;

/// A plain 20-byte position+UV record with an EXPLICIT declaration, rather than the stock
/// `VertexPositionTexture`: that type carries a vtable, so `sizeof` is 32 while its declaration
/// says 20, and a raw upload strided by `sizeof` writes garbage.
struct PT { float x, y, z, u, v; };
static_assert(sizeof(PT) == 20);

/// Column-major mat4, the per-instance world transform both renderers expect.
struct InstMat4 { float m[16]; };
static_assert(sizeof(InstMat4) == 64);

InstMat4 TranslateMat(float tx, float ty, float tz)
{
    InstMat4 m{};
    m.m[0] = 1; m.m[5] = 1; m.m[10] = 1; m.m[15] = 1;
    m.m[12] = tx; m.m[13] = ty; m.m[14] = tz;
    return m;
}

} // namespace

class InstancedTexturedDrawTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D>    blue_;
    std::unique_ptr<VertexBuffer> vb_;
    std::unique_ptr<VertexBuffer> instVb_;
    std::unique_ptr<IndexBuffer>  ib_;
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

    static bool IsBlack(const Color& c)
    {
        return c.getRProperty() <= 40 && c.getGProperty() <= 40 && c.getBProperty() <= 40;
    }

    static bool IsBlue(const Color& c)
    {
        return c.getBProperty() >= 180 && c.getRProperty() <= 80 && c.getGProperty() <= 80;
    }

    /// Names whatever a renderer actually produced, so a FAIL line says which wrong answer it is.
    static std::string Diagnose(const Color& c)
    {
        if (IsBlue(c)) return "the texture";
        if (c.getRProperty() >= 180 && c.getGProperty() >= 180 && c.getBProperty() >= 180)
            return "WHITE -- a default 1x1 image bound instead of the texture";
        if (c.getRProperty() >= 180 && c.getGProperty() <= 80 && c.getBProperty() <= 80)
            return "RED -- the material DiffuseColor, i.e. a program that ignores the texture";
        if (c.getGProperty() >= 180 && c.getRProperty() <= 80 && c.getBProperty() <= 80)
            return "GREEN -- the clear colour, i.e. nothing drew at all";
        return "something else";
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle reg(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    /// One draw: `instanced` picks the entry point, `diffuse` the material colour. Everything else
    /// -- the quad, the blue texture, the state -- is identical, which is what makes the two
    /// entry points comparable.
    Color Render(GraphicsDevice& dev, bool instanced, const Vector3& diffuse)
    {
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setLightingEnabledProperty(false);
        fx.setVertexColorEnabledProperty(false);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(blue_.get());
        fx.setDiffuseColorProperty(diffuse);
        fx.setAlphaProperty(1.0f);
        fx.Apply();

        dev.SetVertexBuffer(vb_.get());
        dev.SetIndexBuffer(ib_.get());
        if (instanced)
        {
            std::vector<VertexBufferBinding> bindings = {
                VertexBufferBinding(vb_.get(),     0, 0),
                VertexBufferBinding(instVb_.get(), 0, 1),
            };
            dev.SetVertexBuffers(bindings);
            dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
        }
        else
        {
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }
        return ReadPixel(dev, kN / 2, kN / 2);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        // A blue 1x1. Blue, so a white result -- the default 1x1 image a renderer binds when no
        // texture reaches the draw -- is visibly wrong.
        blue_ = std::make_unique<Texture2D>(dev, 1, 1, false, SurfaceFormat::Color);
        const std::uint8_t px[4] = { 0, 0, 255, 255 };
        blue_->SetDataRGBA(px, 1);   // ONE pixel -- the parameter is a pixel count, not a byte count

        // A quad at the origin, NDC +/-0.12, fully covered by the 1x1 texture.
        const PT quad[4] = {
            { -0.12f,  0.12f, 0.0f, 0.0f, 0.0f },
            { -0.12f, -0.12f, 0.0f, 0.0f, 1.0f },
            {  0.12f, -0.12f, 0.0f, 1.0f, 1.0f },
            {  0.12f,  0.12f, 0.0f, 1.0f, 0.0f },
        };
        const VertexDeclaration quadDecl(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
        vb_ = std::make_unique<VertexBuffer>(dev, quadDecl, 4, BufferUsage::None);
        vb_->SetDataRaw(quad, 4, static_cast<int>(sizeof(PT)));

        const std::uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };
        ib_ = std::make_unique<IndexBuffer>(dev, 6);
        ib_->SetData(idx, 6);

        // The four matrix columns, spelled the way this repo's existing instancing tests spell
        // them. A per-instance buffer with no declaration is refused outright by EasyGL
        // ("unsupported vertex stride 64 without a VertexDeclaration"), so this is not optional.
        const InstMat4 mats[3] = {
            TranslateMat(-0.5f, 0.0f, 0.0f),
            TranslateMat( 0.0f, 0.0f, 0.0f),
            TranslateMat(+0.5f, 0.0f, 0.0f),
        };
        const VertexDeclaration instDecl(64, {
            VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 2),
            VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 3),
        });
        instVb_ = std::make_unique<VertexBuffer>(dev, instDecl, 3, BufferUsage::None);
        instVb_->SetDataRaw(mats, 3, static_cast<int>(sizeof(InstMat4)));

        const Vector3 kWhite(1.0f, 1.0f, 1.0f);
        const Vector3 kRedDiffuse(1.0f, 0.0f, 0.0f);

        const Color a  = Render(dev, false, kWhite);
        const Color a2 = Render(dev, false, kRedDiffuse);
        const Color b  = Render(dev, true,  kWhite);
        const Color b2 = Render(dev, true,  kRedDiffuse);

        check(IsBlue(a),
              "A control, NON-instanced, white DiffuseColor: " + Text(a) + " = " + Diagnose(a) +
                  " (want blue -- without this leg a blue-less instanced result could mean "
                  "\"textured draws are broken here\", a much larger claim)");
        check(IsBlack(a2),
              "A' control, NON-instanced, RED DiffuseColor: " + Text(a2) + " (want black, because "
              "red x blue = 0; this is the leg that proves the texture is MULTIPLIED in rather "
              "than merely present)");
        check(IsBlue(b),
              "B INSTANCED, white DiffuseColor: " + Text(b) + " = " + Diagnose(b) +
                  " (want blue)");
        check(IsBlack(b2),
              "B' INSTANCED, RED DiffuseColor: " + Text(b2) + " = " + Diagnose(b2) +
                  " (want black; RED here is the specific signature of a program that draws the "
                  "material colour and never samples the texture at all)");
        // ---- D/E: does the per-instance matrix COMPOSE with BasicEffect.World, or replace it?
        // The three instances sit at x = -0.5, 0, +0.5 and the quad is +/-0.12 wide, so a World
        // translation of +0.3 moves the centre instance clear of the centre pixel if World is
        // applied and leaves it covering the centre if it is not. Blue at the centre therefore
        // means World was dropped; the clear colour means it was honoured.
        {
            auto worldShift = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::CreateTranslation(Vector3(0.3f, 0.0f, 0.0f)));
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setLightingEnabledProperty(false);
                fx.setVertexColorEnabledProperty(false);
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(blue_.get());
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setAlphaProperty(1.0f);
                fx.Apply();
                dev.SetVertexBuffer(vb_.get());
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(vb_.get(),     0, 0),
                        VertexBufferBinding(instVb_.get(), 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return ReadPixel(dev, kN / 2, kN / 2);
            };
            const Color wn = worldShift(false);
            const Color wi = worldShift(true);
            check(!IsBlue(wn),
                  "D control: a NON-instanced draw honours BasicEffect.World -- with World=+0.3 the "
                  "quad leaves the centre, which reads " + Text(wn) +
                      " (blue would mean World never reached the draw at all)");
            check(!IsBlue(wi),
                  "E an INSTANCED draw honours it too -- the per-instance matrix COMPOSES with "
                  "World rather than replacing it: " + Text(wi) +
                      " (blue means the centre instance is still at the origin, i.e. World was "
                      "dropped; leg B has already shown that this same draw covers the centre when "
                      "World is identity, so this is a transform test and not a coverage one)");
        }

        // ---- F/G: colour AND texture on the same instanced draw ----------------------------
        // A grey(128) vertex colour over the blue texture: the product is (0,0,128), which is
        // distinct from (0,0,255) -- the colour dropped -- and from (128,128,128) -- the texture
        // dropped. Three outcomes, so a failure says which half went missing.
        {
            struct PCT { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
            static_assert(sizeof(PCT) == 24);
            const std::uint8_t G = 128;
            const PCT cq[4] = {
                { -0.12f,  0.12f, 0.0f, G, G, G, 255, 0.0f, 0.0f },
                { -0.12f, -0.12f, 0.0f, G, G, G, 255, 0.0f, 1.0f },
                {  0.12f, -0.12f, 0.0f, G, G, G, 255, 1.0f, 1.0f },
                {  0.12f,  0.12f, 0.0f, G, G, G, 255, 1.0f, 0.0f },
            };
            const VertexDeclaration cqDecl(24, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
                VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            VertexBuffer cvb(dev, cqDecl, 4, BufferUsage::None);
            cvb.SetDataRaw(cq, 4, static_cast<int>(sizeof(PCT)));

            auto colTex = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setLightingEnabledProperty(false);
                fx.setVertexColorEnabledProperty(true);
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(blue_.get());
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setAlphaProperty(1.0f);
                fx.Apply();
                dev.SetVertexBuffer(&cvb);
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(&cvb,          0, 0),
                        VertexBufferBinding(instVb_.get(), 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return ReadPixel(dev, kN / 2, kN / 2);
            };
            auto isProduct = [](const Color& c) {
                return c.getRProperty() <= 40 && c.getGProperty() <= 40 &&
                       c.getBProperty() >= 100 && c.getBProperty() <= 160;
            };
            auto why = [](const Color& c) {
                if (c.getBProperty() > 200) return " -- the vertex COLOUR was dropped";
                if (c.getRProperty() > 100) return " -- the TEXTURE was dropped";
                return "";
            };
            const Color cn = colTex(false);
            const Color ci = colTex(true);
            check(isProduct(cn),
                  "F control: a NON-instanced draw multiplies the vertex colour AND the texture: " +
                      Text(cn) + " (want ~(0,0,128))" + why(cn));
            check(isProduct(ci),
                  "G an INSTANCED draw does too: " + Text(ci) + " (want ~(0,0,128))" + why(ci));
        }

        check(IsBlue(a) == IsBlue(b) && IsBlack(a2) == IsBlack(b2),
              "C what the texture contributes does not depend on whether the draw was instanced: "
              "non-instanced " + Text(a) + "/" + Text(a2) + " vs instanced " + Text(b) + "/" +
                  Text(b2));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    InstancedTexturedDrawTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kN);
        gdm_->setPreferredBackBufferHeightProperty(kN);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    InstancedTexturedDrawTest game;
    game.Run();
    return game.getResult();
}
