// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-400: does a FULL-BACKBUFFER SpriteBatch draw, issued before any 3D
// draw in the same frame, break that frame's 3D rendering?
//
// Where the question comes from
// -----------------------------
// `docs/migration-guide.md` lists, among the known issues, that on EasyGL "a full-backbuffer
// SpriteBatch draw before any 3D draw in the same frame breaks that frame's 3D rendering" (Task
// 933) -- investigated, root cause not isolated. `easygl_spritebatch_blendstate_leak_test.cpp`
// works around it by deliberately drawing its sprite in a small corner, and says so. So the case
// is documented, avoided, and -- until this file -- never checked on any other renderer.
//
// Renderer-neutral on purpose: it lives here rather than under one renderer's examples so the same
// source answers the question for every family that registers it. A failure on Vulkan alone is a
// Vulkan defect; a failure everywhere is the upstream finding reproduced, which CLAUDE.md says to
// reproduce rather than fix.
//
// Why two legs and not one
// -----------------------
// Leg A draws the sprite in a corner, leg B draws it over the whole backbuffer. Everything else is
// identical. Without leg A, a failure in B says only "SpriteBatch before 3D is broken"; with it,
// a B-only failure isolates FULL COVERAGE as the trigger, which is the claim under test. And if
// both fail, the finding is broader than the one being checked -- also worth knowing.
//
// Sequence, per leg:
//   1. Clear to green.
//   2. SpriteBatch.Begin(Deferred, Opaque) -> draw a blue sprite (corner or full) -> End().
//   3. Without touching any state, draw a full-viewport RED 3D quad (VertexColorEnabled
//      BasicEffect, CullNone, Opaque).
//   4. Read the centre pixel. The 3D quad is last, opaque and covers the centre, so the centre
//      must be RED. Blue means the sprite survived on top of it; green means neither drew.
//
// Exit code 0 = both legs PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {

class FullscreenSpriteThen3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    Texture2D                              blue_;
    int  leg_      = 0;
    int  failures_ = 0;

    void check(bool ok, const std::string& what)
    {
        std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
        if (!ok) ++failures_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        const std::vector<std::uint8_t> px = { 0, 0, 255, 255 };
        blue_ = Texture2D::CreateFromPixels(dev, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        if (leg_ > 1) { Exit(); return; }
        const bool fullscreen = (leg_ == 1);

        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Step 2: the sprite, corner or full-backbuffer -- the only difference between the legs.
        const Rectangle dest = fullscreen ? Rectangle(0, 0, W, H)
                                          : Rectangle(0, 0, W / 8, H / 8);
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        sb_->Draw(blue_, dest, Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        // Step 3: a full-viewport red 3D quad, no state touched in between.
        static const Color kRed(255, 0, 0, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kRed },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        // Step 4.
        const Rectangle reg(W / 2, H / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);

        const bool red = got.getRProperty() >= 200 && got.getGProperty() <= 60 &&
                         got.getBProperty() <= 60;
        const std::string seen = "(" + std::to_string(got.getRProperty()) + "," +
                                 std::to_string(got.getGProperty()) + "," +
                                 std::to_string(got.getBProperty()) + ")";
        check(red,
              std::string(fullscreen ? "B full-backbuffer" : "A corner") +
                  " sprite then a 3D draw: centre=" + seen +
                  ", expected red" +
                  (red ? ""
                       : (got.getBProperty() > 150
                              ? " -- BLUE means the sprite survived on top of the 3D draw"
                              : " -- GREEN means neither the sprite nor the 3D draw reached it")));

        ++leg_;
    }

public:
    FullscreenSpriteThen3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return failures_ == 0 ? 0 : 1; }
};

} // namespace

int main()
{
    FullscreenSpriteThen3DTest game;
    game.Run();
    return game.getResult();
}
