// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_texture_filter_linear_golden_test.cpp's own scene verbatim: a 2-texel texture
// (Red|Green) stretched across a wide quad, sampled with TextureFilter::Linear via a
// DualTextureEffect (DiffuseColor=0.5 compensates the doubling factor), at the exact texel-centre
// midpoint (UV=0.5). Expected: a genuine ~50/50 blend (127,128,0), not a flat colour -- proves
// real linear filtering, not a Point/broken fallback (which would read (255,0,0) or (0,255,0)).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OpenGL2TextureFilterLinearGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        const std::vector<std::uint8_t> pattern2 = {
            255,   0,   0, 255,
              0, 255,   0, 255
        };
        Texture2D tex2 = Texture2D::CreateFromPixels(device, 2, 1, pattern2);
        const std::vector<std::uint8_t> white = { 255, 255, 255, 255 };
        Texture2D whiteTex = Texture2D::CreateFromPixels(device, 1, 1, white);

        SamplerState ss;
        ss.setFilterProperty(TextureFilter::Linear);
        ss.setAddressUProperty(TextureAddressMode::Clamp);
        ss.setAddressVProperty(TextureAddressMode::Clamp);
        device.getSamplerStatesProperty()[0] = ss;

        DualTextureEffect fx(device);
        fx.setTextureProperty(&tex2);
        fx.setTexture2Property(&whiteTex);
        fx.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const float xLeftPx = 256.0f, xRightPx = 512.0f;
        const float ndcLeft  = (xLeftPx  / static_cast<float>(W)) * 2.0f - 1.0f;
        const float ndcRight = (xRightPx / static_cast<float>(W)) * 2.0f - 1.0f;
        const VertexPositionTexture verts[6] = {
            { Vector3(ndcLeft,   1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(ndcLeft,  -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(ndcRight, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(ndcLeft,   1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(ndcRight, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(ndcRight,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const int samplePx = 384;
        ExpectPixel("linear-blend-vs-expected",
                    Rectangle(samplePx, H / 2, 1, 1), Color(127, 128, 0, 255), /*tolerance=*/10);
        CompareGoldenImage("texture-filter-linear-blend-vs-easygl",
                            Rectangle(samplePx - 4, H / 2 - 4, 8, 8),
                            "examples/golden/easygl_texture_filter_linear_golden_test.png",
                            /*tolerance=*/10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2TextureFilterLinearGoldenTest>();
}
