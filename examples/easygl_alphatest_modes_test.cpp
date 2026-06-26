// SPDX-License-Identifier: MS-PL
// Task 190: AlphaTestEffect all CompareFunction modes — EasyGL pixel integration test.
//
// Draws a full-screen textured quad with pixel alpha = 128/255 and reference = 128,
// testing all 8 CompareFunction values.  The background is black; drawn pixels are gray.
//
// Expected results with pixel.a == reference (both 128/255):
//   Always      → drawn (always passes)
//   Never       → discarded (always fails)
//   Less        → discarded  (128 < 128 is false)
//   LessEqual   → drawn      (128 <= 128 is true)
//   Equal       → drawn      (128 == 128 is true)
//   NotEqual    → discarded  (128 != 128 is false)
//   GreaterEqual→ drawn      (128 >= 128 is true)
//   Greater     → discarded  (128 > 128 is false)
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kBlack(0, 0, 0, 255);
static const Color kWhite(255, 255, 255, 255);

struct AlphaTestCase
{
    CompareFunction func;
    bool            expectDrawn;
    const char*     label;
};

static const AlphaTestCase kCases[] = {
    { CompareFunction::Always,       true,  "Always       (always passes)" },
    { CompareFunction::Never,        false, "Never        (always discards)" },
    { CompareFunction::Less,         false, "Less         (128 < 128 → false)" },
    { CompareFunction::LessEqual,    true,  "LessEqual    (128 <= 128 → true)" },
    { CompareFunction::Equal,        true,  "Equal        (128 == 128 → true)" },
    { CompareFunction::NotEqual,     false, "NotEqual     (128 != 128 → false)" },
    { CompareFunction::GreaterEqual, true,  "GreaterEqual (128 >= 128 → true)" },
    { CompareFunction::Greater,      false, "Greater      (128 > 128 → false)" },
};

class AlphaTestModesTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

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

        // White 1×1 texture with full alpha; alpha is controlled via setAlphaProperty.
        Texture2D whiteTex(dev, 1, 1);
        whiteTex.SetData(&kWhite, 1);

        const auto& vp = dev.getViewportProperty();
        const Rectangle centerReg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);

        // Full-screen NDC quad.
        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        for (const auto& tc : kCases)
        {
            dev.Clear(kBlack);

            AlphaTestEffect fx(dev);
            fx.setTextureProperty(&whiteTex);
            fx.setAlphaProperty(128.0f / 255.0f);
            fx.setReferenceAlphaProperty(128);
            fx.setAlphaFunctionProperty(tc.func);
            fx.Apply();

            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

            Color got(0, 0, 0, 0);
            dev.GetBackBufferData(&centerReg, &got, 0, 1);

            // Drawn pixels are gray (~128,128,128); discarded pixels leave the black clear.
            const bool isDrawn = got.getRProperty() > 50;
            const bool ok      = (isDrawn == tc.expectDrawn);

            std::printf("[%s] %s: pixel=(%d,%d,%d) %s\n",
                ok ? "PASS" : "FAIL",
                tc.label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                tc.expectDrawn ? "(expected drawn)" : "(expected discarded)");

            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    AlphaTestModesTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(200);
    }

    int getResult() const { return result_; }
};

int main()
{
    AlphaTestModesTest game;
    game.Run();
    return game.getResult();
}
