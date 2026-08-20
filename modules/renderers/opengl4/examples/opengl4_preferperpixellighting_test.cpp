// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-29: PreferPerPixelLighting vertex-lit shader variant for the OpenGL4
// graphics renderer -- GpuDrawParams::preferPerPixelLighting was previously read by no shader on
// this renderer; lit_textured3d/skinned3d always rendered per-pixel regardless of its value, the
// opposite of real XNA's own default (BasicEffect/SkinnedEffect both default
// PreferPerPixelLighting=false, i.e. per-vertex/Gouraud-shaded lighting). Ported
// EasyGLRenderer's own EnsureLit3DVertexLitProgram()/EnsureSkinnedVertexLitProgram() GLSL
// (desktop GLSL 410 core translation only, same Blinn-Phong math moved from the fragment stage
// into the vertex stage and Gouraud-interpolated via new out vec3 vLitRGB/vSpecularRGB varyings).
//
// Reuses easygl_basiceffect_preferperpixellighting_test.cpp's/
// easygl_skinnedeffect_preferperpixellighting_test.cpp's own exact scene and analytically
// re-derived expected values verbatim (same Blinn-Phong formula ported byte-for-byte, so the same
// oracle applies): a 2-triangle quad with a single shared vertex normal (0,0,1) makes diffuse
// lighting spatially constant across the surface, but specular still varies with the view vector,
// so the centre pixel -- sitting exactly on the diagonal seam between the two triangles, at the
// shared TL=(-1,1,0)/BR=(1,-1,0) vertices' own midpoint -- discriminates cleanly between:
//   - PreferPerPixelLighting=false (vertex-lit): centre reads the Gouraud-interpolated AVERAGE of
//     TL's and BR's own independently-computed per-vertex specular terms (~127,127,127).
//   - PreferPerPixelLighting=true (pixel-lit): centre reads a FRESH per-fragment evaluation at the
//     origin itself (~155,155,155).
//
// Checks A-C: BasicEffect (stride 32) -- (a) default/false reads vertex-lit, (b) true reads
// pixel-lit, (c) (a) != (b) proves a genuine live dispatch selector, not a decorative no-op.
// Checks D-F: SkinnedEffect (stride 52) -- identical scene, a single Identity bone at 100% weight
// keeps skinning a mathematical no-op so the lighting-mode difference is isolated.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const Color kWhite(255, 255, 255, 255);
    const Vector3 kAmbient(0.02f, 0.02f, 0.02f);
    const Vector3 kMaterialDiffuse(0.4f, 0.4f, 0.4f);
    const Vector3 kLightDiffuse(0.5f, 0.5f, 0.5f);
    const Vector3 kLightSpecular(1.0f, 1.0f, 1.0f);
    const Vector3 kSpecularColor(1.0f, 1.0f, 1.0f);
    const Vector3 kZero(0.0f, 0.0f, 0.0f);
    constexpr float kSpecularPower = 32.0f;
    const Vector3 kLightDirRaw(0.5f, 0.0f, -1.0f);
    const Vector3 kNormal(0.0f, 0.0f, 1.0f);
    const Vector3 kEyeStraightOn(0.0f, 0.0f, 3.0f);

    // Analytically re-derived (same values easygl_basiceffect_preferperpixellighting_test.cpp/
    // easygl_skinnedeffect_preferperpixellighting_test.cpp already established for this exact
    // scene -- this renderer ported the identical Blinn-Phong formula, so the same oracle applies).
    const Color kExpectedVertexLit(127, 127, 127, 255);
    const Color kExpectedPixelLit(155, 155, 155, 255);

    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");
}

class OpenGL4PreferPerPixelLightingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void Check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool Matches(const Color& c, const Color& expected)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), 10)
            && CloseTo(c.getGProperty(), expected.getGProperty(), 10)
            && CloseTo(c.getBProperty(), expected.getBProperty(), 10);
    }

    static Color ReadCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color RenderBasicEffect(GraphicsDevice& dev, Texture2D& tex, bool preferPerPixelLighting)
    {
        Vector3 lightDir = kLightDirRaw;
        lightDir.Normalize();

        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&tex);
        fx.setLightingEnabledProperty(true);
        fx.setPreferPerPixelLightingProperty(preferPerPixelLighting);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(kSpecularColor);
        fx.setSpecularPowerProperty(kSpecularPower);

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(kLightSpecular);

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEyeStraightOn, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));

        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), kNormal, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), kNormal, Vector2(1.0f, 1.0f) },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = ReadCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    Color RenderSkinnedEffect(GraphicsDevice& dev, Texture2D& tex, bool preferPerPixelLighting)
    {
        Vector3 lightDir = kLightDirRaw;
        lightDir.Normalize();

        SkinnedEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setPreferPerPixelLightingProperty(preferPerPixelLighting);
        fx.setAmbientLightColorProperty(kAmbient);
        fx.setDiffuseColorProperty(kMaterialDiffuse);
        fx.setEmissiveColorProperty(kZero);
        fx.setSpecularColorProperty(kSpecularColor);
        fx.setSpecularPowerProperty(kSpecularPower);

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(kLightSpecular);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);

        fx.SetBoneTransforms(std::vector<Matrix>{ Matrix::getIdentityProperty() });

        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(kEyeStraightOn, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.Apply();

        const SkinnedGpuVertex verts[6] = {
            { -1,  1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0 },
            { -1, -1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0 },
            { -1,  1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0 },
            {  1, -1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0 },
            {  1,  1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = ReadCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        const Color basicVertexLit = RenderBasicEffect(dev, tex, false);
        Check(Matches(basicVertexLit, kExpectedVertexLit),
              "Check A: BasicEffect PreferPerPixelLighting=false (XNA's real default) -- Gouraud-interpolated specular",
              basicVertexLit, "(127,127,127)");

        const Color basicPixelLit = RenderBasicEffect(dev, tex, true);
        Check(Matches(basicPixelLit, kExpectedPixelLit),
              "Check B: BasicEffect PreferPerPixelLighting=true -- fresh per-fragment specular",
              basicPixelLit, "(155,155,155)");

        Check(!Matches(basicVertexLit, basicPixelLit),
              "Check C: BasicEffect (A) differs from (B) -- a real dispatch selector, not a no-op",
              basicVertexLit, "!= (155,155,155)");

        const Color skinnedVertexLit = RenderSkinnedEffect(dev, tex, false);
        Check(Matches(skinnedVertexLit, kExpectedVertexLit),
              "Check D: SkinnedEffect PreferPerPixelLighting=false (XNA's real default) -- Gouraud-interpolated specular",
              skinnedVertexLit, "(127,127,127)");

        const Color skinnedPixelLit = RenderSkinnedEffect(dev, tex, true);
        Check(Matches(skinnedPixelLit, kExpectedPixelLit),
              "Check E: SkinnedEffect PreferPerPixelLighting=true -- fresh per-fragment specular",
              skinnedPixelLit, "(155,155,155)");

        Check(!Matches(skinnedVertexLit, skinnedPixelLit),
              "Check F: SkinnedEffect (D) differs from (E) -- a real dispatch selector, not a no-op",
              skinnedVertexLit, "!= (155,155,155)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL4PreferPerPixelLightingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    OpenGL4PreferPerPixelLightingTest game;
    game.Run();
    return game.getResult();
}
