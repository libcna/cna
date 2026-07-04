// SPDX-License-Identifier: MS-PL
// Task 303: verify BlendState::Opaque genuinely discards destination content and ignores source
// alpha, rather than merely "not looking obviously broken" the way Task 255's existing
// DrawUserPrimitives<VertexPositionColor> test does (which only ever draws a fully-opaque source
// colour — that case can't distinguish Opaque from AlphaBlend, since AlphaBlend's math collapses
// to the same result when the source alpha is 255).
//
// Clears to GREEN, then draws a quad with a PARTIALLY TRANSPARENT red (alpha=128) using
// BlendState::Opaque (colorSourceBlend=One, colorDestinationBlend=Zero, same for alpha). Per FNA's
// blend equation, output = fragColor*colorSourceBlend + dest*colorDestinationBlend
// = fragColor*1 + dest*0 = fragColor exactly — the destination (green) must be completely
// discarded and the low source alpha must NOT cause any blending, unlike AlphaBlend/
// NonPremultiplied, which would produce a visible red/green mix at alpha=128.
//
// Exit code 0 = PASS (pure red, no green bleed-through), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BlendStateOpaqueTest : public Game
{
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

        dev.Clear(Color(0, 255, 0, 255)); // green background
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const Color kTranslucentRed(255, 0, 0, 128);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kTranslucentRed },
            { Vector3(-1.0f, -1.0f, 0.0f), kTranslucentRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kTranslucentRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kTranslucentRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kTranslucentRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kTranslucentRed },
        };

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);

        const bool isPureRed = got.getRProperty() >= 200 && got.getGProperty() <= 50 && got.getBProperty() <= 50;

        if (isPureRed)
        {
            std::printf("[PASS] BlendState::Opaque with alpha=128 source: centre=(%d,%d,%d) is pure red\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BlendState::Opaque did not discard destination/alpha: centre=(%d,%d,%d),\n"
                        "       expected pure red (R>=200,G<=50,B<=50) - green background must not bleed through.\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BlendStateOpaqueTest game;
    game.Run();
    return game.getResult();
}
