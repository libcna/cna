// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: EnvironmentMapEffect's fixed-function cube-map reflection subset
// (plans/plan_opengl1.md phase 5).
//
// OpenGL1Renderer::DrawInternal blends a real ARB_texture_cube_map cube texture into a
// VertexPositionNormalTexture draw via GL_REFLECTION_MAP texture-coordinate generation on
// texture unit 1, GL_INTERPOLATEd against unit 0's lit/textured result using
// GL_CONSTANT.a = EnvironmentMapAmount -- i.e. exactly FNA's real
// `mix(baseColor, envColor, Amount)` blend (see docs/environmentmapeffect-support.md's own
// Task 394 for the formula FNA actually uses, lerp not additive).
//
// This test deliberately isolates the blend formula from reflection-vector face selection: the
// cube map used here is a SOLID color on every face, so which face the fixed-function reflection
// vector happens to hit is irrelevant to the expected result -- only the blend math is under
// test. It also deliberately disables DirectionalLight0 (enabled by default) so the "base" color
// going into the blend is exactly EnvironmentMapEffect's own EmissiveColor+AmbientLightColor*
// DiffuseColor formula (GpuDrawParams::emissiveColor, plans/plan_opengl1.md phase 5's own GL_EMISSION
// fix) with no NdotL lighting-direction math to additionally verify.
//
// Deliberately NOT tested here (OpenGL1's own documented, intentional limitation): Fresnel edge-
// weighting and EnvironmentMapSpecular's alpha-scaled specular term. Both are inherently per-
// pixel/view-angle-dependent and cannot be expressed with fixed-function texture combiners --
// OPENGL1 always behaves as if FresnelEnabled=false/EnvironmentMapSpecular=0, regardless of what
// the effect's own properties say.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kBackground(10, 10, 10, 255);
}

class OpenGL1EnvironmentMapEffectTest : public Game
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

    static bool Near(int got, int expected, int tolerance)
    {
        return std::abs(got - expected) <= tolerance;
    }

    Color ReadCenter(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        if (!dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        {
            std::printf("SKIP: no 3D support\n=== 0/0 PASS (skipped) ===\n");
            Exit();
            return;
        }

        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Solid green cube map -- every face identical, so the fixed-function reflection
        // vector's exact face selection cannot affect the sampled color.
        const Color kEnvColor(0, 255, 0, 255);
        TextureCube envMap(dev, 4, false, SurfaceFormat::Color);
        Color faceTexels[16] = {
            kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor,
            kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor, kEnvColor,
        };
        envMap.SetData(CubeMapFace::PositiveX, faceTexels, 16);
        envMap.SetData(CubeMapFace::NegativeX, faceTexels, 16);
        envMap.SetData(CubeMapFace::PositiveY, faceTexels, 16);
        envMap.SetData(CubeMapFace::NegativeY, faceTexels, 16);
        envMap.SetData(CubeMapFace::PositiveZ, faceTexels, 16);
        envMap.SetData(CubeMapFace::NegativeZ, faceTexels, 16);

        const Vector3 eye(0.0f, 0.0f, 3.0f);
        const Vector3 target(0.0f, 0.0f, 0.0f);
        Matrix view = Matrix::CreateLookAt(eye, target, Vector3(0.0f, 1.0f, 0.0f));
        Matrix proj = Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, dev.getViewportProperty().getAspectRatioProperty(), 0.1f, 100.0f);
        Matrix world = Matrix::getIdentityProperty();

        // Quad facing the camera (normal = +Z), large enough to fill the screen centre.
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-5.0f,  5.0f, 0.0f), Vector3(0, 0, 1), Vector2(0.0f, 0.0f) },
            { Vector3(-5.0f, -5.0f, 0.0f), Vector3(0, 0, 1), Vector2(0.0f, 1.0f) },
            { Vector3( 5.0f, -5.0f, 0.0f), Vector3(0, 0, 1), Vector2(1.0f, 1.0f) },
            { Vector3(-5.0f,  5.0f, 0.0f), Vector3(0, 0, 1), Vector2(0.0f, 0.0f) },
            { Vector3( 5.0f, -5.0f, 0.0f), Vector3(0, 0, 1), Vector2(1.0f, 1.0f) },
            { Vector3( 5.0f,  5.0f, 0.0f), Vector3(0, 0, 1), Vector2(0.0f, 0.0f) },
        };

        // base = EmissiveColor + AmbientLightColor*DiffuseColor (DirectionalLight0 disabled, so
        // no NdotL diffuse term reaches GpuDrawParams::emissiveColor -- see
        // EnvironmentMapEffect::FillGpuDrawParams). DiffuseColor=(1,1,1) so base == AmbientLightColor.
        const Vector3 kAmbient(0.4f, 0.2f, 0.1f);          // -> base (255*0.4, 255*0.2, 255*0.1) = (102,51,26)
        const int baseR = 102, baseG = 51, baseB = 26;
        const int envR = 0, envG = 255, envB = 0;

        auto drawWithAmount = [&](float amount) -> Color
        {
            dev.Clear(kBackground);
            EnvironmentMapEffect fx(dev);
            fx.setWorldProperty(world);
            fx.setViewProperty(view);
            fx.setProjectionProperty(proj);
            fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            fx.setAmbientLightColorProperty(kAmbient);
            fx.setEmissiveColorProperty(Vector3::Zero);
            fx.setAlphaProperty(1.0f);
            fx.DirectionalLight0.setEnabledProperty(false);
            fx.setEnvironmentMapProperty(&envMap);
            fx.setEnvironmentMapAmountProperty(amount);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            return ReadCenter(dev);
        };

        // Amount=0: cube map must have ZERO influence -- pure base color.
        {
            Color got = drawWithAmount(0.0f);
            std::printf("amount=0 got=(%d,%d,%d)\n", got.getRProperty(), got.getGProperty(), got.getBProperty());
            Check(Near(got.getRProperty(), baseR, 8) && Near(got.getGProperty(), baseG, 8) && Near(got.getBProperty(), baseB, 8),
                  "EnvironmentMapAmount=0: pure base (ambient+emissive) color, no cube map contribution");
        }

        // Amount=1: base must have ZERO influence -- pure cube map color.
        {
            Color got = drawWithAmount(1.0f);
            std::printf("amount=1 got=(%d,%d,%d)\n", got.getRProperty(), got.getGProperty(), got.getBProperty());
            Check(Near(got.getRProperty(), envR, 8) && Near(got.getGProperty(), envG, 8) && Near(got.getBProperty(), envB, 8),
                  "EnvironmentMapAmount=1: pure cube map color, no base contribution");
        }

        // Amount=0.5: the real discriminating case -- proves this is a genuine mix(), not a
        // saturating add (Task 383/394's own precedent: 0/1 alone can't tell mix() from additive
        // once either term saturates, a mid-value with two genuinely different non-saturated
        // colors is required).
        {
            Color got = drawWithAmount(0.5f);
            const int expR = (baseR + envR) / 2, expG = (baseG + envG) / 2, expB = (baseB + envB) / 2;
            std::printf("amount=0.5 got=(%d,%d,%d) expected=(%d,%d,%d)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty(), expR, expG, expB);
            Check(Near(got.getRProperty(), expR, 12) && Near(got.getGProperty(), expG, 12) && Near(got.getBProperty(), expB, 12),
                  "EnvironmentMapAmount=0.5: mix(base,envColor,0.5), not an additive/saturating blend");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1EnvironmentMapEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1EnvironmentMapEffectTest game;
    game.Run();
    return game.getResult();
}
