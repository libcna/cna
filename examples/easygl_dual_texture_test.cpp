// SPDX-License-Identifier: MS-PL
// Task 133: EasyGL integration test — DualTextureEffect two-layer blending.
//
// Renders a full-screen quad with two textures multiplied together:
//   texture 0: solid magenta  (1, 0, 1, 1)
//   texture 1: solid yellow   (1, 1, 0, 1)
//   diffuseColor: white       (1, 1, 1, 1)
//   result: magenta × yellow × white = (1, 0, 0, 1) = red
//
// Exercises EasyGL prog_dual_textured_ (loc_texture2, GL_TEXTURE1 path).
// Uses VertexPositionTexture (stride=20) through DrawUserPrimitives.
// Background is cleared to green; pixel readback asserts red centre.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DualTextureTest : public Game
{
    Texture2D tex0_; // magenta (1, 0, 1, 1)
    Texture2D tex1_; // yellow  (1, 1, 0, 1)
    bool      done_   = false;
    int       result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const std::vector<uint8_t> magenta = { 255,   0, 255, 255 };
        const std::vector<uint8_t> yellow  = { 255, 255,   0, 255 };
        tex0_ = Texture2D::CreateFromPixels(device, 1, 1, magenta);
        tex1_ = Texture2D::CreateFromPixels(device, 1, 1, yellow);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255)); // green background
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        DualTextureEffect fx(device);
        fx.setTextureProperty(&tex0_);
        fx.setTexture2Property(&tex1_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        // Full-screen quad (NDC -1..1), UV 0..1 maps to entire 1×1 texture.
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        // Centre pixel: magenta × yellow = (1,0,1)×(1,1,0) = (1,0,0) = red.
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        // R should be high, G and B should be near zero.
        const bool pass = (centPx.getRProperty() >= 200 &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() <= 50);

        if (pass)
        {
            std::printf("[PASS] DualTextureEffect: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] DualTextureEffect: centre=(%d,%d,%d), "
                        "expected red (R>=200, G<=50, B<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    DualTextureTest game;
    game.Run();
    return game.getResult();
}
