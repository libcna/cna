// SPDX-License-Identifier: MS-PL
// Task 680: SDL_Renderer Texture2D::SetData startIndex/elementCount slice pixel test.
//
// The existing backend-agnostic examples/easygl_texture2d_partial_rect_test.cpp (Task 170)
// exercises the startIndex/elementCount slicing math with a pure SetData()-then-GetData()
// round trip and no framebuffer readback at all. Per Task 678's finding, Texture2D::GetData
// never touches the GPU texture (it is a CPU-side cache read), so that CPU-side slicing
// arithmetic (in the shared, backend-agnostic Texture2D::SetData(level, rect, data,
// startIndex, elementCount) -- "src = startIndex + row * w + col") is guaranteed correct
// identically on every backend by construction. The genuinely backend-specific question,
// mirroring Task 679: once that slice has been merged into the per-level CPU buffer and
// handed to SdlTextureBackend::UpdatePixels() (which always re-uploads the WHOLE merged
// buffer via SDL_UpdateTexture(texture, nullptr, ...)), does the REAL GPU texture actually
// contain the correct pixels when drawn -- not just what the CPU cache reports.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels
// operates in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Design: direct port of Task 170A's scenario. A 4x1 texture filled entirely Red, then a
// 6-element source array [Green, Green, Blue, Blue, Green, Green] sliced with
// startIndex=2/elementCount=2 into rect (1,0,2,1) -- only data[2]/data[3] (Blue) are written;
// the Green elements at either end of the source array must be skipped, not read:
//
//     R B B R
//
// Drawn scaled up (16x4 destination, 4 screen pixels per texel, PointClamp sampling) via
// SpriteBatch and read back from the real framebuffer.
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
static const Color kGreen(  0, 255,   0, 255);

class SdlTexture2DStartIndexTest : public Game
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
        tex_ = std::make_unique<Texture2D>(dev, 4, 1);

        std::vector<Color> allRed(4, kRed);
        tex_->SetData(0, nullptr, allRed.data(), 0, 4);

        // 6-element source array; startIndex=2, elementCount=2 -> only data[2]/data[3]
        // (Blue) are read. The Green elements at both ends must be skipped.
        const Color src6[6] = { kGreen, kGreen, kBlue, kBlue, kGreen, kGreen };
        const Rectangle rect2x1(1, 0, 2, 1);
        tex_->SetData(0, &rect2x1, src6, 2, 2);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        // 4x1 source scaled 4x per texel -> 16x4 destination.
        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*tex_, Rectangle(0, 0, 16, 4), Rectangle(0, 0, 4, 1), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            {  2, 2, kRed,  "texel(0,0) -> Red (unwritten)"  },
            {  6, 2, kBlue, "texel(1,0) -> Blue (data[2])"   },
            { 10, 2, kBlue, "texel(2,0) -> Blue (data[3])"   },
            { 14, 2, kRed,  "texel(3,0) -> Red (unwritten)"  },
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
    SdlTexture2DStartIndexTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(16);
        gdm_->setPreferredBackBufferHeightProperty(4);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTexture2DStartIndexTest game;
    game.Run();
    return game.getResult();
}
