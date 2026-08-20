// SPDX-License-Identifier: MS-PL
// plans/plan_d3d10.md: verifies GraphicsDevice::SupportsCapability() correctly reports D3D10's
// capability set (plans/plan_d3d10.md design decision 5). ThreeD/DepthStencilBuffer/WireFrame/
// AnisotropicFiltering/MultipleRenderTargets all report true (real GPU via DXVK, real
// OMSetRenderTargets(count>1,...) support -- a genuine difference from every DIRECTX1..DIRECTX8 renderer).
// OcclusionQuery/CustomEffects report false -- out of this v1's scope (plans/plan_d3d10.md design
// decision 3), not a hardware limitation.
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

class D3D10GraphicsCapabilityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        check(dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD supported");
        check(dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer supported");
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported");
        check(dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering supported (real GPU via DXVK)");
        check(dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets),
              "MultipleRenderTargets supported (real, unlike every DIRECTX1..DIRECTX8 renderer)");
        check(!dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery not supported (out of this v1's scope)");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects not supported (out of this v1's scope)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    D3D10GraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }
    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    D3D10GraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
