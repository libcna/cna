// SPDX-License-Identifier: MS-PL
// Task 684: Verify non-power-of-two (NPOT) texture upload+sample correctness on SDL_Renderer.
// Mirrors Task 268 (EasyGL's easygl_npot_texture_test.cpp), extended to both a 3x5 and a
// 7x11 texture per this task's own row.
//
// SDL_Renderer's SdlTextureRenderer creates textures via SDL_CreateTexture+SDL_UpdateTexture,
// which abstracts away POT/NPOT handling entirely (unlike EasyGL's raw OpenGL calls, which
// must manage GL row alignment/padding manually for non-multiple-of-4 widths). This is a
// genuinely renderer-specific question since SDL_Renderer's own internal implementation could
// in principle pad an NPOT texture to POT on an older/limited GL profile, corrupting
// sampling -- worth verifying with a real render+readback rather than assuming.
//
// Each texture has a distinct solid colour per row; drawn full-viewport via SpriteBatch with
// PointClamp sampling (keeps row boundaries crisp, no bilinear blending across rows), then
// each row's screen-space vertical centre is read back from the real framebuffer.
//
// 3 (not a multiple of 4) and 7/11 (both odd, non-power-of-two in both dimensions) are
// deliberately chosen to broadly exercise row-alignment-sensitive upload paths.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRowColors[] = {
        Color(255,   0,   0, 255), // red
        Color(  0, 255,   0, 255), // green
        Color(  0,   0, 255, 255), // blue
        Color(255, 255,   0, 255), // yellow
        Color(255,   0, 255, 255), // magenta
        Color(  0, 255, 255, 255), // cyan
        Color(255, 128,   0, 255), // orange
        Color(128,   0, 255, 255), // purple
        Color(255, 255, 255, 255), // white
        Color(128, 128, 128, 255), // grey
        Color(  0, 128,   0, 255), // dark green
    };
}

class SdlNpotTextureTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;

    bool done_   = false;
    int  result_ = 0;

    void RunCase(int texW, int texH, const char* label)
    {
        auto& dev = getGraphicsDeviceProperty();

        std::vector<std::uint8_t> px(static_cast<std::size_t>(texW) * texH * 4);
        for (int y = 0; y < texH; ++y)
        {
            const Color& c = kRowColors[y];
            for (int x = 0; x < texW; ++x)
            {
                std::uint8_t* p = &px[(static_cast<std::size_t>(y) * texW + x) * 4];
                p[0] = c.getRProperty();
                p[1] = c.getGProperty();
                p[2] = c.getBProperty();
                p[3] = c.getAProperty();
            }
        }
        Texture2D tex = Texture2D::CreateFromPixels(dev, texW, texH, px);

        const auto& vp = dev.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(tex, Rectangle(0, 0, W, H), Rectangle(0, 0, texW, texH), Color::White);
        sb_->End();

        for (int y = 0; y < texH; ++y)
        {
            const int sy = (2 * y + 1) * H / (2 * texH);
            Color px1(0, 0, 0, 0);
            Rectangle reg(W / 2, sy, 1, 1);
            dev.GetBackBufferData(&reg, &px1, 0, 1);

            const Color& want = kRowColors[y];
            const bool ok = px1.getRProperty() == want.getRProperty()
                         && px1.getGProperty() == want.getGProperty()
                         && px1.getBProperty() == want.getBProperty();
            std::printf("[%s] NPOT %s row %d: got=(%d,%d,%d) want=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", label, y,
                px1.getRProperty(), px1.getGProperty(), px1.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            if (!ok) result_ = 1;
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        RunCase(3, 5, "3x5");
        RunCase(7, 11, "7x11");

        Exit();
    }

public:
    SdlNpotTextureTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(22);
        gdm_->setPreferredBackBufferHeightProperty(22);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlNpotTextureTest game;
    game.Run();
    return game.getResult();
}
