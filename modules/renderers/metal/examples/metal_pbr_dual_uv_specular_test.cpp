// SPDX-License-Identifier: MS-PL
// plans/plan_apple_m4.md AM4-085: glTF's per-map coordinate set and KHR_materials_specular's two maps
// in Metal's metallic-roughness product.
//
// Every draw covers the screen with one stride-60 record whose TEXCOORD_0 is (0.25, 0.5) and whose
// TEXCOORD_1 is (0.75, 0.5), so a 2x1 texture answers which coordinate set, and which transform,
// reached the sample: its left texel for TEXCOORD_0, its right one for TEXCOORD_1 or a +0.5 offset.
//
// A -- base colour, unlit by the lights (white ambient): the left texel (red) on set 0, the right one
//      (green) on set 1, and the right one again on set 0 shifted by a texture transform.
// B -- KHR_materials_specular, one light along the view direction onto a rough dielectric whose
//      specular colour factor drives F0 to 1. At normal incidence the BRDF reduces to
//      (1 - F0) / pi + F0 / (4 pi), so F0 = 1 and F0 = 0 differ by a factor of four:
//        no maps                                          -> F0 = 1;
//        strength map, alpha 0 on the left texel, set 0   -> F0 = 0;
//        the same map on set 1 (alpha 1 on the right)     -> F0 = 1;
//        a black colour map                               -> F0 = 0;
//        a black|white colour map shifted by +0.5         -> F0 = 1.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const std::string& label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount;
    }

    std::string describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + ")";
    }

    bool near(const Color& c, int r, int g, int b)
    {
        return std::abs(c.getRProperty() - r) <= 3 && std::abs(c.getGProperty() - g) <= 3 &&
               std::abs(c.getBProperty() - b) <= 3;
    }

    // The renderer's own encode: glTF's output is linear, the backbuffer holds sRGB.
    int Encoded(double linear)
    {
        const double srgb = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
        return static_cast<int>(std::lround(srgb * 255.0));
    }

    // Rough dielectric, white albedo, unit light along N = V = L: (1 - F0) / pi + F0 / (4 pi).
    int Grey(double f0)
    {
        const double pi = 3.14159265358979323846;
        return Encoded((1.0 - f0) / pi + f0 / (4.0 * pi));
    }

    const Vector3 kCorners[6] = {
        Vector3(-1, -1, 0), Vector3(-1, 1, 0), Vector3(1, -1, 0),
        Vector3(1, -1, 0), Vector3(-1, 1, 0), Vector3(1, 1, 0)};

    struct Rigid60
    {
        Vector3 position; Vector3 normal; Vector4 tangent; Vector2 uv0; Vector2 uv1;
        std::uint32_t packedColor;
    };
    static_assert(sizeof(Rigid60) == 60);
}

class MetalPbrDualUvSpecularTest final : public Game
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

    static Color draw(GraphicsDevice& dev, VertexBuffer& buffer, PbrEffect& effect)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetVertexBuffer(&buffer);
        effect.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        for (int slot = 0; slot < 8; ++slot) dev.getTexturesProperty()(slot, nullptr);
        return centre(dev);
    }

    static std::unique_ptr<Texture2D> pair(GraphicsDevice& dev, const Color& left, const Color& right)
    {
        auto texture = std::make_unique<Texture2D>(dev, 2, 1);
        const Color texels[2] = {left, right};
        texture->SetData(texels, 0, 2);
        return texture;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        for (int slot = 0; slot < 8; ++slot) dev.getSamplerStatesProperty()(slot, SamplerState::PointClamp);

        const VertexDeclaration declaration(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
            VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(48, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(56, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
        std::vector<Rigid60> vertices(6);
        for (int i = 0; i < 6; ++i)
            vertices[i] = {kCorners[i], Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2(0.25f, 0.5f),
                           Vector2(0.75f, 0.5f), Color::White.getPackedValueProperty()};
        VertexBuffer buffer(dev, declaration, 6, BufferUsage::WriteOnly);
        buffer.SetData(vertices.data(), 0, 6);

        try
        {
            auto redGreen = pair(dev, Color(255, 0, 0, 255), Color(0, 255, 0, 255));
            PbrEffect effect(dev);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::getIdentityProperty());
            effect.setProjectionProperty(Matrix::getIdentityProperty());
            effect.getDirectionalLight0Property().setEnabledProperty(false);
            effect.getDirectionalLight1Property().setEnabledProperty(false);
            effect.getDirectionalLight2Property().setEnabledProperty(false);
            effect.setAmbientLightColorProperty(Vector3(1, 1, 1));
            effect.setDiffuseColorProperty(Vector3(1, 1, 1));
            effect.setTextureProperty(redGreen.get());

            const Color set0 = draw(dev, buffer, effect);
            check(near(set0, 255, 0, 0), "A: base colour on TEXCOORD_0 samples the left texel (got " +
                                             describe(set0) + ", want (255,0,0))");
            effect.setTextureCoordinateSetEXTProperty(0, 1);
            const Color set1 = draw(dev, buffer, effect);
            check(near(set1, 0, 255, 0), "A: base colour on TEXCOORD_1 samples the right texel (got " +
                                             describe(set1) + ", want (0,255,0))");
            effect.setTextureCoordinateSetEXTProperty(0, 0);
            TextureTransformEXT shift;
            shift.Offset = Vector2(0.5f, 0.0f);
            effect.setTextureTransformEXTProperty(0, shift);
            const Color shifted = draw(dev, buffer, effect);
            check(near(shifted, 0, 255, 0), "A: a +0.5 base-colour transform reaches the right texel (got " +
                                                describe(shifted) + ", want (0,255,0))");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("A: a dual-UV PbrEffect draw threw: ") + e.what());
        }

        try
        {
            auto strength = pair(dev, Color(255, 255, 255, 0), Color(255, 255, 255, 255));
            auto blackColour = pair(dev, Color(0, 0, 0, 255), Color(0, 0, 0, 255));
            auto blackWhite = pair(dev, Color(0, 0, 0, 255), Color(255, 255, 255, 255));
            const auto lit = [&]()
            {
                auto effect = std::make_unique<PbrEffect>(dev);
                effect->setWorldProperty(Matrix::getIdentityProperty());
                effect->setViewProperty(Matrix::CreateLookAt(Vector3(0, 0, 1), Vector3(0, 0, 0), Vector3::Up));
                effect->setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f));
                effect->getDirectionalLight0Property().setEnabledProperty(true);
                effect->getDirectionalLight0Property().setDirectionProperty(Vector3(0, 0, -1));
                effect->getDirectionalLight0Property().setDiffuseColorProperty(Vector3(1, 1, 1));
                effect->getDirectionalLight1Property().setEnabledProperty(false);
                effect->getDirectionalLight2Property().setEnabledProperty(false);
                effect->setAmbientLightColorProperty(Vector3(0, 0, 0));
                effect->setDiffuseColorProperty(Vector3(1, 1, 1));
                effect->setMetallicFactorProperty(0.0f);
                effect->setRoughnessFactorProperty(1.0f);
                // IOR 1.5's F0 is 0.04; a colour factor of 25 lifts it to 1 before the clamp.
                effect->setSpecularColorFactorEXTProperty(Vector3(25, 25, 25));
                return effect;
            };

            auto plain = lit();
            const Color none = draw(dev, buffer, *plain);
            check(near(none, Grey(1.0), Grey(1.0), Grey(1.0)),
                  "B: no specular maps keep the factor-only F0 = 1 (got " + describe(none) + ", want " +
                      std::to_string(Grey(1.0)) + ")");

            auto weighted = lit();
            weighted->setSpecularMapEXTProperty(strength.get());
            const Color weight0 = draw(dev, buffer, *weighted);
            check(near(weight0, Grey(0.0), Grey(0.0), Grey(0.0)),
                  "B: a strength map's alpha 0 removes the dielectric lobe (got " + describe(weight0) +
                      ", want " + std::to_string(Grey(0.0)) + ")");
            weighted->setSpecularTextureCoordinateSetEXTProperty(1);
            const Color weight1 = draw(dev, buffer, *weighted);
            check(near(weight1, Grey(1.0), Grey(1.0), Grey(1.0)),
                  "B: the strength map on TEXCOORD_1 reads alpha 1 (got " + describe(weight1) + ", want " +
                      std::to_string(Grey(1.0)) + ")");

            auto tinted = lit();
            tinted->setSpecularColorMapEXTProperty(blackColour.get());
            const Color black = draw(dev, buffer, *tinted);
            check(near(black, Grey(0.0), Grey(0.0), Grey(0.0)),
                  "B: a black specular colour map tints F0 to 0 (got " + describe(black) + ", want " +
                      std::to_string(Grey(0.0)) + ")");

            auto shiftedColour = lit();
            shiftedColour->setSpecularColorMapEXTProperty(blackWhite.get());
            TextureTransformEXT shift;
            shift.Offset = Vector2(0.5f, 0.0f);
            shiftedColour->setSpecularColorTextureTransformEXTProperty(shift);
            const Color white = draw(dev, buffer, *shiftedColour);
            check(near(white, Grey(1.0), Grey(1.0), Grey(1.0)),
                  "B: a +0.5 specular-colour transform reaches the white texel (got " + describe(white) +
                      ", want " + std::to_string(Grey(1.0)) + ")");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("B: a KHR_materials_specular PbrEffect draw threw: ") + e.what());
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    MetalPbrDualUvSpecularTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(128);
        gdm_->setPreferredBackBufferHeightProperty(96);
    }
};

int main()
{
    {
        MetalPbrDualUvSpecularTest game;
        game.Run();
    }
    return (passCount == totalCount && totalCount > 0) ? 0 : 1;
}
