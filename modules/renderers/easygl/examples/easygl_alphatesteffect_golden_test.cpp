// SPDX-License-Identifier: MS-PL
// Task 469: golden-image consumer reusing Phase 43's already-verified AlphaTestEffect scene
// (Task 373, examples/easygl_alphatest_comparefunction_sweep_test.cpp) via
// PixelTestGame::CompareGoldenImage() (Task 463) instead of Task 373's own 24-case sweep.
//
// Recreates only Task 373's "Greater, above reference" sub-case (one of its 24): a 1x1 white
// texture, AlphaTestEffect with ReferenceAlpha=128, AlphaFunction=Greater, alpha=192/255
// (strictly above reference) -- FNA's AlphaTest switch means Greater passes when alpha > 128/255,
// so this sub-case must be DRAWN, not discarded (the clear colour, Black). See Task 373's own
// file for the full 8-CompareFunction x 3-alpha-value sweep this is one point of.
//
// Task 373's own check is a coarse binary predicate (R>50, drawn-vs-discarded only) and never
// asserted the exact RGB value, so it could not simply be reused here -- independently derived
// it instead: AlphaTestEffect.Alpha modulates the whole draw colour (same premultiplied-alpha
// convention as every other stock effect's Alpha property), so White(255,255,255) * 192/255 =
// 192 exactly (an exact integer, no rounding uncertainty) on every channel, alpha=192.
// Confirmed by live execution: pixel=(192,192,192,192), matching this derivation exactly.
// tolerance=20 (loose enough for normal GPU rounding, tight enough to clearly discriminate
// against a fully-opaque White(255,255,255,255) or a discarded Black(0,0,0,255) result).

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class AlphaTestEffectGoldenTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(Color(0, 0, 0, 255));

        const Color kWhite(255, 255, 255, 255);
        Texture2D whiteTex(device, 1, 1);
        whiteTex.SetData(&kWhite, 1);

        AlphaTestEffect fx(device);
        fx.setTextureProperty(&whiteTex);
        fx.setAlphaProperty(192.0f / 255.0f); // above reference
        fx.setReferenceAlphaProperty(128);
        fx.setAlphaFunctionProperty(CompareFunction::Greater);
        fx.Apply();

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        // Task 896 finding -- same as Task 373's own comment.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const auto& vp = device.getViewportProperty();
        const int samplePx = vp.getWidthProperty() / 2;
        const int sampleY = vp.getHeightProperty() / 2;
        // Cross-check against the independently-derived expected value (see file header),
        // independent of the golden PNG's own contents (same rationale as Tasks 464-468).
        ExpectPixel("greater-above-vs-derived-expected", Rectangle(samplePx, sampleY, 1, 1),
                    Color(192, 192, 192, 192), /*tolerance=*/20);
        CompareGoldenImage("alphatesteffect-greater-above-drawn",
                            Rectangle(samplePx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_alphatesteffect_golden_test.png",
                            /*tolerance=*/20);
    }

public:
    AlphaTestEffectGoldenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<AlphaTestEffectGoldenTest>();
}
