// SPDX-License-Identifier: MS-PL
// One assertion per current CNA::GraphicsCapability. This is intentionally conservative: known
// broken or unadapted paths assert false so adding a renderer enum cannot inherit a default true.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
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
        // plans/plan_apple_m4.md AM4-141: every Metal GPU multisamples at 4x (supportsTextureSampleCount).
        check(dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing), "MultiSampleAntiAliasing supported");
        // plans/plan_apple_m4.md AM4-097: asked of the renderer, like Texture3D below -- the device's
        // answer folds in the profile (RLGL-040), and Reach, the default here, has one render target.
        check(dev.GetRenderer().SupportsCapability(GraphicsCapability::MultipleRenderTargets), "MultipleRenderTargets supported");
        check(dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering), "AnisotropicFiltering supported");
        check(dev.SupportsCapability(GraphicsCapability::WireFrame), "WireFrame supported");
        check(dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery supported");
        // plans/plan_apple_m4.md AM4-077: SpriteBatch-scoped MSL, see Metal_SpriteBatch_CustomEffect.
        check(dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects supported");
        // The renderer's contract, asked of the renderer: the device's answer also folds in the
        // graphics profile (BINDFIX-037), and Reach -- the default here -- has no volume textures.
        check(dev.GetRenderer().SupportsCapability(GraphicsCapability::Texture3D), "Texture3D supported");
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
