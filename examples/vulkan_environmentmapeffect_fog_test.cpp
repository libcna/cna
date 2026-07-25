// SPDX-License-Identifier: MS-PL
// Task 899: EnvironmentMapEffect linear fog pixel integration test — Vulkan backend.
//
// env_map3d was Task 899's own noted cheap leftover, deliberately left out of the task's title
// scope (5 pipelines sharing FillExtPushConst()'s fully-packed push constant) since its existing
// EnvMapParams UBO (set=0, binding=2) already had ~160 spare bytes past the 96 originally used
// (eyePos/diffuse/emissive+envMapAmount/light0Dir/light0Diffuse+fresnelEnabled/envMapSpecular+
// fresnelFactor). Fog packed into those spare bytes at [24..31] (fogColorEnabled/fogStartEnd),
// mirroring lit_textured3d.vert.glsl/.frag.glsl's identical "fog packed into an existing UBO's
// spare tail" pattern.
//
// Formula (matches the established Task 888 formula exactly):
//   fogFactor = clamp((Z + FogEnd) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB  = mix(FogColor, geomRGB, fogFactor)
//
// EnvironmentMapAmount=0 removes the cube map's contribution entirely, and
// EnvironmentMapSpecular=0 removes its additive specular term, so the pre-fog color reduces to
// exactly EmissiveColor -- isolating fog cleanly from the reflection math. Direct port of
// examples/bgfx_environmentmapeffect_fog_test.cpp / examples/easygl_environmentmapeffect_fog_test.cpp
// (Task 900).
//
// REMED-GFX-011: this test previously omitted RasterizerState::CullNone, justified by a comment
// claiming Vulkan's default cull state is effectively no-culling and that no sibling test sets it.
// Both claims were false -- all nine other Vulkan EnvironmentMapEffect pixel tests do set it. The
// quad survived only because env_map3d.vert.glsl was missing the backend-wide NDC Y-flip, and that
// omission also inverted the effective winding. With the flip restored this quad is back-facing
// under the real default state and is culled to black, exactly as in REMED-GFX-052.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

static const Color kWhite(255, 255, 255, 255);
static const Color kBlack(0, 0, 0, 255);
static const Color kBlue(0, 0, 255, 255);
static const Color kRed(255, 0, 0, 255);

class VulkanEnvironmentMapEffectFogTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
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

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected, int tol = 30)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), tol)
            && closeTo(c.getGProperty(), expected.getGProperty(), tol)
            && closeTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    std::unique_ptr<TextureCube> makeSolidCube(GraphicsDevice& dev, Color col)
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

    Color renderQuad(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube, float z,
                      bool fogEnabled, const Vector3& fogColor, float fogStart, float fogEnd)
    {
        EnvironmentMapEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(fogColor);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, z), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, z), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, z), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, z), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, z), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, z), n, Vector2(1.0f, 1.0f) },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);
        auto cube = makeSolidCube(dev, Color(0, 255, 0, 255)); // green -- would leak if Amount!=0

        // (a) Fog disabled: blue quad at Z=0 -> pure blue.
        const Color gotOff = renderQuad(dev, tex, *cube, 0.0f, false, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotOff, kBlue), "(a) fog OFF: blue quad -> pure blue", gotOff, "(0,0,255)");

        // (b) Fog 50%: Z=0.45, FogStart=0, FogEnd=-0.9 (see header) -> factor=(0.45-0.9)/(-0.9)=0.5 -> mix(red,blue,0.5)=(128,0,128).
        const Color gotHalf = renderQuad(dev, tex, *cube, 0.45f, true, Vector3(1, 0, 0), 0.0f, -0.9f);
        check(matches(gotHalf, Color(128, 0, 128, 255)),
              "(b) fog 50%: Z=0.45 -> purple mix", gotHalf, "(128,0,128)");

        // (c) Full fog: FogStart=0, FogEnd=-0.9, Z=0.9 -> factor=(0.9-0.9)/(-0.9)=0 -> pure red.
        const Color gotFull = renderQuad(dev, tex, *cube, 0.9f, true, Vector3(1, 0, 0), 0.0f, -0.9f);
        check(matches(gotFull, kRed), "(c) full fog: FogStart=0,FogEnd=-0.9,Z=0.9 -> full red", gotFull, "(255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    VulkanEnvironmentMapEffectFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanEnvironmentMapEffectFogTest game;
    game.Run();
    return game.getResult();
}
