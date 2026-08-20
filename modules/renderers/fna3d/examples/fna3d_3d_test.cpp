// SPDX-License-Identifier: MS-PL
// plans/plan_fna3d.md FNA3D-8: pixel oracle for the FNA3D renderer's 3D path. Unlike every other CNA
// renderer, this one does not translate BasicEffect into shaders of its own -- it executes XNA's
// actual compiled BasicEffect through MojoShader, selecting the shader variant with the same
// integer ShaderIndex arithmetic XNA's own BasicEffect.OnApply() computes. These checks therefore
// prove that variant selection, the effect parameter packing and the vertex-declaration binding
// are all right, not merely that a draw call returned.
//
// Check A -- a vertex-coloured quad renders through the unlit, untextured BasicEffect variant.
// Check B -- the indexed draw route renders the same quad.
// Check C -- DiffuseColor modulates an untextured, non-vertex-coloured draw (a different
//   ShaderIndex, and a real float4 effect-parameter write).
// Check D -- the depth test rejects the farther quad regardless of submission order.
// Check E -- a textured quad samples the bound texture (the textureEnabled ShaderIndex branch).
// Check F -- AlphaTestEffect's clip() really discards: a quad whose alpha fails the test leaves
//   the cleared colour behind, and the same quad passes when the test is satisfied.
// Check G -- a wireframe RasterizerState leaves the interior of a triangle unfilled.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class Fna3d3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<BasicEffect> basic_;
    std::unique_ptr<AlphaTestEffect> alphaTest_;

    void DrawColoredQuad(float z, const Color& color)
    {
        auto& device = getGraphicsDeviceProperty();
        const VertexPositionColor vertices[6] = {
            { Vector3(-1.0f, 1.0f, z), color },  { Vector3(-1.0f, -1.0f, z), color },
            { Vector3(1.0f, -1.0f, z), color },  { Vector3(-1.0f, 1.0f, z), color },
            { Vector3(1.0f, -1.0f, z), color },  { Vector3(1.0f, 1.0f, z), color },
        };
        basic_->VertexColorEnabled = true;
        basic_->setTextureEnabledProperty(false);
        basic_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        basic_ = std::make_unique<BasicEffect>(device);
        alphaTest_ = std::make_unique<AlphaTestEffect>(device);

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        const int width = device.getViewportProperty().getWidthProperty();
        const int height = device.getViewportProperty().getHeightProperty();
        const Rectangle centre(width / 2, height / 2, 1, 1);

        // Check A -- unlit vertex-coloured quad.
        device.Clear(Color(0, 0, 0, 255));
        DrawColoredQuad(0.5f, Color(255, 0, 0, 255));
        ExpectPixel("BasicEffect renders a vertex-coloured quad", centre, Color(255, 0, 0, 255), 2);

        // Check B -- the indexed route.
        device.Clear(Color(0, 0, 0, 255));
        {
            const VertexPositionColor vertices[4] = {
                { Vector3(-1.0f, 1.0f, 0.5f), Color(0, 0, 255, 255) },
                { Vector3(1.0f, 1.0f, 0.5f), Color(0, 0, 255, 255) },
                { Vector3(-1.0f, -1.0f, 0.5f), Color(0, 0, 255, 255) },
                { Vector3(1.0f, -1.0f, 0.5f), Color(0, 0, 255, 255) },
            };
            const std::uint16_t indices[6] = { 0, 1, 2, 1, 3, 2 };
            basic_->VertexColorEnabled = true;
            basic_->setTextureEnabledProperty(false);
            basic_->Apply();
            device.DrawUserIndexedPrimitives(PrimitiveType::TriangleList, vertices, 0, 4, indices,
                                             0, 2);
        }
        ExpectPixel("the indexed draw route renders the same quad", centre, Color(0, 0, 255, 255),
                    2);

        // Check C -- DiffuseColor on a non-vertex-coloured draw (a different ShaderIndex).
        device.Clear(Color(0, 0, 0, 255));
        {
            const VertexPositionColor vertices[6] = {
                { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
                { Vector3(-1.0f, -1.0f, 0.5f), Color::White },
                { Vector3(1.0f, -1.0f, 0.5f), Color::White },
                { Vector3(-1.0f, 1.0f, 0.5f), Color::White },
                { Vector3(1.0f, -1.0f, 0.5f), Color::White },
                { Vector3(1.0f, 1.0f, 0.5f), Color::White },
            };
            basic_->VertexColorEnabled = false;
            basic_->setTextureEnabledProperty(false);
            basic_->setDiffuseColorProperty(Vector3(0.0f, 1.0f, 0.0f));
            basic_->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
            basic_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        }
        ExpectPixel("DiffuseColor drives an untextured, non-vertex-coloured draw", centre,
                    Color(0, 255, 0, 255), 2);

        // Check D -- depth test, both submission orders.
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f,
                     0);
        DrawColoredQuad(0.9f, Color(255, 0, 0, 255));
        DrawColoredQuad(0.1f, Color(0, 255, 0, 255));
        ExpectPixel("depth test: the near quad drawn second wins", centre, Color(0, 255, 0, 255),
                    2);

        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0, 0, 0, 255), 1.0f,
                     0);
        DrawColoredQuad(0.1f, Color(0, 255, 0, 255));
        DrawColoredQuad(0.9f, Color(255, 0, 0, 255));
        ExpectPixel("depth test: the near quad drawn first still wins", centre,
                    Color(0, 255, 0, 255), 2);
        device.setDepthStencilStateProperty(DepthStencilState::None);

        // Check E -- textured draw.
        const std::vector<std::uint8_t> texelBytes = { 0, 128, 255, 255 };
        Texture2D texel = Texture2D::CreateFromPixels(device, 1, 1, texelBytes);
        device.Clear(Color(0, 0, 0, 255));
        {
            const VertexPositionTexture vertices[6] = {
                { Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
                { Vector3(1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
                { Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.0f, 0.0f) },
                { Vector3(1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
                { Vector3(1.0f, 1.0f, 0.5f), Vector2(1.0f, 0.0f) },
            };
            basic_->VertexColorEnabled = false;
            basic_->setTextureEnabledProperty(true);
            basic_->setTextureProperty(&texel);
            basic_->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
            basic_->setTextureEnabledProperty(false);
        }
        ExpectPixel("a textured quad samples the bound texture", centre, Color(0, 128, 255, 255),
                    2);

        // Check F -- AlphaTestEffect really discards.
        const std::vector<std::uint8_t> lowAlpha = { 255, 255, 255, 32 };
        Texture2D faint = Texture2D::CreateFromPixels(device, 1, 1, lowAlpha);
        const VertexPositionTexture alphaQuad[6] = {
            { Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.5f), Vector2(0.0f, 1.0f) },
            { Vector3(1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(-1.0f, 1.0f, 0.5f), Vector2(0.0f, 0.0f) },
            { Vector3(1.0f, -1.0f, 0.5f), Vector2(1.0f, 1.0f) },
            { Vector3(1.0f, 1.0f, 0.5f), Vector2(1.0f, 0.0f) },
        };

        device.Clear(Color(0, 0, 255, 255));
        alphaTest_->setTextureProperty(&faint);
        alphaTest_->setAlphaFunctionProperty(CompareFunction::GreaterEqual);
        alphaTest_->setReferenceAlphaProperty(200);
        alphaTest_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, alphaQuad, 0, 2);
        ExpectPixel("AlphaTestEffect discards a fragment below the reference alpha", centre,
                    Color(0, 0, 255, 255), 2);

        device.Clear(Color(0, 0, 255, 255));
        alphaTest_->setReferenceAlphaProperty(8);
        alphaTest_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, alphaQuad, 0, 2);
        ExpectPixel("AlphaTestEffect keeps a fragment above the reference alpha", centre,
                    Color(255, 255, 255, 255), 2);

        // Check G -- wireframe leaves the interior unfilled.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(Color(0, 0, 0, 255));
        DrawColoredQuad(0.5f, Color(255, 0, 0, 255));
        const bool solidFilled =
            ExpectPixel("solid fill covers a point away from every edge",
                        Rectangle(width / 4, height / 2, 1, 1), Color(255, 0, 0, 255), 2);

        RasterizerState wireframe;
        wireframe.setCullModeProperty(CullMode::None);
        wireframe.setFillModeProperty(FillMode::WireFrame);
        device.setRasterizerStateProperty(wireframe);
        device.Clear(Color(0, 0, 0, 255));
        DrawColoredQuad(0.5f, Color(255, 0, 0, 255));
        if (solidFilled)
        {
            ExpectPixel("wireframe leaves that same interior point unfilled",
                        Rectangle(width / 4, height / 2, 1, 1), Color(0, 0, 0, 255));
        }
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Fna3d3DTest>();
}
