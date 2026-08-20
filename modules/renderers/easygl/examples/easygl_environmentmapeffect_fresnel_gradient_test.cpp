// SPDX-License-Identifier: MS-PL
// Task 1112: EnvironmentMapEffect's per-vertex Fresnel term must Gouraud-interpolate across a
// triangle, not get recomputed per-fragment from a renormalized interpolated normal.
//
// Real XNA (EnvironmentMapEffect.fx's ComputeFresnelFactor) evaluates the Fresnel blend factor
// PER-VERTEX in the vertex shader from each vertex's own un-interpolated normal/eye vector, then
// linearly (Gouraud) interpolates the resulting SCALAR across the triangle -- it never recomputes
// dot()/pow() from an interpolated-and-renormalized normal in the pixel shader. The two are not
// equivalent once a triangle's vertices carry different normals.
//
// Geometry matches tools/xna-oracle/scenes/envmap_fresnel_quad.scene exactly (a quad split into
// 2 triangles, top edge normal=(0,0,1), bottom edge normal=(1,0,0), World=View=Projection=
// Identity, EyePosition=(0,0,0)). lighting=true with Light0 disabled and AmbientLight=0 zeroes
// the diffuse term so result = fresnelFactor * environmentMapColor exactly, isolating the
// Fresnel math from lighting.
//
// Hand-derived per-vertex Fresnel values (viewAngle = dot(eyeVector, normal), eyeVector =
// normalize(-vertexPos) since EyePosition=(0,0,0)):
//   Top edge    (y=0.6):  eyeVector.z=0 (quad/eye coplanar) -> viewAngle=0 -> fresnelFactor=1
//   Bottom edge (y=-0.6): |viewAngle|=0.70711 by symmetry   -> fresnelFactor=1-0.70711=0.29289
// Barycentric-interpolating the correct per-vertex scalars (not re-deriving per-pixel) at 3
// sample points along the vertical centre line (x=0) gives:
//   (0, 0.5):  weights (TL=0.41667, TR=0.5, BL=0.08333)              -> fresnel=0.94108
//   (0, 0.3):  weights (TL=0.25,    TR=0.5, BL=0.25)                 -> fresnel=0.82322
//   (0,-0.5):  weights (TR=0.08333, BR=0.41667, BL=0.5)              -> fresnel=0.35181
// (weights derived by solving P = sum(w_i * v_i) per triangle; BL/BR always carry the bottom-
// edge scalar, TL/TR the top-edge scalar). Expected colour = fresnel * (200,100,50).
//
// The bug being tested for (Task 1112, found via plans/plan_dx9.md's D9-A6 oracle cross-check): with
// the OLD per-fragment computation, interpolating and renormalizing the RAW NORMAL VECTORS
// (not the final scalar) then re-running dot/pow on the result does not preserve the true
// per-vertex-interpolated shape here -- most of the surface reads close to the top vertex's
// value, with only a narrow dip near the diagonal seam. The (0,0.3) sample point below is
// the discriminating check: it is 75% of the way from the seam to the top edge, so the old
// per-fragment code reads close to the top color there, while the correct per-vertex
// computation gives a genuinely different, smaller value (0.823 vs 1.0).
//
// Exit code 0 = all PASS, 1 = any FAIL.

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

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 256;

class EnvironmentMapFresnelGradientTest : public Game
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

    static bool colourMatch(Color got, Color want, int tol = 12)
    {
        return closeTo(got.getRProperty(), want.getRProperty(), tol)
            && closeTo(got.getGProperty(), want.getGProperty(), tol)
            && closeTo(got.getBProperty(), want.getBProperty(), tol);
    }

    // Reads the pixel at clip-space (ndcX, ndcY) — valid only because World=View=Projection
    // are all Identity in this test, so clip-space coordinates equal NDC directly.
    Color readAtNdc(GraphicsDevice& dev, float ndcX, float ndcY)
    {
        const int px = static_cast<int>((ndcX + 1.0f) * 0.5f * kSize);
        const int py = static_cast<int>((1.0f - ndcY) * 0.5f * kSize);
        const Rectangle reg(px, py, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);
        return got;
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

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.Clear(Color(0, 0, 0, 255));

        const Color kEnvColor(200, 100, 50, 255);
        Texture2D tex(dev, 1, 1);
        Color white(255, 255, 255, 255);
        tex.SetData(&white, 1);
        auto envCube = makeSolidCube(dev, kEnvColor);

        const Vector3 nTop(0.0f, 0.0f, 1.0f);
        const Vector3 nBottom(1.0f, 0.0f, 0.0f);
        // Matches tools/xna-oracle/scenes/envmap_fresnel_quad.scene's own vertex list exactly.
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-0.6f,  0.6f, 0.0f), nTop,    Vector2(0.0f, 0.0f) },
            { Vector3( 0.6f,  0.6f, 0.0f), nTop,    Vector2(1.0f, 0.0f) },
            { Vector3(-0.6f, -0.6f, 0.0f), nBottom, Vector2(0.0f, 1.0f) },
            { Vector3( 0.6f,  0.6f, 0.0f), nTop,    Vector2(1.0f, 0.0f) },
            { Vector3( 0.6f, -0.6f, 0.0f), nBottom, Vector2(1.0f, 1.0f) },
            { Vector3(-0.6f, -0.6f, 0.0f), nBottom, Vector2(0.0f, 1.0f) },
        };

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(envCube.get());
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
        fx.setFresnelFactorProperty(1.0f);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        // Barycentric-interpolated hand-derivations, see file header comment.
        Color top    = readAtNdc(dev, 0.0f,  0.5f);
        Color mid    = readAtNdc(dev, 0.0f,  0.3f);
        Color bottom = readAtNdc(dev, 0.0f, -0.5f);

        const Color kTop(   188,  94, 47, 255); // fresnel=0.94108
        const Color kMid(   165,  82, 41, 255); // fresnel=0.82322 -- the discriminating check
        const Color kBottom( 70,  35, 18, 255); // fresnel=0.35181

        check(colourMatch(top, kTop),
              "(a) near top edge (mostly top-normal vertices) -> high fresnel", top, kTop);
        check(colourMatch(mid, kMid),
              "(b) 75% toward top edge, interior point -> discriminates per-vertex vs per-fragment",
              mid, kMid);
        check(colourMatch(bottom, kBottom),
              "(c) near bottom edge (mostly bottom-normal vertices) -> low fresnel", bottom, kBottom);

        // A genuine smooth gradient must be monotonic top->mid->bottom, not "flat near top with a
        // narrow dip" (the pre-fix per-fragment symptom, Task 1112's own report language).
        check(top.getRProperty() > mid.getRProperty() && mid.getRProperty() > bottom.getRProperty(),
              "(d) monotonic gradient top > mid > bottom (rules out the pre-fix 'mostly flat' shape)",
              mid, Color(0, 0, 0, 255));

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    EnvironmentMapFresnelGradientTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapFresnelGradientTest game;
    game.Run();
    return game.getResult();
}
