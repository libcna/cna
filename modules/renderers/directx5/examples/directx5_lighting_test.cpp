// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O9 (DX2-91..DX2-96): real CPU-side BasicEffect-style per-vertex lighting for
// the normal-bearing vertex layouts (stride 32 = VertexPositionNormalTexture), computed on the CPU
// before packing into D3DTLVERTEX, with the specular highlight composited by real Direct3D
// fixed-function hardware via D3DRENDERSTATE_SPECULARENABLE (spike-confirmed real,
// dx2_spike10_specular_wireframe_aniso.cpp Test C).
//
// All checks use World=View=Projection=Identity (BasicEffect's own defaults, matching every other
// DIRECTX2 3D CTest's convention) with a small quad at z=0.9, Normal=(0,0,-1) -- since View=Identity
// puts eyePositionWorld at the origin (Matrix::Invert(Identity).Translation=(0,0,0)), a surface at
// z=0.9 facing back toward the origin (-Z) is directly lit/viewed close to head-on. The quad is
// deliberately kept SMALL (NDC half-extent 0.12, not a full-viewport quad): lighting here is
// evaluated per-VERTEX (not per-pixel -- CNA's default, matching real XNA's
// PreferPerPixelLighting=false), so each of the 4 corner vertices computes its own eye/half-vector
// from its own position; a small quad keeps every corner close enough to the optical axis that all
// 4 corners compute nearly-identical values (confirmed by the symmetry of the corner positions
// around x=y=0), so the interpolated center pixel is a reliable, uniform readback -- a
// full-viewport quad's far corners see a much more oblique eye direction, which was measured to
// crush the Blinn-Phong specular term to near-zero (real per-vertex-lighting behavior, not a bug;
// this test's geometry is chosen to demonstrate the highlight clearly, not to fight this effect).
//
// Check A -- ambient=0, one directional light facing the surface head-on (N.(-L)=1), no specular:
//   the lit pixel must read pure white (full Lambertian intensity, matching DiffuseColor=White).
// Check B -- same light, but pointed the OTHER way (N.(-L)=-1, facing away from the surface): the
//   lit pixel must read pure black -- proving the dot product is clamped to 0, not left negative
//   or wrapped, when the surface faces away from the light.
// Check C -- material/light DiffuseColor=Black (isolates the diffuse channel to exactly 0),
//   material+light SpecularColor=White, geometry arranged so the Blinn-Phong half-vector is close
//   to the normal at every corner vertex: the pixel must read clearly bright, coming ONLY from the
//   real D3DRENDERSTATE_SPECULARENABLE hardware compositing path, not the diffuse channel.
// Check D -- identical to Check C except material SpecularColor=Black: the pixel must read pure
//   black, confirming Check C's bright result really was the specular contribution appearing (and
//   disappearing), not some unrelated fixed bright source.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

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
}

class DirectX5LightingTest : public Game
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

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        // Small quad (NDC half-extent 0.12) at z=0.9, normal facing back toward the origin
        // (View=Identity's own implicit eye position) -- see this file's header comment for why
        // the quad is small rather than full-viewport.
        constexpr float kHalf = 0.12f;
        constexpr float kZ = 0.9f;
        const VertexPositionNormalTexture verts[6] = {
            { Vector3(-kHalf,  kHalf, kZ), Vector3(0.0f, 0.0f, -1.0f), Vector2(0.0f, 0.0f) },
            { Vector3(-kHalf, -kHalf, kZ), Vector3(0.0f, 0.0f, -1.0f), Vector2(0.0f, 1.0f) },
            { Vector3( kHalf,  kHalf, kZ), Vector3(0.0f, 0.0f, -1.0f), Vector2(1.0f, 0.0f) },
            { Vector3( kHalf,  kHalf, kZ), Vector3(0.0f, 0.0f, -1.0f), Vector2(1.0f, 0.0f) },
            { Vector3(-kHalf, -kHalf, kZ), Vector3(0.0f, 0.0f, -1.0f), Vector2(0.0f, 1.0f) },
            { Vector3( kHalf, -kHalf, kZ), Vector3(0.0f, 0.0f, -1.0f), Vector2(1.0f, 1.0f) },
        };
        VertexBuffer vb(dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 6);

        BasicEffect fx(dev);
        fx.setLightingEnabledProperty(true);
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.getDirectionalLight1Property().setEnabledProperty(false);
        fx.getDirectionalLight2Property().setEnabledProperty(false);

        // --- Check A: full-intensity Lambertian diffuse, no specular.
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.getDirectionalLight0Property().setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        fx.getDirectionalLight0Property().setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.getDirectionalLight0Property().setSpecularColorProperty(Vector3::Zero);
        fx.Apply();
        dev.Clear(Color::Black, 1.0f);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        const Color litFullOn = ReadPixel(dev, 64, 64);
        Check(Close(litFullOn.getRProperty(), 255, 6) && Close(litFullOn.getGProperty(), 255, 6) &&
              Close(litFullOn.getBProperty(), 255, 6),
              "Check A: ambient=0 + one head-on directional light (N.(-L)=1) lights the surface to pure white");

        // --- Check B: same light, pointed the OTHER way -- surface faces away, must clamp to 0.
        fx.getDirectionalLight0Property().setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.Apply();
        dev.Clear(Color::Black, 1.0f);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        const Color litFacingAway = ReadPixel(dev, 64, 64);
        Check(Close(litFacingAway.getRProperty(), 0, 6) && Close(litFacingAway.getGProperty(), 0, 6) &&
              Close(litFacingAway.getBProperty(), 0, 6),
              "Check B: a light facing away from the surface (N.(-L)=-1) contributes 0, not negative/wrapped");

        // --- Check C: specular-only isolation. DiffuseColor=Black on both material and light (so
        // the diffuse channel is exactly 0 regardless of N.L), material+light SpecularColor=White,
        // light direction chosen so each corner vertex's Blinn-Phong half-vector is close to the
        // normal (near-1.0 dot product before the specularPower exponent -- not exactly 1.0, since
        // per-vertex lighting means each of the 4 corners has its own slightly-off-axis eye
        // direction; the small quad, decision above, keeps this close enough that the exponentiated
        // result is still clearly bright, not washed out to near-zero the way a full-viewport
        // quad's far corners were measured to be).
        fx.setDiffuseColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.getDirectionalLight0Property().setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        fx.getDirectionalLight0Property().setDiffuseColorProperty(Vector3::Zero);
        fx.getDirectionalLight0Property().setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.Apply();
        dev.Clear(Color::Black, 1.0f);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        const Color specOn = ReadPixel(dev, 64, 64);
        Check(specOn.getRProperty() >= 150 && specOn.getGProperty() >= 150 && specOn.getBProperty() >= 150,
              "Check C: a real specular highlight (D3DRENDERSTATE_SPECULARENABLE) appears clearly bright while diffuse is fully black");

        // --- Check D: same as C, but material SpecularColor=Black -- the highlight must vanish.
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.Apply();
        dev.Clear(Color::Black, 1.0f);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        const Color specOff = ReadPixel(dev, 64, 64);
        Check(Close(specOff.getRProperty(), 0, 6) && Close(specOff.getGProperty(), 0, 6) &&
              Close(specOff.getBProperty(), 0, 6),
              "Check D: SpecularColor=Black removes the highlight (confirms Check C wasn't some unrelated white source)");

        std::printf("=== %d/%d PASS ===\n", g_passCount, 4);
        result_ = (g_passCount == 4) ? 0 : 1;
        Exit();
    }

public:
    DirectX5LightingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(128);
        gdm_->setPreferredBackBufferHeightProperty(128);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX5LightingTest game;
    game.Run();
    return game.getResult();
}
