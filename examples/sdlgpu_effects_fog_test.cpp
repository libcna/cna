// SPDX-License-Identifier: MS-PL
// REMED-GFX-009 (SdlGpu slice): stock-effect fog for the AlphaTestEffect, DualTextureEffect and
// EnvironmentMapEffect shader families (all previously fog-free on this backend).
//
//   alpha_test3d          (AlphaTestEffect,       VertexPositionTexture, stride 20)
//   alpha_test_colored3d  (AlphaTestEffect,       VertexPositionColorTexture, stride 24)  [smoke]
//   dual_texture3d        (DualTextureEffect,     VertexPositionTexture, stride 20)
//   dual_texture_colored3d(DualTextureEffect,     VertexPositionColorTexture, stride 24)  [smoke]
//   env_map3d             (EnvironmentMapEffect,  VertexPositionNormalTexture, stride 32)
//
// Same corrected fog formula and shared scene as sdlgpu_basiceffect_fog_test.cpp: blue geometry,
// red fog, FogStart=0, FogEnd=-0.9 -> Z=0 pure blue, Z=0.45 (128,0,128), Z=0.9 pure red; fog OFF at
// Z=0.45 stays pure blue. Each family builds its blue base differently:
//   AlphaTest  : white opaque texture * blue Diffuse (alpha test passes everywhere).
//   DualTexture: gray(0.5) tex0 (x2 -> white) * white tex1 * blue Diffuse.
//   EnvMap     : EnvironmentMapAmount=0 (cube suppressed), no lights, Emissive=white,
//                Diffuse=white -> litRGB=white; blue texture -> blue base.
//
// Pre-fix (no fog) every Z renders blue, so half/full checks FAIL, controls PASS. Linear RT readback.
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 64;

    const Color kBlue(0, 0, 255, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kPurple(128, 0, 128, 255);

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Match(const Color& c, const Color& e, int tol = 16)
    {
        return CloseTo(c.getRProperty(), e.getRProperty(), tol)
            && CloseTo(c.getGProperty(), e.getGProperty(), tol)
            && CloseTo(c.getBProperty(), e.getBProperty(), tol);
    }
}

class SdlGpuEffectsFogTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<Texture2D> gray_;
    std::unique_ptr<Texture2D> blue_;
    std::unique_ptr<TextureCube> cube_;
    int pass_ = 0, fail_ = 0;

    void Check(const char* family, const char* label, const Color& got, const Color& want)
    {
        const bool ok = Match(got, want);
        std::printf("[%s] %s / %s: got=(%d,%d,%d) expected=(%d,%d,%d)\n",
            ok ? "PASS" : "FAIL", family, label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(),
            want.getRProperty(), want.getGProperty(), want.getBProperty());
        if (ok) ++pass_; else ++fail_;
        std::fflush(stdout);
    }

    void BeginRT(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    Color EndRT(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Rectangle reg(kRTSize / 2, kRTSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        rt_->GetData(0, &reg, &px, 0, 1);
        return px;
    }

    static VertexPositionTexture QuadPT(float x, float y, float z) { return { Vector3(x, y, z), Vector2(0, 0) }; }
    static VertexPositionColorTexture QuadPCT(float x, float y, float z)
    { return { Vector3(x, y, z), Color(255, 255, 255, 255), Vector2(0, 0) }; }
    static VertexPositionNormalTexture QuadPNT(float x, float y, float z)
    { return { Vector3(x, y, z), Vector3(0, 0, 1), Vector2(0, 0) }; }

    // ---- AlphaTestEffect (blue = white tex * blue diffuse) ----
    Color RenderAlpha(GraphicsDevice& dev, float z, bool fog, bool vertexColor)
    {
        BeginRT(dev);
        AlphaTestEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(white_.get());
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        fx.setAlphaFunctionProperty(CompareFunction::Greater);
        fx.setReferenceAlphaProperty(0); // white tex alpha=255 > 0 -> always passes
        fx.setVertexColorEnabledProperty(vertexColor);
        fx.setFogEnabledProperty(fog);
        fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.setFogStartProperty(0.0f);
        fx.setFogEndProperty(-0.9f);
        fx.Apply();
        if (vertexColor)
        {
            const VertexPositionColorTexture q[6] = {
                QuadPCT(-1, 1, z), QuadPCT(-1, -1, z), QuadPCT(1, -1, z),
                QuadPCT(-1, 1, z), QuadPCT(1, -1, z), QuadPCT(1, 1, z) };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        }
        else
        {
            const VertexPositionTexture q[6] = {
                QuadPT(-1, 1, z), QuadPT(-1, -1, z), QuadPT(1, -1, z),
                QuadPT(-1, 1, z), QuadPT(1, -1, z), QuadPT(1, 1, z) };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        }
        return EndRT(dev);
    }

    // ---- DualTextureEffect (blue = gray(0.5)*2 * white * blue diffuse) ----
    Color RenderDual(GraphicsDevice& dev, float z, bool fog, bool vertexColor)
    {
        BeginRT(dev);
        DualTextureEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(gray_.get());
        fx.setTexture2Property(white_.get());
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        fx.setVertexColorEnabledProperty(vertexColor);
        fx.setFogEnabledProperty(fog);
        fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.setFogStartProperty(0.0f);
        fx.setFogEndProperty(-0.9f);
        fx.Apply();
        if (vertexColor)
        {
            const VertexPositionColorTexture q[6] = {
                QuadPCT(-1, 1, z), QuadPCT(-1, -1, z), QuadPCT(1, -1, z),
                QuadPCT(-1, 1, z), QuadPCT(1, -1, z), QuadPCT(1, 1, z) };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        }
        else
        {
            const VertexPositionTexture q[6] = {
                QuadPT(-1, 1, z), QuadPT(-1, -1, z), QuadPT(1, -1, z),
                QuadPT(-1, 1, z), QuadPT(1, -1, z), QuadPT(1, 1, z) };
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        }
        return EndRT(dev);
    }

    // ---- EnvironmentMapEffect (blue base via amount=0, no lights, white emissive+diffuse, blue tex) ----
    Color RenderEnvMap(GraphicsDevice& dev, float z, bool fog)
    {
        BeginRT(dev);
        EnvironmentMapEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(blue_.get());
        fx.setEnvironmentMapProperty(cube_.get());
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setEmissiveColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(1.0f);
        fx.setEnvironmentMapAmountProperty(0.0f);
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.DirectionalLight0.setEnabledProperty(false);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setFogEnabledProperty(fog);
        fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.setFogStartProperty(0.0f);
        fx.setFogEndProperty(-0.9f);
        fx.Apply();
        const VertexPositionNormalTexture q[6] = {
            QuadPNT(-1, 1, z), QuadPNT(-1, -1, z), QuadPNT(1, -1, z),
            QuadPNT(-1, 1, z), QuadPNT(1, -1, z), QuadPNT(1, 1, z) };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
        return EndRT(dev);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        const Color w(255, 255, 255, 255), g(128, 128, 128, 255), b(0, 0, 255, 255);
        white_ = std::make_unique<Texture2D>(dev, 1, 1); white_->SetData(&w, 1);
        gray_  = std::make_unique<Texture2D>(dev, 1, 1); gray_->SetData(&g, 1);
        blue_  = std::make_unique<Texture2D>(dev, 1, 1); blue_->SetData(&b, 1);
        cube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ };
        const Color green(0, 255, 0, 255);
        for (CubeMapFace f : faces) cube_->SetData(f, &green, 1);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;
        auto& dev = getGraphicsDeviceProperty();

        // AlphaTestEffect (stride-20) -- strong off/half/full.
        Check("alpha_test3d", "fog OFF Z=0.45 (control)", RenderAlpha(dev, 0.45f, false, false), kBlue);
        Check("alpha_test3d", "no fog Z=0.00",            RenderAlpha(dev, 0.0f,  true,  false), kBlue);
        Check("alpha_test3d", "half fog Z=0.45",          RenderAlpha(dev, 0.45f, true,  false), kPurple);
        Check("alpha_test3d", "full fog Z=0.90",          RenderAlpha(dev, 0.9f,  true,  false), kRed);
        // alpha_test_colored3d (stride-24) -- smoke: fog off blue, full fog red.
        Check("alpha_test_colored3d", "fog OFF Z=0.45", RenderAlpha(dev, 0.45f, false, true), kBlue);
        Check("alpha_test_colored3d", "full fog Z=0.90", RenderAlpha(dev, 0.9f, true, true), kRed);

        // DualTextureEffect (stride-20) -- strong off/half/full.
        Check("dual_texture3d", "fog OFF Z=0.45 (control)", RenderDual(dev, 0.45f, false, false), kBlue);
        Check("dual_texture3d", "no fog Z=0.00",            RenderDual(dev, 0.0f,  true,  false), kBlue);
        Check("dual_texture3d", "half fog Z=0.45",          RenderDual(dev, 0.45f, true,  false), kPurple);
        Check("dual_texture3d", "full fog Z=0.90",          RenderDual(dev, 0.9f,  true,  false), kRed);
        // dual_texture_colored3d (stride-24) -- smoke.
        Check("dual_texture_colored3d", "fog OFF Z=0.45", RenderDual(dev, 0.45f, false, true), kBlue);
        Check("dual_texture_colored3d", "full fog Z=0.90", RenderDual(dev, 0.9f, true, true), kRed);

        // EnvironmentMapEffect (stride-32) -- strong off/half/full.
        Check("env_map3d", "fog OFF Z=0.45 (control)", RenderEnvMap(dev, 0.45f, false), kBlue);
        Check("env_map3d", "no fog Z=0.00",            RenderEnvMap(dev, 0.0f,  true),  kBlue);
        Check("env_map3d", "half fog Z=0.45",          RenderEnvMap(dev, 0.45f, true),  kPurple);
        Check("env_map3d", "full fog Z=0.90",          RenderEnvMap(dev, 0.9f,  true),  kRed);

        std::printf("=== SdlGpuEffectsFog: %d passed, %d failed ===\n", pass_, fail_);
        Exit();
    }

public:
    SdlGpuEffectsFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kRTSize);
        gdm_->setPreferredBackBufferHeightProperty(kRTSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuEffectsFogTest game;
    game.Run();
    return game.getResult();
}
