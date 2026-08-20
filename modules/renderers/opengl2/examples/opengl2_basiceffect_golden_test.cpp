// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof #2 -- reuses
// examples/easygl_basiceffect_golden_test.cpp's own BasicEffect scene verbatim (TextureEnabled+
// VertexColorEnabled, DiffuseColor+EmissiveColor, LightingEnabled=false, constant UV=(0.25,0.25))
// and its own golden PNG (examples/golden/easygl_basiceffect_golden_test.png), instead of a
// same-renderer-only numeric check.
//
// Expected = TexelColor * VertexColor/255 * (DiffuseColor+EmissiveColor), component-wise --
// reuses that test's own derived (99,52,23) expected value and tolerance=8.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // 2x2 texture, row-major: [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right --
    // identical to easygl_basiceffect_golden_test.cpp's own kTexels.
    const Color kTexels[4] = {
        Color(200, 100, 50,  255),
        Color( 50, 200, 100, 255),
        Color(100,  50, 200, 255),
        Color(150, 150, 150, 255),
    };

    const Color kVertexColor(180, 220, 140, 255);
    const Vector3 kDiffuse(0.6f, 0.4f, 0.8f);
    const Vector3 kEmissive(0.1f, 0.2f, 0.05f);
}

class OpenGL2BasicEffectGoldenTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        Texture2D tex(device, 2, 2);
        tex.SetData(kTexels, 4);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);

        // Constant UV=(0.25,0.25) across the whole quad -- every pixel samples the top-left texel.
        const Vector2 uv(0.25f, 0.25f);
        const VertexPositionColorTexture q[6] = {
            { tl, kVertexColor, uv }, { bl, kVertexColor, uv }, { br, kVertexColor, uv },
            { tl, kVertexColor, uv }, { br, kVertexColor, uv }, { tr, kVertexColor, uv },
        };

        BasicEffect fx(device);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.VertexColorEnabled = true;
        fx.setDiffuseColorProperty(kDiffuse);
        fx.setEmissiveColorProperty(kEmissive);

        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        fx.Apply();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        ExpectPixel("centre-vs-derived-expected", Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(99, 52, 23, 255), /*tolerance=*/8);
        CompareGoldenImage("basiceffect-top-left-texel-vs-easygl",
                            Rectangle(kSize / 2 - 4, kSize / 2 - 4, 8, 8),
                            "examples/golden/easygl_basiceffect_golden_test.png",
                            /*tolerance=*/8);
    }

public:
    OpenGL2BasicEffectGoldenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2BasicEffectGoldenTest>();
}
