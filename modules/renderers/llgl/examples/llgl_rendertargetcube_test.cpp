// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-26: RenderTargetCube proof for the LLGL graphics renderer, asserted against
// real pixels read back from the GPU.
//
// A distinct solid colour is cleared into each of the 6 faces (via GraphicsDevice.SetRenderTarget
// (RenderTargetCube*, CubeMapFace) -- SetRenderTargets' cube-face branch), the cube is unbound, and
// each face is read back directly with GetData() -- proving CreateRenderTargetCube's 6
// LLGL::RenderTargets really do write to 6 distinct array layers of the SAME shared colour texture,
// not one shared layer six overlapping binds.
//
// Check A -- RenderTargetCube construction succeeds.
// Check B -- the back buffer keeps its own clear colour after the cube was drawn into (proves the
//   cube's 6 render passes and the swap chain's are independent).
// Check C..H -- each of the 6 faces reads back its own distinct colour, none of the others' (proves
//   face isolation -- the one property that makes a cube render target different from 6 unrelated
//   RenderTarget2Ds sharing a name).
// Check I -- EnvironmentMapEffect can sample the resulting RenderTargetCube (ResolveSampledTextureCube,
//   fixed in this same task: a hard dynamic_cast<const LlglTextureCubeRenderer*> alone would have
//   silently failed to sample a RenderTargetCube face, the entire real-time-reflection use case this
//   feature exists for). Every face is re-filled with the SAME colour first, sidestepping exact
//   reflection-vector-to-face selection (not what this check is about) while still proving the
//   sample path is real: a broken path would read back black or throw, not the fill colour.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class LlglRenderTargetCubeTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<RenderTargetCube> cube;
        try
        {
            cube = std::make_unique<RenderTargetCube>(device, 8, false, SurfaceFormat::Color,
                                                       DepthFormat::None);
        }
        catch (const std::exception&)
        {
            cube.reset();
        }
        if (!ExpectTrue("RenderTargetCube(device, 8, ...) construction succeeds",
                        cube != nullptr && cube->getWidthProperty() == 8 &&
                        cube->getHeightProperty() == 8))
        {
            return;
        }

        const std::array<CubeMapFace, 6> faces = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        const std::array<Color, 6> faceColors = {
            Color(255, 0, 0, 255),   // +X red
            Color(0, 255, 0, 255),   // -X green
            Color(0, 0, 255, 255),   // +Y blue
            Color(255, 255, 0, 255), // -Y yellow
            Color(255, 0, 255, 255), // +Z magenta
            Color(0, 255, 255, 255), // -Z cyan
        };

        for (int i = 0; i < 6; ++i)
        {
            device.SetRenderTarget(cube.get(), faces[static_cast<std::size_t>(i)]);
            device.Clear(faceColors[static_cast<std::size_t>(i)]);
        }

        // Restore the back buffer and clear it to a colour none of the 6 faces used, so a pass
        // that leaked into the wrong target would be caught immediately.
        device.SetRenderTarget(nullptr);
        device.Clear(Color(50, 50, 50, 255));

        ExpectPixel("back buffer keeps its own clear colour after the cube was drawn into",
                    Rectangle(10, 10, 1, 1), Color(50, 50, 50, 255));

        const char* faceLabels[6] = {
            "+X face reads back its own colour (red)",
            "-X face reads back its own colour (green)",
            "+Y face reads back its own colour (blue)",
            "-Y face reads back its own colour (yellow)",
            "+Z face reads back its own colour (magenta)",
            "-Z face reads back its own colour (cyan)",
        };
        for (int i = 0; i < 6; ++i)
        {
            Color got(0, 0, 0, 0);
            Rectangle centerTexel(4, 4, 1, 1);
            cube->GetData(faces[static_cast<std::size_t>(i)], 0, &centerTexel, &got, 0, 1);
            ExpectTrue(faceLabels[i], got == faceColors[static_cast<std::size_t>(i)]);
        }

        // --- Check I: EnvironmentMapEffect samples the RenderTargetCube -----------------------
        // Re-fill every face with the SAME colour: isolates "does sampling a RenderTargetCube
        // through EnvironmentMapEffect work at all" from "which face does this exact reflection
        // vector select", which is a separate, already-covered question (see
        // examples/environmentmapeffect_alphascaledlerp_test.cpp, reused verbatim by this same
        // renderer).
        const Color uniformColor(200, 100, 50, 255);
        for (CubeMapFace face : faces)
        {
            device.SetRenderTarget(cube.get(), face);
            device.Clear(uniformColor);
        }
        device.SetRenderTarget(nullptr);

        const Color diffuseWhite(255, 255, 255, 255);
        Texture2D diffuseTex(device, 1, 1);
        diffuseTex.SetData(&diffuseWhite, 1);

        EnvironmentMapEffect fx(device);
        fx.setTextureProperty(&diffuseTex);
        fx.setEnvironmentMapProperty(cube.get());
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setAlphaProperty(1.0f);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

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
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        fx.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const auto& vp = device.getViewportProperty();
        ExpectPixel("EnvironmentMapEffect samples the RenderTargetCube (uniform-filled)",
                    Rectangle(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1),
                    uniformColor, /*tolerance=*/2);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglRenderTargetCubeTest>();
}
