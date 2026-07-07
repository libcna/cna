// SPDX-License-Identifier: MS-PL
// Task 397: verify EnvironmentMapEffect's reflection vector responds to EyePosition
// (EasyGL backend).
//
// EyePosition is derived from the inverse View matrix (`Matrix::Invert(view_).Translation`)
// and used to compute `eyeVector = normalize(EyePosition - worldPos)`, which in turn drives
// the reflection vector `reflDir = reflect(-eyeVector, worldNormal)` used to sample the cube
// map. Every prior env-map test in this phase (393-396) used solid-color cube maps, which
// cannot detect a wrong reflection vector -- every face samples the same value regardless of
// which face is actually hit. This test uses a cube map with a DISTINCT color per face and 2
// different camera positions that should hit 2 different, clearly dominant faces.
//
// EnvironmentMapAmount=1 and FresnelFactor=0 (explicitly disabling Task 396's newly-added
// Fresnel edge-weighting) so the blend factor is the flat 1.0 regardless of view angle,
// isolating this test purely to reflection-vector correctness. EmissiveColor=0 (and no lights
// enabled) zeroes the lit/textured contribution, so the output is exactly the sampled cube
// face color with no other confound.
//
// (a) Eye straight on at (0,0,3), quad centered at the origin with normal (0,0,1): eyeVector
//     = (0,0,1) = the normal itself, so reflDir = reflect((0,0,-1),(0,0,1)) = (0,0,1) exactly
//     -- dominant +Z -- expect the PositiveZ face's color.
// (b) Eye off-axis at (5,0,0.5): eyeVector ~= (0.995,0,0.0995), reflDir ~= (-0.995,0,0.0995)
//     -- dominant -X by a 10x margin -- expect the NegativeX face's color, DIFFERENT from (a).
//     `Matrix::CreateLookAt`'s target stays at the origin in both cases, so the quad's centre
//     always projects to the screen centre regardless of how oblique the eye position is.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
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

class EnvironmentMapEyePositionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool cond, const char* label, Color got, Color want)
    {
        if (cond)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected≈(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool colourMatch(Color got, Color want, int tol = 20)
    {
        return closeTo(got.getRProperty(), want.getRProperty(), tol)
            && closeTo(got.getGProperty(), want.getGProperty(), tol)
            && closeTo(got.getBProperty(), want.getBProperty(), tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    std::unique_ptr<TextureCube> makeDistinctCube(GraphicsDevice& dev)
    {
        auto cube = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        Color posX(255, 0, 0, 255);   // red
        Color negX(0, 255, 255, 255); // cyan
        Color posY(0, 255, 0, 255);   // green
        Color negY(255, 0, 255, 255); // magenta
        Color posZ(0, 0, 255, 255);   // blue
        Color negZ(255, 255, 0, 255); // yellow
        cube->SetData(CubeMapFace::PositiveX, &posX, 1);
        cube->SetData(CubeMapFace::NegativeX, &negX, 1);
        cube->SetData(CubeMapFace::PositiveY, &posY, 1);
        cube->SetData(CubeMapFace::NegativeY, &negY, 1);
        cube->SetData(CubeMapFace::PositiveZ, &posZ, 1);
        cube->SetData(CubeMapFace::NegativeZ, &negZ, 1);
        return cube;
    }

    Color renderWith(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube,
                      const VertexPositionNormalTexture (&quad)[6],
                      const Matrix& view, const Matrix& proj)
    {
        dev.Clear(Color(0, 0, 0, 255));
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setFresnelFactorProperty(0.0f); // disable Task 396's Fresnel weighting for this test
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return readCenter(dev);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);

        const Color kTex(255, 255, 255, 255);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto cube = makeDistinctCube(dev);

        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
        };

        Matrix proj = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);

        // (a) Straight-on eye → reflDir=(0,0,1) exactly → PositiveZ face (blue).
        Matrix viewA = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        Color a = renderWith(dev, tex, *cube, quad, viewA, proj);
        check(colourMatch(a, Color(0, 0, 255, 255)),
              "(a) eye straight on (0,0,3) → PositiveZ face (blue)",
              a, Color(0, 0, 255, 255));

        // (b) Off-axis eye → reflDir dominant -X by a 10x margin → NegativeX face (cyan).
        Matrix viewB = Matrix::CreateLookAt(Vector3(5.0f, 0.0f, 0.5f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
        Color b = renderWith(dev, tex, *cube, quad, viewB, proj);
        check(colourMatch(b, Color(0, 255, 255, 255)),
              "(b) eye off-axis (5,0,0.5) → NegativeX face (cyan), different from (a)",
              b, Color(0, 255, 255, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapEyePositionTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapEyePositionTest game;
    game.Run();
    return game.getResult();
}
