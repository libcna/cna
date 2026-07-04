// SPDX-License-Identifier: MS-PL
// Task 297: verify TextureFilter::Point vs Linear on a 3D stock effect (DualTextureEffect),
// for both magnification and minification.
//
// CNA maps XNA's single TextureFilter value to one GL/Vulkan min+mag filter pair (matching FNA's
// flat enum — there is no separate min/mag control), and generates no mipmaps by default, so
// magnification and minification share identical underlying sampler math here; this test still
// exercises both scales explicitly since that's the task's ask, and to confirm scale itself
// doesn't break the sampler-state fix (Task 293).
//
// Four columns are drawn left-to-right on one frame, each with its own SamplerState:
//   Col 1 [0,256)px:   magnification, Point  — 2-texel (Red|Green) texture stretched wide.
//   Col 2 [256,512)px: magnification, Linear — same texture, same stretch.
//   Col 3 [512,640)px: minification,  Point  — 256-texel alternating R/G texture compressed
//                                               into a 128px-wide quad (2 texels/pixel).
//   Col 4 [640,768)px: minification,  Linear — same compressed texture.
// Each column is sampled exactly at its texture's texel boundary (the midpoint between two
// texel centers, guaranteed NOT to coincide with any texel center): Point sampling always
// returns one pure texel colour there; Linear sampling always returns a genuine ~50/50 blend
// (both R and G channels significant) there, regardless of on-screen scale.
//
// Exit code 0 = PASS (Point columns pure, Linear columns blended in both scales), 1 = FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    SamplerState MakeSampler(TextureFilter filter)
    {
        SamplerState ss;
        ss.setFilterProperty(filter);
        ss.setAddressUProperty(TextureAddressMode::Clamp);
        ss.setAddressVProperty(TextureAddressMode::Clamp);
        return ss;
    }

    // True if both channels are in a genuine mid-range blend (~50/50 of 255).
    bool IsBlended(const Color& c)
    {
        return c.getRProperty() >= 90 && c.getRProperty() <= 165
            && c.getGProperty() >= 90 && c.getGProperty() <= 165;
    }

    // True if exactly one of R/G is a pure, unblended channel value.
    bool IsPure(const Color& c)
    {
        const bool rHigh = c.getRProperty() >= 235, rLow = c.getRProperty() <= 20;
        const bool gHigh = c.getGProperty() >= 235, gLow = c.getGProperty() <= 20;
        return (rHigh && gLow) || (rLow && gHigh);
    }
}

class TextureFilterPointVsLinearTest : public Game
{
    Texture2D tex2_;   // 2x1: Red | Green
    Texture2D tex256_; // 256x1: alternating Red/Green per texel
    Texture2D whiteTex_;
    bool      done_   = false;
    int       result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        const std::vector<uint8_t> pattern2 = {
            255,   0,   0, 255,
              0, 255,   0, 255
        };
        tex2_ = Texture2D::CreateFromPixels(device, 2, 1, pattern2);

        std::vector<uint8_t> pattern256(256 * 4);
        for (int i = 0; i < 256; ++i)
        {
            const bool red = (i % 2) == 0;
            pattern256[i * 4 + 0] = red ? 255 : 0;
            pattern256[i * 4 + 1] = red ? 0   : 255;
            pattern256[i * 4 + 2] = 0;
            pattern256[i * 4 + 3] = 255;
        }
        tex256_ = Texture2D::CreateFromPixels(device, 256, 1, pattern256);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        whiteTex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void DrawColumn(GraphicsDevice& device, Texture2D& tex, TextureFilter filter,
                    float xLeftPx, float xRightPx, int screenW)
    {
        device.getSamplerStatesProperty()[0] = MakeSampler(filter);

        DualTextureEffect fx(device);
        fx.setTextureProperty(&tex);
        fx.setTexture2Property(&whiteTex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const float ndcLeft  = (xLeftPx  / static_cast<float>(screenW)) * 2.0f - 1.0f;
        const float ndcRight = (xRightPx / static_cast<float>(screenW)) * 2.0f - 1.0f;
        const VertexPositionTexture verts[6] = {
            { Vector3(ndcLeft,   1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(ndcLeft,  -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3(ndcRight, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(ndcLeft,   1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(ndcRight, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(ndcRight,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // Magnification: 2-texel texture stretched across a 256px-wide column.
        DrawColumn(device, tex2_,   TextureFilter::Point,  0.0f,   256.0f, W);
        DrawColumn(device, tex2_,   TextureFilter::Linear, 256.0f, 512.0f, W);
        // Minification: 256-texel texture compressed into a 128px-wide column (2 texels/pixel).
        DrawColumn(device, tex256_, TextureFilter::Point,  512.0f, 640.0f, W);
        DrawColumn(device, tex256_, TextureFilter::Linear, 640.0f, 768.0f, W);

        auto sample = [&](float px) {
            const Rectangle reg(static_cast<int>(px), H / 2, 1, 1);
            Color c(0, 0, 0, 0);
            device.GetBackBufferData(&reg, &c, 0, 1);
            return c;
        };

        const Color magPoint    = sample(128.0f); // texel boundary of col 1
        const Color magLinear   = sample(384.0f); // texel boundary of col 2
        const Color minPoint    = sample(576.0f); // texel boundary of col 3
        const Color minLinear   = sample(704.0f); // texel boundary of col 4

        struct Check { const char* label; Color c; bool (*pred)(const Color&); };
        const Check checks[4] = {
            { "Magnification/Point (expect pure)",    magPoint,  IsPure    },
            { "Magnification/Linear (expect blend)",  magLinear, IsBlended },
            { "Minification/Point (expect pure)",     minPoint,  IsPure    },
            { "Minification/Linear (expect blend)",   minLinear, IsBlended },
        };

        int passCount = 0;
        for (const auto& chk : checks)
        {
            const bool ok = chk.pred(chk.c);
            std::printf("[%s] %s: sample=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", chk.label,
                        chk.c.getRProperty(), chk.c.getGProperty(), chk.c.getBProperty());
            if (ok) ++passCount;
        }

        result_ = (passCount == 4) ? 0 : 1;
        std::printf("=== %d/4 PASS ===\n", passCount);
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    TextureFilterPointVsLinearTest game;
    game.Run();
    return game.getResult();
}
