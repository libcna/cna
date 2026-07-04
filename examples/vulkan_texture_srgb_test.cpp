// SPDX-License-Identifier: MS-PL
// Task 284: Vulkan Texture2D Color format mapping — linear vs. sRGB decode.
//
// FNA's SurfaceFormat.Color is explicitly NOT sRGB (there is a separate
// SurfaceFormat.ColorSrgbEXT for that) — plain Color texture bytes must be sampled as linear,
// with no automatic gamma decode. Existing pixel-readback tests only ever use saturated 0/255
// component values (Red/Green/Blue/White/Black), which cannot reveal an sRGB-vs-linear mixup:
// 0 and 255 are both fixed points of the sRGB transfer curve, so a wrongly-sRGB-decoded texture
// still round-trips correctly at the extremes. This test instead uses a genuine mid-range grey
// value (128), which the sRGB curve maps to a very different value (~55) than linear (128) —
// large enough to be unmistakable, small enough to still be exact-integer-comparable.
//
// Two scenes are rendered independently, so any transform the swapchain itself applies (e.g. an
// sRGB-format presentation surface) affects both equally and cancels out of the comparison:
//   (a) a VertexPositionColor quad, vertex colour = (128,128,128,255), no texture — the swapchain's
//       own gamma handling (if any) is the ONLY transform this path goes through.
//   (b) a VertexPositionTexture quad sampling a 1x1 Texture2D filled with (128,128,128,255),
//       diffuseColor = white (no tint) — goes through the same swapchain transform PLUS whatever
//       the texture sampler itself does.
// If Texture2D's Vulkan image format wrongly applies an sRGB decode on sample, (b)'s backbuffer
// pixel will read dramatically brighter than (a)'s, even though both should be identical.
//
// Exit code 0 = PASS (both scenes read back the same value, within a tight tolerance).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class VulkanTextureSrgbTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();
        const Rectangle centReg(W / 2, H / 2, 1, 1);

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // ── Scene (a): vertex-colour-only mid-grey quad ──────────────────────
        Color vertexColorPixel(0, 0, 0, 0);
        {
            device.Clear(Color(0, 0, 0, 255));

            BasicEffect fx(device);
            fx.VertexColorEnabled = true;
            fx.setTextureEnabledProperty(false);
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();

            const Color grey(128, 128, 128, 255);
            const VertexPositionColor verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), grey },
                { Vector3(-1.0f, -1.0f, 0.0f), grey },
                { Vector3( 1.0f, -1.0f, 0.0f), grey },
                { Vector3(-1.0f,  1.0f, 0.0f), grey },
                { Vector3( 1.0f, -1.0f, 0.0f), grey },
                { Vector3( 1.0f,  1.0f, 0.0f), grey },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
            device.GetBackBufferData(&centReg, &vertexColorPixel, 0, 1);
        }

        // ── Scene (b): textured mid-grey quad (no vertex colour tint) ────────
        Color texturePixel(0, 0, 0, 0);
        {
            device.Clear(Color(0, 0, 0, 255));

            const uint8_t greyPixel[4] = { 128, 128, 128, 255 };
            const std::vector<uint8_t> greyBytes(greyPixel, greyPixel + 4);
            Texture2D greyTex = Texture2D::CreateFromPixels(device, 1, 1, greyBytes);

            BasicEffect fx(device);
            fx.VertexColorEnabled = false;
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&greyTex);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();

            const VertexPositionTexture verts[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
            device.GetBackBufferData(&centReg, &texturePixel, 0, 1);
        }

        const int diff = std::abs(static_cast<int>(vertexColorPixel.getRProperty())
                                 - static_cast<int>(texturePixel.getRProperty()));
        const bool pass = diff <= 5;

        std::printf("[%s] Vulkan Texture2D sRGB check: vertexColor=(%d,%d,%d) texture=(%d,%d,%d) diff=%d\n",
                    pass ? "PASS" : "FAIL",
                    vertexColorPixel.getRProperty(), vertexColorPixel.getGProperty(), vertexColorPixel.getBProperty(),
                    texturePixel.getRProperty(), texturePixel.getGProperty(), texturePixel.getBProperty(),
                    diff);

        result_ = pass ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanTextureSrgbTest game;
    game.Run();
    return game.getResult();
}
