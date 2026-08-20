// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-19: AlphaTestEffect + DualTextureEffect for the OpenGL4 graphics renderer.
// Both effects reuse the existing textured3d (stride 20, VertexPositionTexture) and
// colored_textured3d (stride 24) shader programs GL4-13 already built -- DualTextureEffect
// samples both textures with the SAME UV set in real XNA (no separate UV1 vertex attribute), and
// AlphaTestEffect's discard test is just an extra uniform + branch on the same fragment output --
// so neither needed a new stride case, only new uniforms (uAlphaTest, uTexture2,
// uDualTextureEnabled) folded into the two existing programs.
//
// AlphaTestEffect (methodology matches easygl_alphatest_modes_test.cpp): a white 1x1 texture with
// alpha=128/255 and ReferenceAlpha=128, swept across a representative subset of CompareFunction
// values (not the full 8 -- AlphaTestEffectTests.cpp already covers the alphaTest-vec4 encoding
// itself in isolation at the unit-test layer; this is a real end-to-end GPU discard proof).
// Check A -- CompareFunction::Always: always passes, pixel is drawn.
// Check B -- CompareFunction::Never: always fails, pixel is discarded (background shows through).
// Check C -- CompareFunction::LessEqual with alpha==reference: passes (128<=128), pixel is drawn.
// Check D -- CompareFunction::Equal with alpha==reference: passes (128==128), pixel is drawn.
//
// DualTextureEffect (methodology matches easygl_dualtexture_test.cpp): FragColor = (tex1*2) *
// tex2 * diffuse (the real XNA/D3D DualTextureEffect.fx "2x-modulate" lightmap-style blend, not a
// naive single multiply).
// Check E -- tex1=white, tex2=blue,  diffuse=white -> blue (tex2 genuinely contributes).
// Check F -- tex1=red,   tex2=white, diffuse=white -> red  (tex1 genuinely contributes).
// Check G -- tex1=white, tex2=mid-gray, diffuse=green -> green at diffuse's own intensity (proves
//   diffuseColor genuinely multiplies in; tex2 is mid-gray rather than white so that
//   tex1(1)*2*tex2(0.5)~=1 -- with BOTH textures white the real formula's tex1*2 term would itself
//   saturate to 2.0 and clamp to full brightness on write, masking diffuseColor's own value).
// Check H -- tex1=yellow, tex2=cyan, diffuse=white  -> green (both textures genuinely multiply
//   simultaneously: (1,1,0)*(0,1,1)=(0,1,0) -- the decisive proof neither slot is ignored).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const VertexPositionTexture kQuad[6] = {
        {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 1.0f)},
        {Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f)},
        {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
        {Vector3(-1.0f, 1.0f, 0.0f), Vector2(0.0f, 1.0f)},
        {Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f)},
        {Vector3(1.0f, 1.0f, 0.0f), Vector2(1.0f, 1.0f)},
    };
}

class OpenGL4AlphaTestDualTextureTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    Color ReadCentre(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color white(255, 255, 255, 255);
        Texture2D whiteTex(dev, 1, 1);
        whiteTex.SetData(&white, 1);

        // --- AlphaTestEffect ---
        {
            auto drawWithFunc = [&](CompareFunction func) {
                dev.Clear(Color::Black);
                AlphaTestEffect fx(dev);
                fx.setTextureProperty(&whiteTex);
                fx.setAlphaProperty(128.0f / 255.0f);
                fx.setReferenceAlphaProperty(128);
                fx.setAlphaFunctionProperty(func);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, kQuad, 0, 2);
                return ReadCentre(dev);
            };

            const Color always = drawWithFunc(CompareFunction::Always);
            Check(always.getRProperty() > 50, "Check A: AlphaTestEffect CompareFunction::Always draws the pixel");

            const Color never = drawWithFunc(CompareFunction::Never);
            Check(never.getRProperty() <= 10, "Check B: AlphaTestEffect CompareFunction::Never discards the pixel");

            const Color lessEqual = drawWithFunc(CompareFunction::LessEqual);
            Check(lessEqual.getRProperty() > 50, "Check C: AlphaTestEffect CompareFunction::LessEqual (128<=128) draws the pixel");

            const Color equal = drawWithFunc(CompareFunction::Equal);
            Check(equal.getRProperty() > 50, "Check D: AlphaTestEffect CompareFunction::Equal (128==128) draws the pixel");
        }

        // --- DualTextureEffect ---
        {
            const Color kBlack(0, 0, 0, 255);
            const Color kRed(255, 0, 0, 255);
            const Color kBlue(0, 0, 255, 255);
            const Color kYellow(255, 255, 0, 255);
            const Color kCyan(0, 255, 255, 255);
            const Color kGreenXna(0, 128, 0, 255); // real XNA Color::Green
            const Color kGray(128, 128, 128, 255);

            Texture2D whiteT(dev, 1, 1); whiteT.SetData(&white, 1);
            Texture2D redT(dev, 1, 1); redT.SetData(&kRed, 1);
            Texture2D blueT(dev, 1, 1); blueT.SetData(&kBlue, 1);
            Texture2D yellowT(dev, 1, 1); yellowT.SetData(&kYellow, 1);
            Texture2D cyanT(dev, 1, 1); cyanT.SetData(&kCyan, 1);
            Texture2D grayT(dev, 1, 1); grayT.SetData(&kGray, 1);

            auto drawDual = [&](Texture2D& tex1, Texture2D& tex2, const Color& diffuse) {
                dev.Clear(kBlack);
                DualTextureEffect fx(dev);
                fx.setTextureProperty(&tex1);
                fx.setTexture2Property(&tex2);
                fx.setDiffuseColorProperty(Vector3(diffuse.getRProperty() / 255.0f,
                                                   diffuse.getGProperty() / 255.0f,
                                                   diffuse.getBProperty() / 255.0f));
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, kQuad, 0, 2);
                return ReadCentre(dev);
            };

            const Color got1 = drawDual(whiteT, blueT, Color::White);
            Check(got1.getBProperty() > 200 && got1.getRProperty() < 40,
                  "Check E: DualTextureEffect tex1=white, tex2=blue -> blue (tex2 contributes)");

            const Color got2 = drawDual(redT, whiteT, Color::White);
            Check(got2.getRProperty() > 200 && got2.getGProperty() < 40,
                  "Check F: DualTextureEffect tex1=red, tex2=white -> red (tex1 contributes)");

            const Color got3 = drawDual(whiteT, grayT, kGreenXna);
            Check(got3.getGProperty() > 90 && got3.getGProperty() < 170 &&
                  got3.getRProperty() < 40 && got3.getBProperty() < 40,
                  "Check G: DualTextureEffect diffuse=green multiplies through at its own intensity (tex1*2*tex2~=1)");

            const Color got4 = drawDual(yellowT, cyanT, Color::White);
            Check(got4.getGProperty() > 200 && got4.getRProperty() < 40 && got4.getBProperty() < 40,
                  "Check H: DualTextureEffect yellow*cyan -> green (both textures multiply simultaneously)");
        }
    }

public:
    OpenGL4AlphaTestDualTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4AlphaTestDualTextureTest>();
}
