// SPDX-License-Identifier: MS-PL
// CNA::GraphicsCapability: verifies GraphicsDevice::SupportsCapability() correctly reports that
// Canvas (HTML Canvas 2D, 2D-only by design) supports none of the currently-enumerated
// capabilities, and that calling the corresponding 3D methods anyway still throws
// (SupportsCapability() is a way to check ahead of time, not a way to make the underlying call
// itself succeed). Twin of sdlrenderer_graphics_capability_test.cpp/freedirect_graphics_capability_test.cpp.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

class CanvasGraphicsCapabilityTest : public Game
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
        check(!dev.SupportsCapability(GraphicsCapability::StencilBuffer), "StencilBuffer not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets not supported");
        check(!dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering not supported");
        check(!dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame not supported");
        check(!dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery not supported");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects not supported");

        // SupportsCapability() is a check, not an enforcement mechanism -- calling the actual 3D
        // method anyway still throws exactly as before this feature existed.
        check(Throws([&] { dev.SetDepthTestEnabled(true); }),
              "SetDepthTestEnabled still throws when called without checking first");
        check(Throws([&] { VertexBuffer vb(dev, 4); (void)vb; }),
              "Constructing a VertexBuffer still throws when called without checking first");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    CanvasGraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    CanvasGraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
