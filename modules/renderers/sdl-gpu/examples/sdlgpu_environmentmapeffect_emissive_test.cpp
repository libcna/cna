// SPDX-License-Identifier: MS-PL
// REMED-GFX-007 (SdlGpu slice): EnvironmentMapEffect's fragment shader must add EmissiveColor
// UNSCALED (FNA Lighting.fxh: litRGB = lightSum*DiffuseColor + EmissiveColor), NOT
// (EmissiveColor + lightSum)*DiffuseColor. The wrong form re-multiplies the already-ambient-folded
// emissive by DiffuseColor a second time.
//
// This test also isolates the audit's separately-reported "alpha-squaring" symptom (see
// audit/examples/sdlgpu_smoke_test.cpp.audit.md). That symptom is NOT an independent defect: it is
// a direct arithmetic consequence of the SAME wrong multiply. The CPU layer pre-folds Alpha into
// BOTH operands -- EnvironmentMapEffect::FillGpuDrawParams() uploads
//   diffuseColor = (DiffuseColor*Alpha, Alpha)   (fragTint.rgb == DiffuseColor*Alpha)
//   emissiveColor = (EmissiveColor + AmbientLightColor*DiffuseColor) * Alpha
// so `(emissiveColor + lightSum) * fragTint.rgb` produces emissive*DiffuseColor*Alpha^2 and
// ambient*DiffuseColor^2*Alpha^2 terms that must not exist. Fixing the one line
// (litRGB = lightSum * fragTint.rgb + pc.emissiveAmount.xyz) removes the emissive re-multiply AND
// the alpha-squaring together. The Alpha=0.5 sub-check below fails pre-fix specifically because of
// the Alpha^2 term and passes post-fix.
//
// SdlGpu renders into a RenderTarget2D (SurfaceFormat::Color = RGBA8 UNORM, linear -- confirmed by
// examples/sdlgpu_envmap_test.cpp's own gray-cube 128->128 readback), so the expected pixel values
// below are asserted directly in linear space, exactly like the already-verified Vulkan/Bgfx
// slices (vulkan_environmentmapeffect_amount_zero_test.cpp, bgfx_environmentmapeffect_amount_zero_test.cpp).
//
// All four checks set EnvironmentMapAmount=0 to suppress the cube-map blend entirely, isolating the
// lit/emissive term, and use zero directional lights (lightSum=0) so the pixel reduces to
// litRGB * textureColor.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, kSkipExitCode = no GPU display.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    bool ColourMatch(const Color& got, const Color& want, int tol = 16)
    {
        return CloseTo(got.getRProperty(), want.getRProperty(), tol)
            && CloseTo(got.getGProperty(), want.getGProperty(), tol)
            && CloseTo(got.getBProperty(), want.getBProperty(), tol);
    }
}

class SdlGpuEnvMapEmissiveTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    int passCount_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, const Color& got, const Color& want)
    {
        ++total_;
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

    // EnvironmentMapAmount=0, no directional lights -> pixel = litRGB * textureColor.
    Color RenderEmissive(GraphicsDevice& dev, Texture2D& tex, TextureCube& cube,
                         Vector3 diffuse, Vector3 emissive, float alpha,
                         const VertexPositionNormalTexture (&quad)[6])
    {
        dev.SetRenderTarget(rt_.get());
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color(0, 0, 0, 255));

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&tex);
        fx.setEnvironmentMapProperty(&cube);
        fx.setDiffuseColorProperty(diffuse);
        fx.setEmissiveColorProperty(emissive);
        fx.setAlphaProperty(alpha);
        fx.setEnvironmentMapAmountProperty(0.0f);
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
        Texture2D tex(dev, 1, 1);
        tex.SetData(&kTex, 1);
        // Amount=0 suppresses the cube entirely; a bound env map is still required.
        auto cube = MakeSolidCube(dev, Color(0, 255, 0, 255));

        // CW winding + normal (0,0,1): what stays visible under this renderer's default
        // RasterizerState.CullCounterClockwise (identical to sdlgpu_envmap_test.cpp's own quad).
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
            { Vector3(-1.0f, -1.0f, 0.0f), n, Vector2(0.0f, 0.0f) },
            { Vector3(-1.0f,  1.0f, 0.0f), n, Vector2(0.0f, 1.0f) },
            { Vector3( 1.0f,  1.0f, 0.0f), n, Vector2(1.0f, 1.0f) },
            { Vector3( 1.0f, -1.0f, 0.0f), n, Vector2(1.0f, 0.0f) },
        };

        // (1) CORE discriminator -- non-white Diffuse + non-zero Emissive, Alpha=1.
        //   correct   litRGB = 0*0.5 + 0.5 = 0.5   -> pixel = tex*0.5 = (100,50,25)
        //   wrong  (emissive+lightSum)*diffuse = 0.5*0.5 = 0.25 -> pixel = (50,25,13)
        Color core = RenderEmissive(dev, tex, *cube, Vector3(0.5f, 0.5f, 0.5f),
                                    Vector3(0.5f, 0.5f, 0.5f), 1.0f, quad);
        Check(ColourMatch(core, Color(100, 50, 25, 255)),
              "REMED-GFX-007 core: non-white Diffuse + Emissive, Alpha=1 -> emissive added unscaled",
              core, Color(100, 50, 25, 255));

        // (2) ALPHA-SQUARING discriminator -- Alpha=0.5.
        //   correct   litRGB = emissive*Alpha = 0.5*0.5 = 0.25 -> pixel = tex*0.25 = (50,25,13)
        //   wrong  (emissive*Alpha)*(diffuse*Alpha) = 0.25*0.25 = 0.0625 -> pixel = (13,6,3)
        // The wrong value carries Alpha^2; the correct one carries exactly one Alpha.
        Color alphaHalf = RenderEmissive(dev, tex, *cube, Vector3(0.5f, 0.5f, 0.5f),
                                         Vector3(0.5f, 0.5f, 0.5f), 0.5f, quad);
        Check(ColourMatch(alphaHalf, Color(50, 25, 13, 128)),
              "REMED-GFX-007 alpha: Alpha=0.5 -> no alpha-squaring (single Alpha, not Alpha^2)",
              alphaHalf, Color(50, 25, 13, 128));

        // (3) WHITE-DIFFUSE control -- proves both formulas coincide when Diffuse=white (the reason
        // the pre-existing sdlgpu_envmap_test.cpp could never catch this). Passes pre- and post-fix.
        //   both -> litRGB = 0.5 -> pixel = tex*0.5 = (100,50,25)
        Color whiteDiffuse = RenderEmissive(dev, tex, *cube, Vector3(1.0f, 1.0f, 1.0f),
                                            Vector3(0.5f, 0.5f, 0.5f), 1.0f, quad);
        Check(ColourMatch(whiteDiffuse, Color(100, 50, 25, 255)),
              "control: white Diffuse -> both formulas coincide (masked case)",
              whiteDiffuse, Color(100, 50, 25, 255));

        // (4) ZERO-EMISSIVE control -- litRGB=0 -> black, regardless of formula. Guards against a
        // spurious emissive contribution being introduced by the fix.
        Color zeroEmissive = RenderEmissive(dev, tex, *cube, Vector3(0.5f, 0.5f, 0.5f),
                                            Vector3(0.0f, 0.0f, 0.0f), 1.0f, quad);
        Check(ColourMatch(zeroEmissive, Color(0, 0, 0, 255)),
              "control: zero Emissive, no lights -> black",
              zeroEmissive, Color(0, 0, 0, 255));

        std::printf("=== %d/%d PASS ===\n", passCount_, total_);
        result_ = (passCount_ == total_) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuEnvMapEmissiveTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRTSize);
        gdm_->setPreferredBackBufferHeightProperty(kRTSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuEnvMapEmissiveTest game;
    game.Run();
    return game.getResult();
}
