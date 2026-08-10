// SPDX-License-Identifier: MS-PL
// CNA::GraphicsCapability: verifies GraphicsDevice::SupportsCapability() correctly reports DIRECTX8's
// capability set. DIRECTX8's 3D pipeline is genuinely real end-to-end -- ThreeD and DepthStencilBuffer
// both report true. What remains false is genuinely unavailable at this DirectX era (MSAA/MRT/
// occlusion query/custom effects). WireFrame reports true (D3DFILL_WIREFRAME genuinely renders
// edge-only output). Unlike DX2-DIRECTX7's own software rasterizer, AnisotropicFiltering also reports
// true here -- DIRECTX8 runs on a real GPU via DXVK (see DirectX8Renderer.hpp's own
// SupportsCapability() comment for the full rationale).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/DirectX8/DirectX8Renderer.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;
using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::RenderTargetBindingDescriptor;

class DirectX8GraphicsCapabilityTest : public Game
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
        auto& renderer = static_cast<CNA::Internal::Renderers::DirectX8::DirectX8Renderer&>(dev.GetRenderer());

        // Phase O6 completes the real 3D pipeline -- both report true now.
        check(dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD supported");
        check(dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer supported");

        // Genuinely unavailable at this DirectX era.
        check(!dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing not supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets not supported");
        check(!dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery not supported");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects not supported");

        // WireFrame is real (D3DFILL_WIREFRAME genuinely renders edge-only output). Unlike
        // DX2-DIRECTX7's own software rasterizer, AnisotropicFiltering also reports true -- DIRECTX8 runs
        // on a real GPU via DXVK.
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported");
        check(dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering supported (real GPU via DXVK)");

        // SupportsCapability() is a check, not an enforcement mechanism -- calling a method for a
        // genuinely-unsupported capability anyway still throws. MultipleRenderTargets (checked
        // above) is a real DIRECTX8 (and DIRECTX2/DIRECTX3, its own porting sources) boundary: DirectDraw has exactly one active render target.
        const RenderTargetBindingDescriptor twoTargets[2] = {
            RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0),
            RenderTargetBindingDescriptor::ForRenderTarget2D(nullptr, 0, 4, 4, 0)};
        check(Throws([&] { renderer.SetRenderTargets(twoTargets, 2); }),
              "SetRenderTargets(count=2) still throws (no MRT support)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DirectX8GraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DirectX8GraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
