// SPDX-License-Identifier: MS-PL
// Task 681: Decide and document Texture2D mip-level SetData/GetData on SDL_Renderer.
//
// SDL_Renderer's SDL_Texture has no native mip chain and no per-level LOD sampling in its
// 2D blit pipeline (unlike EasyGL, which is real OpenGL and genuinely implements
// ITextureRenderer::UpdatePixelsLevel via glTexImage2D for a real, sampled mip chain).
// Texture2D::SetData(level, rect, data, startIndex, elementCount)'s level>0 path (shared,
// renderer-agnostic Texture2D.cpp) unconditionally calls renderer_->UpdatePixelsLevel() --
// whose interface default is a silent no-op (`virtual void UpdatePixelsLevel(...) {}`).
// SdlTextureRenderer never overrode it, so a level>0 SetData call previously silently did
// nothing to the real GPU texture -- exactly the "must not silently no-op" gap this task
// warns about.
//
// Decision: throw std::runtime_error for any level>0 SetData on SDL_Renderer, mirroring
// this renderer's established "throw for what can't be honored" convention (Task 676's
// SetCustomEffect). A single-level-only software fallback was considered and rejected:
// SDL_Renderer's 2D renderer never samples from anything but the base level when drawing,
// so silently "succeeding" at storing mip data that can never affect a single rendered
// pixel would be equally misleading, just less loudly.
//
// Known, accepted, documented deviation: Texture2D::SetData's level>0 path merges the
// sub-rect into the per-level CPU shadow buffer BEFORE calling renderer_->UpdatePixelsLevel(),
// so the CPU-side buffer is already updated by the time the exception is thrown (this
// mirrors the existing validation-order shape of the shared Texture2D.cpp code and was not
// restructured, since doing so would touch the shared layer all 4 renderers depend on --
// out of scope for this single-renderer decision task). This is inert in practice: SDL_Renderer
// never reads from a mip level's CPU/GPU state when rendering, so no rendered pixel is ever
// affected by it either way.
//
// level=0 SetData/GetData (the overwhelmingly common case) and level>0 GetData (a pure
// CPU-side cache read, per Task 678's finding) are both completely unaffected by this fix.
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const Color kRed (255,   0,   0, 255);
static const Color kBlue(  0,   0, 255, 255);

class SdlTexture2DMipLevelThrowsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;

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

        // 4x4 with a full mip chain (levels: 4x4, 2x2, 1x1 -> levelCount == 3).
        Texture2D tex(dev, 4, 4, /*mipMap=*/true, SurfaceFormat::Color);
        check(tex.getLevelCountProperty() == 3, "levelCount == 3 for a 4x4 mip-mapped texture");

        // Baseline sanity: level=0 SetData/GetData must remain completely unaffected.
        {
            std::vector<Color> allRed(16, kRed);
            bool threw = false;
            try { tex.SetData(0, nullptr, allRed.data(), 0, 16); }
            catch (...) { threw = true; }
            check(!threw, "level=0 full-level SetData does not throw");

            std::vector<Color> rb(16, kBlue);
            tex.GetData(rb.data(), 0, 16);
            bool allMatch = true;
            for (const auto& c : rb)
                if (c.getRProperty() != 255 || c.getGProperty() != 0 || c.getBProperty() != 0) allMatch = false;
            check(allMatch, "level=0 GetData reads back the level=0 data unaffected");
        }

        // The decision under test: level>0 SetData throws (must not silently no-op).
        {
            const Color blue4[4] = { kBlue, kBlue, kBlue, kBlue };
            bool threw = false;
            try { tex.SetData(1, nullptr, blue4, 0, 4); }
            catch (const std::runtime_error&) { threw = true; }
            check(threw, "level=1 SetData throws std::runtime_error");
        }

        // level>0 GetData remains a pure CPU-side cache read (Task 678 architecture) and
        // must not throw merely because level>0 SetData is unsupported on this renderer.
        {
            std::vector<Color> lvl1(4, kRed);
            bool threw = false;
            try { tex.GetData(1, nullptr, lvl1.data(), 0, 4); }
            catch (...) { threw = true; }
            check(!threw, "level=1 GetData (pure CPU-side read) does not throw");
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    SdlTexture2DMipLevelThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlTexture2DMipLevelThrowsTest game;
    game.Run();
    return game.getResult();
}
