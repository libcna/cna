// SPDX-License-Identifier: MS-PL
// CNA::GraphicsCapability: verifies GraphicsDevice::SupportsCapability() reports OpenGL2's real
// capability set. Unlike EasyGL/GLES3 (which has no wireframe fill mode at all --
// GraphicsDeviceCapabilityTests.cpp's own DoesNotSupportWireFrame), desktop GL 2.1's
// compatibility profile genuinely supports glPolygonMode(GL_FRONT_AND_BACK, GL_LINE), already
// wired in ApplyRasterizerState() -- so unlike that shared, EasyGL-specific gtest (whose own
// header comment says it "only ever builds against a fully 3D-capable backend (EasyGL by default
// on Linux)"), this backend-specific test asserts WireFrame=true instead.
//
// MultiSampleAntiAliasing/AnisotropicFiltering/Instancing are genuinely device/driver-dependent --
// same treatment as GraphicsDeviceCapabilityTests.cpp's own
// MultiSampleAntiAliasingQueryDoesNotThrow: don't assert a specific value, just that querying
// doesn't throw. Instancing specifically reflects GL_ARB_draw_instanced/GL_ARB_instanced_arrays
// presence -- see opengl2_instancedmodel_test.cpp for the actual hardware-instancing proof.
//
// MultipleRenderTargets: real MRT via a shared FBO (glFramebufferTexture2D per target +
// glDrawBuffers), see OpenGL2GraphicsBackend::SetRenderTargets -- asserted true below; see
// opengl2_mrt_test.cpp for the actual multi-attachment rendering proof.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

class OpenGL2GraphicsCapabilityTest : public Game
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
        check(dev.SupportsCapability(GraphicsCapability::MultipleRenderTargets),
              "MultipleRenderTargets supported (real shared-FBO MRT)");
        check(dev.SupportsCapability(GraphicsCapability::OcclusionQuery), "OcclusionQuery supported");
        check(dev.SupportsCapability(GraphicsCapability::CustomEffects), "CustomEffects supported");
        check(dev.SupportsCapability(GraphicsCapability::WireFrame),
              "WireFrame supported (unlike EasyGL/GLES3 -- desktop GL 2.1 has glPolygonMode(GL_LINE))");

        bool threw = false;
        try { (void)dev.SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing); }
        catch (const std::exception&) { threw = true; }
        check(!threw, "MultiSampleAntiAliasing query does not throw");

        threw = false;
        try { (void)dev.SupportsCapability(GraphicsCapability::AnisotropicFiltering); }
        catch (const std::exception&) { threw = true; }
        check(!threw, "AnisotropicFiltering query does not throw");

        threw = false;
        try { (void)dev.SupportsCapability(GraphicsCapability::Instancing); }
        catch (const std::exception&) { threw = true; }
        check(!threw, "Instancing query does not throw (GL_ARB_draw_instanced/GL_ARB_instanced_arrays presence, device/driver-dependent)");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL2GraphicsCapabilityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2GraphicsCapabilityTest game;
    game.Run();
    return game.getResult();
}
