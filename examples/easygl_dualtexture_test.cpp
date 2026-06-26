// SPDX-License-Identifier: MS-PL
// Task 191: DualTextureEffect pixel integration test — EasyGL backend.
//
// The dual-texture shader computes:
//   FragColor = texture(uTexture, vUV) * texture(uTexture2, vUV) * uDiffuseColor
//
// Four sub-tests verify that both texture slots and the diffuse multiplier are active:
//   (a) tex1=white, tex2=blue,  diffuse=white → blue   (tex2 contributes)
//   (b) tex1=red,   tex2=white, diffuse=white → red    (tex1 contributes)
//   (c) tex1=white, tex2=white, diffuse=green → green  (diffuseColor multiplied in)
//   (d) tex1=yellow, tex2=cyan, diffuse=white → green  (both multiply: (1,1,0)×(0,1,1)=(0,1,0))
//
// Test (d) is the decisive proof that both textures are multiplied simultaneously.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

class DualTextureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            result_ = 1;
        }
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        // Shared 1×1 textures.
        const Color kBlack (  0,   0,   0, 255);
        const Color kRed   (255,   0,   0, 255);
        const Color kGreen (  0, 255,   0, 255);
        const Color kBlue  (  0,   0, 255, 255);
        const Color kWhite (255, 255, 255, 255);
        const Color kYellow(255, 255,   0, 255);  // (1,1,0)
        const Color kCyan  (  0, 255, 255, 255);  // (0,1,1)

        Texture2D texWhite (dev, 1, 1); texWhite .SetData(&kWhite,  1);
        Texture2D texRed   (dev, 1, 1); texRed   .SetData(&kRed,    1);
        Texture2D texBlue  (dev, 1, 1); texBlue  .SetData(&kBlue,   1);
        Texture2D texYellow(dev, 1, 1); texYellow.SetData(&kYellow, 1);
        Texture2D texCyan  (dev, 1, 1); texCyan  .SetData(&kCyan,   1);

        // Full-screen NDC quad using VertexPositionTexture (stride 20).
        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        // ── (a) tex1=white, tex2=blue → blue ─────────────────────────────
        {
            dev.Clear(kBlack);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&texWhite);
            fx.setTexture2Property(&texBlue);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kBlue), "(a) tex1=white tex2=blue → blue", got, kBlue);
        }

        // ── (b) tex1=red, tex2=white → red ───────────────────────────────
        {
            dev.Clear(kBlack);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&texRed);
            fx.setTexture2Property(&texWhite);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kRed), "(b) tex1=red tex2=white → red", got, kRed);
        }

        // ── (c) tex1=white, tex2=white, diffuse=green → green ────────────
        {
            dev.Clear(kBlack);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&texWhite);
            fx.setTexture2Property(&texWhite);
            fx.setDiffuseColorProperty(Vector3(0.0f, 1.0f, 0.0f));
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kGreen), "(c) diffuse=green → green", got, kGreen);
        }

        // ── (d) tex1=yellow(1,1,0) × tex2=cyan(0,1,1) → green(0,1,0) ───
        // (1,1,0)×(0,1,1) = (0,1,0) proves simultaneous multiplication.
        {
            dev.Clear(kBlack);
            DualTextureEffect fx(dev);
            fx.setTextureProperty(&texYellow);
            fx.setTexture2Property(&texCyan);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            Color got = readCenter(dev);
            check(colourMatch(got, kGreen), "(d) yellow×cyan → green", got, kGreen);
        }

        Exit();
    }

public:
    DualTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    DualTextureTest game;
    game.Run();
    return game.getResult();
}
