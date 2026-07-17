// SPDX-License-Identifier: MS-PL
// CNA::UnsupportedGraphicsCallBehavior: verifies DX3's 2D-only "fire and forget" 3D calls
// (state toggles, clears, draws) can be configured to silently no-op instead of throwing,
// while resource-creation calls (VertexBuffer/IndexBuffer construction) always throw regardless
// -- see that enum's own header comment for why the split is deliberate. Twin of
// sdlrenderer_unsupported_call_behavior_test.cpp/canvas_unsupported_call_behavior_test.cpp.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/UnsupportedGraphicsCallBehavior.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::UnsupportedGraphicsCallBehavior;

class Dx3UnsupportedCallBehaviorTest : public Game
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
    static bool Throws(F&& fn)
    {
        try { fn(); return false; }
        catch (const std::runtime_error&) { return true; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        check(dev.GetUnsupportedGraphicsCallBehavior() == UnsupportedGraphicsCallBehavior::Throw,
              "Default behavior is Throw");

        check(Throws([&] { dev.SetDepthTestEnabled(true); }),
              "SetDepthTestEnabled throws by default");

        dev.SetUnsupportedGraphicsCallBehavior(UnsupportedGraphicsCallBehavior::Ignore);
        check(dev.GetUnsupportedGraphicsCallBehavior() == UnsupportedGraphicsCallBehavior::Ignore,
              "GetUnsupportedGraphicsCallBehavior reflects Ignore after being set");

        check(!Throws([&] { dev.SetDepthTestEnabled(true); }),
              "SetDepthTestEnabled no longer throws once Ignore is set");
        check(!Throws([&] { dev.SetBlendEnabled(true); }),
              "SetBlendEnabled no longer throws once Ignore is set");

        // Resource creation always throws, deliberately ignoring this setting -- a silently
        // null-backed VertexBuffer would crash on first real use, worse than an immediate,
        // honest exception at construction time.
        check(Throws([&] { VertexBuffer vb(dev, 4); (void)vb; }),
              "Constructing a VertexBuffer still throws even with Ignore set");

        dev.SetUnsupportedGraphicsCallBehavior(UnsupportedGraphicsCallBehavior::Throw);
        check(dev.GetUnsupportedGraphicsCallBehavior() == UnsupportedGraphicsCallBehavior::Throw,
              "GetUnsupportedGraphicsCallBehavior reflects Throw after being set back");
        check(Throws([&] { dev.SetDepthTestEnabled(true); }),
              "SetDepthTestEnabled throws again after switching back to Throw");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    Dx3UnsupportedCallBehaviorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    Dx3UnsupportedCallBehaviorTest game;
    game.Run();
    return game.getResult();
}
