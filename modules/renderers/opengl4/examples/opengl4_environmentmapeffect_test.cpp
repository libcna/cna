// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-21: EnvironmentMapEffect for the OpenGL4 graphics renderer -- adds a
// dedicated env_map3d GLSL 410 core program (stride 32, VertexPositionNormalTexture), selected
// by BindProgramForStride instead of lit_textured3d when GpuDrawParams::envMapping is set. Ported
// from EasyGLRenderer::EnsureEnvMapped3DProgram (near-verbatim GLSL ES 300 -> 410 core
// translation), cross-verified against VulkanRenderer's env_map3d.frag.glsl formula:
// reflection vector reflect(-eyeVector, worldNormal), Fresnel blend factor
// pow(max(1-|dot(eye,normal)|,0), FresnelFactor)*EnvironmentMapAmount, final colour a LERP (not
// additive) between lit diffuse*texture and the alpha-scaled cubemap sample, plus a separately
// alpha-scaled specular term (see docs/environmentmapeffect-support.md for the two real formula
// bugs -- additive-not-lerp, missing alpha scaling -- found and fixed while porting this to 3
// other renderers; this OpenGL4 port uses the already-corrected formula from day one).
//
// Check A reuses Task 399's own cross-renderer-verified combined-scene oracle verbatim (see
// examples/easygl_environmentmapeffect_golden_test.cpp) -- the strongest possible correctness
// check for a 4th renderer port: if the same scene produces the same pixel value FNA-independently
// derived math predicts, the whole formula chain (reflection, Fresnel, lerp, alpha scaling,
// specular) is correct end-to-end, not just "didn't throw".
// Check B -- EnvironmentMapAmount=0 makes the rendered pixel identical regardless of the cube
//   map's own color (the environment map genuinely has zero influence, not just "looks similar").
// Check C -- EnvironmentMapAmount=1 with FresnelFactor=0 (flat blend, no per-angle falloff) and
//   DiffuseColor=black/EmissiveColor=black (zero base color) makes the rendered pixel an exact,
//   fully-opaque pass-through of the cube map's own color -- decisive proof the reflection sample
//   and lerp/alpha-scale math are correct, not just present.
// Check D -- EnvironmentMapSpecular genuinely adds an additive highlight: the same scene rendered
//   with a non-zero specular value is measurably brighter than with EnvironmentMapSpecular=0.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& dev, Color col)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces)
            cube->SetData(face, &col, 1);
        return cube;
    }

    const Vector3 kNormal(0.0f, 0.0f, 1.0f);
    const VertexPositionNormalTexture kQuad[6] = {
        {Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f)},
        {Vector3(-1.0f, -1.0f, 0.0f), kNormal, Vector2(0.0f, 0.0f)},
        {Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 0.0f)},
        {Vector3(-1.0f,  1.0f, 0.0f), kNormal, Vector2(0.0f, 1.0f)},
        {Vector3( 1.0f, -1.0f, 0.0f), kNormal, Vector2(1.0f, 1.0f)},
        {Vector3( 1.0f,  1.0f, 0.0f), kNormal, Vector2(1.0f, 1.0f)},
    };
}

class OpenGL4EnvironmentMapEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    Color ReadCentre(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // --- Check A: Task 399's own cross-renderer-verified combined scene ---
        {
            const Color kTex(200, 100, 50, 255);
            const Color kTranslucentCube(0, 0, 0, 128);

            Texture2D tex(dev, 1, 1);
            tex.SetData(&kTex, 1);
            auto cube = MakeSolidCube(dev, kTranslucentCube);

            dev.Clear(Color(0, 0, 0, 255));
            EnvironmentMapEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setEnvironmentMapProperty(cube.get());
            fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3(0.4f, 0.4f, 0.4f));
            fx.setFresnelFactorProperty(1.0f); // real FNA default -- Fresnel enabled
            fx.setWorldProperty(Matrix::CreateScale(2.0f, 1.0f, 1.0f));
            fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
            fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, kQuad, 0, 2);

            ExpectPixel("Check A: Task-399 combined-scene oracle (formula cross-verified against 3 other renderers)",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), Color(151, 101, 76, 255), /*tolerance=*/20);
        }

        // Shared flat-facing quad setup for Checks B-D (identity world, camera looking straight on).
        const Matrix identityView = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        const Matrix proj = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);

        // --- Check B: EnvironmentMapAmount=0 -> cube map colour has zero influence ---
        {
            const Color white(255, 255, 255, 255);
            Texture2D tex(dev, 1, 1);
            tex.SetData(&white, 1);

            auto drawWithCube = [&](Color cubeColor) {
                auto cube = MakeSolidCube(dev, cubeColor);
                dev.Clear(Color::Black);
                EnvironmentMapEffect fx(dev);
                fx.setTextureProperty(&tex);
                fx.setEnvironmentMapProperty(cube.get());
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setEnvironmentMapAmountProperty(0.0f);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(identityView);
                fx.setProjectionProperty(proj);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, kQuad, 0, 2);
                return ReadCentre(dev);
            };

            const Color withRedCube = drawWithCube(Color(255, 0, 0, 255));
            const Color withBlueCube = drawWithCube(Color(0, 0, 255, 255));
            Check(withRedCube.getRProperty() == withBlueCube.getRProperty() &&
                  withRedCube.getGProperty() == withBlueCube.getGProperty() &&
                  withRedCube.getBProperty() == withBlueCube.getBProperty(),
                  "Check B: EnvironmentMapAmount=0 makes the render identical regardless of the cube map's own color");
        }

        // --- Check C: EnvironmentMapAmount=1, FresnelFactor=0 (flat) -> exact reflection pass-through ---
        {
            const Color black(0, 0, 0, 255);
            Texture2D tex(dev, 1, 1);
            tex.SetData(&black, 1);
            auto cube = MakeSolidCube(dev, Color(0, 200, 100, 255));

            dev.Clear(Color::Black);
            EnvironmentMapEffect fx(dev);
            fx.setTextureProperty(&tex);
            fx.setEnvironmentMapProperty(cube.get());
            fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            fx.setEnvironmentMapAmountProperty(1.0f);
            fx.setFresnelFactorProperty(0.0f); // disables Fresnel -- flat EnvironmentMapAmount blend
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(identityView);
            fx.setProjectionProperty(proj);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, kQuad, 0, 2);

            ExpectPixel("Check C: EnvironmentMapAmount=1/FresnelFactor=0 with zero base color exactly passes through the cube map color",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), Color(0, 200, 100, 255), /*tolerance=*/10);
        }

        // --- Check D: EnvironmentMapSpecular genuinely adds an additive highlight ---
        {
            const Color gray(80, 80, 80, 255);
            Texture2D tex(dev, 1, 1);
            tex.SetData(&gray, 1);
            auto cube = MakeSolidCube(dev, Color(255, 255, 255, 255));

            auto drawWithSpecular = [&](Vector3 specular) {
                dev.Clear(Color::Black);
                EnvironmentMapEffect fx(dev);
                fx.setTextureProperty(&tex);
                fx.setEnvironmentMapProperty(cube.get());
                fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.setEnvironmentMapAmountProperty(0.3f);
                fx.setEnvironmentMapSpecularProperty(specular);
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(identityView);
                fx.setProjectionProperty(proj);
                fx.Apply();
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, kQuad, 0, 2);
                return ReadCentre(dev);
            };

            const Color noSpecular = drawWithSpecular(Vector3(0.0f, 0.0f, 0.0f));
            const Color withSpecular = drawWithSpecular(Vector3(0.5f, 0.5f, 0.5f));
            Check(withSpecular.getRProperty() > noSpecular.getRProperty() &&
                  withSpecular.getGProperty() > noSpecular.getGProperty() &&
                  withSpecular.getBProperty() > noSpecular.getBProperty(),
                  "Check D: a non-zero EnvironmentMapSpecular renders measurably brighter than EnvironmentMapSpecular=0 (additive highlight)");
        }
    }

public:
    OpenGL4EnvironmentMapEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4EnvironmentMapEffectTest>();
}
