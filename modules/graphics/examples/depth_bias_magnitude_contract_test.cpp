// SPDX-License-Identifier: MS-PL
// plans/plan_apple_m4.md AM4-134: RasterizerState.DepthBias is an offset in normalized depth, and
// its magnitude -- not only its sign -- has to survive the renderer's conversion to native units.
//
// The coplanar fixture (easygl_depth_bias_test.cpp) cannot see a wrong scale on Apple GPUs: there
// ANY non-zero negative bias, even 1e-12, wins an exact-equal LESS comparison. Here the second
// triangle is drawn 1e-4 behind the first instead (1e-4 in clip z, which is 1e-4 of depth under
// XNA's [0,1] mapping and 5e-5 under OpenGL's [-1,1] one), so only a bias of the right size moves
// it in front. Metal handed XNA's value to setDepthBias: unscaled and stayed red at every size.
//
//   bias -3e-5  -> RED    (smaller than the gap on either depth mapping)
//   bias -3e-4  -> GREEN  (larger than the gap on either depth mapping)
//   bias +3e-4  -> RED    (B drawn 1e-4 IN FRONT, pushed behind A)
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DepthBiasMagnitudeContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    // A CW triangle centred at NDC x = cx, flat at clip z.
    static void drawTri(GraphicsDevice& dev, float cx, float z, const Color& col)
    {
        const VertexPositionColor verts[3] = {
            { Vector3(cx, 0.8f, z), col },
            { Vector3(cx + 0.15f, -0.8f, z), col },
            { Vector3(cx - 0.15f, -0.8f, z), col },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 1);
    }

    Color readAt(GraphicsDevice& dev, float ndcX)
    {
        const auto& vp = dev.getViewportProperty();
        const int px = static_cast<int>((ndcX + 1.0f) * 0.5f * vp.getWidthProperty());
        Rectangle region(px, vp.getHeightProperty() / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &c, 0, 1);
        return c;
    }

    static bool isRed(const Color& px)
    {
        return px.getRProperty() >= 200 && px.getGProperty() <= 60 && px.getBProperty() <= 60;
    }
    static bool isGreen(const Color& px)
    {
        return px.getGProperty() >= 200 && px.getRProperty() <= 60 && px.getBProperty() <= 60;
    }

    void check(bool ok, float bias, const Color& px, const char* expected)
    {
        std::printf("[%s] DepthBias=%g: (%d,%d,%d) expected %s\n", ok ? "PASS" : "FAIL", bias,
                    px.getRProperty(), px.getGProperty(), px.getBProperty(), expected);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;

        DepthStencilState less;
        less.setDepthBufferFunctionProperty(CompareFunction::Less);
        dev.setDepthStencilStateProperty(less);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color(0, 0, 0, 255));
        fx.Apply();

        struct Leg { float cx; float bias; float bOffset; bool expectGreen; };
        const Leg legs[] = {
            { -0.6f, -3.0e-5f, +1.0e-4f, false },
            {  0.0f, -3.0e-4f, +1.0e-4f, true  },
            {  0.6f, +3.0e-4f, -1.0e-4f, false },
        };
        for (const Leg& leg : legs)
        {
            dev.setRasterizerStateProperty(RasterizerState());
            drawTri(dev, leg.cx, 0.5f, Color(255, 0, 0, 255));
            RasterizerState biased;
            biased.setDepthBiasProperty(leg.bias);
            dev.setRasterizerStateProperty(biased);
            drawTri(dev, leg.cx, 0.5f + leg.bOffset, Color(0, 255, 0, 255));
        }
        dev.setRasterizerStateProperty(RasterizerState());
        for (const Leg& leg : legs)
        {
            const Color px = readAt(dev, leg.cx);
            check(leg.expectGreen ? isGreen(px) : isRed(px), leg.bias, px,
                  leg.expectGreen ? "GREEN" : "RED");
        }

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DepthBiasMagnitudeContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    DepthBiasMagnitudeContractTest game;
    game.Run();
    return game.getResult();
}
