// SPDX-License-Identifier: MS-PL
// CNA::GraphicsCapability: verifies GraphicsDevice::SupportsCapability() correctly reports false
// for every currently-enumerated capability -- CNA::GraphicsCapability::ThreeD's own documented
// definition bundles vertex/index buffers, 3D draw calls, AND depth/stencil clears/state as one
// flag (see Dx2GraphicsBackend.hpp's own comment), so it stays false even though real geometry
// drawing is now genuinely implemented (Phase O4/O5, plan_dx2.md) -- state APPLICATION
// (SetDepthTestEnabled/ApplyRasterizerState/etc, Phase O6) is not. This test also confirms calling
// the still-unimplemented state-toggle methods anyway still throws (SupportsCapability() is a way
// to check ahead of time, not a way to make the underlying call itself succeed). Twin of
// sdlrenderer_graphics_capability_test.cpp/canvas_graphics_capability_test.cpp.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

class Dx2GraphicsCapabilityTest : public Game
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

        check(!dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD not supported");
        check(!dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets not supported");
        check(!dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering not supported");
        check(!dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame not supported");
        check(!dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery not supported");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects not supported");

        // SupportsCapability() is a check, not an enforcement mechanism -- calling the actual 3D
        // state-toggle methods anyway still throws exactly as before this feature existed. Unlike
        // an earlier version of this test, constructing a VertexBuffer is deliberately NOT checked
        // here anymore -- Phase O5 made vertex/index buffer construction genuinely real.
        check(Throws([&] { dev.SetDepthTestEnabled(true); }),
              "SetDepthTestEnabled still throws when called without checking first");
        check(Throws([&] { dev.SetBlendEnabled(true); }),
              "SetBlendEnabled still throws when called without checking first");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    Dx2GraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    Dx2GraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
