// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-25: real fog for the OpenGL4 graphics renderer -- adds fog uniforms
// (uFogEnabled/uFogColor/uFogStart/uFogEnd) and Task 1111's own already-verified fog-factor
// formula (matches FNA's EffectHelpers.SetFogVector/Common.fxh ComputeFogFactor exactly when
// World=View=Identity) to all 7 GpuDrawParams-driven stride/dispatch shaders: the new
// coloredParams3d (stride 16), textured3d (20), colored_textured3d (24), lit_textured3d (32),
// env_map3d (32, envMapping variant), skinned3d (52/56), and pbr3d/pbr_skinned3d (48/68).
//
// Along the way, GL4-25 also closed a real, separate parity gap: BindProgramForStride previously
// had no stride-16 (VertexPositionColor) case at all, so ANY stride-16 draw issued via a real
// Effect.Apply() silently fell back to the GpuDrawParams-free DrawColoredPrimitives path --
// DiffuseColor/VertexColorEnabled/AlphaTest/fog were all unavailable to it, unlike every other
// stride. The new coloredParams3d program (a separate program from the legacy
// colored3DProgram_, which stays reserved for DrawColoredPrimitives's own params-free callers)
// closes that gap.
//
// Checks A-C port easygl_basiceffect_fog_test.cpp's own already-cross-renderer-verified oracle
// verbatim for the new coloredParams3d (stride 16) program: fog disabled -> pure geometry colour;
// 50% fog (Z=0 with FogStart=-0.9/FogEnd=0.9) -> an exact purple blend; full fog (Z=FogStart)
// -> exact fog colour.
//
// Checks D-H use a simpler, still-exact, effect-agnostic proof for the remaining 5 shader
// families: mix(fogColor, colour, 0) == fogColor EXACTLY, regardless of what "colour" the
// effect's own lighting/texture/BRDF math would otherwise produce -- so placing each quad's Z
// exactly at FogStart (making the fog factor exactly 0) predicts the rendered pixel must read
// EXACTLY FogColor, without needing to hand-derive each effect's own lit/textured/BRDF result
// under fog. A distinctive, none-of-the-effects'-own-palette fog colour (cyan) makes this a
// decisive, not-accidentally-passing check.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "common/PixelTestGame.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr float kFogStart = -0.9f;
    constexpr float kFogEnd = 0.9f;
    const Color kFogCyan(0, 255, 255, 255);

    // Skinned vertex: matches OpenGL4VertexBufferRenderer::ApplyLayout's stride-52 case.
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

    // PBR vertex: matches OpenGL4VertexBufferRenderer::ApplyLayout's stride-48 case.
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

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

class OpenGL4FogTest : public CNA::Examples::PixelTestGame
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

        const Matrix identity = Matrix::getIdentityProperty();

        // --- Checks A-C: coloredParams3d (stride 16) -- exact oracle port from
        // easygl_basiceffect_fog_test.cpp ---
        {
            const Color kBlue(0, 0, 255, 255);
            const Color kRed(255, 0, 0, 255);

            auto makeQuad = [](float z, Color col) {
                return std::vector<VertexPositionColor>{
                    {Vector3(-1, 1, z), col}, {Vector3(-1, -1, z), col}, {Vector3(1, -1, z), col},
                    {Vector3(-1, 1, z), col}, {Vector3(1, -1, z), col}, {Vector3(1, 1, z), col},
                };
            };

            // A: fog disabled, Z=0 -> pure blue.
            {
                dev.Clear(Color::Black);
                BasicEffect fx(dev);
                fx.setWorldProperty(identity);
                fx.setViewProperty(identity);
                fx.setProjectionProperty(identity);
                fx.VertexColorEnabled = true;
                fx.setFogEnabledProperty(false);
                fx.Apply();
                const auto quad = makeQuad(0.0f, kBlue);
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
                const Color got = ReadCentre(dev);
                Check(got.getBProperty() > 200 && got.getRProperty() < 50 && got.getGProperty() < 50,
                      "Check A: fog disabled -> pure geometry colour (blue)");
            }

            // B: fog 50%, Z=0, FogStart=-0.9/FogEnd=0.9/FogColor=red -> purple mix (128,0,128).
            {
                dev.Clear(Color::Black);
                BasicEffect fx(dev);
                fx.setWorldProperty(identity);
                fx.setViewProperty(identity);
                fx.setProjectionProperty(identity);
                fx.VertexColorEnabled = true;
                fx.setFogEnabledProperty(true);
                fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
                fx.setFogStartProperty(kFogStart);
                fx.setFogEndProperty(kFogEnd);
                fx.Apply();
                const auto quad = makeQuad(0.0f, kBlue);
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
                const Color got = ReadCentre(dev);
                const int r = got.getRProperty(), g = got.getGProperty(), b = got.getBProperty();
                Check(std::abs(r - 128) <= 30 && g < 30 && std::abs(b - 128) <= 30,
                      "Check B: 50% fog (Z=0, FogStart=-0.9/FogEnd=0.9) -> purple mix (128,0,128)");
            }

            // C: full fog, Z=FogStart=-0.9 -> exact fog colour (red).
            {
                dev.Clear(Color::Black);
                BasicEffect fx(dev);
                fx.setWorldProperty(identity);
                fx.setViewProperty(identity);
                fx.setProjectionProperty(identity);
                fx.VertexColorEnabled = true;
                fx.setFogEnabledProperty(true);
                fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
                fx.setFogStartProperty(kFogStart);
                fx.setFogEndProperty(kFogEnd);
                fx.Apply();
                const auto quad = makeQuad(kFogStart, kBlue);
                dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
                const Color got = ReadCentre(dev);
                Check(got.getRProperty() > 200 && got.getGProperty() < 50 && got.getBProperty() < 50,
                      "Check C: full fog (Z=FogStart) -> exact fog colour (red)");
            }
        }

        // --- Checks D-H: mix(fogColor, colour, 0) == fogColor EXACTLY when Z==FogStart,
        // regardless of the underlying effect's own lit/textured/BRDF result. ---
        const auto setCommonFog = [&](auto& fx) {
            fx.setFogEnabledProperty(true);
            fx.setFogColorProperty(Vector3(0.0f, 1.0f, 1.0f)); // cyan
            fx.setFogStartProperty(kFogStart);
            fx.setFogEndProperty(kFogEnd);
        };

        // D: textured3d (BasicEffect + Texture).
        {
            dev.Clear(Color::Black);
            const Color red(255, 0, 0, 255);
            Texture2D tex(dev, 1, 1);
            tex.SetData(&red, 1);
            BasicEffect fx(dev);
            fx.setWorldProperty(identity);
            fx.setViewProperty(identity);
            fx.setProjectionProperty(identity);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(&tex);
            setCommonFog(fx);
            fx.Apply();
            const VertexPositionTexture quad[6] = {
                {Vector3(-1, 1, kFogStart), Vector2(0, 0)}, {Vector3(-1, -1, kFogStart), Vector2(0, 1)},
                {Vector3(1, -1, kFogStart), Vector2(1, 1)}, {Vector3(-1, 1, kFogStart), Vector2(0, 0)},
                {Vector3(1, -1, kFogStart), Vector2(1, 1)}, {Vector3(1, 1, kFogStart), Vector2(1, 0)},
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            ExpectPixel("Check D: textured3d -- full fog at Z=FogStart reads exact fog colour (cyan)",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), kFogCyan, /*tolerance=*/5);
        }

        // E: lit_textured3d (BasicEffect + Normal + EnableDefaultLighting).
        {
            dev.Clear(Color::Black);
            BasicEffect fx(dev);
            fx.setWorldProperty(identity);
            fx.setViewProperty(identity);
            fx.setProjectionProperty(identity);
            fx.EnableDefaultLighting();
            setCommonFog(fx);
            fx.Apply();
            const Vector3 n(0, 0, 1);
            const VertexPositionNormalTexture quad[6] = {
                {Vector3(-1, 1, kFogStart), n, Vector2(0, 0)}, {Vector3(-1, -1, kFogStart), n, Vector2(0, 1)},
                {Vector3(1, -1, kFogStart), n, Vector2(1, 1)}, {Vector3(-1, 1, kFogStart), n, Vector2(0, 0)},
                {Vector3(1, -1, kFogStart), n, Vector2(1, 1)}, {Vector3(1, 1, kFogStart), n, Vector2(1, 0)},
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            ExpectPixel("Check E: lit_textured3d -- full fog at Z=FogStart reads exact fog colour (cyan)",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), kFogCyan, /*tolerance=*/5);
        }

        // F: env_map3d (EnvironmentMapEffect).
        {
            dev.Clear(Color::Black);
            const Color white(255, 255, 255, 255);
            Texture2D tex(dev, 1, 1);
            tex.SetData(&white, 1);
            auto cube = MakeSolidCube(dev, Color(255, 0, 0, 255));
            EnvironmentMapEffect fx(dev);
            fx.setWorldProperty(identity);
            fx.setViewProperty(identity);
            fx.setProjectionProperty(identity);
            fx.setTextureProperty(&tex);
            fx.setEnvironmentMapProperty(cube.get());
            fx.setEnvironmentMapAmountProperty(1.0f);
            setCommonFog(fx);
            fx.Apply();
            const Vector3 n(0, 0, 1);
            const VertexPositionNormalTexture quad[6] = {
                {Vector3(-1, 1, kFogStart), n, Vector2(0, 0)}, {Vector3(-1, -1, kFogStart), n, Vector2(0, 1)},
                {Vector3(1, -1, kFogStart), n, Vector2(1, 1)}, {Vector3(-1, 1, kFogStart), n, Vector2(0, 0)},
                {Vector3(1, -1, kFogStart), n, Vector2(1, 1)}, {Vector3(1, 1, kFogStart), n, Vector2(1, 0)},
            };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            ExpectPixel("Check F: env_map3d -- full fog at Z=FogStart reads exact fog colour (cyan)",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), kFogCyan, /*tolerance=*/5);
        }

        // G: skinned3d (SkinnedEffect, identity bone).
        {
            dev.Clear(Color::Black);
            const Color red(255, 0, 0, 255);
            Texture2D tex(dev, 1, 1);
            tex.SetData(&red, 1);
            SkinnedEffect fx(dev);
            fx.setWorldProperty(identity);
            fx.setViewProperty(identity);
            fx.setProjectionProperty(identity);
            fx.setTextureProperty(&tex);
            std::vector<Matrix> bones = {Matrix::getIdentityProperty()};
            fx.SetBoneTransforms(bones);
            fx.setWeightsPerVertexProperty(1);
            fx.EnableDefaultLighting();
            setCommonFog(fx);
            fx.Apply();
            const SkinnedGpuVertex verts[6] = {
                {-1, 1, kFogStart, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0},
                {-1, -1, kFogStart, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0},
                {1, -1, kFogStart, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0},
                {-1, 1, kFogStart, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0},
                {1, -1, kFogStart, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0},
                {1, 1, kFogStart, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0},
            };
            VertexBuffer vb(dev, 6);
            vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            ExpectPixel("Check G: skinned3d -- full fog at Z=FogStart reads exact fog colour (cyan)",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), kFogCyan, /*tolerance=*/5);
        }

        // H: pbr3d (PbrEffect).
        {
            dev.Clear(Color::Black);
            const Color red(255, 0, 0, 255);
            Texture2D tex(dev, 1, 1);
            tex.SetData(&red, 1);
            PbrEffect fx(dev);
            fx.setWorldProperty(identity);
            fx.setViewProperty(identity);
            fx.setProjectionProperty(identity);
            fx.setTextureProperty(&tex);
            fx.setRoughnessFactorProperty(1.0f);
            fx.setMetallicFactorProperty(0.0f);
            fx.EnableDefaultLighting();
            setCommonFog(fx);
            fx.Apply();
            const PbrGpuVertex verts[6] = {
                {-1, 1, kFogStart, 0, 0, 1, 1, 0, 0, 1, 0, 0}, {-1, -1, kFogStart, 0, 0, 1, 1, 0, 0, 1, 0, 1},
                {1, -1, kFogStart, 0, 0, 1, 1, 0, 0, 1, 1, 1}, {-1, 1, kFogStart, 0, 0, 1, 1, 0, 0, 1, 0, 0},
                {1, -1, kFogStart, 0, 0, 1, 1, 0, 0, 1, 1, 1}, {1, 1, kFogStart, 0, 0, 1, 1, 0, 0, 1, 1, 0},
            };
            VertexBuffer vb(dev, 6);
            vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(PbrGpuVertex)));
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            ExpectPixel("Check H: pbr3d -- full fog at Z=FogStart reads exact fog colour (cyan)",
                        Rectangle(kSize / 2, kSize / 2, 1, 1), kFogCyan, /*tolerance=*/5);
        }
    }

public:
    OpenGL4FogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4FogTest>();
}
