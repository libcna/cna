// SPDX-License-Identifier: MS-PL
// REMED-GFX-077: the Bgfx renderer must honor BlendState.ColorWriteChannels via the per-draw
// BGFX_STATE_WRITE_{R,G,B,A} bits (identical bit values to the XNA ColorWriteChannels flags), now
// that the mask is plumbed through IGraphicsRenderer::ApplyBlendState. Previously every bgfx::setState
// hardcoded BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A -> the mask was a silent no-op.
//
// Bgfx colour write is a single GLOBAL per-draw mask (not per-attachment), so independent MRT masks
// (ColorWriteChannels1/2/3) are not representable (documented gap, REMED-GFX-085); bgfx also has no
// per-draw sample coverage mask, so MultiSampleMask is not representable (documented). This test
// covers the RGB per-channel write masks on the backbuffer (alpha write mask + sample mask are
// proven on Software/Vulkan). One case per frame -- Bgfx defers presentation, so GetBackBufferData
// is driven one scene per Draw() call.
//
// Exit code 0 = all PASS, 1 = any FAIL. Runs under Xvfb + llvmpipe (bgfx-GL).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BgfxColorWriteChannelsTest : public Game
{
    struct Case { ColorWriteChannels cw; int r, g, b; const char* name; };
    std::array<Case, 6> cases_{{
        { ColorWriteChannels::None,                            10,  20, 30, "None" },
        { ColorWriteChannels::Red,                            200,  20, 30, "Red" },
        { ColorWriteChannels::Green,                           10, 100, 30, "Green" },
        { ColorWriteChannels::Blue,                            10,  20, 50, "Blue" },
        { ColorWriteChannels::Red | ColorWriteChannels::Blue, 200,  20, 50, "Red|Blue" },
        { ColorWriteChannels::All,                            200, 100, 50, "All" },
    }};
    std::size_t index_ = 0;
    int passCount_ = 0, totalCount_ = 0, result_ = 1;

    static BlendState MakeBlend(ColorWriteChannels cw)
    {
        BlendState bs; // Opaque (One/Zero) => blend off, mask isolated
        bs.setColorWriteChannelsProperty(cw);
        return bs;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (index_ >= cases_.size())
        {
            std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
            result_ = (passCount_ == totalCount_ && totalCount_ > 0) ? 0 : 1;
            Exit();
            return;
        }
        const Case c = cases_[index_++];

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();
        const Color D(10, 20, 30, 255), S(200, 100, 50, 255);

        DepthStencilState ds; ds.setDepthBufferEnableProperty(false);
        dev.setDepthStencilStateProperty(ds);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(MakeBlend(c.cw));
        dev.Clear(D);

        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), S }, { Vector3(-1.0f, -1.0f, 0.0f), S },
            { Vector3( 1.0f, -1.0f, 0.0f), S }, { Vector3(-1.0f,  1.0f, 0.0f), S },
            { Vector3( 1.0f, -1.0f, 0.0f), S }, { Vector3( 1.0f,  1.0f, 0.0f), S },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);
        const bool ok = got.getRProperty() == c.r && got.getGProperty() == c.g && got.getBProperty() == c.b;
        std::printf("[%s] ColorWriteChannels.%s RGB -> (%d,%d,%d), got (%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", c.name, c.r, c.g, c.b,
                    got.getRProperty(), got.getGProperty(), got.getBProperty());
        std::fflush(stdout);
        ++totalCount_; if (ok) ++passCount_;
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BgfxColorWriteChannelsTest game;
    game.Run();
    return game.getResult();
}
