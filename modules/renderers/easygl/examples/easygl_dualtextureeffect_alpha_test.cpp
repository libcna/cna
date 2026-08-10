// SPDX-License-Identifier: MS-PL
// Task 385: verify DualTextureEffect's Alpha property produces correctly
// alpha-premultiplied output, matching FNA's OnApply() formula:
//   diffuseColorParam.SetValue(new Vector4(diffuseColor * alpha, alpha));
//
// The shader multiplies textures into this already-premultiplied
// diffuseColor, so the final fragment RGB is inherently premultiplied by
// Alpha -- this is why FNA's stock effects pair with BlendState.AlphaBlend
// (ColorSourceBlend=One, ColorDestinationBlend=InverseSourceAlpha) rather
// than a traditional non-premultiplied SourceAlpha/InverseSourceAlpha blend.
//
// tex0=white, tex1=gray(~0.5) -- the gray overlay exactly cancels FNA's
// `color.rgb *= 2` doubling factor (Task 383) so this test isolates Alpha's
// effect cleanly, independent of that already-verified factor:
//   1(tex0) * 2(doubling) * 0.5(tex1 gray) = 1.0 -- a no-op multiplier.
//
// DiffuseColor=red(1,0,0), Alpha=0.5, drawn with BlendState.AlphaBlend over
// an opaque blue background:
//   correct (premultiplied):   src=(0.5,0,0,0.5) -> blend -> (0.5,0,0.5) = (128,0,128)
//   buggy (not premultiplied): src=(1,0,0,0.5)   -> blend -> (1,0,0.5)   = (255,0,128)
// The R channel's 127-unit separation gives this test real discriminating
// power for whether premultiplication actually happens end-to-end on real
// hardware, not just in the FillGpuDrawParams() formula (already directly
// unit-tested in DualTextureEffectTests.cpp).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    bool colourMatch(Color got, Color want, int tol = 20)
    {
        return closeTo(got.getRProperty(), want.getRProperty(), tol)
            && closeTo(got.getGProperty(), want.getGProperty(), tol)
            && closeTo(got.getBProperty(), want.getBProperty(), tol);
    }
}

class DualTextureAlphaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            result_ = 1;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);

        const Color kBlue(0, 0, 255, 255);
        const Color kWhite(255, 255, 255, 255);
        const Color kGrayHalf(128, 128, 128, 255);

        Texture2D texWhite(dev, 1, 1); texWhite.SetData(&kWhite, 1);
        Texture2D texGray(dev, 1, 1);  texGray.SetData(&kGrayHalf, 1);

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        dev.Clear(kBlue);
        dev.setBlendStateProperty(BlendState::AlphaBlend);

        DualTextureEffect fx(dev);
        fx.setTextureProperty(&texWhite);
        fx.setTexture2Property(&texGray);
        fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.setAlphaProperty(0.5f);
        fx.Apply();
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's winding
        // is culled by the real default RasterizerState once EasyGL pushes it at construction.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        Color got = readCenter(dev);
        check(colourMatch(got, Color(128, 0, 128, 255)),
              "Alpha=0.5 red over blue, AlphaBlend → premultiplied (128,0,128)",
              got, Color(128, 0, 128, 255));

        Exit();
    }

public:
    DualTextureAlphaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DualTextureAlphaTest game;
    game.Run();
    return game.getResult();
}
