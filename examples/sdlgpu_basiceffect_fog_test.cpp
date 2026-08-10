// SPDX-License-Identifier: MS-PL
// REMED-GFX-009 (SdlGpu slice): stock-effect fog, entirely unimplemented on this renderer until this
// task (grep of every .glsl + SdlGpuRenderer.cpp for any fog identifier returned zero).
//
// This file covers the four BasicEffect shader families:
//   colored3d           (VertexPositionColor,        stride 16)
//   textured3d          (VertexPositionTexture,       stride 20)
//   colored_textured3d  (VertexPositionColorTexture,  stride 24)
//   lit_textured3d      (VertexPositionNormalTexture, stride 32, LightingEnabled=false)
//
// Formula (the REMED-GFX-005 corrected, oracle-validated form, identical to Vulkan/Bgfx/EasyGL):
//   keep     = clamp((Z + FogEnd) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB = mix(FogColor, geomRGB, keep)                      (keep=1 -> no fog, 0 -> full fog)
//
// Shared scene (identical to the verified Vulkan fog tests, so results are numerically cross-renderer
// comparable): blue geometry (0,0,255), red fog (255,0,0), FogStart=0, FogEnd=-0.9.
//   Z=0.00 -> keep=1.0 -> pure blue        (0,0,255)
//   Z=0.45 -> keep=0.5 -> mix -> purple    (128,0,128)
//   Z=0.90 -> keep=0.0 -> pure red         (255,0,0)
// A fog-DISABLED control at Z=0.45 must stay pure blue (proves FogEnabled=false is a true no-op).
//
// Pre-fix (no fog on SdlGpu at all) every Z renders pure blue, so the half/full-fog checks FAIL and
// the fog-disabled + no-fog checks coincidentally PASS. Post-fix all pass.
//
// SdlGpu renders into a RenderTarget2D (SurfaceFormat::Color = RGBA8 UNORM, linear -- see
// sdlgpu_environmentmapeffect_emissive_test.cpp / sdlgpu_envmap_test.cpp) whose GetData readback is
// the linear byte value directly (the swapchain download path segfaults on this renderer, SDLGPU-39).
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU-backed display).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
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

class SdlGpuBasicEffectFogTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> white_;
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

    void ConfigFog(BasicEffect& fx, bool on, float z)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setLightingEnabledProperty(false);
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
        fx.setFogEnabledProperty(on);
        fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));     // red
        fx.setFogStartProperty(0.0f);
        fx.setFogEndProperty(-0.9f);
        (void)z;
    }

    // colored3d: VertexPositionColor, no texture, no vertex color.
    Color RenderColored(GraphicsDevice& dev, float z, bool fog)
    {
        BeginRT(dev);
        BasicEffect fx(dev);
        ConfigFog(fx, fog, z);
        fx.setTextureEnabledProperty(false);
        fx.VertexColorEnabled = false;
        fx.Apply();
        const Color vc(0, 255, 0, 255); // green, ignored (VertexColorEnabled=false)
        const VertexPositionColor quad[6] = {
            { Vector3(-1,  1, z), vc }, { Vector3(-1, -1, z), vc }, { Vector3(1, -1, z), vc },
            { Vector3(-1,  1, z), vc }, { Vector3( 1, -1, z), vc }, { Vector3(1,  1, z), vc },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return EndRT(dev);
    }

    // textured3d: VertexPositionTexture, white texture.
    Color RenderTextured(GraphicsDevice& dev, float z, bool fog)
    {
        BeginRT(dev);
        BasicEffect fx(dev);
        ConfigFog(fx, fog, z);
        fx.setTextureProperty(white_.get());
        fx.setTextureEnabledProperty(true);
        fx.Apply();
        const Vector2 uv(0, 0);
        const VertexPositionTexture quad[6] = {
            { Vector3(-1,  1, z), uv }, { Vector3(-1, -1, z), uv }, { Vector3(1, -1, z), uv },
            { Vector3(-1,  1, z), uv }, { Vector3( 1, -1, z), uv }, { Vector3(1,  1, z), uv },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return EndRT(dev);
    }

    // colored_textured3d: VertexPositionColorTexture, white texture + white vertex color (multiply
    // by white keeps blue), VertexColorEnabled=true to exercise the vertex-color path.
    Color RenderColoredTextured(GraphicsDevice& dev, float z, bool fog)
    {
        BeginRT(dev);
        BasicEffect fx(dev);
        ConfigFog(fx, fog, z);
        fx.setTextureProperty(white_.get());
        fx.setTextureEnabledProperty(true);
        fx.VertexColorEnabled = true;
        fx.Apply();
        const Color vc(255, 255, 255, 255); // white -> inColor*diffuse = diffuse (blue)
        const Vector2 uv(0, 0);
        const VertexPositionColorTexture quad[6] = {
            { Vector3(-1,  1, z), vc, uv }, { Vector3(-1, -1, z), vc, uv }, { Vector3(1, -1, z), vc, uv },
            { Vector3(-1,  1, z), vc, uv }, { Vector3( 1, -1, z), vc, uv }, { Vector3(1,  1, z), vc, uv },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return EndRT(dev);
    }

    // lit_textured3d: VertexPositionNormalTexture, LightingEnabled=false (else-branch: fragTint*tex).
    Color RenderLit(GraphicsDevice& dev, float z, bool fog)
    {
        BeginRT(dev);
        BasicEffect fx(dev);
        ConfigFog(fx, fog, z);
        fx.setTextureProperty(white_.get());
        fx.setTextureEnabledProperty(true);
        fx.Apply();
        const Vector3 n(0, 0, 1);
        const Vector2 uv(0, 0);
        const VertexPositionNormalTexture quad[6] = {
            { Vector3(-1,  1, z), n, uv }, { Vector3(-1, -1, z), n, uv }, { Vector3(1, -1, z), n, uv },
            { Vector3(-1,  1, z), n, uv }, { Vector3( 1, -1, z), n, uv }, { Vector3(1,  1, z), n, uv },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return EndRT(dev);
    }

    template <class RenderFn>
    void RunFamily(GraphicsDevice& dev, const char* family, RenderFn render)
    {
        Check(family, "fog OFF at Z=0.45 (control)", render(dev, 0.45f, false), kBlue);
        Check(family, "no fog: Z=0.00 keep=1.0",     render(dev, 0.0f,  true),  kBlue);
        Check(family, "half fog: Z=0.45 keep=0.5",   render(dev, 0.45f, true),  kPurple);
        Check(family, "full fog: Z=0.90 keep=0.0",   render(dev, 0.9f,  true),  kRed);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        const Color w(255, 255, 255, 255);
        white_ = std::make_unique<Texture2D>(dev, 1, 1);
        white_->SetData(&w, 1);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;
        auto& dev = getGraphicsDeviceProperty();

        RunFamily(dev, "colored3d",          [this](GraphicsDevice& d, float z, bool f){ return RenderColored(d, z, f); });
        RunFamily(dev, "textured3d",         [this](GraphicsDevice& d, float z, bool f){ return RenderTextured(d, z, f); });
        RunFamily(dev, "colored_textured3d", [this](GraphicsDevice& d, float z, bool f){ return RenderColoredTextured(d, z, f); });
        RunFamily(dev, "lit_textured3d",     [this](GraphicsDevice& d, float z, bool f){ return RenderLit(d, z, f); });

        std::printf("=== SdlGpuBasicEffectFog: %d passed, %d failed ===\n", pass_, fail_);
        Exit();
    }

public:
    SdlGpuBasicEffectFogTest()
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

    SdlGpuBasicEffectFogTest game;
    game.Run();
    return game.getResult();
}
