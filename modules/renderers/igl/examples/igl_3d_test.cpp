// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-20: the colour-only 3D path -- a real VertexBuffer, a real IndexBuffer, the
// generated uber-effect shader with lighting off, and a real depth test.
//
// Scene: a red-cleared 64x64 back buffer with two full-screen quads drawn back to front. The green
// quad is nearer the camera than the blue one, so with DepthStencilState::Default the visible
// result is green -- and with the depth test broken it would be whichever was drawn last.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class Igl3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// One screen-filling quad at a fixed depth, in the identity clip space the test draws in.
    static std::vector<VertexPositionColor> Quad(const float z, const Color& colour)
    {
        return {
            VertexPositionColor(Vector3(-1.0f, -1.0f, z), colour),
            VertexPositionColor(Vector3(1.0f, -1.0f, z), colour),
            VertexPositionColor(Vector3(1.0f, 1.0f, z), colour),
            VertexPositionColor(Vector3(-1.0f, 1.0f, z), colour),
        };
    }

    void DrawQuad(GraphicsDevice& device, BasicEffect& effect, const float z, const Color& colour)
    {
        std::vector<VertexPositionColor> vertices = Quad(z, colour);
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                                  static_cast<int>(vertices.size()), BufferUsage::WriteOnly);
        vertexBuffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));

        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
        indexBuffer.SetData(indices, 0, 6);

        device.SetVertexBuffer(&vertexBuffer);
        device.setIndicesProperty(&indexBuffer);

        for (EffectPass& pass : effect.getCurrentTechniqueProperty()->getPassesProperty())
        {
            pass.Apply();
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        }
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());

        // Nearer quad first: the farther one must then fail the depth test.
        DrawQuad(device, effect, 0.25f,
                 Color(static_cast<bytecs>(0), static_cast<bytecs>(255), static_cast<bytecs>(0),
                       static_cast<bytecs>(255)));
        DrawQuad(device, effect, 0.75f,
                 Color(static_cast<bytecs>(0), static_cast<bytecs>(0), static_cast<bytecs>(255),
                       static_cast<bytecs>(255)));

        ExpectPixel("the nearer quad survives the depth test", Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(255)));
        ExpectPixel("the quad covers the top-left corner too", Rectangle(1, 1, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(255)));
        ExpectPixel("the quad covers the bottom-right corner too",
                    Rectangle(kSize - 2, kSize - 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(255)));
    }

public:
    Igl3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<Igl3DTest>();
}
