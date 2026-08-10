// SPDX-License-Identifier: MS-PL
// Task 469: golden-image consumer reusing Phase 45's already-verified EnvironmentMapEffect
// capstone scene (Task 399, examples/easygl_environmentmapeffect_combined_test.cpp) via
// PixelTestGame::CompareGoldenImage() (Task 463) instead of Task 399's own single hand-picked
// centre pixel.
//
// Recreates Task 399's exact combined scene: a 1x1 (200,100,50) texture, a solid translucent
// cube map (alpha=128), EmissiveColor=(0.5,0.5,0.5), EnvironmentMapAmount=1,
// EnvironmentMapSpecular=(0.4,0.4,0.4), FresnelFactor=1 (real FNA default), a non-identity
// World=CreateScale(2,1,1), and a real (non-identity) View/Projection camera. Expected
// (Task 399's own derivation): baseColor=EmissiveColor*Texture=(100,50,25); Fresnel-suppressed
// blend is negligible at this near-head-on view; rgb ~= baseColor + EnvironmentMapSpecular *
// envmap.a * combinedAlpha ~= (100,50,25) + (51,51,51) = (151,101,76). See Task 399's own file
// for the full per-term derivation.
//
// Reuses Task 399's own tolerance=20.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

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
}

class EnvironmentMapEffectGoldenTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Task 896 finding -- same as Task 399's own comment.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kTex(200, 100, 50, 255);
        const Color kTranslucentCube(0, 0, 0, 128);

        Texture2D tex(device, 1, 1);
        tex.SetData(&kTex, 1);
        auto cube = MakeSolidCube(device, kTranslucentCube);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        device.Clear(Color(0, 0, 0, 255));
        EnvironmentMapEffect fx(device);
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
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        // Cross-check against Task 399's own derived expected value, independent of the golden
        // PNG's own contents (same rationale as Tasks 464-468).
        ExpectPixel("combined-scene-vs-task399-expected", Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(151, 101, 76, 255), /*tolerance=*/20);
        CompareGoldenImage("environmentmapeffect-combined-scene",
                            Rectangle(kSize / 2 - 4, kSize / 2 - 4, 8, 8),
                            "examples/golden/easygl_environmentmapeffect_golden_test.png",
                            /*tolerance=*/20);
    }

public:
    EnvironmentMapEffectGoldenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<EnvironmentMapEffectGoldenTest>();
}
