// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-55: AlphaTestEffect pixel conformance -- the first stock-effect parity test for
// this renderer family beyond BasicEffect's colour-only path.
//
// Scene: a red-cleared 64x64 back buffer, two textured quads side by side (left half, right half),
// both drawn with AlphaTestEffect using CompareFunction::Greater and ReferenceAlpha=128. The left
// quad's 1x1 texture is opaque green (alpha=255, passes 255>128) so it must be visible; the right
// quad's 1x1 texture is fully transparent green (alpha=0, fails 0>128) so its pixels must be
// discarded and the red clear must show through -- proving the alpha test itself runs, not just
// that a textured quad renders.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
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

class IglAlphaTestEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    /// A screen-half-covering quad at a fixed X range, sampling the whole of a 1x1 texture.
    static std::vector<VertexPositionTexture> Quad(const float left, const float right)
    {
        return {
            VertexPositionTexture(Vector3(left, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(right, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(right, 1.0f, 0.0f), Vector2(1.0f, 0.0f)),
            VertexPositionTexture(Vector3(left, 1.0f, 0.0f), Vector2(0.0f, 0.0f)),
        };
    }

    void DrawQuad(GraphicsDevice& device, AlphaTestEffect& effect, const float left,
                 const float right, Texture2D& texture)
    {
        effect.setTextureProperty(&texture);

        std::vector<VertexPositionTexture> vertices = Quad(left, right);
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
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        std::unique_ptr<Texture2D> opaqueTexture =
            MakeSolidTexture(device, Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                                           static_cast<bytecs>(0), static_cast<bytecs>(255)));
        std::unique_ptr<Texture2D> transparentTexture =
            MakeSolidTexture(device, Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                                           static_cast<bytecs>(0), static_cast<bytecs>(0)));

        AlphaTestEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setAlphaFunctionProperty(CompareFunction::Greater);
        effect.setReferenceAlphaProperty(128);

        // Left half: alpha=255 passes (255 > 128) -- must be visible.
        DrawQuad(device, effect, -1.0f, 0.0f, *opaqueTexture);
        // Right half: alpha=0 fails (0 > 128) -- must be discarded, red clear shows through.
        DrawQuad(device, effect, 0.0f, 1.0f, *transparentTexture);

        ExpectPixel("the passing quad (alpha=255) is visible", Rectangle(kSize / 4, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));
        ExpectPixel("the failing quad (alpha=0) is discarded, showing the clear colour",
                    Rectangle(3 * kSize / 4, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)));
    }

public:
    IglAlphaTestEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglAlphaTestEffectTest>();
}
