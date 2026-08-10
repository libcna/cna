// SPDX-License-Identifier: MS-PL
// Task 469: golden-image consumer reusing Phase 44's already-verified DualTextureEffect
// capstone scene (Task 389, examples/easygl_dualtextureeffect_combined_test.cpp) via
// PixelTestGame::CompareGoldenImage() (Task 463) instead of Task 389's own 4 hand-picked
// sample pixels.
//
// Recreates only Task 389's "top-left texel" sample: a 2x2 multi-texel texture (identical
// kTexels to Task 464's BasicEffect reuse), Texture2=gray(128,128,128) (cancels the
// `color.rgb *= 2` doubling factor, Task 383), DiffuseColor=(0.6,0.4,0.8), constant
// UV=(0.25,0.25). Expected = TexelColor * 2 * (128/255) * DiffuseColor ~= TexelColor *
// DiffuseColor = (120,40,40,255) (Task 389's own derivation). See Task 389's own file for the
// full 4-sample capstone this is one point of.
//
// Reuses Task 389's own tolerance=8.

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

    // 2x2 texture, row-major: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right --
    // identical to Task 389's own kTexels (and Task 464's BasicEffect reuse).
    const Color kTexels[4] = {
        Color(200, 100, 50,  255),
        Color( 50, 200, 100, 255),
        Color(100,  50, 200, 255),
        Color(150, 150, 150, 255),
    };
    const Color kGrayHalf(128, 128, 128, 255);
    const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);
}

class DualTextureEffectGoldenTest : public CNA::Examples::PixelTestGame
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
        // Task 896 finding -- same as Task 389's own comment.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        // Cross-check against Task 389's own derived expected value, independent of the golden
        // PNG's own contents (same rationale as Tasks 464-468).
        ExpectPixel("top-left-texel-vs-task389-expected", Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(120, 40, 40, 255), /*tolerance=*/8);
        CompareGoldenImage("dualtextureeffect-top-left-texel",
                            Rectangle(kSize / 2 - 4, kSize / 2 - 4, 8, 8),
                            "examples/golden/easygl_dualtextureeffect_golden_test.png",
                            /*tolerance=*/8);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<DualTextureEffectGoldenTest>();
}
