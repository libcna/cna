// SPDX-License-Identifier: MS-PL
// plans/plan_apple_m4.md AM4-080: declarations that carry only what the active BasicEffect
// permutation reads.
//
// XNA's BasicEffect has separate permutations, and the untextured ones do not read
// TEXCOORD0; the ones without VertexColorEnabled do not read COLOR0. Metal's vertex functions are
// coarser, and used to refuse such declarations ("the declaration has no TextureCoordinate0"),
// which stopped cna-samples Primitives3D, TiltPerspective, InverseKinematics and others outright.
//
// A -- lit, untextured, over VertexPositionNormal: the pixel is the emissive colour exactly (no
//      light enabled, black ambient), so the constant coordinate changes nothing.
// B -- unlit, VertexColorEnabled off, over a position-only declaration: the pixel is DiffuseColor.
// C -- textured over VertexPositionNormal still refuses, as XNA does.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <cstdint>
#include <cstdio>
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

    struct PositionNormal { Vector3 position; Vector3 normal; };

    // A full-screen triangle pair in clip space: identity matrices, so every pixel is covered.
    const Vector3 kCorners[6] = {
        Vector3(-1, -1, 0), Vector3(-1, 1, 0), Vector3(1, -1, 0),
        Vector3(1, -1, 0), Vector3(-1, 1, 0), Vector3(1, 1, 0)};
}

class MetalUntexturedDeclarationTest final : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);

        // A: lit, untextured, Position + Normal only.
        const VertexDeclaration positionNormal(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0)});
        PositionNormal litVertices[6];
        for (int i = 0; i < 6; ++i) litVertices[i] = {kCorners[i], Vector3(0, 0, 1)};
        VertexBuffer litBuffer(dev, positionNormal, 6, BufferUsage::WriteOnly);
        litBuffer.SetData(litVertices, 0, 6);
        BasicEffect lit(dev);
        lit.setLightingEnabledProperty(true);
        lit.getDirectionalLight0Property().setEnabledProperty(false);
        lit.getDirectionalLight1Property().setEnabledProperty(false);
        lit.getDirectionalLight2Property().setEnabledProperty(false);
        lit.setAmbientLightColorProperty(Vector3(0, 0, 0));
        lit.setDiffuseColorProperty(Vector3(1, 1, 1));
        lit.setEmissiveColorProperty(Vector3(0, 1, 0));
        lit.setSpecularColorProperty(Vector3(0, 0, 0));
        try
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffer(&litBuffer);
            lit.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            const Color a = centre(dev);
            check(a.getRProperty() == 0 && a.getGProperty() == 255 && a.getBProperty() == 0,
                  "A: a lit untextured BasicEffect over VertexPositionNormal draws its emissive colour "
                  "(got " + describe(a) + ", want (0,255,0))");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("A: drawing VertexPositionNormal threw: ") + e.what());
        }

        // B: unlit, VertexColorEnabled off, Position only.
        const VertexDeclaration positionOnly(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
        VertexBuffer plainBuffer(dev, positionOnly, 6, BufferUsage::WriteOnly);
        plainBuffer.SetData(kCorners, 0, 6);
        BasicEffect unlit(dev);
        unlit.setVertexColorEnabledProperty(false);
        unlit.setDiffuseColorProperty(Vector3(1, 0, 0));
        try
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffer(&plainBuffer);
            unlit.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            const Color b = centre(dev);
            check(b.getRProperty() == 255 && b.getGProperty() == 0 && b.getBProperty() == 0,
                  "B: an unlit BasicEffect without vertex colour over a position-only declaration draws "
                  "DiffuseColor (got " + describe(b) + ", want (255,0,0))");
        }
        catch (const std::exception& e)
        {
            check(false, std::string("B: drawing a position-only declaration threw: ") + e.what());
        }

        // D (AM4-081): lit with VertexColorEnabled over Position + Normal + Color, in both lighting
        // variants. No light and black ambient leave the emissive colour, which XNA's BasicEffect
        // multiplies by the vertex colour: white emissive x blue vertices = blue.
        const VertexDeclaration positionNormalColor(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Color, VertexElementUsage::Color, 0)});
        struct PositionNormalColor { Vector3 position; Vector3 normal; std::uint32_t packedColor; };
        PositionNormalColor colouredVertices[6];
        const std::uint32_t blue = Color(0, 0, 255, 255).getPackedValueProperty();
        for (int i = 0; i < 6; ++i) colouredVertices[i] = {kCorners[i], Vector3(0, 0, 1), blue};
        VertexBuffer colouredBuffer(dev, positionNormalColor, 6, BufferUsage::WriteOnly);
        colouredBuffer.SetData(colouredVertices, 0, 6);
        for (const bool perPixel : {false, true})
        {
            BasicEffect coloured(dev);
            coloured.setLightingEnabledProperty(true);
            coloured.setPreferPerPixelLightingProperty(perPixel);
            coloured.setVertexColorEnabledProperty(true);
            coloured.getDirectionalLight0Property().setEnabledProperty(false);
            coloured.getDirectionalLight1Property().setEnabledProperty(false);
            coloured.getDirectionalLight2Property().setEnabledProperty(false);
            coloured.setAmbientLightColorProperty(Vector3(0, 0, 0));
            coloured.setEmissiveColorProperty(Vector3(1, 1, 1));
            coloured.setSpecularColorProperty(Vector3(0, 0, 0));
            const std::string variant = perPixel ? "per-pixel" : "per-vertex";
            try
            {
                dev.Clear(Color(0, 0, 0, 255));
                dev.SetVertexBuffer(&colouredBuffer);
                coloured.Apply();
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                const Color d = centre(dev);
                check(d.getRProperty() == 0 && d.getGProperty() == 0 && d.getBProperty() == 255,
                      "D: a lit " + variant + " BasicEffect with VertexColorEnabled multiplies by the "
                      "declared colour (got " + describe(d) + ", want (0,0,255))");
            }
            catch (const std::exception& e)
            {
                check(false, "D: lit " + variant + " with vertex colour threw: " + e.what());
            }
        }

        // C: textured over Position + Normal is refused, as XNA refuses it.
        Texture2D white(dev, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 0, 1);
        lit.setTextureEnabledProperty(true);
        lit.setTextureProperty(&white);
        bool refused = false;
        try
        {
            dev.SetVertexBuffer(&litBuffer);
            lit.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        catch (const std::exception&)
        {
            refused = true;
        }
        dev.SetVertexBuffer(nullptr);
        dev.getTexturesProperty()(0, nullptr);
        check(refused, "C: a textured BasicEffect over a declaration without TextureCoordinate0 refuses");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    MetalUntexturedDeclarationTest()
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
        MetalUntexturedDeclarationTest game;
        game.Run();
    }
    return (passCount == totalCount && totalCount > 0) ? 0 : 1;
}
