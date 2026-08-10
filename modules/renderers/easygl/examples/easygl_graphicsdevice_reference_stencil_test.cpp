// SPDX-License-Identifier: MS-PL
// Task 319: verify GraphicsDevice.ReferenceStencil is used by all renderers, independent of the
// currently-assigned DepthStencilState's own ReferenceStencil field.
//
// IMPORTANT: PresentationParameters.DepthStencilFormat defaults to DepthFormat::Depth24 (no
// stencil aspect) -- this test's constructor explicitly requests DepthFormat::Depth24Stencil8 via
// GraphicsDeviceManager, same as Tasks 315-318's tests. Do not remove this. Also remember
// GraphicsDevice::Clear ignores ClearOptions::Stencil entirely (Task 871) -- the baseline is
// established via a real "stamp" draw, never via Clear().
//
// FNA's GraphicsDevice.ReferenceStencil is a real, independent device property
// (FNA3D_Get/SetReferenceStencil), analogous to GraphicsDevice.BlendFactor (Task 309) -- it can be
// changed WITHOUT reassigning the whole DepthStencilState, and that change should immediately
// affect subsequent stencil compares.
//
// Method: stamp stencil=0x05, then assign a DepthStencilState with StencilFunction=Equal and a
// baked-in ReferenceStencil=0x05 (so, taken at face value, a compare would PASS). Then call
// GraphicsDevice.setReferenceStencilProperty(0x99) DIRECTLY -- NOT via a new DepthStencilState --
// which should override the ACTIVE reference used by the next draw's stencil compare. Draw a GREEN
// quad using the SAME (unchanged) DepthStencilState object: if the override genuinely took effect,
// the compare becomes 0x99 vs buffer 0x05 (Equal, false) -> REJECTED -> stays BACKGROUND. If
// setReferenceStencilProperty has no real effect (does not reach any renderer), the compare still
// uses the state's own baked-in 0x05 vs buffer 0x05 -> PASSES -> incorrectly shows GREEN.
//
// NOTE: this project's GraphicsDevice::setReferenceStencilProperty is confirmed (via code reading)
// to be a pure local no-op with ZERO renderer connection on ALL THREE renderers -- there is no
// SetReferenceStencil method anywhere in IGraphicsRenderer at all, so there is no possible code path
// for this override to ever take effect on ANY renderer currently, not just Vulkan (a different,
// broader gap than Task 870, tracked separately as Task 872). Expect this test to FAIL on every
// renderer that can run it (EasyGL here) -- this is confirming a real, universal, not-yet-fixed
// bug, not a regression in this test.
//
// Exit code 0 = PASS (correct override behavior), 1 = FAIL (confirms Task 872).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);

    void DrawQuad(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class GraphicsDeviceReferenceStencilTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kBackground, 1.0f, 0);
        dev.setBlendStateProperty(BlendState::Opaque);

        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Stamp stencil=0x05.
        DepthStencilState stamp;
        stamp.setDepthBufferEnableProperty(false);
        stamp.setStencilEnableProperty(true);
        stamp.setStencilFunctionProperty(CompareFunction::Always);
        stamp.setStencilPassProperty(StencilOperation::Replace);
        stamp.setReferenceStencilProperty(0x05);
        dev.setDepthStencilStateProperty(stamp);
        DrawQuad(dev, kBackground);

        // Assign a compare state with a baked-in ReferenceStencil=0x05 (would PASS at face value).
        DepthStencilState compare;
        compare.setDepthBufferEnableProperty(false);
        compare.setStencilEnableProperty(true);
        compare.setStencilFunctionProperty(CompareFunction::Equal);
        compare.setReferenceStencilProperty(0x05);
        compare.setStencilPassProperty(StencilOperation::Keep);
        compare.setStencilFailProperty(StencilOperation::Keep);
        dev.setDepthStencilStateProperty(compare);

        // Override the ACTIVE reference directly -- should make the next draw's compare use 0x99,
        // not the state object's own 0x05.
        dev.setReferenceStencilProperty(0x99);

        DrawQuad(dev, kGreen);

        Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);

        const bool isGreen = c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60;
        const bool ok = !isGreen;

        std::printf("[%s] centre=(%d,%d,%d), expected BACKGROUND (override reference 0x99 must reject)\n",
                    ok ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty());
        if (!ok)
        {
            std::printf("[INFO] GraphicsDevice.setReferenceStencilProperty() had no effect -- the\n"
                        "       compare still used the state's own baked-in ReferenceStencil.\n");
        }

        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    GraphicsDeviceReferenceStencilTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    GraphicsDeviceReferenceStencilTest game;
    game.Run();
    return game.getResult();
}
