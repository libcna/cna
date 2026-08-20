// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: BasicEffect specular highlights (plans/plan_opengl1.md item 15, EasyGL parity).
//
// Before this, GpuDrawParams::specularColor/specularPower/light0-2Specular were carried by every
// draw but never read by DrawInternal at all -- a game setting BasicEffect.SpecularColor got
// zero specular contribution, silently.
//
// Method: material DiffuseColor/AmbientLightColor/EmissiveColor are all BLACK, isolating the
// specular term completely -- if specular is genuinely computed, the surface reads bright; if
// not (or if SpecularColor is itself black), it reads pure black. A small quad is placed away
// from the origin along -Z with View=Identity/World=Identity, so GL_LIGHT_MODEL_LOCAL_VIEWER's
// "eye at the eye-space origin" placement sits roughly in front of the quad -- combined with a
// light shining toward -Z (same side as the viewer relative to the quad's +Z normal), this is the
// classic near-maximal Blinn-Phong configuration (light direction and view direction both close
// to the surface normal), giving a strong, driver-portable specular response instead of a
// razor-thin highlight that would be fragile to assert against.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport = 32;
}

class OpenGL1SpecularTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    // SpecularColor black vs white -- everything else held fixed. Returns the read-back centre
    // pixel's R channel (material specular is white, so R/G/B all move together; R alone suffices).
    int DrawAndReadCentre(GraphicsDevice& dev, const Vector3& specularColor)
    {
        dev.Clear(Color::Black, 1.0f);

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(-1.2f, 1.2f, -1.2f, 1.2f, 0.0f, -20.0f);

        const VertexPositionNormalTexture q[6] = {
            { Vector3(-1.0f, -1.0f, -10.0f), Vector3(0, 0, 1), Vector2(0, 0) },
            { Vector3(-1.0f,  1.0f, -10.0f), Vector3(0, 0, 1), Vector2(0, 1) },
            { Vector3( 1.0f,  1.0f, -10.0f), Vector3(0, 0, 1), Vector2(1, 1) },
            { Vector3(-1.0f, -1.0f, -10.0f), Vector3(0, 0, 1), Vector2(0, 0) },
            { Vector3( 1.0f,  1.0f, -10.0f), Vector3(0, 0, 1), Vector2(1, 1) },
            { Vector3( 1.0f, -1.0f, -10.0f), Vector3(0, 0, 1), Vector2(1, 0) },
        };

        BasicEffect fx(dev);
        fx.setWorldProperty(world);
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);
        fx.setTextureEnabledProperty(false);
        fx.setLightingEnabledProperty(true);
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setSpecularColorProperty(specularColor);
        fx.setSpecularPowerProperty(32.0f);
        fx.setAlphaProperty(1.0f);

        DirectionalLight& l0 = fx.getDirectionalLight0Property();
        l0.setEnabledProperty(true);
        l0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        l0.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f)); // isolate specular: no diffuse contribution.
        l0.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));

        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        const Rectangle centReg(kViewport / 2, kViewport / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        dev.GetBackBufferData(&centReg, &centPx, 0, 1);
        return centPx.getRProperty();
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(true);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const int noSpecular = DrawAndReadCentre(dev, Vector3(0.0f, 0.0f, 0.0f));
        std::printf("SpecularColor=black centre R=%d\n", noSpecular);
        Check(noSpecular <= 5, "SpecularColor=black: no specular contribution, centre stays black "
              "(sanity -- diffuse/ambient/emissive are all black too)");

        const int withSpecular = DrawAndReadCentre(dev, Vector3(1.0f, 1.0f, 1.0f));
        std::printf("SpecularColor=white centre R=%d\n", withSpecular);
        Check(withSpecular >= 150, "SpecularColor=white: a real, strong specular highlight is "
              "computed and visible -- not silently dropped");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1SpecularTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1SpecularTest game;
    game.Run();
    return game.getResult();
}
