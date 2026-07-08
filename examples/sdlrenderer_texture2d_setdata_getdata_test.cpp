// SPDX-License-Identifier: MS-PL
// Task 678: SDL_Renderer Texture2D::SetData/GetData full-array round-trip audit.
//
// Texture2D::GetData is implemented entirely via a CPU-side cache (cpuPixels_, maintained in the
// shared, backend-agnostic Texture2D.cpp) -- it never queries the GPU texture at all, so a plain
// SetData()-then-GetData() round trip is guaranteed correct identically on every backend by
// construction, not something specific to SDL_Renderer to verify. The genuinely backend-specific
// question this task's own hint ("SDL_UpdateTexture/SDL_LockTexture path") points at is whether
// the REAL GPU texture SDL_Renderer creates from SetData's full-array overload (which calls
// GraphicsDevice::GetBackend().CreateTexture(), constructing a brand-new SdlTextureBackend via
// SDL_CreateTexture()+SDL_UpdateTexture()) actually contains the correct pixels when drawn --
// not just whatever the CPU-side cache happens to report.
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this backend's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
//
// Design: a 2x2 texture, 4 distinct solid-colour pixels (Red/Green/Blue/Yellow), written via the
// full-array Texture2D::SetData(Color*, int) overload, then:
//   1. GetData(Color*, int) readback must return the exact same 4 colours in the same order.
//   2. Drawing the texture (scaled up, PointClamp sampling) via SpriteBatch and reading back the
//      REAL rendered pixels must also show the correct colour in each quadrant -- proving the
//      real GPU texture SDL_Renderer created actually contains the right data, not just the CPU
//      cache.
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

static const Color kRed   (255,   0,   0, 255);
static const Color kGreen (  0, 255,   0, 255);
static const Color kBlue  (  0,   0, 255, 255);
static const Color kYellow(255, 255,   0, 255);

class SdlTexture2DSetDataGetDataTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             tex_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_  = std::make_unique<SpriteBatch>(dev);
        tex_ = std::make_unique<Texture2D>(dev, 2, 2);

        // 2x2, [Red Green / Blue Yellow] (row-major: index 0=TL, 1=TR, 2=BL, 3=BR).
        const Color pixels[4] = { kRed, kGreen, kBlue, kYellow };
        tex_->SetData(pixels, 4);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // 1. GetData readback must match exactly what was written.
        Color readBack[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
        tex_->GetData(readBack, 4);
        check(readBack[0] == kRed,    "GetData[0] (TL) == Red");
        check(readBack[1] == kGreen,  "GetData[1] (TR) == Green");
        check(readBack[2] == kBlue,   "GetData[2] (BL) == Blue");
        check(readBack[3] == kYellow, "GetData[3] (BR) == Yellow");

        // 2. Draw the real GPU texture (scaled 32x per source pixel) and read back real pixels --
        // proves the actual SDL texture SetData created contains the right data.
        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);

        sb_->Begin(SpriteSortMode::Deferred,
                   BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp),
                   nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        sb_->Draw(*tex_, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 2, 2), Color::White);
        sb_->End();

        struct Check { int x; int y; Color want; const char* label; };
        const Check checks[] = {
            { 16, 16, kRed,    "Rendered TL quadrant -> Red"    },
            { 48, 16, kGreen,  "Rendered TR quadrant -> Green"  },
            { 16, 48, kBlue,   "Rendered BL quadrant -> Blue"   },
            { 48, 48, kYellow, "Rendered BR quadrant -> Yellow" },
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
    SdlTexture2DSetDataGetDataTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTexture2DSetDataGetDataTest game;
    game.Run();
    return game.getResult();
}
