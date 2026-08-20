// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof -- reuses
// examples/easygl_dualtextureeffect_golden_test.cpp's own DualTextureEffect scene verbatim: a
// 2x2 multi-texel texture, Texture2=gray(128,128,128) (cancels the `color.rgb *= 2` doubling
// factor), DiffuseColor=(0.6,0.4,0.8), constant UV=(0.25,0.25) (top-left texel).
// Expected = TexelColor * 2 * (128/255) * DiffuseColor ~= TexelColor * DiffuseColor
// = (120,40,40,255).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const Color kTexels[4] = {
        Color(200, 100, 50,  255),
        Color( 50, 200, 100, 255),
        Color(100,  50, 200, 255),
        Color(150, 150, 150, 255),
    };
    const Color kGrayHalf(128, 128, 128, 255);
    const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);
}

class OpenGL2DualTextureEffectGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D tex(device, 2, 2);
        tex.SetData(kTexels, 4);
        Texture2D texGray(device, 1, 1);
        texGray.SetData(&kGrayHalf, 1);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv(0.25f, 0.25f); // top-left texel

        const VertexPositionTexture q[6] = {
            { tl, uv }, { bl, uv }, { br, uv },
            { tl, uv }, { br, uv }, { tr, uv },
        };

        DualTextureEffect fx(device);
        fx.setTextureProperty(&tex);
        fx.setTexture2Property(&texGray);
        fx.setDiffuseColorProperty(kDiffuse);
        fx.Apply();

        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        ExpectPixel("top-left-texel-vs-expected", Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(120, 40, 40, 255), /*tolerance=*/8);
        CompareGoldenImage("dualtextureeffect-top-left-texel-vs-easygl",
                            Rectangle(kSize / 2 - 4, kSize / 2 - 4, 8, 8),
                            "examples/golden/easygl_dualtextureeffect_golden_test.png",
                            /*tolerance=*/8);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2DualTextureEffectGoldenTest>();
}
