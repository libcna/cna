// SPDX-License-Identifier: MS-PL
// Task 122: EasyGL integration test — AlphaTestEffect alpha cutout + pixel readback.
//
// Renders a full-screen quad split into a left half (opaque red pixel) and a
// right half (transparent pixel) using AlphaTestEffect with
// CompareFunction::Greater / ReferenceAlpha=128.
// Background is cleared to green.
//
// Expected results after alpha discard:
//   - Left pixel (alpha=255 > 128)  → red   (texture colour passes the test)
//   - Right pixel (alpha=0 ≤ 128)   → green (fragment discarded, background shows)
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class AlphaTestIntegrationTest : public Game
{
    Texture2D tex_;
    bool      done_   = false;
    int       result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // 2×1 texture:
        //   pixel 0 (left)  = red, alpha=255  → passes CompareFunction::Greater(128)
        //   pixel 1 (right) = blue, alpha=0   → fails  CompareFunction::Greater(128) → discard
        const std::vector<uint8_t> px = {
            255, 0,   0, 255,   // pixel 0: red, opaque
              0, 0, 255,   0,   // pixel 1: blue, transparent
        };
        tex_ = Texture2D::CreateFromPixels(device, 2, 1, px);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        // Background = green.  Discarded pixels will leave this colour.
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // Set up AlphaTestEffect.
        AlphaTestEffect fx(device);
        fx.setAlphaFunctionProperty(CompareFunction::Greater);
        fx.setReferenceAlphaProperty(128);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();  // registers the effect as currentEffect_ on GraphicsDevice

        // Full-screen quad: left half with UV.x=0.25 (red texel), right half with
        // UV.x=0.75 (transparent texel).  Constant UV per side avoids sampling
        // ambiguity at the texel boundary.
        //
        // NDC layout:   x=-1..0 → left half   x=0..1 → right half
        const VertexPositionTexture verts[12] = {
            // Left half (UV.x=0.25 → samples red pixel, alpha=255)
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.25f, 0.5f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.25f, 0.5f) },
            { Vector3( 0.0f, -1.0f, 0.0f), Vector2(0.25f, 0.5f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.25f, 0.5f) },
            { Vector3( 0.0f, -1.0f, 0.0f), Vector2(0.25f, 0.5f) },
            { Vector3( 0.0f,  1.0f, 0.0f), Vector2(0.25f, 0.5f) },
            // Right half (UV.x=0.75 → samples transparent pixel, alpha=0)
            { Vector3(0.0f,  1.0f, 0.0f), Vector2(0.75f, 0.5f) },
            { Vector3(0.0f, -1.0f, 0.0f), Vector2(0.75f, 0.5f) },
            { Vector3(1.0f, -1.0f, 0.0f), Vector2(0.75f, 0.5f) },
            { Vector3(0.0f,  1.0f, 0.0f), Vector2(0.75f, 0.5f) },
            { Vector3(1.0f, -1.0f, 0.0f), Vector2(0.75f, 0.5f) },
            { Vector3(1.0f,  1.0f, 0.0f), Vector2(0.75f, 0.5f) },
        };
        // Task 896 finding: both half-quads' winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 4);

        // Read back one pixel from each half.
        const Rectangle leftReg(W / 4,     H / 2, 1, 1);
        const Rectangle rightReg(3 * W / 4, H / 2, 1, 1);
        Color leftPx(0, 0, 0, 0);
        Color rightPx(0, 0, 0, 0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        // Left should be red (texture colour), right should be green (background).
        const bool leftPass  = (leftPx.getRProperty()  >= 200 && leftPx.getGProperty()  <= 50);
        const bool rightPass = (rightPx.getGProperty() >= 200 && rightPx.getRProperty() <= 50);

        if (leftPass && rightPass)
        {
            std::printf("[PASS] AlphaTestIntegration: left=(%d,%d,%d) right=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] AlphaTestIntegration: left=(%d,%d,%d) exp(~255,0,*) "
                        "right=(%d,%d,%d) exp(0,~255,*)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    AlphaTestIntegrationTest game;
    game.Run();
    return game.getResult();
}
