// SPDX-License-Identifier: MS-PL
// plans/plan_apple_m4.md AM4-084: glTF's COLOR_0 in Metal's metallic-roughness product.
//
// glTF 2.0 3.9.2 makes COLOR_0 a linear multiplier on base colour, alpha included. Metal used to
// refuse every PBR draw that enabled it (GLTF-465); it now reads the colour from the stride-60 and
// stride-80 records when PbrEffect/SkinnedPbrEffect.VertexColorEnabledEXT asks for it.
//
// Every draw covers the screen with lights off, white ambient, white base colour and no maps, so the
// pixel is the base-colour product itself (encoded to sRGB, which keeps 0 and 1 exact):
//   A -- rigid stride 60, VertexColorEnabledEXT: the vertex colour (blue);
//   B -- the same record with VertexColorEnabledEXT off: the identity (white);
//   C -- a half-transparent vertex colour under NonPremultiplied blending: its alpha reaches the
//        product (half blue over black);
//   D -- a stride-48 record with no colour and the switch on: glTF's absent COLOR_0, the identity;
//   E -- skinned stride 80 through SkinnedPbrEffect with identity bones: the vertex colour (red).
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
        return std::abs(c.getRProperty() - r) <= 2 && std::abs(c.getGProperty() - g) <= 2 &&
               std::abs(c.getBProperty() - b) <= 2;
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

class MetalPbrVertexColourTest final : public Game
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
        effect.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
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

        try
        {
            auto blue = rigid60(Color(0, 0, 255, 255));
            PbrEffect effect(dev);
            Neutral(effect);
            effect.VertexColorEnabledEXT = true;
            const Color a = draw(dev, *blue, effect);
            check(near(a, 0, 0, 255), "A: a stride-60 PbrEffect draw multiplies base colour by COLOR_0 (got " +
                                          describe(a) + ", want (0,0,255))");

            effect.VertexColorEnabledEXT = false;
            const Color b = draw(dev, *blue, effect);
            check(near(b, 255, 255, 255), "B: with VertexColorEnabledEXT off the multiplier is the identity (got " +
                                              describe(b) + ", want (255,255,255))");

            auto halfBlue = rigid60(Color(0, 0, 255, 128));
            effect.VertexColorEnabledEXT = true;
            dev.setBlendStateProperty(BlendState::NonPremultiplied);
            const Color c = draw(dev, *halfBlue, effect);
            dev.setBlendStateProperty(BlendState::Opaque);
            check(near(c, 0, 0, 128), "C: COLOR_0's alpha multiplies base colour alpha (got " + describe(c) +
                                          ", want (0,0,128) under NonPremultiplied over black)");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("A-C: a stride-60 PbrEffect draw threw: ") + e.what());
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
            check(near(d, 255, 255, 255), "D: a record without COLOR_0 is the identity even with the switch on (got " +
                                              describe(d) + ", want (255,255,255))");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("D: a stride-48 PbrEffect draw threw: ") + e.what());
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
            check(near(e, 255, 0, 0), "E: a stride-80 SkinnedPbrEffect draw multiplies base colour by COLOR_0 (got " +
                                          describe(e) + ", want (255,0,0))");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("E: a stride-80 SkinnedPbrEffect draw threw: ") + e.what());
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    MetalPbrVertexColourTest()
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
        MetalPbrVertexColourTest game;
        game.Run();
    }
    return (passCount == totalCount && totalCount > 0) ? 0 : 1;
}
