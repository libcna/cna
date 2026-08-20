// SPDX-License-Identifier: MS-PL
// CNA::GraphicsCapability: verifies GraphicsDevice::SupportsCapability() correctly reports DIRECTX6's
// capability set. Unlike an earlier version of this test (mirroring a 2D-only renderer's
// capability test, matching DIRECTX1/SDL_RENDERER/CANVAS), DIRECTX6's 3D pipeline is now genuinely real
// end-to-end (Phase O3-O6, plans/plan_dx2.md) -- ThreeD and DepthStencilBuffer both report true. What
// remains false is genuinely unavailable at this DirectX era (MSAA/MRT/occlusion query/custom
// effects), or empirically confirmed absent on this software RGB device (AnisotropicFiltering,
// Phase O9's dx2_spike10 Test E). WireFrame reports true as of Phase O9 -- the same spike's Test D
// confirmed D3DFILL_WIREFRAME genuinely renders edge-only output here (see DirectX6Renderer.hpp's
// own SupportsCapability() comment for the full rationale).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/DirectX6/DirectX6Renderer.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;
using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::RenderTargetBindingDescriptor;

class DirectX6GraphicsCapabilityTest : public Game
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
        auto& renderer = static_cast<CNA::Internal::Renderers::DirectX6::DirectX6Renderer&>(dev.GetRenderer());

        // Phase O6 completes the real 3D pipeline -- both report true now.
        check(dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD supported");
        check(dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer supported");

        // Genuinely unavailable at this DirectX era.
        check(!dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets not supported");
        check(!dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery not supported");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects not supported");

        // Phase O9 (dx2_spike10_specular_wireframe_aniso.cpp): WireFrame is real (Test D --
        // D3DFILL_WIREFRAME genuinely renders edge-only output), AnisotropicFiltering is
        // empirically confirmed absent (Test E -- byte-identical readback across POINT/LINEAR/
        // ANISOTROPIC filters on this software RGB device), not merely "never tested" anymore.
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported (Phase O9, empirically verified)");
        check(!dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering not supported (Phase O9, empirically confirmed absent)");

        // SupportsCapability() is a check, not an enforcement mechanism -- calling a method for a
        // genuinely-unsupported capability anyway still throws. MultipleRenderTargets (checked
        // above) is a real DIRECTX6 (and DIRECTX2/DIRECTX3, its own porting sources) boundary: DirectDraw has exactly one active render target.
        const RenderTargetBindingDescriptor twoTargets[2] = {
            RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
            RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0)};
        check(Throws([&] { renderer.SetRenderTargets(twoTargets, 2); }),
              "SetRenderTargets(count=2) still throws (no MRT support)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DirectX6GraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DirectX6GraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
