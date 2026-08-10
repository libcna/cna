// SPDX-License-Identifier: MS-PL
// Task 466: golden-image consumer reusing Phase 35's already-verified texture-filter scene
// (Task 297, examples/easygl_texture_filter_point_vs_linear_test.cpp) via
// PixelTestGame::CompareGoldenImage() (Task 463) instead of Task 297's own predicate-based
// (IsBlended/IsPure range) sample checks.
//
// Recreates only Task 297's "Magnification/Linear" column -- the most distinctive case for a
// golden image (a genuine ~50/50 blend, not a flat colour): a 2-texel texture (Red|Green)
// stretched across a wide quad, sampled with TextureFilter::Linear via a DualTextureEffect
// (Task 383's DiffuseColor=0.5 compensation for its color.rgb*=2 doubling, same as Task 297),
// at the exact texel-centre midpoint (UV=0.5, guaranteed not to coincide with either texel
// centre at UV=0.25/0.75 for a 2-texel texture) -- Task 297's own column 2, sampled at its own
// pixel 384.
//
// Independently derived expected value: at UV=0.5, linear filtering blends texel 0 (Red) and
// texel 1 (Green) at their exact 50/50 midpoint, giving (127.5, 127.5, 0) before rounding --
// matches Task 297's own live-observed (127,128,0) in this sandbox. tolerance=10 is tight
// enough to still clearly discriminate against a broken/Point-filtered result (255,0,0) or
// (0,255,0), which differ by 128 from the expected blend.

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

class TextureFilterLinearGoldenTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // 2x1: Red | Green -- identical to Task 297's tex2_.
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
        // Task 383 fix compensation -- same as Task 297's own comment.
        fx.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        // Task 297's own column 2 (magnification, Linear): pixel range [256,512) of a
        // W-wide backbuffer, sampled at its own midpoint, pixel 384 -- UV=0.5 there regardless
        // of the actual W, same proportional-to-W technique as Task 297 itself.
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
        // Task 896 finding -- same as Task 297's own comment.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const int samplePx = 384;
        // Cross-check against the independently-derived expected blend, matching Task 297's
        // own live-observed value -- independent of the golden PNG's own contents (same
        // rationale as Tasks 464/465).
        ExpectPixel("linear-blend-vs-task297-expected",
                    Rectangle(samplePx, H / 2, 1, 1), Color(127, 128, 0, 255), /*tolerance=*/10);
        CompareGoldenImage("texture-filter-linear-blend",
                            Rectangle(samplePx - 4, H / 2 - 4, 8, 8),
                            "examples/golden/easygl_texture_filter_linear_golden_test.png",
                            /*tolerance=*/10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<TextureFilterLinearGoldenTest>();
}
