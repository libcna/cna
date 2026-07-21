// SPDX-License-Identifier: MS-PL
// plan_metal.md METAL-198: one assertion per CNA::GraphicsCapability, meant to be extended
// incrementally as each phase's real behavior lands, not written once and left stale. Twin of
// dx3_graphics_capability_test.cpp/sdlrenderer_graphics_capability_test.cpp/
// canvas_graphics_capability_test.cpp, but for a fully 3D-capable backend expected to support
// every currently-enumerated capability (unlike those three, which are intentionally 2D-only) --
// this session landed real MultipleRenderTargets (METAL-112), CustomEffects (Phase 14), and
// MultiSampleAntiAliasing (METAL-104/105) support, so this is also a real regression guard for
// those 3 SupportsCapability() flags specifically, each of which was a stale false-positive
// `false` at some point this session before its own phase actually landed.
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

class MetalGraphicsCapabilityTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // plan_metal.md METAL-189/190: inherited-default cases, confirmed correct by earlier
        // inspection (Phase 20) -- real assertions here, not just trusted from that inspection.
        check(dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD supported");
        check(dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer supported");

        // plan_metal.md METAL-191: real, device-clamped MSAA landed this session (METAL-104/105) --
        // was a stale `false` earlier in the same session before that phase actually landed.
        check(dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing supported");

        // plan_metal.md METAL-192: real MRT landed this session (METAL-112) -- was also a stale
        // `false` left behind for one commit even after METAL-112 landed, caught and fixed
        // alongside MultiSampleAntiAliasing's own identical case.
        check(dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets supported");

        // plan_metal.md METAL-193: real anisotropic filtering via the sampler cache's own
        // maxAnisotropy application.
        check(dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering supported");

        // plan_metal.md METAL-194: FillMode::WireFrame -> MTLTriangleFillModeLines is a real,
        // already-shipping mapping -- unlike EasyGL (GLES3 has no wireframe fill mode at all,
        // confirmed false on that backend's own GraphicsDeviceCapabilityTest), Metal genuinely
        // supports this.
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported");

        // plan_metal.md METAL-136-139: real occlusion queries via MTLVisibilityResultBuffer.
        check(dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery supported");

        // plan_metal.md METAL-150: real custom ShaderEffect support landed this session (Phase 14,
        // SpriteBatch-scoped) -- was a stale `false` earlier in the same session before that phase
        // actually landed.
        check(dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects supported");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    MetalGraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    MetalGraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
