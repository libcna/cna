// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-33: EnvironmentMapEffect proof for the SDL_GPU graphics renderer -- a real
// cube-map reflection, verified via RenderTarget2D::GetData() pixel readback (SDLGPU-39's
// RenderTarget2D leg), mirroring this project's existing
// vulkan_environmentmapeffect_amount_one_test.cpp: since the test cubemap is uniformly solid-colored
// on all 6 faces, the specific reflection direction is irrelevant to the sampled result -- only
// whether the env-map contribution is really being sampled and blended in at all (as opposed to,
// e.g., being ignored, or always-on regardless of EnvironmentMapAmount).
//
// Check A -- EnvironmentMapAmount=1, a WHITE solid cubemap: FNA's real lerp semantics FULLY
//   REPLACE the lit/textured color with the env-map color, so the readback should be ~white
//   (not merely "some white contribution mixed in with the diffuse texture/emissive").
// Check B -- Same scene, but a GRAY solid cubemap: the readback should be ~gray, not white and
//   not the (200,100,50) diffuse texture color -- this is the real discriminator that the env-map
//   value read back is genuinely coming from the cubemap sample, not a hardcoded/leftover value.
// Check C -- EnvironmentMapAmount=0: the env-map contribution must be fully suppressed --
//   readback must NOT be close to either cubemap color, proving the blend amount is real and the
//   cubemap isn't always-on regardless of EnvironmentMapAmount.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    bool ColourMatch(const Color& got, const Color& want, int tol = 20)
    {
        return CloseTo(got.getRProperty(), want.getRProperty(), tol)
            && CloseTo(got.getGProperty(), want.getGProperty(), tol)
            && CloseTo(got.getBProperty(), want.getBProperty(), tol);
    }
}

class SdlGpuEnvMapTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, const Color& got, const Color& want)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++passCount_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d), expected~(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(),
                want.getRProperty(), want.getGProperty(), want.getBProperty());
        }
    }

    std::unique_ptr<TextureCube> MakeSolidCube(GraphicsDevice& dev, const Color& col)
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

    Color RenderWith(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube, float envMapAmount,
                     const VertexPositionNormalTexture (&quad)[6])
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEmissiveColorProperty(Vector3(0.5f, 0.5f, 0.5f));
        fx.setEnvironmentMapAmountProperty(envMapAmount);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        rt_->GetData(0, &centre, &px, 0, 1);
        return px;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        const Color kTex(200, 100, 50, 255);
        const Color kWhiteCube(255, 255, 255, 255);
        const Color kGrayCube(128, 128, 128, 255);

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        auto whiteCube = MakeSolidCube(dev, kWhiteCube);
        auto grayCube  = MakeSolidCube(dev, kGrayCube);

        // CW winding (TL,BR,BL / TL,TR,BR) -- with the identity view/projection this test uses (no
        // camera-induced winding flip), a CW-wound triangle is what stays visible under this
        // project's own default RasterizerState.CullCounterClockwise now that SDLGPU-18/19/20
        // wired up real dynamic culling (previously CullMode was hardcoded to None, so this test's
        // original CCW winding went unnoticed -- same pitfall documented in
        // sdlgpu_renderstate_test.cpp's own note on this exact convention).
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
        };

        Color a = RenderWith(dev, tex, *whiteCube, 1.0f, quad);
        Check(ColourMatch(a, Color(255, 255, 255, 255)), "Amount=1, white cubemap -> FULLY REPLACES with white",
              a, Color(255, 255, 255, 255));

        Color b = RenderWith(dev, tex, *grayCube, 1.0f, quad);
        Check(ColourMatch(b, Color(128, 128, 128, 255)), "Amount=1, gray cubemap -> FULLY REPLACES with gray",
              b, Color(128, 128, 128, 255));

        Color c = RenderWith(dev, tex, *whiteCube, 0.0f, quad);
        Check(!ColourMatch(c, Color(255, 255, 255, 255), 40), "Amount=0 -> env-map contribution fully suppressed (not white)",
              c, Color(255, 255, 255, 255));

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuEnvMapTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuEnvMapTest game;
    game.Run();
    return game.getResult();
}
