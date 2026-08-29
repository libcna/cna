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
// HISTORY: this file used to carry a note saying setReferenceStencilProperty was a pure local
// no-op, that IGraphicsRenderer had no SetReferenceStencil at all, and that the test should be
// expected to fail everywhere. All three stopped being true. Task 870/319 added
// IGraphicsRenderer::SetReferenceStencil (a defaulted no-op on the interface, so a renderer that
// never implements it fails this test rather than failing to build), GraphicsDevice forwards to it,
// and 26 renderers implement it. EasyGL was the one that did not, which is what this test was
// still reporting -- REMED-GFX-236. A note that says "expect this to fail" outlives the reason and
// turns a real signal into background noise, so it is replaced rather than amended.
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

    /// The same quad wound the other way, so it reaches GL's OTHER stencil face.
    ///
    /// Under TwoSidedStencilMode the reference is bound per face (`glStencilFuncSeparate`), and the
    /// quad above turns out to rasterize as the FRONT face -- measured, by removing each face's
    /// reissue in turn and watching which one this test noticed. Without a second winding the back
    /// face is never compared and half the two-sided path is untested.
    void DrawQuadReversedWinding(GraphicsDevice& dev, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f, -1.0f, 0.5f), color },
            { Vector3(-1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f,  1.0f, 0.5f), color },
            { Vector3( 1.0f, -1.0f, 0.5f), color },
        };
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

        // REMED-GFX-236 leg B: the same override with TwoSidedStencilMode on. GL binds the
        // reference PER FACE (glStencilFuncSeparate), so reissuing only one face would leave the
        // other comparing against the state's own value. Both windings are drawn because one quad
        // reaches one face only -- verified by removing each face's reissue in turn: with only one
        // winding, dropping the back face went unnoticed.
        DepthStencilState twoSided;
        twoSided.setDepthBufferEnableProperty(false);
        twoSided.setStencilEnableProperty(true);
        twoSided.setTwoSidedStencilModeProperty(true);
        twoSided.setStencilFunctionProperty(CompareFunction::Equal);
        twoSided.setCounterClockwiseStencilFunctionProperty(CompareFunction::Equal);
        twoSided.setReferenceStencilProperty(0x05);
        twoSided.setStencilPassProperty(StencilOperation::Keep);
        twoSided.setStencilFailProperty(StencilOperation::Keep);
        twoSided.setCounterClockwiseStencilPassProperty(StencilOperation::Keep);
        twoSided.setCounterClockwiseStencilFailProperty(StencilOperation::Keep);
        dev.setDepthStencilStateProperty(twoSided);
        dev.setReferenceStencilProperty(0x99);

        DrawQuad(dev, kGreen);
        DrawQuadReversedWinding(dev, kGreen);
        Color twoSidedCentre(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &twoSidedCentre, 0, 1);
        const bool twoSidedGreen = twoSidedCentre.getGProperty() >= 200
            && twoSidedCentre.getRProperty() <= 60 && twoSidedCentre.getBProperty() <= 60;
        const bool twoSidedOk = !twoSidedGreen;
        std::printf("[%s] two-sided centre=(%d,%d,%d), expected BACKGROUND (the override must reach "
                    "both faces)\n",
                    twoSidedOk ? "PASS" : "FAIL", twoSidedCentre.getRProperty(),
                    twoSidedCentre.getGProperty(), twoSidedCentre.getBProperty());

        result_ = (ok && twoSidedOk) ? 0 : 1;
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
