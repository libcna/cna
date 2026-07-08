// SPDX-License-Identifier: MS-PL
// Task 679: SDL_Renderer Texture2D::SetData partial-rectangle region pixel test.
//
// Task 678 found that Texture2D::GetData is implemented entirely via a CPU-side cache, so a pure
// SetData()-then-GetData() round trip (like the existing backend-agnostic
// examples/easygl_texture2d_partial_rect_test.cpp, which does no framebuffer readback at all) is
// guaranteed correct identically on every backend by construction -- not something SDL_Renderer-
// specific to verify. The genuinely backend-specific question here: Texture2D::SetData's PARTIAL
// overload (SetData(level, rect, data, startIndex, elementCount)) merges the sub-rect write into
// the full per-level CPU buffer, then calls SdlTextureBackend::UpdatePixels() -- which always
// re-uploads via SDL_UpdateTexture(texture, nullptr /* whole texture */, rgba, stride), i.e. the
// WHOLE merged buffer, not just the sub-rect. This test confirms that "always re-upload the
// whole buffer" approach genuinely produces the correct GPU-visible result end-to-end (not just
// a correct CPU-side cache).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Design: a 4x4 texture, filled entirely Red via the full-array SetData overload, then a 2x2 Blue
// sub-rect written at (1,1) via the partial SetData overload. Drawn scaled up (16x16 destination,
// 4 screen pixels per texel, PointClamp sampling) via SpriteBatch:
//
//     R R R R
//     R B B R
//     R B B R
//     R R R R
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool colourMatch(Color got, Color want, int tol = 40)
{
    return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
        && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
        && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
}

static const Color kRed  (255,   0,   0, 255);
static const Color kBlue (  0,   0, 255, 255);

class SdlTexture2DPartialRectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 0;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_  = std::make_unique<SpriteBatch>(dev);
        tex_ = std::make_unique<Texture2D>(dev, 4, 4);

        std::vector<Color> allRed(16, kRed);
        tex_->SetData(0, nullptr, allRed.data(), 0, 16);

        const Rectangle subRect(1, 1, 2, 2);
        const Color blue4[4] = { kBlue, kBlue, kBlue, kBlue };
        tex_->SetData(0, &subRect, blue4, 0, 4);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        // 4x4 source scaled 4x per texel -> 16x16 destination.
        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*tex_, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 4, 4), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  2,  2, kRed,  "(0,0) corner texel -> Red"  },
            { 14,  2, kRed,  "(3,0) corner texel -> Red"  },
            {  2, 14, kRed,  "(0,3) corner texel -> Red"  },
            { 14, 14, kRed,  "(3,3) corner texel -> Red"  },
            {  6,  6, kBlue, "(1,1) sub-rect texel -> Blue" },
            { 10,  6, kBlue, "(2,1) sub-rect texel -> Blue" },
            {  6, 10, kBlue, "(1,2) sub-rect texel -> Blue" },
            { 10, 10, kBlue, "(2,2) sub-rect texel -> Blue" },
        };

        for (const auto& c : checks)
        {
            Color px(0, 0, 0, 0);
            Rectangle reg(c.x, c.y, 1, 1);
            dev.GetBackBufferData(&reg, &px, 0, 1);
            const bool ok = colourMatch(px, c.want);
            std::printf("[%s] %s: got=(%d,%d,%d)\n",
                ok ? "PASS" : "FAIL", c.label,
                px.getRProperty(), px.getGProperty(), px.getBProperty());
            if (!ok) result_ = 1;
        }

        Exit();
    }

public:
    SdlTexture2DPartialRectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTexture2DPartialRectTest game;
    game.Run();
    return game.getResult();
}
