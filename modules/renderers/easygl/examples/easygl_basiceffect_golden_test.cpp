// SPDX-License-Identifier: MS-PL
// Task 464: first real (non-canary) golden-image consumer, reusing Phase 42's already-verified
// BasicEffect capstone scene (Task 370, examples/easygl_basiceffect_combined_test.cpp) via the
// new PixelTestGame::CompareGoldenImage() helper (Task 463) instead of a hand-picked centre
// pixel. Renders the exact same quad/texture/material setup as Task 370's "top-left texel"
// sample (TextureEnabled+VertexColorEnabled, DiffuseColor+EmissiveColor, LightingEnabled=false,
// constant UV=(0.25,0.25)) and checks an 8x8 region around the centre against a checked-in
// reference PNG, rather than just the single centre pixel -- this genuinely exercises more of
// the rendered output than the original test's own point samples do.
//
// Expected = TexelColor * VertexColor/255 * (DiffuseColor+EmissiveColor), component-wise, same
// formula and same (99,52,23) expected value as Task 370's own "top-left texel" case; reuses
// that test's own tolerance=8 (Task 462's own 98-file survey found this the single most common
// value for exactly this class of GPU rounding/blending noise).

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
    // identical to Task 370's own kTexels.
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

class BasicEffectGoldenTest : public CNA::Examples::PixelTestGame
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

        // Constant UV=(0.25,0.25) across the whole quad -- every pixel samples the top-left
        // texel, same as Task 370's own "top-left texel" sample.
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
        // Task 896 finding (same as Task 370's own comment): this quad's winding is culled by
        // the real default RasterizerState once EasyGL pushes it at construction.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        // Cross-check against Task 370's own hardcoded expected value, independent of whatever
        // is actually stored in the golden PNG below -- catches the case where the golden image
        // was regenerated (CNA_UPDATE_GOLDEN=1) against a broken render and would otherwise
        // silently "pass" against its own now-wrong reference.
        ExpectPixel("centre-vs-task370-expected", Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(99, 52, 23, 255), /*tolerance=*/8);
        CompareGoldenImage("basiceffect-top-left-texel",
                            Rectangle(kSize / 2 - 4, kSize / 2 - 4, 8, 8),
                            "examples/golden/easygl_basiceffect_golden_test.png",
                            /*tolerance=*/8);
    }

public:
    BasicEffectGoldenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<BasicEffectGoldenTest>();
}
