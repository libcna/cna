// SPDX-License-Identifier: MS-PL
// Task 955: verify that a freshly-constructed GraphicsDevice's own DEFAULT state -- with NO game
// code ever explicitly calling setDepthStencilStateProperty()/setBlendStateProperty() itself,
// exactly like ../cna-samples/samples/SimpleAnimation/src/Tank.hpp's own Draw() (mirroring real
// XNA's Tank.cs) -- actually produces correct opaque depth occlusion.
//
// Root cause this guards against: GraphicsDevice's constructor only ever pushed its
// RasterizerState default to the backend (Task 896). BlendState/DepthStencilState were left as
// C++-level fields only, never applied via ApplyBlendState()/ApplyDepthStencilState() -- so each
// backend's OWN internal hardcoded default silently won instead of XNA's real
// DepthStencilState.Default (depth test+write ON, LessEqual). On EasyGL this meant depth testing
// was plain OpenGL's raw default: DISABLED -- any two overlapping opaque draws simply painted in
// draw order regardless of Z, which is exactly the "background parts visible through foreground
// parts" symptom reported against SimpleAnimation. Real FNA's own GraphicsDevice constructor
// (GraphicsDevice.cs) sets all 3 of BlendState/DepthStencilState/RasterizerState unconditionally;
// Task 896 ported only the RasterizerState line. Task 955 ports the other two.
//
// Method (two-quad, draw-order-defies-depth-order test -- deliberately NOT a same-order draw,
// which a broken/disabled depth test would pass by accident): draw a NEAR quad (green, z=0.2)
// FIRST, then a FAR quad (red, z=0.8) fully overlapping it SECOND. Neither draw call -- nor
// anything else in this file -- ever calls setDepthStencilStateProperty()/setBlendStateProperty()/
// setRasterizerStateProperty(). Only BasicEffect.Apply() runs, exactly matching Tank.hpp's own
// draw loop. Correct default behaviour (depth test+write genuinely ON) rejects the far quad's
// later z=0.8 fragment against the near quad's already-written z=0.2 -- pixel stays GREEN. If the
// default depth state is not actually reaching the backend (the bug), the far quad is drawn with
// no depth test at all and simply overpaints the near quad in draw order -- pixel goes RED.
//
// Check A (the discriminating check): near green drawn first, far red drawn second (reversed vs.
//   depth order) -- expect GREEN. This is the one that actually catches the bug.
// Check B (sanity/positive control): far red drawn first, near green drawn second (draw order
//   matches depth order) -- expect GREEN regardless of depth-test correctness. Proves the
//   quad/colour/pixel-readback pipeline itself works, so Check A's result is really about depth
//   state and not some unrelated rendering failure.
//
// Each check is its own real frame/Draw() call with its own Clear()+2 draws+1 GetBackBufferData()
// read, matching graphicsdevice_clear_depth_test.cpp's own established multi-frame-state-machine
// shape -- required for Bgfx, whose GetBackBufferData() only reliably reflects the FIRST read of
// a given rendered frame (Task 406).
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
    const Color kBackground(20, 20, 20, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kRed(255, 0, 0, 255);

    // Winding is front-facing under CNA's real default RasterizerState (CullCounterClockwise) --
    // deliberately so this test never needs to touch RasterizerState either, unlike every other
    // quad-drawing test in this project (which all explicitly set CullNone).
    void DrawQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    void ApplyBasicEffect(GraphicsDevice& dev)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = true;
        fx->Apply();
    }

    // Returns the sampled centre-pixel colour after clearing and drawing the near/far quads in
    // the given order. NEVER touches BlendState/DepthStencilState/RasterizerState -- the entire
    // point is to exercise GraphicsDevice's own untouched defaults, exactly like Tank.hpp's Draw().
    Color ClearAndDrawOverlappingQuads(GraphicsDevice& dev, bool nearFirst)
    {
        // Single-Color overload, matching SimpleAnimationGame::Draw()'s own
        // device.Clear(Color(...)) call exactly (defaults to Target|DepthBuffer).
        dev.Clear(kBackground);
        ApplyBasicEffect(dev);

        if (nearFirst)
        {
            DrawQuad(dev, 0.2f, kGreen); // near
            DrawQuad(dev, 0.8f, kRed);   // far
        }
        else
        {
            DrawQuad(dev, 0.8f, kRed);   // far
            DrawQuad(dev, 0.2f, kGreen); // near
        }

        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color c(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &c, 0, 1);
        return c;
    }

    bool IsGreen(const Color& c) { return c.getGProperty() >= 200 && c.getRProperty() <= 60 && c.getBProperty() <= 60; }
}

class GraphicsDeviceDefaultStateOcclusionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  step_      = 0;
    int  passCount_ = 0;
    bool done_      = false;
    int  result_    = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;

        auto& dev = getGraphicsDeviceProperty();

        switch (step_)
        {
        case 0:
        {
            // Check A -- the discriminating check: near drawn first, far drawn second.
            const Color c = ClearAndDrawOverlappingQuads(dev, /*nearFirst=*/true);
            const bool ok = IsGreen(c);
            std::printf("[%s] near-first (green z=0.2 then red z=0.8), default state only: "
                        "centre=(%d,%d,%d), expected GREEN\n",
                        ok ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty());
            if (!ok)
            {
                std::printf("[INFO] RED here means the far quad overpainted the near quad in draw\n"
                            "       order despite being farther away -- GraphicsDevice's default\n"
                            "       DepthStencilState is not actually reaching the backend.\n");
            }
            if (ok) ++passCount_;
            break;
        }
        case 1:
        {
            // Check B -- sanity/positive control: far drawn first, near drawn second (draw order
            // already matches depth order, so this passes even with depth testing fully broken --
            // proves the quad/colour/readback pipeline itself is sound).
            const Color c = ClearAndDrawOverlappingQuads(dev, /*nearFirst=*/false);
            const bool ok = IsGreen(c);
            std::printf("[%s] far-first (red z=0.8 then green z=0.2), sanity check: "
                        "centre=(%d,%d,%d), expected GREEN\n",
                        ok ? "PASS" : "FAIL", c.getRProperty(), c.getGProperty(), c.getBProperty());
            if (ok) ++passCount_;

            std::printf("=== %d/2 PASS ===\n", passCount_);
            result_ = (passCount_ == 2) ? 0 : 1;
            done_ = true;
            Exit();
            break;
        }
        default:
            break;
        }
        ++step_;
    }

public:
    GraphicsDeviceDefaultStateOcclusionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        // Deliberately NOT overriding PreferredDepthStencilFormat -- GraphicsDeviceManager's own
        // default (DepthFormat::Depth24) is exactly what SimpleAnimation itself gets, and is part
        // of what this test is verifying end-to-end.
    }

    int getResult() const { return result_; }
};

int main()
{
    GraphicsDeviceDefaultStateOcclusionTest game;
    game.Run();
    return game.getResult();
}
