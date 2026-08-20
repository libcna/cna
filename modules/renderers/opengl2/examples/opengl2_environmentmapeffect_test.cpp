// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact EnvironmentMapEffect proof for the native OpenGL 2.1 graphics
// renderer -- the one remaining piece needed for cube-map feature parity, now that both
// TextureCube and RenderTargetCube exist (see plans/plan_opengl2.md's session 6 status entries).
//
// Checks A/B use World=View=Projection=Identity (this project's own established convention --
// see opengl2_effects_test.cpp) with the quad at local Z=-0.8 and eye derived from
// Matrix::Invert(View)'s translation (=(0,0,0) here), so the eye-to-surface vector at the quad's
// centre pixel is exactly (0,0,1) -- a clean, hand-verifiable head-on view.
//
// Check A -- EnvironmentMapAmount=1, Fresnel disabled: the reflection fully replaces the base
//   (mix(base,env,1)=env), and the reflection direction at a head-on view is exactly (0,0,1) --
//   proves both the blend-replaces-base path AND that the correct cube face
//   (GL_TEXTURE_CUBE_MAP_POSITIVE_Z) is the one actually sampled (it is given a distinct color
//   from all 5 other faces).
// Check B -- same scene, Fresnel enabled, FresnelFactor=1: Fresnel is evaluated per-VERTEX (see
//   the vertex shader's own comment) from each of the quad's 4 corners, NOT recomputed
//   per-fragment from the interpolated (constant) normal -- so it is NOT simply "0 at the
//   geometric centre". By symmetry all 4 corners of this quad share the same |viewAngle|~=0.4924
//   (eyeVector=normalize((0,0,0)-corner), corner=(+-1,+-1,-0.8)), giving Fresnel~=1-0.4924=0.5076
//   at every vertex -- a CONSTANT that Gouraud-interpolates unchanged to every fragment,
//   including the centre. Expected: mix(base,env,0.5076) with base=EmissiveColor*Texture=
//   (100,50,25) and env=(0,255,0) (the +Z face) => ~=(49,154,12). Differentiates the
//   Fresnel-enabled formula from Check A's flat one using the SAME scene, and proves Fresnel is
//   genuinely evaluated per-vertex rather than being a no-op/always-0/always-1 stub.
// Check C -- reuses examples/easygl_environmentmapeffect_golden_test.cpp's own independently
//   FNA-derived "combined scene" (real non-identity World/View/Projection, EmissiveColor,
//   EnvironmentMapAmount, EnvironmentMapSpecular, Fresnel all combined) and its own derived
//   expected pixel (151,101,76) -- a genuine cross-renderer consistency check against a value
//   already validated correct against FNA's own math (see that file's header comment for the
//   full derivation).
// Check D -- Texture==null and EnvironmentMap==null (never assigned): must not crash/throw, and
//   the renderer's default-white sampler fallback must actually take effect (reads back pure
//   white, not black/garbage) -- proves ensureDefaultEnvMapTextures()'s fallback binding is real.
// Check E -- 30 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

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

    std::unique_ptr<TextureCube> MakeCube(GraphicsDevice& dev, Color posZ, Color other)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
        {
            Color c = (face == CubeMapFace::PositiveZ) ? posZ : other;
            cube->SetData(face, &c, 1);
        }
        return cube;
    }

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& dev, Color col)
    {
        return MakeCube(dev, col, col);
    }
}

class OpenGL2EnvironmentMapEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> tex_;
    std::unique_ptr<TextureCube> cubeDistinct_;
    std::unique_ptr<TextureCube> cubeTranslucent_;

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

    static void HeadOnQuad(VertexPositionNormalTexture out[6], float z)
    {
        const Vector3 n(0.0f, 0.0f, 1.0f);
        out[0] = {Vector3(-1, 1, z), n, Vector2(0, 0)}; out[1] = {Vector3(1, -1, z), n, Vector2(1, 1)}; out[2] = {Vector3(-1, -1, z), n, Vector2(0, 1)};
        out[3] = {Vector3(-1, 1, z), n, Vector2(0, 0)}; out[4] = {Vector3(1, 1, z), n, Vector2(1, 0)};  out[5] = {Vector3(1, -1, z), n, Vector2(1, 1)};
    }

    void DrawHeadOn(GraphicsDevice& dev, bool fresnelEnabled, float envMapAmount, Vector3 envMapSpecular,
                    Texture2D* texture, TextureCube* envMap)
    {
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(texture);
        fx.setEnvironmentMapProperty(envMap);
        fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setEnvironmentMapAmountProperty(envMapAmount);
        fx.setEnvironmentMapSpecularProperty(envMapSpecular);
        fx.setFresnelFactorProperty(fresnelEnabled ? 1.0f : 0.0f);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        VertexPositionNormalTexture verts[6];
        HeadOnQuad(verts, -0.8f);
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    // Reproduces examples/easygl_environmentmapeffect_golden_test.cpp's own combined scene and
    // its own independently FNA-derived expected pixel, verbatim.
    void DrawGoldenScene(GraphicsDevice& dev)
    {
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(tex_.get());
        fx.setEnvironmentMapProperty(cubeTranslucent_.get());
        fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.4f, 0.4f, 0.4f));
        fx.setFresnelFactorProperty(1.0f);
        fx.setWorldProperty(Matrix::CreateScale(2.0f, 1.0f, 1.0f));
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.Apply();

        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(quad, 0, 6);
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

        cubeDistinct_ = MakeCube(dev, Color(0, 255, 0, 255), Color(255, 0, 0, 255));
        cubeTranslucent_ = MakeSolidCube(dev, Color(0, 0, 0, 128));
    }

    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Check A: Fresnel disabled, amount=1 -- reflection fully replaces the base, and the
        // reflected direction at this head-on view must sample the +Z face (green), not any of
        // the 5 red faces.
        dev.Clear(Color::Black);
        DrawHeadOn(dev, /*fresnelEnabled=*/false, /*envMapAmount=*/1.0f, Vector3::Zero,
                  tex_.get(), cubeDistinct_.get());
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("Fresnel disabled, amount=1: reflection replaces base and samples +Z face: got=" + ColorStr(got),
                  Matches(got, Color(0, 255, 0, 255), 20));
        }

        // Check B: same scene, Fresnel enabled -- per-vertex Fresnel is a constant ~=0.5076 at
        // every corner of this quad (see header comment for the derivation), so the base and env
        // colors blend ~=49/51: mix((100,50,25),(0,255,0),0.5076) ~= (49,154,12).
        dev.Clear(Color::Black);
        DrawHeadOn(dev, /*fresnelEnabled=*/true, /*envMapAmount=*/1.0f, Vector3::Zero,
                  tex_.get(), cubeDistinct_.get());
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("Fresnel enabled: per-vertex constant blend base/env ~=49/51: got=" + ColorStr(got),
                  Matches(got, Color(49, 154, 12, 255), 15));
        }

        // Check C: cross-renderer consistency against the EasyGL golden scene's own
        // independently FNA-derived expected value.
        dev.Clear(Color::Black);
        DrawGoldenScene(dev);
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("combined scene matches EasyGL golden test's own FNA-derived value: got=" + ColorStr(got),
                  Matches(got, Color(151, 101, 76, 255), 20));
        }

        // Check D: Texture==null, EnvironmentMap==null -- must not crash and must fall back to
        // the renderer's default-white samplers (not black/garbage).
        dev.Clear(Color::Black);
        DrawHeadOn(dev, /*fresnelEnabled=*/false, /*envMapAmount=*/1.0f, Vector3::Zero,
                  nullptr, nullptr);
        if (runChecks)
        {
            const Color got = ReadPixel(kSize / 2, kSize / 2);
            Check("null Texture/EnvironmentMap falls back to default-white samplers: got=" + ColorStr(got),
                  Matches(got, Color(255, 255, 255, 255), 20));
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(std::to_string(kTotalFrames) + " frames render with no exception", true);
            std::printf("=== %d/%d PASS ===\n", passCount_, 5);
            result_ = (passCount_ == 5) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2EnvironmentMapEffectTest()
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

    OpenGL2EnvironmentMapEffectTest game;
    game.Run();
    return game.getResult();
}
