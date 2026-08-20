// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: genuine cross-renderer visual-parity proof -- reuses
// examples/easygl_environmentmapeffect_golden_test.cpp's own combined EnvironmentMapEffect scene
// verbatim (same texture, cube map, matrices, effect parameters) and its own golden PNG
// (examples/golden/easygl_environmentmapeffect_golden_test.png, originally captured from the
// EasyGL/GLES3 renderer) via PixelTestGame::CompareGoldenImage(), instead of the single
// hand-picked centre pixel opengl2_environmentmapeffect_test.cpp's own Check C already covers.
// This is the first OpenGL2 test in this renderer's suite that compares actual rendered PIXELS,
// not just a numeric derivation, against another renderer's own output -- a real (llvmpipe
// software rasterizer, direct GL 2.1 shaders) vs (GLES3, EasyGL's own shader family) rendering
// path comparison, not a same-renderer regression check.
//
// See easygl_environmentmapeffect_golden_test.cpp's own header comment for the full per-term
// derivation of the scene's expected result; this file intentionally keeps the same tolerance=20
// (Task 462's own project-wide finding: exact pixel-perfect reproduction across this project's
// renderers/drivers is not realistic, only "close enough").
//
// Exit code 0 = PASS, 1 = FAIL.

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

class OpenGL2EnvironmentMapEffectGoldenTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Same real-camera backface-winding finding easygl_environmentmapeffect_golden_test.cpp
        // itself documents (its own Task 896 comment).
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

        // Cross-check against the same independently-derived expected value
        // opengl2_environmentmapeffect_test.cpp's own Check C already uses, so a regression in
        // either the numeric derivation or the golden PNG comparison is caught independently.
        ExpectPixel("combined-scene-vs-derived-expected", Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(151, 101, 76, 255), /*tolerance=*/20);
        CompareGoldenImage("environmentmapeffect-combined-scene-vs-easygl",
                            Rectangle(kSize / 2 - 4, kSize / 2 - 4, 8, 8),
                            "examples/golden/easygl_environmentmapeffect_golden_test.png",
                            /*tolerance=*/20);
    }

public:
    OpenGL2EnvironmentMapEffectGoldenTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2EnvironmentMapEffectGoldenTest>();
}
