// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: separate alpha blend factors (plans/plan_opengl1.md item 19, EasyGL parity).
//
// Before this, ApplyBlendState() reused the color factors for alpha too -- a game with different
// BlendState.ColorSourceBlend/ColorDestinationBlend vs AlphaSourceBlend/AlphaDestinationBlend
// silently got the color factors applied to the alpha channel as well.
//
// Uses a RenderTarget2D (not the window backbuffer) so the alpha channel can be read back for
// real -- RenderTarget2D::GetData() reads the FBO's own color attachment directly via glReadPixels
// (OpenGL1RenderTargetRenderer::GetData()), a genuine GPU readback including alpha, not a CPU
// shadow. Also uses the 3D draw path (BasicEffect + DrawUserPrimitives), not SpriteBatch -- see
// opengl1_blendfactor_test.cpp's own header for why.
//
// Method: clear the RT to destination alpha=100, then draw a full-quad source with alpha=200
// using ColorSourceBlend=One/ColorDestinationBlend=Zero (color channel: source passes through
// unmodified, irrelevant to this test) but AlphaSourceBlend=Zero/AlphaDestinationBlend=One
// (alpha channel: destination's alpha is explicitly PRESERVED, source alpha discarded). Correct
// result: alpha=100 (dest, unchanged). The pre-existing bug (alpha reusing color's One/Zero
// factors) would instead give alpha=200 (source, following color's own formula by mistake).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 4;
}

class OpenGL1BlendAlphaFactorTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        RenderTarget2D rt(dev, kRTSize, kRTSize, /*mipMap=*/false, SurfaceFormat::Color, DepthFormat::None);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(0, 0, 0, 100));

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kRTSize, (float)kRTSize, 0.0f, -1.0f, 1.0f);
        const Color srcColor(255, 255, 255, 200);
        const VertexPositionColor q[6] = {
            { Vector3(0.0f, 0.0f, 0.0f), srcColor }, { Vector3(0.0f, (float)kRTSize, 0.0f), srcColor },
            { Vector3((float)kRTSize, (float)kRTSize, 0.0f), srcColor }, { Vector3(0.0f, 0.0f, 0.0f), srcColor },
            { Vector3((float)kRTSize, (float)kRTSize, 0.0f), srcColor }, { Vector3((float)kRTSize, 0.0f, 0.0f), srcColor },
        };

        BlendState bs;
        bs.setColorSourceBlendProperty(Blend::One);
        bs.setColorDestinationBlendProperty(Blend::Zero);
        bs.setColorBlendFunctionProperty(BlendFunction::Add);
        bs.setAlphaSourceBlendProperty(Blend::Zero);
        bs.setAlphaDestinationBlendProperty(Blend::One);
        bs.setAlphaBlendFunctionProperty(BlendFunction::Add);
        dev.setBlendStateProperty(bs);

        BasicEffect fx(dev);
        fx.setWorldProperty(world);
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> pixels(kRTSize * kRTSize, Color(0, 0, 0, 0));
        rt.GetData(pixels.data(), kRTSize * kRTSize);
        const Color centre = pixels[(kRTSize / 2) * kRTSize + (kRTSize / 2)];
        std::printf("centre alpha=%d (color=%d,%d,%d)\n", centre.getAProperty(),
                    centre.getRProperty(), centre.getGProperty(), centre.getBProperty());

        Check(centre.getRProperty() >= 250,
              "color channel unaffected -- ColorSourceBlend=One/Dest=Zero passes source through "
              "(sanity: the color-vs-alpha factor split is genuinely different, not both defaulted)");
        Check(centre.getAProperty() >= 90 && centre.getAProperty() <= 110,
              "alpha = dest(100), NOT src(200) -- alpha genuinely used its OWN "
              "AlphaSourceBlend=Zero/AlphaDestinationBlend=One factors, not color's One/Zero");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1BlendAlphaFactorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1BlendAlphaFactorTest game;
    game.Run();
    return game.getResult();
}
