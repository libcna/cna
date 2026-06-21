// SPDX-License-Identifier: MS-PL
// Task 135: Vulkan integration test — DualTextureEffect two-layer blending.
//
// Mirrors Task 133 (EasyGL) but exercises the Vulkan dual-texture pipeline:
//   - GetOrCreatePipelineDualTex3D (stride=20, VertexPositionTexture)
//   - GetOrCreateDualTexDescSet (2-sampler descriptor set)
//   - kDualTexture3dFragSpv: outColor = tex1 * tex2 * fragTint
//
// Texture choices that give a predictable red output:
//   texture 0 = solid magenta (1, 0, 1, 1)
//   texture 1 = solid yellow  (1, 1, 0, 1)
//   diffuseColor (fragTint) = white (1, 1, 1, 1) from DualTextureEffect defaults
//   result: (1×1×1, 0×1×1, 1×0×1, 1×1×1) = (1, 0, 0, 1) = red
//
// The Vulkan textured3d vertex shader applies a Y-flip (pos.y = -pos.y) and
// a depth remap; a full-screen NDC quad (-1..1) covers the viewport regardless.
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

class VulkanDualTextureTest : public Game
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

        // Full-screen quad (NDC -1..1).  Vulkan Y-flip in vertex shader preserves
        // full-screen coverage.
        const VertexPositionTexture verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        // Centre pixel: magenta × yellow × white = (1,0,0) = red.
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool pass = (centPx.getRProperty() >= 200 &&
                           centPx.getGProperty() <= 50  &&
                           centPx.getBProperty() <= 50);

        if (pass)
        {
            std::printf("[PASS] VulkanDualTexture: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] VulkanDualTexture: centre=(%d,%d,%d), "
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
    VulkanDualTextureTest game;
    game.Run();
    return game.getResult();
}
