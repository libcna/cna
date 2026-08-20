// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-55: DualTextureEffect pixel conformance.
//
// XNA's DualTextureEffect multiplies the two textures together and DOUBLES the result before the
// diffuse tint (`color *= texture(uTexture1, vTexCoord1) * 2.0;` in IglShaderLibrary.cpp) --
// texture2 is a solid grey (128,128,128) here specifically so its ~0.5 factor times the doubling
// cancels out, leaving `expected = texture0.rgb * diffuseColor` and proving both textures and the
// tint genuinely reached the shader rather than one being silently ignored.
//
// Scene: a 64x64 back buffer, one full-screen quad with texture0=(200,100,50), texture2=grey,
// DiffuseColor=(0.6,0.4,0.8) -- expected pixel ~= (120,40,40,255).
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    std::unique_ptr<Texture2D> MakeSolidTexture(GraphicsDevice& device, const Color& colour)
    {
        auto texture = std::make_unique<Texture2D>(device, 1, 1);
        const Color pixel[1] = {colour};
        texture->SetData(pixel, 1);
        return texture;
    }
}

class IglDualTextureEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

        std::unique_ptr<Texture2D> baseTexture =
            MakeSolidTexture(device, Color(static_cast<bytecs>(200), static_cast<bytecs>(100),
                                           static_cast<bytecs>(50), static_cast<bytecs>(255)));
        std::unique_ptr<Texture2D> overlayTexture =
            MakeSolidTexture(device, Color(static_cast<bytecs>(128), static_cast<bytecs>(128),
                                           static_cast<bytecs>(128), static_cast<bytecs>(255)));

        DualTextureEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setTextureProperty(baseTexture.get());
        effect.setTexture2Property(overlayTexture.get());
        effect.setDiffuseColorProperty(Vector3(0.6f, 0.4f, 0.8f));

        const std::vector<VertexPositionTexture> vertices = {
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
            VertexPositionTexture(Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        VertexBuffer vertexBuffer(device, VertexPositionTexture::getVertexDeclarationStatic(),
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

        // texture0 x texture2 x 2.0 x diffuse: (200,100,50) x (~1.0 after doubling) x (0.6,0.4,0.8)
        ExpectPixel("both textures and the diffuse tint reach the shader",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(120), static_cast<bytecs>(40),
                          static_cast<bytecs>(40), static_cast<bytecs>(255)),
                    /*tolerance=*/8);
    }

public:
    IglDualTextureEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglDualTextureEffectTest>();
}
