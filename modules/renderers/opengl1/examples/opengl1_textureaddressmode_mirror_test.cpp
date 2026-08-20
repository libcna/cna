// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: TextureAddressMode.Mirror (plans/plan_opengl1.md item 16, EasyGL parity).
//
// Before this, WrapMode()'s only two outcomes were GL_REPEAT (Wrap=0) and GL_CLAMP_TO_EDGE
// (everything else, including Mirror=2) -- a game requesting Mirror silently got Clamp instead,
// at both the 3D sampling call site and SpriteBatch::Draw's own.
//
// Method: a 2x1 texture (texel 0 = red, texel 1 = blue) sampled across U=0..2 (two repeats) with
// AddressU=Mirror and TextureFilter.Point (crisp texel reads, no blending across the boundary).
// The three wrap modes are only distinguishable in the SECOND repeat (U in [1,2)):
//   Wrap    (plain repeat):        U=1.25 -> red,  U=1.75 -> blue   (identical to the first repeat)
//   Clamp   (the pre-existing bug -- U>=1 always samples the last texel):
//                                  U=1.25 -> blue, U=1.75 -> blue
//   Mirror  (correct):             U=1.25 -> blue, U=1.75 -> red    (second repeat flipped)
// U=1.25 alone cannot tell Mirror apart from the pre-existing Clamp bug (both read blue there --
// pure coincidence of this particular texel layout), so U=1.75 is the primary discriminator;
// U=1.25 additionally rules out a hypothetical "silently still Wrap" regression.
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport = 64; // U(x) = x/32 -> x=0..64 spans U=0..2.
}

class OpenGL1TextureAddressModeMirrorTest : public Game
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

    static bool IsRed(const Color& c)  { return c.getRProperty() >= 200 && c.getBProperty() <= 20; }
    static bool IsBlue(const Color& c) { return c.getBProperty() >= 200 && c.getRProperty() <= 20; }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.Clear(Color::Black);

        Texture2D tex(dev, 2, 1, /*mipMap=*/false, SurfaceFormat::Color);
        Color texels[2] = { Color(255, 0, 0, 255), Color(0, 0, 255, 255) };
        tex.SetData(texels, 2);

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f);

        const VertexPositionTexture q[6] = {
            { Vector3(0.0f, 0.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(0.0f, (float)kViewport, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), Vector2(2.0f, 1.0f) },
            { Vector3(0.0f, 0.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), Vector2(2.0f, 1.0f) },
            { Vector3((float)kViewport, 0.0f, 0.0f), Vector2(2.0f, 0.0f) },
        };

        dev.getSamplerStatesProperty()[0].setFilterProperty(TextureFilter::Point);
        dev.getSamplerStatesProperty()[0].setAddressUProperty(TextureAddressMode::Mirror);
        dev.getSamplerStatesProperty()[0].setAddressVProperty(TextureAddressMode::Clamp);

        BasicEffect fx(dev);
        fx.setWorldProperty(world);
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(1.0f);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        auto readAt = [&](float u) -> Color
        {
            const int x = (int)(u * 32.0f);
            const Rectangle reg(x, kViewport / 2, 1, 1);
            Color px(0, 0, 0, 0);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            return px;
        };

        const Color p025 = readAt(0.25f), p075 = readAt(0.75f), p125 = readAt(1.25f), p175 = readAt(1.75f);
        std::printf("U=0.25 (%d,%d,%d)  U=0.75 (%d,%d,%d)  U=1.25 (%d,%d,%d)  U=1.75 (%d,%d,%d)\n",
                    p025.getRProperty(), p025.getGProperty(), p025.getBProperty(),
                    p075.getRProperty(), p075.getGProperty(), p075.getBProperty(),
                    p125.getRProperty(), p125.getGProperty(), p125.getBProperty(),
                    p175.getRProperty(), p175.getGProperty(), p175.getBProperty());

        Check(IsRed(p025), "U=0.25 (first repeat): red -- sanity, base texel layout is correct");
        Check(IsBlue(p075), "U=0.75 (first repeat): blue -- sanity, base texel layout is correct");
        Check(IsBlue(p125), "U=1.25 (second repeat): blue -- matches Mirror (also matches the "
              "pre-existing Clamp bug by coincidence; U=1.75 below is the real discriminator)");
        Check(IsRed(p175), "U=1.75 (second repeat): red -- genuine Mirror flip, NOT the "
              "pre-existing Clamp-fallback blue and NOT plain Wrap's blue either");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1TextureAddressModeMirrorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1TextureAddressModeMirrorTest game;
    game.Run();
    return game.getResult();
}
