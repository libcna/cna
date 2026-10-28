// SPDX-License-Identifier: MS-PL
// A BasicEffect permutation reads only the vertex elements it uses, and refuses a declaration that
// lacks one it does use.
//
// XNA's BasicEffect selects a vertex shader per permutation, and XNA's GraphicsDevice checks the
// bound declaration against that shader's inputs before every draw (Microsoft.Xna.Framework.
// Graphics.GraphicsDevice's draw validation compares the shader's input semantics with the
// declaration's and throws InvalidOperationException "The current vertex declaration does not
// include all the elements required by the current vertex shader. TextureCoordinate0 is missing.").
//
//   B -- unlit, VertexColorEnabled off, untextured, over a Position-only declaration: that
//        permutation reads POSITION alone, so the draw succeeds and the pixel is DiffuseColor.
//   C -- the textured permutation over Position + Normal: TEXCOORD0 is read and absent, so the draw
//        throws InvalidOperationException rather than sampling a defaulted coordinate.
//
// First written for the retired METAL renderer (Apple M4 AM4-080).
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "System/InvalidOperationException.hpp"

#include <exception>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct PositionNormal { Vector3 position; Vector3 normal; };

    // A full-screen triangle pair in clip space: identity matrices, so every pixel is covered.
    const Vector3 kCorners[6] = {
        Vector3(-1, -1, 0), Vector3(-1, 1, 0), Vector3(1, -1, 0),
        Vector3(1, -1, 0), Vector3(-1, 1, 0), Vector3(1, 1, 0)};
}

class BasicEffectDeclarationInputsTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        const auto& viewport = dev.getViewportProperty();
        const Rectangle centre(viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);

        // B: unlit, VertexColorEnabled off, untextured, Position only.
        const VertexDeclaration positionOnly(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
        VertexBuffer plainBuffer(dev, positionOnly, 6, BufferUsage::WriteOnly);
        plainBuffer.SetData(kCorners, 0, 6);
        BasicEffect unlit(dev);
        unlit.setVertexColorEnabledProperty(false);
        unlit.setTextureEnabledProperty(false);
        unlit.setLightingEnabledProperty(false);
        unlit.setDiffuseColorProperty(Vector3(1, 0, 0));
        try
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffer(&plainBuffer);
            unlit.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            ExpectPixel("B: an unlit, uncoloured, untextured BasicEffect over a Position-only "
                        "declaration draws DiffuseColor",
                        centre, Color(255, 0, 0, 255));
        }
        catch (const std::exception& e)
        {
            dev.SetVertexBuffer(nullptr);
            Check(false, (std::string("B: drawing a Position-only declaration threw: ") + e.what()).c_str());
        }

        // C: textured over Position + Normal is refused, as XNA refuses it.
        const VertexDeclaration positionNormal(std::vector<VertexElement>{
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0)});
        PositionNormal vertices[6];
        for (int i = 0; i < 6; ++i) vertices[i] = {kCorners[i], Vector3(0, 0, 1)};
        VertexBuffer normalBuffer(dev, positionNormal, 6, BufferUsage::WriteOnly);
        normalBuffer.SetData(vertices, 0, 6);

        Texture2D white(dev, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 0, 1);
        BasicEffect textured(dev);
        textured.setLightingEnabledProperty(false);
        textured.setTextureEnabledProperty(true);
        textured.setTextureProperty(&white);

        std::string outcome = "the draw was accepted";
        bool refusedAsXna = false;
        try
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffer(&normalBuffer);
            textured.Apply();
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        catch (const System::InvalidOperationException& e)
        {
            refusedAsXna = true;
            outcome = std::string("InvalidOperationException: ") + e.what();
        }
        catch (const std::exception& e)
        {
            outcome = std::string("a different exception type: ") + e.what();
        }
        dev.SetVertexBuffer(nullptr);
        dev.getTexturesProperty()(0, nullptr);
        Check(refusedAsXna, ("C: a textured BasicEffect over a declaration without TextureCoordinate0 "
                             "throws InvalidOperationException (" + outcome + ")").c_str());
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("basic_effect_declaration_inputs"))
        return CNA::Examples::Conformance::kSkipExitCode;
    return CNA::Examples::RunPixelTest<BasicEffectDeclarationInputsTest>();
}
