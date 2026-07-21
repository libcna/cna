// SPDX-License-Identifier: MS-PL
// REMED-GFX-009 (SdlGpu slice): stock-effect fog for the skinned and PBR shader families (all
// previously fog-free on this backend):
//
//   skinned3d          (SkinnedEffect,     VertexPositionNormalTextureSkinned,        stride 52)
//   skinned_colored3d  (SkinnedEffect+VC,  same + per-vertex Color,                   stride 56)
//   pbr3d              (PbrEffect,         VertexPositionNormalTangentTexture,        stride 48)
//   pbr_skinned3d      (SkinnedPbrEffect,  VertexPositionNormalTangentTextureSkinned, stride 68)
//
// Same corrected fog formula/scene (FogStart=0, FogEnd=-0.9 -> keep = 1/0.5/0 at Z = 0/0.45/0.9;
// red fog). skinned3d/skinned_colored3d build an exact blue base (identity bones, one directional
// light giving N.L=1 on a +Z-facing quad, DiffuseColor=blue) so they assert exact blue/purple/red
// like the BasicEffect families.
//
// pbr3d/pbr_skinned3d run a physically based BRDF whose exact base color is not worth predicting
// analytically, so they use a shading-invariant discriminator that still proves the fog factor is
// exact, not mirrored/absent:
//   * FULL fog (keep=0) -> output must equal FogColor (red) regardless of the underlying shade.
//   * HALF fog (keep=0.5) -> output must equal mix(FogColor, refShade, 0.5), where refShade is the
//     SAME z=0.45 draw with fog OFF (identical shading, so the only variable is the fog factor).
//
// Pre-fix (no fog) every case renders the un-fogged shade, so the half/full checks FAIL and the
// fog-OFF controls PASS. Linear RenderTarget2D readback (swapchain download segfaults, SDLGPU-39).
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// Stride-52 skinned vertex.
struct SkinnedGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

// Stride-56 skinned+color vertex.
struct SkinnedColorGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
    std::uint8_t r, g, b, a;
};
static_assert(sizeof(SkinnedColorGpuVertex) == 56, "skinned+color vertex must be 56 bytes");

// Stride-48 PBR vertex.
struct PbrGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
};
static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

// Stride-68 skinned PBR vertex.
struct SkinnedPbrGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedPbrGpuVertex) == 68, "skinned PBR vertex must be 68 bytes");

namespace
{
    constexpr int kRTSize = 64;
    constexpr float kQ = 0.9f;

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
    Color MixRed(const Color& base) // mix(red, base, 0.5)
    {
        return Color((255 + base.getRProperty()) / 2,
                     (0   + base.getGProperty()) / 2,
                     (0   + base.getBProperty()) / 2, 255);
    }
}

class SdlGpuSkinnedFogTest : public Game
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

    template <class Fx>
    void ConfigFog(Fx& fx, bool fog)
    {
        fx.setFogEnabledProperty(fog);
        fx.setFogColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.setFogStartProperty(0.0f);
        fx.setFogEndProperty(-0.9f);
    }

    // ---- SkinnedEffect (blue: N.L=1, diffuse blue, white tex) ----
    Color RenderSkinned(GraphicsDevice& dev, float z, bool fog, bool vertexColor)
    {
        BeginRT(dev);
        SkinnedEffect fx(dev);
        fx.setTextureProperty(white_.get());
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.setSpecularPowerProperty(1.0f);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f)); // -Dir=(0,0,1)=N -> N.L=1
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        ConfigFog(fx, fog);
        fx.VertexColorEnabled = vertexColor;
        fx.Apply();

        VertexBuffer vb(dev, 6);
        if (vertexColor)
        {
            auto mk = [&](float x, float y) {
                SkinnedColorGpuVertex s{};
                s.px = x; s.py = y; s.pz = z; s.nx = 0; s.ny = 0; s.nz = 1;
                s.w0 = 1.0f; s.r = s.g = s.b = s.a = 255; // white vertex color -> keeps blue
                return s;
            };
            const SkinnedColorGpuVertex v[6] = {
                mk(-kQ, kQ), mk(-kQ, -kQ), mk(kQ, -kQ), mk(-kQ, kQ), mk(kQ, -kQ), mk(kQ, kQ) };
            vb.SetDataRaw(v, 6, static_cast<int>(sizeof(SkinnedColorGpuVertex)));
        }
        else
        {
            auto mk = [&](float x, float y) {
                SkinnedGpuVertex s{};
                s.px = x; s.py = y; s.pz = z; s.nx = 0; s.ny = 0; s.nz = 1; s.w0 = 1.0f;
                return s;
            };
            const SkinnedGpuVertex v[6] = {
                mk(-kQ, kQ), mk(-kQ, -kQ), mk(kQ, -kQ), mk(-kQ, kQ), mk(kQ, -kQ), mk(kQ, kQ) };
            vb.SetDataRaw(v, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        }
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        return EndRT(dev);
    }

    // ---- PbrEffect (BRDF base color -- shading-invariant fog discriminator) ----
    Color RenderPbr(GraphicsDevice& dev, float z, bool fog)
    {
        BeginRT(dev);
        PbrEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(white_.get());
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        ConfigFog(fx, fog);
        fx.Apply();

        auto mk = [&](float x, float y) {
            PbrGpuVertex s{};
            s.px = x; s.py = y; s.pz = z; s.nx = 0; s.ny = 0; s.nz = 1;
            s.tx = 1; s.ty = 0; s.tz = 0; s.tw = 1;
            return s;
        };
        const PbrGpuVertex v[6] = {
            mk(-kQ, kQ), mk(-kQ, -kQ), mk(kQ, -kQ), mk(-kQ, kQ), mk(kQ, -kQ), mk(kQ, kQ) };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(v, 6, static_cast<int>(sizeof(PbrGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        return EndRT(dev);
    }

    // ---- SkinnedPbrEffect (stride-68) -- smoke: full fog must reach the fragment via the skinned
    // PBR vertex shader and force pure FogColor. ----
    Color RenderSkinnedPbr(GraphicsDevice& dev, float z, bool fog)
    {
        BeginRT(dev);
        SkinnedPbrEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(white_.get());
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        ConfigFog(fx, fog);
        fx.Apply();

        auto mk = [&](float x, float y) {
            SkinnedPbrGpuVertex s{};
            s.px = x; s.py = y; s.pz = z; s.nx = 0; s.ny = 0; s.nz = 1;
            s.tx = 1; s.ty = 0; s.tz = 0; s.tw = 1; s.w0 = 1.0f;
            return s;
        };
        const SkinnedPbrGpuVertex v[6] = {
            mk(-kQ, kQ), mk(-kQ, -kQ), mk(kQ, -kQ), mk(-kQ, kQ), mk(kQ, -kQ), mk(kQ, kQ) };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(v, 6, static_cast<int>(sizeof(SkinnedPbrGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        return EndRT(dev);
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

        // skinned3d (stride-52) -- exact blue base, strong off/half/full.
        Check("skinned3d", "fog OFF Z=0.45 (control)", RenderSkinned(dev, 0.45f, false, false), kBlue);
        Check("skinned3d", "no fog Z=0.00",            RenderSkinned(dev, 0.0f,  true,  false), kBlue);
        Check("skinned3d", "half fog Z=0.45",          RenderSkinned(dev, 0.45f, true,  false), kPurple);
        Check("skinned3d", "full fog Z=0.90",          RenderSkinned(dev, 0.9f,  true,  false), kRed);

        // skinned_colored3d (stride-56) -- exact blue base, strong off/half/full.
        Check("skinned_colored3d", "fog OFF Z=0.45 (control)", RenderSkinned(dev, 0.45f, false, true), kBlue);
        Check("skinned_colored3d", "half fog Z=0.45",          RenderSkinned(dev, 0.45f, true,  true), kPurple);
        Check("skinned_colored3d", "full fog Z=0.90",          RenderSkinned(dev, 0.9f,  true,  true), kRed);

        // pbr3d (stride-48) -- shading-invariant discriminator.
        const Color pbrRef = RenderPbr(dev, 0.45f, false);
        std::printf("[info] pbr3d reference shade at Z=0.45 = (%d,%d,%d)\n",
            pbrRef.getRProperty(), pbrRef.getGProperty(), pbrRef.getBProperty());
        Check("pbr3d", "full fog Z=0.90 -> FogColor", RenderPbr(dev, 0.9f, true), kRed);
        Check("pbr3d", "half fog Z=0.45 -> mix(red,ref,0.5)", RenderPbr(dev, 0.45f, true), MixRed(pbrRef));

        // pbr_skinned3d (stride-68) -- smoke: full fog reaches fragment.
        const Color spbrRef = RenderSkinnedPbr(dev, 0.45f, false);
        std::printf("[info] pbr_skinned3d reference shade at Z=0.45 = (%d,%d,%d)\n",
            spbrRef.getRProperty(), spbrRef.getGProperty(), spbrRef.getBProperty());
        Check("pbr_skinned3d", "full fog Z=0.90 -> FogColor", RenderSkinnedPbr(dev, 0.9f, true), kRed);
        Check("pbr_skinned3d", "half fog Z=0.45 -> mix(red,ref,0.5)", RenderSkinnedPbr(dev, 0.45f, true), MixRed(spbrRef));

        std::printf("=== SdlGpuSkinnedFog: %d passed, %d failed ===\n", pass_, fail_);
        Exit();
    }

public:
    SdlGpuSkinnedFogTest()
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

    SdlGpuSkinnedFogTest game;
    game.Run();
    return game.getResult();
}
