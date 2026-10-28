// SPDX-License-Identifier: MS-PL
// glTF COLOR_0 in the metallic-roughness product of PbrEffect / SkinnedPbrEffect (CNAEXT).
//
// glTF 2.0 3.9.2 makes COLOR_0 a linear multiplier on base colour, alpha included. CNA carries it
// in the stride-60 rigid and stride-80 skinned records and multiplies it in when the effect's
// VertexColorEnabledEXT asks for it. A renderer has exactly two acceptable answers: draw the
// product, or refuse the draw by name (the shared GLTF-465 refusal, or the GLTF-477 one for a
// renderer with no metallic-roughness path at all). Drawing the opaque-white identity in place of
// an enabled colour is the defect this program exists to catch.
//
// Every draw covers the screen with lights off, white ambient, white base colour and no maps, so
// the pixel is the base-colour product itself (sRGB-encoded, which keeps 0 and 1 exact):
//   A -- rigid stride 60, VertexColorEnabledEXT on: the vertex colour (blue);
//   B -- the same record with the switch off: the identity (white) -- always drawable;
//   C -- a half-transparent vertex colour, AlphaModeEXT::Blend under NonPremultiplied blending:
//        its alpha reaches the product (half blue over black);
//   D -- a stride-48 record with no colour and the switch on: glTF's absent COLOR_0, the identity
//        -- always drawable;
//   E -- skinned stride 80 through SkinnedPbrEffect with an identity bone: the vertex colour (red).
//
// First written for the retired METAL renderer (Apple M4 AM4-084).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = the renderer refused the colour product (or
// PBR shading altogether) by name and every identity case that must still draw did.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int failures = 0;
    int passes = 0;
    std::string skipReason;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ok ? ++passes : ++failures;
    }

    std::string describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + ")";
    }

    bool closeTo(const Color& c, int r, int g, int b)
    {
        return std::abs(c.getRProperty() - r) <= 2 && std::abs(c.getGProperty() - g) <= 2 &&
               std::abs(c.getBProperty() - b) <= 2;
    }

    // The two named refusals a renderer may give instead of drawing. Anything else that throws
    // out of a draw is a failure, not a capability answer.
    bool IsNamedColourRefusal(const std::string& what)
    {
        return what.find("COLOR_0 vertex colour") != std::string::npos;
    }

    bool IsNamedPbrRefusal(const std::string& what)
    {
        return what.find("no metallic-roughness shading path") != std::string::npos;
    }

    // Records a draw that threw: a named refusal becomes the skip reason, anything else fails.
    void refusedOrFailed(const char* label, const std::exception& e, bool colourCase)
    {
        const std::string what = e.what();
        if (IsNamedPbrRefusal(what) || (colourCase && IsNamedColourRefusal(what)))
        {
            std::printf("[SKIP] %s: the renderer refused by name: %s\n", label, what.c_str());
            if (skipReason.empty()) skipReason = what;
            return;
        }
        check(false, std::string(label) + " threw something other than a named refusal: " + what);
    }

    // A full-screen triangle pair in clip space: identity matrices, so every pixel is covered.
    const Vector3 kCorners[6] = {
        Vector3(-1, -1, 0), Vector3(-1, 1, 0), Vector3(1, -1, 0),
        Vector3(1, -1, 0), Vector3(-1, 1, 0), Vector3(1, 1, 0)};

    // The rigid glTF record with TEXCOORD_1 and COLOR_0 (stride 60).
    struct Rigid60
    {
        Vector3 position; Vector3 normal; Vector4 tangent; Vector2 uv0; Vector2 uv1;
        std::uint32_t packedColor;
    };
    static_assert(sizeof(Rigid60) == 60);

    // The plain rigid record (stride 48): no colour.
    struct Rigid48 { Vector3 position; Vector3 normal; Vector4 tangent; Vector2 uv0; };
    static_assert(sizeof(Rigid48) == 48);

    // The skinned glTF record with TEXCOORD_1 and COLOR_0 (stride 80).
    struct Skinned80
    {
        Vector3 position; Vector3 normal; Vector4 tangent; Vector2 uv0; Vector4 weights;
        std::uint32_t packedIndices; Vector2 uv1; std::uint32_t packedColor;
    };
    static_assert(sizeof(Skinned80) == 80);

    template <typename TEffect>
    void Neutral(TEffect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.getDirectionalLight0Property().setEnabledProperty(false);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
        effect.setAmbientLightColorProperty(Vector3(1, 1, 1));
        effect.setDiffuseColorProperty(Vector3(1, 1, 1));
    }
}

class PbrVertexColourTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

    static Color centre(GraphicsDevice& dev)
    {
        const Rectangle one(64, 48, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&one, &pixel, 0, 1);
        return pixel;
    }

    static Color draw(GraphicsDevice& dev, VertexBuffer& buffer, Effect& effect)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetVertexBuffer(&buffer);
        try
        {
            effect.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        catch (...)
        {
            dev.SetVertexBuffer(nullptr);
            throw;
        }
        dev.SetVertexBuffer(nullptr);
        return centre(dev);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        const VertexDeclaration rigid60Declaration(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(48, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(56, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
        const auto rigid60 = [&](const Color& colour)
        {
            std::vector<Rigid60> v(6);
            for (int i = 0; i < 6; ++i)
                v[i] = {kCorners[i], Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(0, 0), Vector2(0, 0),
                        colour.getPackedValueProperty()};
            auto buffer = std::make_unique<VertexBuffer>(dev, rigid60Declaration, 6, BufferUsage::WriteOnly);
            buffer->SetData(v.data(), 0, 6);
            return buffer;
        };

        auto blue = rigid60(Color(0, 0, 255, 255));
        auto halfBlue = rigid60(Color(0, 0, 255, 128));

        // B first: the switch off is the application's explicit identity, which every renderer
        // that shades PBR at all must draw.
        try
        {
            PbrEffect effect(dev);
            Neutral(effect);
            effect.VertexColorEnabledEXT = false;
            const Color b = draw(dev, *blue, effect);
            check(closeTo(b, 255, 255, 255), "B: with VertexColorEnabledEXT off the multiplier is the identity (got " +
                                              describe(b) + ", want (255,255,255))");
        }
        catch (const std::exception& e)
        {
            refusedOrFailed("B: a stride-60 PbrEffect draw with the switch off", e, false);
        }

        try
        {
            PbrEffect effect(dev);
            Neutral(effect);
            effect.VertexColorEnabledEXT = true;
            const Color a = draw(dev, *blue, effect);
            check(closeTo(a, 0, 0, 255), "A: a stride-60 PbrEffect draw multiplies base colour by COLOR_0 (got " +
                                          describe(a) + ", want (0,0,255))");
        }
        catch (const std::exception& e)
        {
            refusedOrFailed("A: a stride-60 PbrEffect draw with COLOR_0 enabled", e, true);
        }

        try
        {
            PbrEffect effect(dev);
            Neutral(effect);
            effect.VertexColorEnabledEXT = true;
            effect.setAlphaModeEXTProperty(AlphaModeEXT::Blend);
            dev.setBlendStateProperty(BlendState::NonPremultiplied);
            Color c;
            try
            {
                c = draw(dev, *halfBlue, effect);
            }
            catch (...)
            {
                dev.setBlendStateProperty(BlendState::Opaque);
                throw;
            }
            dev.setBlendStateProperty(BlendState::Opaque);
            check(closeTo(c, 0, 0, 128), "C: COLOR_0's alpha multiplies base colour alpha (got " + describe(c) +
                                          ", want (0,0,128) under NonPremultiplied over black)");
        }
        catch (const std::exception& e)
        {
            refusedOrFailed("C: a half-transparent stride-60 COLOR_0 draw", e, true);
        }

        try
        {
            const VertexDeclaration rigid48Declaration(std::vector<VertexElement>{
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
                VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)});
            std::vector<Rigid48> v(6);
            for (int i = 0; i < 6; ++i) v[i] = {kCorners[i], Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(0, 0)};
            VertexBuffer buffer(dev, rigid48Declaration, 6, BufferUsage::WriteOnly);
            buffer.SetData(v.data(), 0, 6);
            PbrEffect effect(dev);
            Neutral(effect);
            effect.VertexColorEnabledEXT = true;
            const Color d = draw(dev, buffer, effect);
            check(closeTo(d, 255, 255, 255), "D: a record without COLOR_0 is the identity even with the switch on (got " +
                                              describe(d) + ", want (255,255,255))");
        }
        catch (const std::exception& e)
        {
            // A record with no colour carries nothing to refuse, so only the no-PBR refusal applies.
            refusedOrFailed("D: a stride-48 PbrEffect draw with the switch on", e, false);
        }

        try
        {
            const VertexDeclaration skinned80Declaration(std::vector<VertexElement>{
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
                VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
                VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                VertexElement(64, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
                VertexElement(68, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
                VertexElement(76, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
            std::vector<Skinned80> v(6);
            const std::uint32_t red = Color(255, 0, 0, 255).getPackedValueProperty();
            for (int i = 0; i < 6; ++i)
                v[i] = {kCorners[i], Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(0, 0), Vector4(1, 0, 0, 0),
                        0u, Vector2(0, 0), red};
            VertexBuffer buffer(dev, skinned80Declaration, 6, BufferUsage::WriteOnly);
            buffer.SetData(v.data(), 0, 6);
            SkinnedPbrEffect effect(dev);
            Neutral(effect);
            effect.setWeightsPerVertexProperty(1);
            effect.SetBoneTransforms(std::vector<Matrix>{Matrix::getIdentityProperty()});
            effect.VertexColorEnabledEXT = true;
            const Color e = draw(dev, buffer, effect);
            check(closeTo(e, 255, 0, 0), "E: a stride-80 SkinnedPbrEffect draw multiplies base colour by COLOR_0 (got " +
                                          describe(e) + ", want (255,0,0))");
        }
        catch (const std::exception& e)
        {
            refusedOrFailed("E: a stride-80 SkinnedPbrEffect draw with COLOR_0 enabled", e, true);
        }

        std::printf("=== %d PASS, %d FAIL%s ===\n", passes, failures, skipReason.empty() ? "" : ", refused by name");
        Exit();
    }

public:
    PbrVertexColourTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(128);
        gdm_->setPreferredBackBufferHeightProperty(96);
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("pbr_vertex_colour"))
        return CNA::Examples::Conformance::kSkipExitCode;
    {
        PbrVertexColourTest game;
        game.Run();
    }
    if (failures > 0) return 1;
    if (!skipReason.empty())
    {
        std::printf("[SKIP] the colour product was refused by name, which is this renderer's "
                    "documented answer: %s\n", skipReason.c_str());
        return 77;
    }
    return passes > 0 ? 0 : 1;
}
