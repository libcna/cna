// SPDX-License-Identifier: MS-PL
// Task 384: DualTextureEffect integration test — Bgfx backend.
//
// Mirrors Task 133 (EasyGL) / Task 135 (Vulkan): magenta x yellow = red.
//   texture 0 = solid magenta (1, 0, 1, 1)
//   texture 1 = solid yellow  (1, 1, 0, 1)
//   diffuseColor = white (1, 1, 1, 1) from DualTextureEffect defaults
//   result: (1x1, 0x1, 1x0) = (1, 0, 0) = red (both R and G/B channels stay
//   saturated at 0 or 1 regardless of FNA's `color.rgb *= 2` doubling factor
//   fixed in Task 383 -- this case cannot discriminate that bug, unlike
//   examples/bgfx_dualtextureeffect_doubling_test.cpp's gray/white case,
//   which already covers that separately).
//
// This closes the one remaining gap in DualTextureEffect's basic two-texture
// blend coverage: Bgfx never had this specific magenta x yellow test (Tasks
// 133/135 covered EasyGL/Vulkan; Task 191 covers 4 more combinations on
// EasyGL only).
//
// Exit code 0 = PASS, 1 = FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

class BgfxDualTextureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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

        DualTextureEffect fx(device);
        fx.setTextureProperty(&tex0_);
        fx.setTexture2Property(&tex1_);
        fx.Apply();

        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };

        const Rectangle centReg(kSize / 2, kSize / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 255, 0, 255)); // green background
            // Task 375 finding: GraphicsDevice::SetDepthTestEnabled/SetBlend* are unconditional-
            // throw stubs on Bgfx -- not needed here since Clear() resets the depth buffer and
            // this draws a single full-screen quad at z=0 every frame.
            device.setBlendStateProperty(BlendState::Opaque);
            // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding,
            // unlike EasyGL/Vulkan's own defaults.
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
            device.GetBackBufferData(&centReg, &centPx, 0, 1);
            if (centPx.getRProperty() != 0 || centPx.getGProperty() != 0 || centPx.getBProperty() != 0)
                break; // skip blank/black frames
        }

        // Centre pixel: magenta × yellow = (1,0,1)×(1,1,0) = (1,0,0) = red.
        const bool pass = (centPx.getRProperty() >= 200 &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() <= 50);

        if (pass)
        {
            std::printf("[PASS] BgfxDualTexture: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BgfxDualTexture: centre=(%d,%d,%d), "
                        "expected red (R>=200, G<=50, B<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        Exit();
    }

public:
    BgfxDualTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxDualTextureTest game;
    game.Run();
    return game.getResult();
}
