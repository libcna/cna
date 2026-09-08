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
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cmath>
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

    /// The clear colour every leg below paints, (0,255,0) -- "nothing drew here".
    static bool IsClear(const Color& c)
    {
        return c.getGProperty() >= 180 && c.getRProperty() <= 80 && c.getBProperty() <= 80;
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

        // ---- H/I: AlphaTestEffect on an instanced draw ------------------------------------
        // Instancing plus alpha test is the canonical use of both together -- foliage. A 2x1
        // texture, texel 0 opaque blue and texel 1 fully transparent, on a wide quad: with
        // CompareFunction::Greater and ReferenceAlpha=128 the right half must be DISCARDED, so a
        // pixel inside it reads the clear colour while the left half reads blue. Both halves are
        // sampled, so "nothing drew at all" cannot pass for "the right half was discarded".
        {
            Texture2D at(dev, 2, 1, false, SurfaceFormat::Color);
            const std::uint8_t apx[8] = { 0, 0, 255, 255,   0, 0, 255, 0 };
            at.SetDataRGBA(apx, 2);
            const PT wq[4] = {
                { -0.6f,  0.3f, 0.0f, 0.0f, 0.0f },
                { -0.6f, -0.3f, 0.0f, 0.0f, 1.0f },
                {  0.6f, -0.3f, 0.0f, 1.0f, 1.0f },
                {  0.6f,  0.3f, 0.0f, 1.0f, 0.0f },
            };
            const VertexDeclaration wqDecl(20, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            VertexBuffer wvb(dev, wqDecl, 4, BufferUsage::None);
            wvb.SetDataRaw(wq, 4, static_cast<int>(sizeof(PT)));
            // ONE instance at the origin, so the instanced and non-instanced legs cover exactly the
            // same pixels and the only variable is which entry point drew them.
            const InstMat4 one[1] = { TranslateMat(0.0f, 0.0f, 0.0f) };
            const VertexDeclaration instDecl1(64, {
                VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 2),
                VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 3),
            });
            VertexBuffer ivb1(dev, instDecl1, 1, BufferUsage::None);
            ivb1.SetDataRaw(one, 1, static_cast<int>(sizeof(InstMat4)));

            auto alphaLeg = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                AlphaTestEffect fx(dev);
                fx.setAlphaFunctionProperty(CompareFunction::Greater);
                fx.setReferenceAlphaProperty(128);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(&at);
                fx.setVertexColorEnabledProperty(false);
                fx.Apply();
                dev.SetVertexBuffer(&wvb);
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(&wvb,  0, 0),
                        VertexBufferBinding(&ivb1, 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return std::pair<Color, Color>{ ReadPixel(dev, kN / 4, kN / 2),
                                                ReadPixel(dev, (kN * 3) / 4, kN / 2) };
            };
            auto ok = [&](const std::pair<Color, Color>& p) {
                return IsBlue(p.first) && !IsBlue(p.second);
            };
            const auto an = alphaLeg(false);
            const auto ai = alphaLeg(true);
            check(ok(an),
                  "H control: a NON-instanced AlphaTestEffect draw discards the transparent half -- "
                  "left " + Text(an.first) + " (want blue), right " + Text(an.second) +
                      " (want the clear colour)");
            check(ok(ai),
                  "I an INSTANCED one does too: left " + Text(ai.first) + ", right " +
                      Text(ai.second) +
                      " (a blue right half means the alpha test never ran, which is what a draw "
                      "routed to a program without one looks like)");
        }

        // ---- T/U: the COLOURED alpha-test shape ---------------------------------------------
        // plans/plan_vulkan.md VULKAN-229. H/I drive a Position+TextureCoordinate record; the
        // alpha-test family has a second vertex shape, Position+Colour+TextureCoordinate with
        // VertexColorEnabled, and only that one reads a colour. Same 2x1 texture trick, but WHITE
        // rather than blue, with a grey(128) vertex colour -- so the left half must read
        // (128,128,128), (255,255,255) is the specific signature of a program that dropped the
        // vertex colour, and the right half must still be discarded.
        {
            Texture2D atc(dev, 2, 1, false, SurfaceFormat::Color);
            const std::uint8_t apxc[8] = { 255, 255, 255, 255,   255, 255, 255, 0 };
            atc.SetDataRGBA(apxc, 2);
            struct PCT { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
            static_assert(sizeof(PCT) == 24);
            const PCT cwq[4] = {
                { -0.6f,  0.3f, 0.0f, 128, 128, 128, 255, 0.0f, 0.0f },
                { -0.6f, -0.3f, 0.0f, 128, 128, 128, 255, 0.0f, 1.0f },
                {  0.6f, -0.3f, 0.0f, 128, 128, 128, 255, 1.0f, 1.0f },
                {  0.6f,  0.3f, 0.0f, 128, 128, 128, 255, 1.0f, 0.0f },
            };
            const VertexDeclaration cwqDecl(24, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
                VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            VertexBuffer cwvb(dev, cwqDecl, 4, BufferUsage::None);
            cwvb.SetDataRaw(cwq, 4, static_cast<int>(sizeof(PCT)));
            const InstMat4 one2[1] = { TranslateMat(0.0f, 0.0f, 0.0f) };
            const VertexDeclaration instDecl2(64, {
                VertexElement(0,  VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 2),
                VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 3),
            });
            VertexBuffer ivb2(dev, instDecl2, 1, BufferUsage::None);
            ivb2.SetDataRaw(one2, 1, static_cast<int>(sizeof(InstMat4)));

            auto alphaColorLeg = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                AlphaTestEffect fx(dev);
                fx.setAlphaFunctionProperty(CompareFunction::Greater);
                fx.setReferenceAlphaProperty(128);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(&atc);
                fx.setVertexColorEnabledProperty(true);
                fx.Apply();
                dev.SetVertexBuffer(&cwvb);
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(&cwvb, 0, 0),
                        VertexBufferBinding(&ivb2, 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return std::pair<Color, Color>{ ReadPixel(dev, kN / 4, kN / 2),
                                                ReadPixel(dev, (kN * 3) / 4, kN / 2) };
            };
            auto okc = [&](const std::pair<Color, Color>& q) {
                const int r = q.first.getRProperty();
                const bool leftGrey = r >= 100 && r <= 155 &&
                                      std::abs(r - q.first.getGProperty()) <= 6 &&
                                      std::abs(r - q.first.getBProperty()) <= 6;
                return leftGrey && IsClear(q.second);
            };
            auto whyc = [](const std::pair<Color, Color>& q) {
                if (q.first.getRProperty() > 200 && q.first.getGProperty() > 200)
                    return " -- the vertex colour was dropped";
                if (!IsClear(q.second)) return " -- the alpha test never ran";
                return "";
            };
            const auto acn = alphaColorLeg(false);
            const auto aci = alphaColorLeg(true);
            check(okc(acn),
                  "T control: a NON-instanced COLOURED AlphaTestEffect draw keeps the grey vertex "
                  "colour and discards the transparent half -- left " + Text(acn.first) +
                      " (want ~(128,128,128)), right " + Text(acn.second) + whyc(acn));
            check(okc(aci),
                  "U an INSTANCED one does too: left " + Text(aci.first) + ", right " +
                      Text(aci.second) + whyc(aci));
        }

        // ---- J/K: LIGHTING on an instanced draw ---------------------------------------------
        // One directional light at N.L = 0.5 over a white texture with a white DiffuseColor, so the
        // correct answer is mid-grey and an unlit (255,255,255) quad is unmistakable.
        {
            struct PNT { float x, y, z, nx, ny, nz, u, v; };
            static_assert(sizeof(PNT) == 32);
            const PNT lq[4] = {
                { -0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
                { -0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
                {  0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
                {  0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
            };
            const VertexDeclaration lqDecl(32, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            VertexBuffer lvb(dev, lqDecl, 4, BufferUsage::None);
            lvb.SetDataRaw(lq, 4, static_cast<int>(sizeof(PNT)));
            Texture2D white(dev, 1, 1, false, SurfaceFormat::Color);
            const std::uint8_t wpx[4] = { 255, 255, 255, 255 };
            white.SetDataRGBA(wpx, 1);

            auto lit = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setLightingEnabledProperty(true);
                fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(&white);
                fx.setVertexColorEnabledProperty(false);
                fx.setAlphaProperty(1.0f);
                auto& l0 = fx.getDirectionalLight0Property();
                l0.setEnabledProperty(true);
                l0.setDirectionProperty(Vector3(0.0f, -0.866f, -0.5f));  // N.L = 0.5
                l0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                l0.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.getDirectionalLight1Property().setEnabledProperty(false);
                fx.getDirectionalLight2Property().setEnabledProperty(false);
                fx.Apply();
                dev.SetVertexBuffer(&lvb);
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(&lvb,          0, 0),
                        VertexBufferBinding(instVb_.get(), 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return ReadPixel(dev, kN / 2, kN / 2);
            };
            const Color ln = lit(false);
            const Color li = lit(true);
            auto midGrey = [](const Color& c) {
                const int r = c.getRProperty();
                return r >= 100 && r <= 155 && std::abs(r - c.getGProperty()) <= 6 &&
                       std::abs(r - c.getBProperty()) <= 6;
            };
            check(midGrey(ln),
                  "J control: a NON-instanced lit draw shades by N.L: " + Text(ln) +
                      " (want ~(128,128,128); (255,255,255) would mean lighting never ran)");
            check(midGrey(li),
                  "K an INSTANCED lit draw shades the same way: " + Text(li) +
                      " (want ~(128,128,128); (255,255,255) is an unlit full-bright quad, which is "
                      "what a draw routed to a program with no Normal input looks like)");
        }

        // ---- P/Q: the UNTEXTURED lit shape --------------------------------------------------
        // plans/plan_vulkan.md VULKAN-228. J/K drive Position+Normal+TextureCoordinate, which is
        // one of the lit family's THREE vertex shapes. This is the second: an exactly
        // Position+Normal declaration with BasicEffect.TextureEnabled false -- XNA's Primitives3D
        // vertex, and the shape a game gets from a mesh with no UVs at all. Same light and same
        // white DiffuseColor as J/K, so mid-grey is again the arithmetic answer and (255,255,255)
        // is again the unmistakable signature of a draw that never reached a lit program.
        {
            struct PN { float x, y, z, nx, ny, nz; };
            static_assert(sizeof(PN) == 24);
            const PN uq[4] = {
                { -0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f },
                { -0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f },
                {  0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f },
                {  0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f },
            };
            const VertexDeclaration uqDecl(24, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            });
            VertexBuffer uvb(dev, uqDecl, 4, BufferUsage::None);
            uvb.SetDataRaw(uq, 4, static_cast<int>(sizeof(PN)));

            auto litUntextured = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setLightingEnabledProperty(true);
                fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setTextureEnabledProperty(false);
                fx.setVertexColorEnabledProperty(false);
                fx.setAlphaProperty(1.0f);
                auto& l0 = fx.getDirectionalLight0Property();
                l0.setEnabledProperty(true);
                l0.setDirectionProperty(Vector3(0.0f, -0.866f, -0.5f));  // N.L = 0.5
                l0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                l0.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.getDirectionalLight1Property().setEnabledProperty(false);
                fx.getDirectionalLight2Property().setEnabledProperty(false);
                fx.Apply();
                dev.SetVertexBuffer(&uvb);
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(&uvb,          0, 0),
                        VertexBufferBinding(instVb_.get(), 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return ReadPixel(dev, kN / 2, kN / 2);
            };
            const Color un = litUntextured(false);
            const Color ui = litUntextured(true);
            auto midGrey = [](const Color& c) {
                const int r = c.getRProperty();
                return r >= 100 && r <= 155 && std::abs(r - c.getGProperty()) <= 6 &&
                       std::abs(r - c.getBProperty()) <= 6;
            };
            check(midGrey(un),
                  "P control: a NON-instanced UNTEXTURED lit draw (Position+Normal, "
                  "TextureEnabled=false) shades by N.L: " + Text(un) +
                      " (want ~(128,128,128))");
            check(midGrey(ui),
                  "Q an INSTANCED one does too: " + Text(ui) +
                      " (want ~(128,128,128); (255,255,255) is the flat instanced program drawing "
                      "the material colour with no Normal input at all)");
        }

        // ---- R/S: the COLOURED lit shape ----------------------------------------------------
        // plans/plan_vulkan.md VULKAN-228, the lit family's third vertex shape:
        // Position+Normal+Colour+TextureCoordinate with VertexColorEnabled -- the stock
        // ModelProcessor's colour-carrying mesh. RED vertex colour under the same N.L = 0.5 light
        // over a white texture, so the three outcomes are all distinct: (128,0,0) is correct,
        // (255,0,0) means lighting was dropped, and (128,128,128) means the vertex colour was.
        {
            struct PNCT { float x, y, z, nx, ny, nz; std::uint8_t r, g, b, a; float u, v; };
            static_assert(sizeof(PNCT) == 36);
            const PNCT cq[4] = {
                { -0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 255, 0, 0, 255, 0.0f, 0.0f },
                { -0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 255, 0, 0, 255, 0.0f, 1.0f },
                {  0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 255, 0, 0, 255, 1.0f, 1.0f },
                {  0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 255, 0, 0, 255, 1.0f, 0.0f },
            };
            const VertexDeclaration cqDecl(36, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Color,   VertexElementUsage::Color, 0),
                VertexElement(28, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            VertexBuffer cvb(dev, cqDecl, 4, BufferUsage::None);
            cvb.SetDataRaw(cq, 4, static_cast<int>(sizeof(PNCT)));
            Texture2D white2(dev, 1, 1, false, SurfaceFormat::Color);
            const std::uint8_t wpx2[4] = { 255, 255, 255, 255 };
            white2.SetDataRGBA(wpx2, 1);

            auto litColored = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                BasicEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setLightingEnabledProperty(true);
                fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(&white2);
                fx.setVertexColorEnabledProperty(true);
                fx.setAlphaProperty(1.0f);
                auto& l0 = fx.getDirectionalLight0Property();
                l0.setEnabledProperty(true);
                l0.setDirectionProperty(Vector3(0.0f, -0.866f, -0.5f));  // N.L = 0.5
                l0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                l0.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
                fx.getDirectionalLight1Property().setEnabledProperty(false);
                fx.getDirectionalLight2Property().setEnabledProperty(false);
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
            const Color cn = litColored(false);
            const Color ci2 = litColored(true);
            auto litRed = [](const Color& c) {
                return c.getRProperty() >= 100 && c.getRProperty() <= 155 &&
                       c.getGProperty() <= 40 && c.getBProperty() <= 40;
            };
            auto why = [](const Color& c) {
                if (c.getGProperty() > 60 && c.getRProperty() > 60)
                    return " -- the vertex colour was dropped";
                if (c.getRProperty() > 200) return " -- lighting was dropped";
                return "";
            };
            check(litRed(cn),
                  "R control: a NON-instanced COLOURED lit draw multiplies the RED vertex colour "
                  "by N.L: " + Text(cn) + " (want ~(128,0,0))" + why(cn));
            check(litRed(ci2),
                  "S an INSTANCED one does too: " + Text(ci2) + " (want ~(128,0,0))" + why(ci2));
        }

        // ---- L/M: DualTextureEffect on an instanced draw ------------------------------------
        // FNA/XNA double the product: colour = tex0 * tex2 * 2. With tex0 = blue(255) and
        // tex2 = quarter-grey(64), that is 255 * 0.25 * 2 = 128 -> (0,0,128). If the SECOND
        // texture is dropped the result is (0,0,255), and if the whole effect is ignored it is
        // whatever the fallback draws -- three distinguishable answers.
        {
            Texture2D quarter(dev, 1, 1, false, SurfaceFormat::Color);
            const std::uint8_t qpx[4] = { 64, 64, 64, 255 };
            quarter.SetDataRGBA(qpx, 1);
            struct PTT { float x, y, z, u0, v0, u1, v1; };
            static_assert(sizeof(PTT) == 28);
            const PTT dq[4] = {
                { -0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                { -0.12f, -0.12f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
                {  0.12f, -0.12f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                {  0.12f,  0.12f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f },
            };
            const VertexDeclaration dqDecl(28, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
                VertexElement(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
            });
            VertexBuffer dvb(dev, dqDecl, 4, BufferUsage::None);
            dvb.SetDataRaw(dq, 4, static_cast<int>(sizeof(PTT)));

            auto dual = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
                DualTextureEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(blue_.get());
                fx.setTexture2Property(&quarter);
                fx.setVertexColorEnabledProperty(false);
                fx.Apply();
                dev.SetVertexBuffer(&dvb);
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(&dvb,          0, 0),
                        VertexBufferBinding(instVb_.get(), 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return ReadPixel(dev, kN / 2, kN / 2);
            };
            const Color dn = dual(false);
            const Color di = dual(true);
            auto doubled = [](const Color& c) {
                return c.getRProperty() <= 40 && c.getGProperty() <= 40 &&
                       c.getBProperty() >= 100 && c.getBProperty() <= 160;
            };
            auto why = [](const Color& c) {
                return c.getBProperty() > 200 ? " -- the SECOND texture was dropped" : "";
            };
            check(doubled(dn),
                  "L control: a NON-instanced DualTextureEffect draw multiplies both textures and "
                  "doubles: " + Text(dn) + " (want ~(0,0,128))" + why(dn));
            check(doubled(di),
                  "M an INSTANCED one does too: " + Text(di) + " (want ~(0,0,128))" + why(di));
        }

        // ---- N/O: EnvironmentMapEffect on an instanced draw ---------------------------------
        // A cube map whose every face is RED over a blue base texture, with
        // EnvironmentMapAmount = 1: the reflection wins outright, so the answer is red. If the
        // cube never reaches the draw the base texture shows through as blue instead, and if the
        // whole effect is ignored the fallback draws something else again.
        {
            TextureCube cube(dev, 2, false, SurfaceFormat::Color);
            std::vector<Color> face(4, Color(255, 0, 0, 255));
            for (int f = 0; f < 6; ++f)
                cube.SetData(static_cast<CubeMapFace>(f), face.data(), 0, 4);
            struct PNT { float x, y, z, nx, ny, nz, u, v; };
            static_assert(sizeof(PNT) == 32);
            const PNT eq[4] = {
                { -0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
                { -0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
                {  0.12f, -0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f },
                {  0.12f,  0.12f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
            };
            const VertexDeclaration eqDecl(32, {
                VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            });
            VertexBuffer evb(dev, eqDecl, 4, BufferUsage::None);
            evb.SetDataRaw(eq, 4, static_cast<int>(sizeof(PNT)));

            auto env = [&](bool instanced) {
                dev.Clear(Color(0, 255, 0, 255));
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(BlendState::Opaque);
                dev.setRasterizerStateProperty(RasterizerState::CullNone);
                dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
                dev.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
                EnvironmentMapEffect fx(dev);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.setTextureProperty(blue_.get());
                fx.setEnvironmentMapProperty(&cube);
                fx.setEnvironmentMapAmountProperty(1.0f);
                fx.setFresnelFactorProperty(0.0f);
                fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
                fx.Apply();
                dev.SetVertexBuffer(&evb);
                dev.SetIndexBuffer(ib_.get());
                if (instanced) {
                    std::vector<VertexBufferBinding> bd = {
                        VertexBufferBinding(&evb,          0, 0),
                        VertexBufferBinding(instVb_.get(), 0, 1),
                    };
                    dev.SetVertexBuffers(bd);
                    dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
                } else {
                    dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
                }
                return ReadPixel(dev, kN / 2, kN / 2);
            };
            const Color en = env(false);
            const Color ei = env(true);
            auto reflects = [](const Color& c) {
                return c.getRProperty() >= 150 && c.getBProperty() <= 110;
            };
            auto why = [](const Color& c) {
                return c.getBProperty() > 150 ? " -- the cube map never reached the draw" : "";
            };
            check(reflects(en),
                  "N control: a NON-instanced EnvironmentMapEffect draw samples its cube map: " +
                      Text(en) + " (want red)" + why(en));
            check(reflects(ei),
                  "O an INSTANCED one does too: " + Text(ei) + " (want red)" + why(ei));
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
