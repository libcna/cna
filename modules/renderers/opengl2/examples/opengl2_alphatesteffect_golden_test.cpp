// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: cross-renderer visual-parity proof #3 -- reuses
// examples/easygl_alphatesteffect_golden_test.cpp's own AlphaTestEffect scene verbatim
// (ReferenceAlpha=128, AlphaFunction=Greater, Alpha=192/255, strictly above reference -> drawn,
// not discarded) and its own golden PNG (examples/golden/easygl_alphatesteffect_golden_test.png).
//
// AlphaTestEffect.Alpha modulates the whole draw colour (same premultiplied-alpha convention as
// every other stock effect's Alpha property), so White(255,255,255) * 192/255 = 192 exactly (an
// exact integer, no rounding uncertainty) on every channel, alpha=192 -- reuses that test's own
// derivation and tolerance=20.

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

class OpenGL2AlphaTestEffectGoldenTest : public CNA::Examples::PixelTestGame
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
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const auto& vp = device.getViewportProperty();
        const int samplePx = vp.getWidthProperty() / 2;
        const int sampleY = vp.getHeightProperty() / 2;
        ExpectPixel("greater-above-vs-derived-expected", Rectangle(samplePx, sampleY, 1, 1),
                    Color(192, 192, 192, 192), /*tolerance=*/20);
        CompareGoldenImage("alphatesteffect-greater-above-drawn-vs-easygl",
                            Rectangle(samplePx - 4, sampleY - 4, 8, 8),
                            "examples/golden/easygl_alphatesteffect_golden_test.png",
                            /*tolerance=*/20);
    }

public:
    OpenGL2AlphaTestEffectGoldenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2AlphaTestEffectGoldenTest>();
}
