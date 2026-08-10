// SPDX-License-Identifier: MS-PL
// Task 918: verify the real GL_EXT_texture_filter_anisotropic wiring added to meta-gl/easy-gl
// (SamplerParameter::MaxAnisotropy, GetParameter::MaxTextureMaxAnisotropy) actually reaches the
// driver, rather than trusting the startup capability-dump log alone.
//
// This talks to easy-gl's own Sampler class directly (not through CNA's GraphicsDevice/
// EasyGLRenderer), inside a real GL context established by Game::Run(). It sets
// SamplerParameter::MaxAnisotropy to a specific value, reads it straight back via
// glGetSamplerParameterfv (get_parameter_fv), and confirms the driver actually stored it —
// proving the new enum entries are wired to the real GL calls, not silently ignored.
//
// A genuine over-cap request (Task 299's own 9999 case) is also exercised here directly against
// the raw Sampler API to confirm the driver clamps it to a sane value (rather than storing 9999
// verbatim or erroring), independent of CNA's own EasyGLRenderer::ApplySamplerState clamp.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <metagl/Capabilities.hpp>
#include <easygl/Sampler.hpp>

#include <cmath>
#include <cstdio>

using namespace Microsoft::Xna::Framework;

namespace
{
    bool nearlyEqual(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }
}

class AnisotropicGlStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0, fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (!metagl::HasExtension("GL_EXT_texture_filter_anisotropic"))
        {
            std::printf("[SKIP] GL_EXT_texture_filter_anisotropic not present in this GL context "
                        "-- nothing to verify here (Task 918's fallback path is intentional).\n");
            Exit();
            return;
        }

        GLfloat driverMax = 1.0f;
        metagl::glGetFloatv(::metagl::GetParameter::MaxTextureMaxAnisotropy, &driverMax);
        check(driverMax >= 1.0f, "Driver reports a real MaxTextureMaxAnisotropy cap >= 1.0");

        easygl::Sampler s;
        s.create();

        const float requested = 4.0f;
        s.set_parameter(::easygl::SamplerParameter::MaxAnisotropy, requested);
        float readBack = -1.0f;
        s.get_parameter_fv(::easygl::SamplerParameter::MaxAnisotropy, &readBack);
        check(nearlyEqual(readBack, requested),
              "SamplerParameter::MaxAnisotropy round-trips a real, sub-cap value through the driver");

        // Task 299's own extreme value, exercised directly against the raw Sampler API this time.
        s.set_parameter(::easygl::SamplerParameter::MaxAnisotropy, 9999.0f);
        float readBackExtreme = -1.0f;
        s.get_parameter_fv(::easygl::SamplerParameter::MaxAnisotropy, &readBackExtreme);
        check(readBackExtreme <= driverMax + 0.01f,
              "Driver clamps an over-cap MaxAnisotropy request to its own real cap, not 9999 verbatim");

        s.destroy();

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    AnisotropicGlStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(4);
        gdm_->setPreferredBackBufferHeightProperty(4);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    AnisotropicGlStateTest game;
    game.Run();
    return game.getResult();
}
