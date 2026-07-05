// SPDX-License-Identifier: MS-PL
// Task 337: verify MSAA render target creation and resolve behavior on EasyGL.
//
// A solid-fill test (like easygl_msaa_test.cpp's backbuffer MSAA test) can't distinguish "MSAA
// resolve doesn't corrupt solid colors" from "anti-aliasing genuinely happened" — a non-MSAA
// target passes a solid-fill test just as trivially. This test proves REAL anti-aliasing: render
// a diagonal-edged triangle into a RenderTarget2D, resolve it, sample it back, and check for
// partially-covered (blended, neither pure white nor pure black) pixels along the diagonal edge —
// a signature that can only appear if the multisampled resolve actually averaged sub-pixel
// coverage, not just because the resolve preserved solid interior colors correctly.
//
// Method (differential: same triangle, two RTs, one with MultiSampleCount=0, one with 8):
//   - Draw a white triangle covering the upper-left half of the RT (vertices at NDC
//     (-1,1)/(1,1)/(-1,-1) — hypotenuse runs corner-to-corner through the exact centre), on
//     black, into a mipMap=false RenderTarget2D with the given MultiSampleCount.
//   - Unbind (triggers the MSAA resolve when MultiSampleCount>0).
//   - Sample the RT 1:1 onto the backbuffer via SpriteBatch with SamplerState.PointClamp (avoids
//     the *sampling* step itself introducing blur that would contaminate the result).
//   - Read back the full centre row of pixels. The diagonal crosses this row at the centre
//     column regardless of any backend Y-flip convention (the hypotenuse is symmetric about the
//     RT's centre point).
//   - Assert: MultiSampleCount=0's row is purely binary (every pixel near-black or near-white —
//     hard, aliased edge, no blending pass exists). MultiSampleCount=8's row has at least one
//     genuinely intermediate (partially-covered) pixel — proof that multisample coverage was
//     actually averaged during the resolve blit.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 32;

class RenderTarget2DMsaaTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    bool done_   = false;
    int  result_ = 1;

    // Renders the diagonal triangle into a RenderTarget2D with the given MultiSampleCount, then
    // samples it back onto the backbuffer 1:1, and returns the full centre-row pixel colours.
    std::vector<Color> RenderAndReadRow(GraphicsDevice& device, int multiSampleCount)
    {
        RenderTarget2D rt(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                           DepthFormat::None, multiSampleCount, RenderTargetUsage::DiscardContents);

        device.setBlendStateProperty(BlendState::Opaque);

        device.SetRenderTarget(&rt);
        device.Clear(Color(0, 0, 0, 255));

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Color white(255, 255, 255, 255);
        const VertexPositionColor tri[3] = {
            { Vector3(-1.0f,  1.0f, 0.0f), white },
            { Vector3( 1.0f,  1.0f, 0.0f), white },
            { Vector3(-1.0f, -1.0f, 0.0f), white },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        device.Clear(Color(0, 0, 0, 255));
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(rt,
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Rectangle(0, 0, kRTSize, kRTSize),
                  Color::White);
        sb_->End();

        std::vector<Color> row(kRTSize, Color(0, 0, 0, 0));
        const Rectangle reg(0, kRTSize / 2, kRTSize, 1);
        device.GetBackBufferData(&reg, row.data(), 0, kRTSize);
        return row;
    }

    static bool IsBinary(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return false;
        }
        return true;
    }

    static bool HasIntermediate(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return true;
        }
        return false;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const std::vector<Color> noMsaaRow = RenderAndReadRow(device, 0);
        const std::vector<Color> msaaRow   = RenderAndReadRow(device, 8);

        const bool noMsaaOk = IsBinary(noMsaaRow);
        const bool msaaOk   = HasIntermediate(msaaRow);

        std::printf("[%s] MultiSampleCount=0: diagonal edge is a hard binary transition (no AA)\n",
                    noMsaaOk ? "PASS" : "FAIL");
        std::printf("[%s] MultiSampleCount=8: diagonal edge has genuinely blended pixels (real AA)\n",
                    msaaOk ? "PASS" : "FAIL");

        if (!noMsaaOk)
        {
            std::printf("[INFO] MultiSampleCount=0 row unexpectedly has intermediate pixels — "
                        "either a rasterizer quirk or a false positive in the differential test.\n");
        }
        if (!msaaOk)
        {
            std::printf("[INFO] MultiSampleCount=8 row is purely binary — MSAA resolve is not "
                        "actually averaging sub-pixel coverage.\n");
        }

        result_ = (noMsaaOk && msaaOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    RenderTarget2DMsaaTest game;
    game.Run();
    return game.getResult();
}
