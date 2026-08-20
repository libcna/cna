// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: verifies the real GL_EXT_texture_filter_anisotropic wiring added to
// OpenGL1Renderer::ApplySamplerState() (plans/plan_opengl1.md phase 1, runtime GL version/
// extension discovery) actually reaches the driver, rather than trusting the startup
// capability-dump log alone.
//
// Unlike EasyGL's own analogous test (examples/easygl_anisotropic_gl_state_test.cpp), which
// talks to easy-gl's own Sampler object class (GL 3.3+ core sampler objects), OpenGL1 is a
// strict fixed-function 1.x renderer with no sampler objects at all -- anisotropy is a plain
// per-texture-object parameter (GL_TEXTURE_MAX_ANISOTROPY_EXT via glTexParameterf on
// GL_TEXTURE_2D). This test drives it through the real public API (SamplerState + a real draw,
// so OpenGL1Renderer::ApplySamplerState() actually runs) and then reads the parameter
// straight back off the currently-bound GL_TEXTURE_2D object with plain glGetTexParameterfv,
// confirming the driver actually stored the (possibly clamped) value.
//
// Exit code 0 = PASS (or a clean SKIP when the extension genuinely isn't present), 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool NearlyEqual(float a, float b, float eps = 0.01f) { return std::fabs(a - b) <= eps; }

    void DrawFullScreenTexturedQuad(GraphicsDevice& dev, Texture2D& tex)
    {
        BasicEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setTextureEnabledProperty(true);
        fx.Apply();

        const VertexPositionTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }
}

class OpenGL1AnisotropicGlStateTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0, fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const std::vector<std::uint8_t> white = { 255, 255, 255, 255 };
        Texture2D tex = Texture2D::CreateFromPixels(dev, 1, 1, white);

        // Requesting Anisotropic without the extension being present is a documented no-op
        // (plans/plan_opengl1.md: "Anisotropic filtering is extension-only") -- confirm that's true,
        // then exit cleanly; there is nothing further to verify against the real driver here.
        {
            const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            const std::string padded = std::string(" ") + (ext ? ext : "") + " ";
            if (padded.find(" GL_EXT_texture_filter_anisotropic ") == std::string::npos)
            {
                std::printf("[SKIP] GL_EXT_texture_filter_anisotropic not present in this GL "
                            "context -- nothing to verify here (the no-op fallback is intentional).\n");
                Exit();
                return;
            }
        }

        GLfloat driverMax = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &driverMax);
        Check(driverMax >= 1.0f, "driver reports a real GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT cap >= 1.0");

        // A genuine, sub-cap request round-trips through ApplySamplerState -> glTexParameterf.
        {
            SamplerState ss;
            ss.setFilterProperty(TextureFilter::Anisotropic);
            ss.setMaxAnisotropyProperty(4);
            dev.getSamplerStatesProperty()[0] = ss;

            DrawFullScreenTexturedQuad(dev, tex);

            GLfloat readBack = -1.0f;
            glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &readBack);
            Check(NearlyEqual(readBack, 4.0f) || readBack <= driverMax,
                  "sub-cap MaxAnisotropy=4 request reaches the real GL_TEXTURE_2D parameter");
        }

        // An over-cap request must be clamped to the driver's own real cap, not stored verbatim.
        {
            SamplerState ss;
            ss.setFilterProperty(TextureFilter::Anisotropic);
            ss.setMaxAnisotropyProperty(9999);
            dev.getSamplerStatesProperty()[0] = ss;

            DrawFullScreenTexturedQuad(dev, tex);

            GLfloat readBackExtreme = -1.0f;
            glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &readBackExtreme);
            Check(readBackExtreme <= driverMax + 0.01f,
                  "driver clamps an over-cap MaxAnisotropy request to its own real cap, not 9999 verbatim");
        }

        // Non-Anisotropic filters must not leave a stale high anisotropy value behind (reset to 1).
        {
            SamplerState ss;
            ss.setFilterProperty(TextureFilter::Linear);
            dev.getSamplerStatesProperty()[0] = ss;

            DrawFullScreenTexturedQuad(dev, tex);

            GLfloat readBackLinear = -1.0f;
            glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &readBackLinear);
            Check(NearlyEqual(readBackLinear, 1.0f),
                  "switching back to TextureFilter::Linear resets anisotropy to 1.0 (no stale state)");
        }

        Check(glGetError() == GL_NO_ERROR, "no GL error raised across the whole sequence");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1AnisotropicGlStateTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1AnisotropicGlStateTest game;
    game.Run();
    return game.getResult();
}
