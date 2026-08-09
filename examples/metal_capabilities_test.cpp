// SPDX-License-Identifier: MS-PL
// One assertion per current CNA::GraphicsCapability. This is intentionally conservative: known
// broken or unadapted paths assert false so adding a backend enum cannot inherit a default true.
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

        check(dev.SupportsCapability(GraphicsCapability::ThreeD), "ThreeD supported");
        check(dev.SupportsCapability(GraphicsCapability::DepthStencilBuffer), "DepthStencilBuffer supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing unsupported");
        check(!dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets unsupported");
        check(dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering supported");
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported");
        check(dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery supported");
        check(!dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects unsupported");
        check(dev.SupportsCapability(GraphicsCapability::Texture3D), "Texture3D supported");
        check(!dev.SupportsCapability(GraphicsCapability::MultiStreamVertexInput), "MultiStreamVertexInput unsupported");
        check(!dev.SupportsCapability(GraphicsCapability::Instancing), "Instancing unsupported");
        check(dev.SupportsCapability(GraphicsCapability::StencilBuffer), "StencilBuffer supported");
        check(dev.SupportsCapability(GraphicsCapability::AdditiveBlending), "AdditiveBlending supported");

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
