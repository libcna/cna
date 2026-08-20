// SPDX-License-Identifier: MS-PL
// plans/plan_software.md Phase S9 (SOFTWARE-82): DualTextureEffect, EnvironmentMapEffect, and
// SkinnedEffect support in the Software renderer.
//
// None of these compute per-light diffuse lighting (design decision 6: no lighting engine in
// v1) -- their "lit" base color is just vertexColor*diffuseColor*texture0, the same
// simplification already used for the plain BasicEffect path. What IS genuinely new and tested
// here: DualTextureEffect's second-texture blend, EnvironmentMapEffect's real cube-map storage +
// reflection-vector sampling, and SkinnedEffect's real per-vertex bone-transform skinning.
//
// Check A -- DualTextureEffect: two solid-color textures combine as
//   `(tex0*2) * tex1 * diffuse` (FNA's own PSDualTexture formula), verified against a
//   hand-computed expected color.
// Check B -- EnvironmentMapEffect: a camera-facing quad (normal pointing straight at the eye)
//   reflects exactly along the view axis -- verified to sample the cube's PositiveZ face (set to
//   a distinctive color), not any other face.
// Check C -- EnvironmentMapEffect: EnvironmentMapAmount=0 makes the env map contribute nothing --
//   the pixel shows the plain base texture color instead of the cube face color.
// Check D -- SkinnedEffect: a vertex fully bound to a bone with a known translation renders at
//   the translated screen position, not the original bind-pose position.
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
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    int g_passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++g_passCount;
    }

    bool Close(int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; }
    constexpr int kSize = 64;
}

class SoftwareDualEnvmapSkinnedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool AnyMatchNear(GraphicsDevice& dev, int cx, int cy, int radius,
                      const Color& expected, int tolerance)
    {
        for (int y = std::max(0, cy - radius); y <= std::min(kSize - 1, cy + radius); ++y)
        {
            for (int x = std::max(0, cx - radius); x <= std::min(kSize - 1, cx + radius); ++x)
            {
                const Color p = ReadPixel(dev, x, y);
                if (Close(p.getRProperty(), expected.getRProperty(), tolerance) &&
                    Close(p.getGProperty(), expected.getGProperty(), tolerance) &&
                    Close(p.getBProperty(), expected.getBProperty(), tolerance))
                    return true;
            }
        }
        return false;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Check A: DualTextureEffect. tex0=(100,100,100), tex1=(200,200,200) ->
        // (100/255*2) * (200/255) * 255 = 156.8 per channel (all channels identical here).
        {
            dev.Clear(Color::Black, 1.0f);
            const std::vector<std::uint8_t> tex0Pixels = {100, 100, 100, 255};
            Texture2D tex0 = Texture2D::CreateFromPixels(dev, 1, 1, tex0Pixels);
            const std::vector<std::uint8_t> tex1Pixels = {200, 200, 200, 255};
            Texture2D tex1 = Texture2D::CreateFromPixels(dev, 1, 1, tex1Pixels);

            const VertexPositionNormalTexture verts[6] = {
                {Vector3(-1.0f, 1.0f, 0.5f), Vector3(0, 0, 1), Vector2(0.0f, 0.0f)},
                {Vector3(-1.0f, -1.0f, 0.5f), Vector3(0, 0, 1), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, 1.0f, 0.5f), Vector3(0, 0, 1), Vector2(1.0f, 0.0f)},
                {Vector3(1.0f, 1.0f, 0.5f), Vector3(0, 0, 1), Vector2(1.0f, 0.0f)},
                {Vector3(-1.0f, -1.0f, 0.5f), Vector3(0, 0, 1), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, -1.0f, 0.5f), Vector3(0, 0, 1), Vector2(1.0f, 1.0f)},
            };
            VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 6);

            DualTextureEffect fx(dev);
            fx.setTextureProperty(&tex0);
            fx.setTexture2Property(&tex1);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);

            const Color center = ReadPixel(dev, 32, 32);
            Check(Close(center.getRProperty(), 157, 8) && Close(center.getGProperty(), 157, 8) &&
                  Close(center.getBProperty(), 157, 8),
                  "DualTextureEffect: (tex0*2)*tex1*diffuse matches the hand-computed color");
        }

        // Checks B/C: EnvironmentMapEffect. World=View=identity -> eye at (0,0,0). A quad at
        // Z=-2 facing the camera (Normal=(0,0,1)) has eyeVector=(0,0,1)=Normal, so
        // reflect(-E,N)=(0,0,1) -- must sample the cube's PositiveZ face exactly.
        {
            TextureCube cube(dev, 4, false, SurfaceFormat::Color);
            const std::vector<Color> posZ(16, Color(10, 200, 10, 255));   // distinctive green
            const std::vector<Color> otherFace(16, Color(10, 10, 200, 255)); // distinctive blue
            cube.SetData(CubeMapFace::PositiveZ, posZ.data(), 16);
            cube.SetData(CubeMapFace::NegativeZ, otherFace.data(), 16);
            cube.SetData(CubeMapFace::PositiveX, otherFace.data(), 16);
            cube.SetData(CubeMapFace::NegativeX, otherFace.data(), 16);
            cube.SetData(CubeMapFace::PositiveY, otherFace.data(), 16);
            cube.SetData(CubeMapFace::NegativeY, otherFace.data(), 16);

            const std::vector<std::uint8_t> whitePixels = {255, 255, 255, 255};
            Texture2D white = Texture2D::CreateFromPixels(dev, 1, 1, whitePixels);

            const VertexPositionNormalTexture verts[6] = {
                {Vector3(-1.0f, 1.0f, -2.0f), Vector3(0, 0, 1), Vector2(0.0f, 0.0f)},
                {Vector3(-1.0f, -1.0f, -2.0f), Vector3(0, 0, 1), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, 1.0f, -2.0f), Vector3(0, 0, 1), Vector2(1.0f, 0.0f)},
                {Vector3(1.0f, 1.0f, -2.0f), Vector3(0, 0, 1), Vector2(1.0f, 0.0f)},
                {Vector3(-1.0f, -1.0f, -2.0f), Vector3(0, 0, 1), Vector2(0.0f, 1.0f)},
                {Vector3(1.0f, -1.0f, -2.0f), Vector3(0, 0, 1), Vector2(1.0f, 1.0f)},
            };
            VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
            vb.SetData(verts, 6);

            // Check B: full env-map override, no Fresnel view-angle weighting (FresnelFactor=0),
            // so blendFactor==EnvironmentMapAmount exactly regardless of view angle.
            {
                dev.Clear(Color::Black, 1.0f);
                EnvironmentMapEffect fx(dev);
                fx.setTextureProperty(&white);
                fx.setEnvironmentMapProperty(&cube);
                fx.setEnvironmentMapAmountProperty(1.0f);
                fx.setFresnelFactorProperty(0.0f);
                fx.Apply();
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);

                const Color center = ReadPixel(dev, 32, 32);
                Check(Close(center.getRProperty(), 10, 15) && Close(center.getGProperty(), 200, 15) &&
                      Close(center.getBProperty(), 10, 15),
                      "EnvironmentMapEffect: a camera-facing reflection samples the PositiveZ cube face");
            }

            // Check C: EnvironmentMapAmount=0 -- env map contributes nothing, base texture shows.
            {
                dev.Clear(Color::Black, 1.0f);
                EnvironmentMapEffect fx(dev);
                fx.setTextureProperty(&white);
                fx.setEnvironmentMapProperty(&cube);
                fx.setEnvironmentMapAmountProperty(0.0f);
                fx.setFresnelFactorProperty(0.0f);
                fx.Apply();
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);

                const Color center = ReadPixel(dev, 32, 32);
                Check(Close(center.getRProperty(), 255, 10) && Close(center.getGProperty(), 255, 10) &&
                      Close(center.getBProperty(), 255, 10),
                      "EnvironmentMapEffect: EnvironmentMapAmount=0 shows the plain base texture, no cube contribution");
            }
        }

        // Check D: SkinnedEffect. A small triangle near screen center, fully bound to bone[1]
        // (a +0.5 world-X translation) -- must render shifted right, not at its bind-pose position.
        {
            dev.Clear(Color::Black, 1.0f);
            const std::vector<std::uint8_t> whitePixels = {255, 255, 255, 255};
            Texture2D white = Texture2D::CreateFromPixels(dev, 1, 1, whitePixels);

            const Vector4 fullWeightBone1(1.0f, 0.0f, 0.0f, 0.0f);
            const std::array<std::uint8_t, 4> bone1{1, 0, 0, 0};
            const VertexPositionNormalTextureSkinned verts[3] = {
                {Vector3(0.0f, 0.3f, -2.0f), Vector3(0, 0, 1), Vector2(0.5f, 0.0f), fullWeightBone1, bone1},
                {Vector3(-0.3f, -0.3f, -2.0f), Vector3(0, 0, 1), Vector2(0.0f, 1.0f), fullWeightBone1, bone1},
                {Vector3(0.3f, -0.3f, -2.0f), Vector3(0, 0, 1), Vector2(1.0f, 1.0f), fullWeightBone1, bone1},
            };
            VertexBuffer vb(dev, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 3, BufferUsage::None);
            vb.SetData(verts, 3);

            SkinnedEffect fx(dev);
            fx.setTextureProperty(&white);
            fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
            std::vector<Matrix> bones(2, Matrix::getIdentityProperty());
            bones[1] = Matrix::CreateTranslation(0.5f, 0.0f, 0.0f);
            fx.SetBoneTransforms(bones);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
            dev.SetVertexBuffer(nullptr);

            const Color red(255, 0, 0, 255);
            const bool shiftedIsRed = AnyMatchNear(dev, 40, 33, 3, red, 20);
            const bool originalIsRed = AnyMatchNear(dev, 24, 33, 2, red, 20);
            Check(shiftedIsRed && !originalIsRed,
                  "SkinnedEffect: a vertex fully bound to a translated bone renders at the shifted position, not the bind pose");
        }

        std::printf("=== %d/%d PASS ===\n", g_passCount, 4);
        result_ = (g_passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    SoftwareDualEnvmapSkinnedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareDualEnvmapSkinnedTest game;
    game.Run();
    return game.getResult();
}
