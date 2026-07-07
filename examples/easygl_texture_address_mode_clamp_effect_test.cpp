// SPDX-License-Identifier: MS-PL
// Task 294: verify TextureAddressMode::Clamp on a 3D stock-effect draw (DualTextureEffect),
// as opposed to Task 269's SpriteBatch-only coverage and Task 293's Wrap-focused fix-proof test.
//
// texture0 is a 2x1 texel pattern: left texel red (1,0,0), right texel green (0,1,0).
// texture1 is a solid white 1x1 texture, so the DualTextureEffect multiply (tex0 * tex1 *
// diffuse) leaves tex0's sampled colour unchanged and isolates the test to slot 0's sampler.
//
// The quad's UV spans u=[0,2]. We read back the pixel at destination x-fraction 0.625, i.e.
// raw u=1.25:
//   - TextureAddressMode::Clamp: u clamps to 1.0 -> right edge of the texture -> GREEN
//   - TextureAddressMode::Wrap:  1.25 mod 1 = 0.25 -> left half of the repeat -> RED
// SamplerState::PointClamp is assigned to slot 0 before drawing (mirrors
// easygl_sampler_state_effect_test.cpp's PointWrap case exactly, with the opposite expected
// colour). Point filtering avoids any bilinear blend across the texel boundary.
//
// Exit code 0 = PASS (result matches PointClamp's GREEN), 1 = FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class TextureAddressModeClampEffectTest : public Game
{
    Texture2D patternTex_; // 2x1: red | green
    Texture2D whiteTex_;   // 1x1: white
    bool      done_   = false;
    int       result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const std::vector<uint8_t> pattern = {
            255,   0,   0, 255,   // texel 0: red
              0, 255,   0, 255    // texel 1: green
        };
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        patternTex_ = Texture2D::CreateFromPixels(device, 2, 1, pattern);
        whiteTex_   = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 255, 255)); // blue background (neither expected result)
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        DualTextureEffect fx(device);
        fx.setTextureProperty(&patternTex_);
        fx.setTexture2Property(&whiteTex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        // Full-screen quad (NDC -1..1), U spans 0..2 (destination x-fraction * 2 = raw u).
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(2.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(2.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(2.0f, 1.0f) },
        };
        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        // Sample at destination x-fraction 0.625 (raw u = 1.25).
        const int sampleX = static_cast<int>(W * 0.625f);
        const Rectangle sampleReg(sampleX, H / 2, 1, 1);
        Color samplePx(0, 0, 0, 0);
        device.GetBackBufferData(&sampleReg, &samplePx, 0, 1);

        const bool isGreen = samplePx.getGProperty() >= 200 && samplePx.getRProperty() <= 50;
        const bool isRed   = samplePx.getRProperty() >= 200 && samplePx.getGProperty() <= 50;

        if (isGreen)
        {
            std::printf("[PASS] SamplerState honored: PointClamp sample=(%d,%d,%d) is GREEN as FNA requires\n",
                        samplePx.getRProperty(), samplePx.getGProperty(), samplePx.getBProperty());
            result_ = 0;
        }
        else if (isRed)
        {
            std::printf("[FAIL] SamplerState IGNORED: PointClamp was assigned but sample=(%d,%d,%d) is RED,\n"
                        "       the Wrap result - GraphicsDevice.SamplerStates[0] had no effect on this draw.\n",
                        samplePx.getRProperty(), samplePx.getGProperty(), samplePx.getBProperty());
        }
        else
        {
            std::printf("[FAIL] Unexpected sample=(%d,%d,%d), neither RED nor GREEN\n",
                        samplePx.getRProperty(), samplePx.getGProperty(), samplePx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    TextureAddressModeClampEffectTest game;
    game.Run();
    return game.getResult();
}
