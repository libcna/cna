// SPDX-License-Identifier: MS-PL
// Task 890: EnvironmentMapEffect pixel test — DirectionalLight1/DirectionalLight2 forwarded
// (Bgfx renderer). See examples/easygl_environmentmapeffect_multilight_test.cpp for the full
// FNA-reference derivation and discrimination-trick rationale.
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
static const Vector3 kLightDir(0.0f, 0.0f, 1.0f);
static const Vector3 kLight1DirOffAxis(1.0f, 0.0f, 0.0f);
static const Vector3 kLight0Diffuse(0.6f, 0.0f, 0.0f);
static const Vector3 kLight1Diffuse(0.0f, 0.6f, 0.0f);
static const Vector3 kLight2Diffuse(0.0f, 0.0f, 0.6f);

// dot(-kLightDir, N) = 0.5 for all 3 lights when they share kLightDir.
static const Vector3 kNormal(0.8660254f, 0.0f, -0.5f);

// Check 1: 0.5*(L0+L1+L2) = (0.3,0.3,0.3) -> *255.
static const Color kExpectedAllLights(77, 77, 77, 255);
// Check 2: light2 disabled -> blue channel's 0.5*L2 term drops out.
static const Color kExpectedLight2Disabled(77, 77, 0, 255);
// Check 3: light1 rotated off-axis (NdotL1=0) -> green channel's 0.5*L1 term drops out.
static const Color kExpectedLight1OffAxis(77, 0, 77, 255);

class BgfxEnvironmentMapEffectMultiLightTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0, fail_ = 0;

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

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 8)
            && closeTo(c.getGProperty(), expected.getGProperty(), 8)
            && closeTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Bgfx's env-map path has no null-EnvironmentMap fallback (unlike EasyGL/Vulkan, which both
    // bind a default white cube); leaving it unset crashes with a GL_INVALID_OPERATION deep in
    // bgfx's own renderer. Bind a real (irrelevant, since EnvironmentMapAmount=0 below) cube so
    // this test isolates Task 890's own light-forwarding fix rather than tripping over that
    // separate, pre-existing gap.
    std::unique_ptr<TextureCube> makeCube(GraphicsDevice& dev, Color col)
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

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube, bool light2Enabled,
                      const Vector3& light1Dir)
    {
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(kLightDir);
        fx.DirectionalLight0.setDiffuseColorProperty(kLight0Diffuse);

        fx.DirectionalLight1.setEnabledProperty(true);
        fx.DirectionalLight1.setDirectionProperty(light1Dir);
        fx.DirectionalLight1.setDiffuseColorProperty(kLight1Diffuse);

        fx.DirectionalLight2.setEnabledProperty(light2Enabled);
        fx.DirectionalLight2.setDirectionProperty(kLightDir);
        fx.DirectionalLight2.setDiffuseColorProperty(kLight2Diffuse);

        const Vector3 tl(-1.0f,  1.0f, 0.0f), bl(-1.0f, -1.0f, 0.0f);
        const Vector3 br( 1.0f, -1.0f, 0.0f), tr( 1.0f,  1.0f, 0.0f);
        const Vector2 uv0(0.0f, 0.0f), uv1(0.0f, 1.0f), uv2(1.0f, 1.0f), uv3(1.0f, 0.0f);
        const VertexPositionNormalTexture q[6] = {
            { tl, kNormal, uv0 }, { bl, kNormal, uv1 }, { br, kNormal, uv2 },
            { tl, kNormal, uv0 }, { br, kNormal, uv2 }, { tr, kNormal, uv3 },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            // Task 896 finding: this quad's winding is culled under FNA's real default RasterizerState.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
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
        auto cube = makeCube(dev, Color(0, 255, 0, 255));

        const Color allLightsGot = renderWith(dev, tex, *cube, true, kLightDir);
        check(matches(allLightsGot, kExpectedAllLights),
              "All 3 lights == 0.5*(L0+L1+L2)",
              allLightsGot, "(77,77,77)");

        const Color light2OffGot = renderWith(dev, tex, *cube, false, kLightDir);
        check(matches(light2OffGot, kExpectedLight2Disabled),
              "DirectionalLight2.Enabled=false zeroes its contribution (blue channel drops)",
              light2OffGot, "(77,77,0)");

        const Color light1OffAxisGot = renderWith(dev, tex, *cube, true, kLight1DirOffAxis);
        check(matches(light1OffAxisGot, kExpectedLight1OffAxis),
              "DirectionalLight1 uses its own Direction field (green channel drops when rotated off-axis)",
              light1OffAxisGot, "(77,0,77)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxEnvironmentMapEffectMultiLightTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxEnvironmentMapEffectMultiLightTest game;
    game.Run();
    return game.getResult();
}
