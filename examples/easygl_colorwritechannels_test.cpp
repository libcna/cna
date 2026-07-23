// SPDX-License-Identifier: MS-PL
// REMED-GFX-077: the EasyGL backend must honor BlendState.ColorWriteChannels via glColorMask (a
// global GL device state, GL ES 2.0+), now that the mask is plumbed through
// IGraphicsBackend::ApplyBlendState. Previously EasyGL never called glColorMask -> the mask was a
// silent no-op (always full RGBA).
//
// EasyGL's ColorWriteChannels support is RT0 (glColorMask is not per-attachment; independent MRT
// masks would need glColorMaski, ES 3.2+, a documented follow-up). MultiSampleMask is likewise a
// documented capability gap on the GL/GLES profile (glSampleMaski, ES 3.1+). This test therefore
// covers the RGB per-channel write masks (the reliably-observable backbuffer channels); the alpha
// write mask and the sample mask are proven on the Software and Vulkan backends (real RT alpha /
// pSampleMask). Clear() must ignore the mask (glClear respects glColorMask, but XNA Clear does not)
// -- exercised implicitly since every case clears to D under an active mask.
//
// Exit code 0 = all PASS, 1 = any FAIL. Runs under Xvfb + llvmpipe.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class EasyGLColorWriteChannelsTest : public Game
{
    bool done_ = false;
    int passCount_ = 0, totalCount_ = 0, result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_; if (ok) ++passCount_;
    }

    static BlendState MakeBlend(ColorWriteChannels cw)
    {
        BlendState bs; // Opaque (One/Zero) => blend off, mask isolated
        bs.setColorWriteChannelsProperty(cw);
        return bs;
    }

    Color DrawAndRead(GraphicsDevice& dev, const Color& dest, const Color& src, ColorWriteChannels cw)
    {
        const auto& vp = dev.getViewportProperty();
        dev.SetDepthTestEnabled(false);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(MakeBlend(cw));
        dev.Clear(dest);                                  // Clear must ignore the active mask
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), src }, { Vector3(-1.0f, -1.0f, 0.0f), src },
            { Vector3( 1.0f, -1.0f, 0.0f), src }, { Vector3(-1.0f,  1.0f, 0.0f), src },
            { Vector3( 1.0f, -1.0f, 0.0f), src }, { Vector3( 1.0f,  1.0f, 0.0f), src },
        };
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);
        return got;
    }

    static std::string RGB(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + ")";
    }
    static bool EqRGB(const Color& c, int r, int g, int b)
    {
        return c.getRProperty() == r && c.getGProperty() == g && c.getBProperty() == b;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        const Color D(10, 20, 30, 255), S(200, 100, 50, 255);

        struct Case { ColorWriteChannels cw; int r, g, b; const char* name; };
        const Case cases[] = {
            { ColorWriteChannels::None,                            10,  20, 30, "None" },
            { ColorWriteChannels::Red,                            200,  20, 30, "Red" },
            { ColorWriteChannels::Green,                           10, 100, 30, "Green" },
            { ColorWriteChannels::Blue,                            10,  20, 50, "Blue" },
            { ColorWriteChannels::Red | ColorWriteChannels::Blue, 200,  20, 50, "Red|Blue" },
            { ColorWriteChannels::All,                            200, 100, 50, "All" },
        };
        for (const auto& c : cases)
        {
            const Color px = DrawAndRead(dev, D, S, c.cw);
            check(EqRGB(px, c.r, c.g, c.b),
                  std::string("ColorWriteChannels.") + c.name + " RGB -> (" + std::to_string(c.r) + "," +
                  std::to_string(c.g) + "," + std::to_string(c.b) + "), got " + RGB(px));
        }

        // A(Red)->B(Green)->A(Red): glColorMask is global device state; each draw re-applies its mask.
        const bool a  = EqRGB(DrawAndRead(dev, D, S, ColorWriteChannels::Red),   200, 20, 30);
        const bool b  = EqRGB(DrawAndRead(dev, D, S, ColorWriteChannels::Green),  10, 100, 30);
        const bool a2 = EqRGB(DrawAndRead(dev, D, S, ColorWriteChannels::Red),   200, 20, 30);
        check(a && b && a2, "A(Red)->B(Green)->A(Red): each draw re-applies its own glColorMask");

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLColorWriteChannelsTest game;
    game.Run();
    return game.getResult();
}
