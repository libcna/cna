// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact SkinnedEffect proof for the native OpenGL 2.1 graphics renderer --
// bone-palette vertex skinning, matching EasyGLRenderer::EnsureSkinnedProgram's formula.
// All draws use World=View=Projection=Identity (this project's own established convention -- see
// opengl2_effects_test.cpp) with the quad at local Z=-0.8.
//
// Check A -- bone-index selection actually transforms position: a quad fully weighted
//   (weight=1, weightsPerVertex irrelevant) to bone[1]=Translate(+4,0,0) moves entirely outside
//   the [-1,1] clip range (background shows); the SAME quad fully weighted to bone[0]=Identity
//   stays put (own color shows) -- proves per-vertex bone selection and the position transform
//   are both real, not a static bind-pose no-op.
// Check B -- WeightsPerVertex gating (FNA's real Skin(vin, boneCount) only sums the first N
//   weight/index pairs): weight=(0.5,0.5,0,0) blending bone[0]=Identity and bone[1]=
//   Translate(+4,0,0). With weightsPerVertex=1, bone[1] is never summed at all -- and a *single*
//   bone at weight 0.5 leaves the NDC position unchanged (the whole skin matrix, including its
//   homogeneous W row, is scaled by 0.5, which cancels exactly in the perspective divide), so the
//   quad renders at its original location. With weightsPerVertex=2, both bones contribute and
//   (since weights sum to 1, W stays exactly 1) the quad rigidly translates by exactly half of
//   bone[1]'s offset (+2), moving it clear of the sampled centre pixel. A visible/not-visible
//   differential from the SAME vertex data, gated only by WeightsPerVertex.
// Check C -- per-fragment Lambertian lighting: two identical unlit-texture quads (TextureEnabled
//   still samples uTex, but Texture unset so the white fallback isolates the light response, same
//   technique as opengl2_effects_test.cpp's Check C), one lit by a light pointing straight into
//   the surface (near-white) and one lit by a light pointing away (ambient-only, dim).
// Check D -- Texture==null falls back to the renderer's default-white sampler (reused from
//   EnvironmentMapEffect's ensureDefaultWhiteTextures(), not black/garbage): with lights off and
//   EmissiveColor=0.5, expected rgb = EmissiveColor*white = mid-gray (128,128,128), not black.
// Check E -- 30 frames of the whole scene render with no exception.
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
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

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

class OpenGL2SkinnedEffectTest : public Game
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

    // SkinnedEffect (real XNA/FNA behavior): LightingEnabled cannot be set to false -- always
    // left at its default (true). AmbientLightColor defaults to black, so with DirectionalLight0
    // disabled the only contribution is EmissiveColor, matching this helper's `light0Enabled=false`
    // callers' intent of "no light contribution beyond emissive".
    void DrawSkinnedQuad(GraphicsDevice& dev, const Vector4& weight, const std::array<uint8_t, 4>& indices,
                        int weightsPerVertex, const std::vector<Matrix>& bones, Texture2D* texture,
                        bool light0Enabled, const Vector3& lightDir, const Vector3& emissive)
    {
        SkinnedEffect fx(dev);
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(weightsPerVertex);
        fx.setTextureProperty(texture);
        fx.setEmissiveColorProperty(emissive);
        fx.DirectionalLight0.setEnabledProperty(light0Enabled);
        fx.DirectionalLight0.setDirectionProperty(lightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const float z = -0.8f;
        const VertexPositionNormalTextureSkinned verts[6] = {
            {Vector3(-1, 1, z), n, Vector2(0, 0), weight, indices},
            {Vector3(1, -1, z), n, Vector2(1, 1), weight, indices},
            {Vector3(-1, -1, z), n, Vector2(0, 1), weight, indices},
            {Vector3(-1, 1, z), n, Vector2(0, 0), weight, indices},
            {Vector3(1, 1, z), n, Vector2(1, 0), weight, indices},
            {Vector3(1, -1, z), n, Vector2(1, 1), weight, indices},
        };
        VertexBuffer vb(dev, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 6, BufferUsage::None);
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

        std::vector<Matrix> bones(2, Matrix::getIdentityProperty());
        bones[0] = Matrix::getIdentityProperty();
        bones[1] = Matrix::CreateTranslation(4.0f, 0.0f, 0.0f);

        // Check A: fully weighted to bone[1] (Translate +4) -- moves offscreen.
        dev.Clear(Color::Black);
        DrawSkinnedQuad(dev, Vector4(1, 0, 0, 0), {1, 0, 0, 0}, 1, bones, tex_.get(),
                        /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3(0.5f, 0.5f, 0.5f));
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("bone[1]=Translate(+4,0,0), weight=1: quad moves offscreen: got=" + ColorStr(got),
                  Matches(got, Color::Black, 10));
        }

        // Check A cont'd: fully weighted to bone[0] (Identity) -- stays put.
        dev.Clear(Color::Black);
        DrawSkinnedQuad(dev, Vector4(1, 0, 0, 0), {0, 0, 0, 0}, 1, bones, tex_.get(),
                        /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3(0.5f, 0.5f, 0.5f));
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("bone[0]=Identity, weight=1: quad stays put: got=" + ColorStr(got),
                  !Matches(got, Color::Black, 10));
        }

        // Check B: weight=(0.5,0.5), bones (0,1) -- weightsPerVertex=1 ignores bone[1] entirely.
        dev.Clear(Color::Black);
        DrawSkinnedQuad(dev, Vector4(0.5f, 0.5f, 0, 0), {0, 1, 0, 0}, 1, bones, tex_.get(),
                        /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3(0.5f, 0.5f, 0.5f));
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("weightsPerVertex=1: bone[1] ignored, quad stays put: got=" + ColorStr(got),
                  !Matches(got, Color::Black, 10));
        }

        // Check B cont'd: weightsPerVertex=2 -- both bones blend, quad rigidly shifts by +2.
        dev.Clear(Color::Black);
        DrawSkinnedQuad(dev, Vector4(0.5f, 0.5f, 0, 0), {0, 1, 0, 0}, 2, bones, tex_.get(),
                        /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3(0.5f, 0.5f, 0.5f));
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("weightsPerVertex=2: both bones blend, quad shifts offscreen: got=" + ColorStr(got),
                  Matches(got, Color::Black, 10));
        }

        // Check C: per-fragment lighting differential (light into the surface vs away from it).
        std::vector<Matrix> identityBones(1, Matrix::getIdentityProperty());
        dev.Clear(Color::Black);
        DrawSkinnedQuad(dev, Vector4(1, 0, 0, 0), {0, 0, 0, 0}, 1, identityBones, nullptr,
                        /*light0Enabled=*/true, Vector3(0, 0, -1), Vector3::Zero);
        Color litResult(0, 0, 0, 0);
        if (runChecks) litResult = ReadPixel(kSize / 2, kSize / 2);

        dev.Clear(Color::Black);
        DrawSkinnedQuad(dev, Vector4(1, 0, 0, 0), {0, 0, 0, 0}, 1, identityBones, nullptr,
                        /*light0Enabled=*/true, Vector3(0, 0, 1), Vector3::Zero);
        if (runChecks)
        {
            const Color unlitResult = ReadPixel(kSize / 2, kSize / 2);
            Check("per-fragment lighting: light-into-surface brighter than light-away: lit=" +
                      ColorStr(litResult) + " unlit=" + ColorStr(unlitResult),
                  litResult.getRProperty() > unlitResult.getRProperty() + 20);
        }

        // Check D: Texture==null falls back to the default-white sampler.
        dev.Clear(Color::Black);
        DrawSkinnedQuad(dev, Vector4(1, 0, 0, 0), {0, 0, 0, 0}, 1, identityBones, nullptr,
                        /*light0Enabled=*/false, Vector3(0, 0, -1), Vector3(0.5f, 0.5f, 0.5f));
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("null Texture falls back to default-white sampler: got=" + ColorStr(got),
                  Matches(got, Color(128, 128, 128, 255), 20));
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
    OpenGL2SkinnedEffectTest()
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

    OpenGL2SkinnedEffectTest game;
    game.Run();
    return game.getResult();
}
