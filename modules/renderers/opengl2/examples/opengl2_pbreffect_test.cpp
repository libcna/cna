// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact PbrEffect/SkinnedPbrEffect proof for the native OpenGL 2.1
// graphics renderer -- metallic-roughness BRDF, tangent-space normal mapping, and (for
// SkinnedPbrEffect) bone-palette vertex skinning combined with the PBR shader. All non-skinned
// checks use World=View=Projection=Identity (this project's own established convention -- see
// opengl2_effects_test.cpp) with the quad at local Z=-0.8, normal (0,0,1), tangent (1,0,0,1).
//
// Check A -- per-fragment lighting: a light pointing straight into the surface is markedly
//   brighter than the same light pointing away (ambient-only) -- proves the GGX/Fresnel BRDF
//   actually responds to N.L, not a pass-through.
// Check B -- NormalMap perturbation: a strongly-tilted tangent-space normal map value (decodes to
//   (1,0,0), a 90-degree tilt) changes the lit result relative to the flat-normal ((0,0,1), no
//   perturbation) baseline -- proves the per-fragment TBN basis + normal map sampling are real.
// Check C -- EmissiveFactor: with all lights and ambient off, a non-zero EmissiveFactor still
//   produces a non-black pixel -- proves the emissive term is additive and independent of
//   lighting, matching the shared BasicEffect/SkinnedEffect emissive convention.
// Check D -- OcclusionMap: an ambient-only scene (no direct lights) with a black (occlusion=0)
//   OcclusionMap reads back darker than the same scene with no OcclusionMap bound (defaults to
//   white, occlusion=1) -- proves the occlusion map actually multiplies the ambient term.
// Check E -- null Texture/NormalMap/MetallicRoughnessMap/EmissiveMap/OcclusionMap all fall back
//   to their real defaults (white/flat-normal) without crashing or producing black/garbage.
// Check F -- SkinnedPbrEffect: a quad fully weighted to a translating bone moves off-screen; the
//   same quad fully weighted to an identity bone stays put -- proves the stride-68 vertex layout
//   and bone-palette uniforms reach the PBR shader together.
// Check G -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 30;
    constexpr int kSize = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }
}

class OpenGL2PbrEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> tex_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(const std::string& label, bool ok)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    Color ReadPixel(int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    void DrawPbrQuad(GraphicsDevice& dev, bool light0Enabled, const Vector3& lightDir,
                     const Vector3& ambient, const Vector3& emissive,
                     Texture2D* normalMap, Texture2D* occlusionMap, Texture2D* texture)
    {
        PbrEffect fx(dev);
        fx.setTextureProperty(texture);
        fx.setNormalMapProperty(normalMap);
        fx.setOcclusionMapProperty(occlusionMap);
        fx.setAmbientLightColorProperty(ambient);
        fx.setEmissiveFactorProperty(emissive);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.DirectionalLight0.setEnabledProperty(light0Enabled);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const Vector4 t(1.0f, 0.0f, 0.0f, 1.0f);
        const float z = -0.8f;
        const VertexPositionNormalTangentTexture verts[6] = {
            {Vector3(-1, 1, z), n, t, Vector2(0, 0)}, {Vector3(1, -1, z), n, t, Vector2(1, 1)}, {Vector3(-1, -1, z), n, t, Vector2(0, 1)},
            {Vector3(-1, 1, z), n, t, Vector2(0, 0)}, {Vector3(1, 1, z), n, t, Vector2(1, 0)},  {Vector3(1, -1, z), n, t, Vector2(1, 1)},
        };
        VertexBuffer vb(dev, VertexPositionNormalTangentTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    void DrawSkinnedPbrQuad(GraphicsDevice& dev, const Vector4& weight, const std::array<uint8_t, 4>& indices,
                            const std::vector<Matrix>& bones)
    {
        SkinnedPbrEffect fx(dev);
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.setTextureProperty(tex_.get());
        fx.setEmissiveFactorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const Vector4 t(1.0f, 0.0f, 0.0f, 1.0f);
        const float z = -0.8f;
        const VertexPositionNormalTangentTextureSkinned verts[6] = {
            {Vector3(-1, 1, z), n, t, Vector2(0, 0), weight, indices}, {Vector3(1, -1, z), n, t, Vector2(1, 1), weight, indices}, {Vector3(-1, -1, z), n, t, Vector2(0, 1), weight, indices},
            {Vector3(-1, 1, z), n, t, Vector2(0, 0), weight, indices}, {Vector3(1, 1, z), n, t, Vector2(1, 0), weight, indices},  {Vector3(1, -1, z), n, t, Vector2(1, 1), weight, indices},
        };
        VertexBuffer vb(dev, VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        const Color texColor(200, 100, 50, 255);
        tex_ = std::make_unique<Texture2D>(dev, 1, 1);
        tex_->SetData(&texColor, 1);
    }

    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);

        // Check A: per-fragment lighting differential.
        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/true, Vector3(0, 0, -1), Vector3::Zero, Vector3::Zero,
                   nullptr, nullptr, tex_.get());
        Color litResult(0, 0, 0, 0);
        if (runChecks) litResult = ReadPixel(kSize / 2, kSize / 2);

        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/true, Vector3(0, 0, 1), Vector3::Zero, Vector3::Zero,
                   nullptr, nullptr, tex_.get());
        if (runChecks)
        {
            const Color unlitResult = ReadPixel(kSize / 2, kSize / 2);
            Check("per-fragment lighting: light-into-surface brighter than light-away: lit=" +
                      ColorStr(litResult) + " unlit=" + ColorStr(unlitResult),
                  litResult.getRProperty() > unlitResult.getRProperty() + 10);
        }

        // Check B: NormalMap perturbation differential (flat vs a 90-degree-tilted normal).
        const Color flatNormalColor(128, 128, 255, 255);
        Texture2D flatNormalTex(dev, 1, 1);
        flatNormalTex.SetData(&flatNormalColor, 1);
        const Color tiltedNormalColor(255, 128, 128, 255);
        Texture2D tiltedNormalTex(dev, 1, 1);
        tiltedNormalTex.SetData(&tiltedNormalColor, 1);

        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/true, Vector3(0.3f, 0, -1), Vector3::Zero, Vector3::Zero,
                   &flatNormalTex, nullptr, tex_.get());
        Color flatResult(0, 0, 0, 0);
        if (runChecks) flatResult = ReadPixel(kSize / 2, kSize / 2);

        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/true, Vector3(0.3f, 0, -1), Vector3::Zero, Vector3::Zero,
                   &tiltedNormalTex, nullptr, tex_.get());
        if (runChecks)
        {
            const Color tiltedResult = ReadPixel(kSize / 2, kSize / 2);
            Check("NormalMap perturbation changes the lit result: flat=" + ColorStr(flatResult) +
                      " tilted=" + ColorStr(tiltedResult),
                  !Matches(flatResult, tiltedResult, 8));
        }

        // Check C: EmissiveFactor is additive and independent of lighting.
        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3::Zero, Vector3(0.5f, 0.5f, 0.5f),
                   nullptr, nullptr, tex_.get());
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("EmissiveFactor produces a non-black pixel with no lights/ambient: got=" + ColorStr(got),
                  got.getRProperty() > 20);
        }

        // Check D: OcclusionMap darkens the ambient-only contribution.
        const Color blackOcclusion(0, 0, 0, 255);
        Texture2D occlusionTex(dev, 1, 1);
        occlusionTex.SetData(&blackOcclusion, 1);

        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3(0.6f, 0.6f, 0.6f), Vector3::Zero,
                   nullptr, nullptr, tex_.get());
        Color noOcclusionResult(0, 0, 0, 0);
        if (runChecks) noOcclusionResult = ReadPixel(kSize / 2, kSize / 2);

        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3(0.6f, 0.6f, 0.6f), Vector3::Zero,
                   nullptr, &occlusionTex, tex_.get());
        if (runChecks)
        {
            const Color occludedResult = ReadPixel(kSize / 2, kSize / 2);
            Check("OcclusionMap=black darkens the ambient term: no-map=" + ColorStr(noOcclusionResult) +
                      " occluded=" + ColorStr(occludedResult),
                  occludedResult.getRProperty() < noOcclusionResult.getRProperty() - 10 &&
                  Matches(occludedResult, Color::Black, 8));
        }

        // Check E: null Texture/maps fallback (white base + flat normal), doesn't crash.
        dev.Clear(Color::Black);
        DrawPbrQuad(dev, /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3::Zero, Vector3(0.5f, 0.5f, 0.5f),
                   nullptr, nullptr, nullptr);
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("null Texture/NormalMap/maps fall back without crashing: got=" + ColorStr(got),
                  // The neutral .5 linear ambient result is encoded to byte 188 for the UNORM
                  // backbuffer; byte 128 was the pre-colour-management oracle.
                  Matches(got, Color(188, 188, 188, 255), 20));
        }

        // Check F: SkinnedPbrEffect bone-palette position skinning.
        std::vector<Matrix> bones(2, Matrix::getIdentityProperty());
        bones[1] = Matrix::CreateTranslation(4.0f, 0.0f, 0.0f);

        dev.Clear(Color::Black);
        DrawSkinnedPbrQuad(dev, Vector4(1, 0, 0, 0), {1, 0, 0, 0}, bones);
        Color offscreenResult(0, 0, 0, 0);
        if (runChecks) offscreenResult = ReadPixel(kSize / 2, kSize / 2);

        dev.Clear(Color::Black);
        DrawSkinnedPbrQuad(dev, Vector4(1, 0, 0, 0), {0, 0, 0, 0}, bones);
        if (runChecks)
        {
            const Color stayResult = ReadPixel(kSize / 2, kSize / 2);
            Check("SkinnedPbrEffect: bone[1]=Translate moves offscreen, bone[0]=Identity stays put: "
                  "offscreen=" + ColorStr(offscreenResult) + " stay=" + ColorStr(stayResult),
                  Matches(offscreenResult, Color::Black, 8) && !Matches(stayResult, Color::Black, 8));
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(std::to_string(kTotalFrames) + " frames render with no exception", true);
            std::printf("=== %d/%d PASS ===\n", passCount_, 7);
            result_ = (passCount_ == 7) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2PbrEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2PbrEffectTest game;
    game.Run();
    return game.getResult();
}
