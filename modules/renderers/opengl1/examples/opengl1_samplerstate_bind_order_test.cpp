// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: SamplerState ordering + per-slot bugs found and fixed while implementing
// plans/plan_opengl1.md phase 6's mip-aware filtering (see plans/plan_opengl1.md phase 6's own entry for the
// full writeup).
//
// Bug 1 (ordering): ApplySamplerState() used to issue glTexParameteri() directly, called once per
// draw for every sampler slot BEFORE DrawInternal actually binds the corresponding texture
// (GraphicsDevice::applySamplerStatesToRenderer() fires before DrawPrimitivesEx()). Its writes
// therefore landed on whatever texture happened to be bound at that moment -- which, for a
// texture whose own SetData() was the most recent GL_TEXTURE_2D-affecting call (the common case
// for a freshly-created-and-immediately-drawn texture), is frequently the SAME texture anyway,
// silently masking the bug. XNA's own default SamplerState (AddressU=Wrap) also coincidentally
// matches the fallback (SamplerStateCollection's unused slots keep that same default, and the
// LAST slot processed in GraphicsDevice::applySamplerStatesToRenderer()'s 16-slot loop is what
// ends up actually applied under the bug) -- so a naive "request Wrap, check for Wrap" test
// passes regardless of the bug. This test instead requests the NON-default AddressMode (Clamp),
// which only a genuinely-slot/texture-correct write can produce.
//
// Bug 2 (slot ignored): ApplySamplerState(slot, ...) ignored `slot` entirely -- every call wrote
// to whichever texture unit happened to be active (never switched away from unit 0 during that
// loop), so in a 2-texture DualTextureEffect draw, only the MOST RECENTLY BOUND of the two
// textures (texture B, created/SetData-ed after texture A) receives anything from the sampler
// loop -- and only the LAST-processed slot's value at that (slot 15, unused, default Wrap) -- and
// texture A never receives anything, keeping the plain constructor default (Clamp). A naive
// red/green-texel test that just multiplies the two textures together can't tell "A=Wrap,B=Clamp"
// apart from "A=Clamp,B=Wrap" (both permutations multiply complementary colors to the same black),
// so this test uses grayscale-magnitude texels instead, chosen so the correct pairing and the
// swapped (buggy) pairing produce two clearly different, precomputed brightness values.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport = 32;
}

class OpenGL1SamplerStateBindOrderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    Color ReadCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kViewport / 2, kViewport / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Texture2D MakeStrip(GraphicsDevice& dev, int texel0, int texel1)
    {
        Texture2D tex(dev, 2, 1);
        const Color texels[2] = {
            Color(texel0, texel0, texel0, 255),
            Color(texel1, texel1, texel1, 255),
        };
        tex.SetData(texels, 2);
        return tex;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f);

        // ---- Check 1: ordering bug. texel0=bright(220), texel1=dim(20). Requesting the
        // NON-default AddressMode=Clamp: U=1.25 must clamp to texel1 (dim). A bug that silently
        // falls back to the coincidental Wrap default would instead fold U=1.25 -> 0.25 -> texel0
        // (bright) -- a large, unambiguous brightness gap either way.
        {
            Texture2D tex = MakeStrip(dev, 220, 20);
            dev.getSamplerStatesProperty()[0].setFilterProperty(TextureFilter::Point);
            dev.getSamplerStatesProperty()[0].setAddressUProperty(TextureAddressMode::Clamp);

            const VertexPositionTexture q[6] = {
                { Vector3(0.0f, 0.0f, 0.0f), Vector2(1.25f, 0.5f) },
                { Vector3(0.0f, (float)kViewport, 0.0f), Vector2(1.25f, 0.5f) },
                { Vector3((float)kViewport, (float)kViewport, 0.0f), Vector2(1.25f, 0.5f) },
                { Vector3(0.0f, 0.0f, 0.0f), Vector2(1.25f, 0.5f) },
                { Vector3((float)kViewport, (float)kViewport, 0.0f), Vector2(1.25f, 0.5f) },
                { Vector3((float)kViewport, 0.0f, 0.0f), Vector2(1.25f, 0.5f) },
            };

            dev.Clear(Color(64, 64, 64, 255));
            BasicEffect fx(dev);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setAlphaProperty(1.0f);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            Color got = ReadCenter(dev);
            std::printf("ordering check got=%d (expect dim ~20, not bright ~220)\n", got.getRProperty());
            Check(got.getRProperty() < 60,
                  "ordering fix: first-ever draw honors the requested (non-default) AddressMode=Clamp");
        }

        // ---- Check 2: slot-ignored bug. texA texel0=100 (Wrap target, "correct"), texel1=10.
        // texB texel0=10, texel1=100 (Clamp target, "correct"). DualTextureEffect combines as
        // clamp(texA.rgb*2) * texB.rgb (0..1 fixed-point per stage).
        // Correct (A=Wrap->100, B=Clamp->100): clamp(100/255*2)*100/255*255 ~= 78.
        // Swapped/buggy (A keeps its own default Clamp->10, B ends up with the last-processed-
        // slot's default Wrap->10): clamp(10/255*2)*10/255*255 ~= 1.
        // The two predicted outcomes (~78 vs ~1) are far enough apart to be an unambiguous,
        // rounding-tolerant discriminator.
        {
            Texture2D texA = MakeStrip(dev, 100, 10); // slot 0 -- Wrap should select texel0=100
            Texture2D texB = MakeStrip(dev, 10, 100); // slot 1 -- Clamp should select texel1=100
            dev.getSamplerStatesProperty()[0].setFilterProperty(TextureFilter::Point);
            dev.getSamplerStatesProperty()[0].setAddressUProperty(TextureAddressMode::Wrap);
            dev.getSamplerStatesProperty()[1].setFilterProperty(TextureFilter::Point);
            dev.getSamplerStatesProperty()[1].setAddressUProperty(TextureAddressMode::Clamp);

            const Color white(255, 255, 255, 255);
            const VertexPositionColorTexture q[6] = {
                { Vector3(0.0f, 0.0f, 0.0f), white, Vector2(1.25f, 0.5f) },
                { Vector3(0.0f, (float)kViewport, 0.0f), white, Vector2(1.25f, 0.5f) },
                { Vector3((float)kViewport, (float)kViewport, 0.0f), white, Vector2(1.25f, 0.5f) },
                { Vector3(0.0f, 0.0f, 0.0f), white, Vector2(1.25f, 0.5f) },
                { Vector3((float)kViewport, (float)kViewport, 0.0f), white, Vector2(1.25f, 0.5f) },
                { Vector3((float)kViewport, 0.0f, 0.0f), white, Vector2(1.25f, 0.5f) },
            };

            dev.Clear(Color(64, 64, 64, 255));
            DualTextureEffect fx(dev);
            fx.setWorldProperty(world); fx.setViewProperty(view); fx.setProjectionProperty(proj);
            fx.setTextureProperty(&texA);
            fx.setTexture2Property(&texB);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

            Color got = ReadCenter(dev);
            std::printf("slot check got=%d (expect ~78 correct, ~1 if buggy)\n", got.getRProperty());
            Check(got.getRProperty() > 40,
                  "slot fix: texA (slot 0, Wrap) and texB (slot 1, Clamp) each honor their OWN sampler slot");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1SamplerStateBindOrderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1SamplerStateBindOrderTest game;
    game.Run();
    return game.getResult();
}
