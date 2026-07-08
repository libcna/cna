// SPDX-License-Identifier: MS-PL
// Task 727: Verify OcclusionQuery::Begin/End throw correctly on SDL_Renderer.
//
// Found and fixed a real bug while investigating: SdlGraphicsBackend never overrode
// CreateOcclusionQuery, so it fell through to IGraphicsBackend's own default (silent nullptr, no
// throw) -- OcclusionQuery construction silently succeeded with a permanently-null backend, and
// Begin()/End() both guard with `if (backend_) ...`, silently no-op-ing instead of ever running a
// real occlusion query. Every OTHER 3D-only entry point on this backend (CreateVertexBuffer,
// CreateIndexBuffer16, DrawColoredPrimitives, etc.) throws loudly and immediately -- this was the
// one inconsistent silent gap. Confirmed safe to fix (unlike Task 725's Texture3D/TextureCube,
// which has a 94-test blast radius): OcclusionQuery's only existing test coverage is
// EasyGL_OcclusionQuery_Cycle (examples/occlusion_query_test.cpp), registered ONLY for the EasyGL
// backend -- zero existing SDL_Renderer-run tests construct an OcclusionQuery at all.
//
// Fixed by adding SdlGraphicsBackend::CreateOcclusionQuery() -> ThrowNo3D("CreateOcclusionQuery"),
// mirroring CreateVertexBuffer/CreateIndexBuffer16's exact pattern (Tasks 720-723). Since
// construction itself now throws first, Begin()/End() can never actually be reached on a valid
// instance on this backend -- same shape as Task 720's DrawPrimitives/VertexBuffer finding.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlOcclusionQueryThrowsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template <typename F>
    static bool ThrowsExactRuntimeError(F&& fn, const std::string& expectedMessage)
    {
        try
        {
            fn();
            return false;
        }
        catch (const std::runtime_error& e)
        {
            return std::strcmp(e.what(), expectedMessage.c_str()) == 0;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        check(ThrowsExactRuntimeError(
                  [&] { OcclusionQuery q(dev); (void)q; },
                  "SDL_Renderer does not support 3D: CreateOcclusionQuery"),
              "OcclusionQuery construction throws the exact expected message on SDL_Renderer");

        // --- Since construction always throws, Begin()/End() can never be reached on a valid
        //     instance -- there is no way to construct one to call them on. This mirrors Task
        //     720's DrawPrimitives/VertexBuffer finding exactly. ---

        // --- The device must remain fully usable after the throw above. ---
        dev.Clear(Color(0, 255, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional after the throw above");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlOcclusionQueryThrowsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    SdlOcclusionQueryThrowsTest game;
    game.Run();
    return game.getResult();
}
