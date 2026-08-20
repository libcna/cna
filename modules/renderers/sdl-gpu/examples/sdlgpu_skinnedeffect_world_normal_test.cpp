// SPDX-License-Identifier: MS-PL
// REMED-GFX-006: SkinnedEffect world-space normal transform, SdlGpu renderer.
//
// FNA's authoritative path (SkinnedEffect.fx + Lighting.fxh):
//   Skin():                        vin.Normal = mul(vin.Normal, (float3x3)skinning);
//   ComputeCommonVSOutput...():    worldNormal = normalize(mul(normal, WorldInverseTranspose));
// so the correct world-space normal is
//   normalize( InverseTranspose(World3x3) * ( mat3(skinMatrix) * objectNormal ) ).
//
// SdlGpu's skinned3d.vert.glsl (and skinned_colored3d) computed only mat3(skinMat)*normal, dropping
// the outer world normal matrix (audit Variant A); pbr_skinned3d used raw mat3(lp.world) (Variant
// B). Every pre-existing skinned test uses World = Identity, where the missing/mis-applied factor
// is the identity and the bug is invisible. This is the non-identity-World harness those tests
// cannot cover -- a port of vulkan_skinnedeffect_world_normal_test.cpp using RenderTarget2D::GetData
// readback (this renderer's swapchain-download path segfaults; see plans/plan_sdlgpu.md SDLGPU-39) and the
// standard GPU-display skip guard the other SdlGpu pixel tests use.
//
// Design notes that make the bug *numerically* visible (identical to the Vulkan harness):
//   * The quad lies in the XY plane facing the camera, and every World keeps it facing the camera
//     and covering the centre pixel -- so a failure is a lighting difference, never a coverage
//     artifact.
//   * The vertex normal is an in-plane direction (not the face normal), so a rotation ABOUT Z
//     changes the world normal while leaving the quad's coverage untouched.
//   * Bones are an identity bind pose, so mat3(skinMatrix) = I and the measurement isolates the
//     World factor alone.
//   * AmbientLightColor = 0 and a single directional light, so the pixel is a pure N.L readout.
//
// SdlGpu render targets are R8G8B8A8_UNORM (linear, not sRGB), so the expected byte is the linear
// N.L * 255 exactly, like EasyGL/Vulkan.
//
// Cases and analytically-derived expectations (light chosen so -Direction = (0,1,0), diffuse
// white, pixel intensity = 255 * max(N.L, 0)):
//   1. World = Identity,        n0 = (1,0,0):  correct N.L = 0     -> BLACK  (control)
//   2. World = RotationZ(90),   n0 = (1,0,0):  correct N   = (0,1,0) -> WHITE (255)
//   3. World = Scale(2,1,1),    n0 = norm(1,1,0):
//        correct   InvTranspose(diag(2,1,1))*n = norm(0.5,1,0) -> N.L = 0.894 -> ~228
//        raw-World (Variant B fix)             = norm(2,1,0)   -> N.L = 0.447 -> ~114
//        no-world  (Variant A)                 = norm(1,1,0)   -> N.L = 0.707 -> ~180
//   4. World = RotationZ(90) * Scale(2,1,1): CPU-derived by the same formula.
//
// Exit code 0 = PASS, 1 = FAIL, 77 = SKIP (no GPU-backed display).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include "common/PixelTestGame.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// Stride-52 skinned vertex: pos(12) + normal(12) + uv(8) + weights(16) + indices(4).
struct SkinnedGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

namespace
{
    constexpr int   kRTSize = 64;
    constexpr float kQuad = 0.9f;

    Vector3 ExpectedWorldNormal(const Matrix& world, const Vector3& n)
    {
        const Matrix nm = Matrix::Transpose(Matrix::Invert(world));
        Vector3 r(
            nm.M11 * n.X + nm.M21 * n.Y + nm.M31 * n.Z,
            nm.M12 * n.X + nm.M22 * n.Y + nm.M32 * n.Z,
            nm.M13 * n.X + nm.M23 * n.Y + nm.M33 * n.Z);
        const float len = std::sqrt(r.X * r.X + r.Y * r.Y + r.Z * r.Z);
        return len > 0.0f ? Vector3(r.X / len, r.Y / len, r.Z / len) : r;
    }
}

class SdlGpuSkinnedEffectWorldNormalTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> tex_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    Color Render(GraphicsDevice& dev, const Matrix& world, const Vector3& n)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(dev);
        fx.setTextureProperty(tex_.get());
        fx.setWorldProperty(world);
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.setSpecularPowerProperty(1.0f);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));  // -Dir = (0,1,0)
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);

        std::vector<Matrix> bones = { Matrix::getIdentityProperty() };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.Apply();

        auto mk = [&](float x, float y, float u, float v) {
            SkinnedGpuVertex s{};
            s.px = x; s.py = y; s.pz = 0.0f;
            s.nx = n.X; s.ny = n.Y; s.nz = n.Z;
            s.u = u; s.v = v;
            s.w0 = 1.0f; s.w1 = s.w2 = s.w3 = 0.0f;
            s.i0 = s.i1 = s.i2 = s.i3 = 0;
            return s;
        };
        const SkinnedGpuVertex verts[6] = {
            mk(-kQuad,  kQuad, 0.0f, 0.0f), mk(-kQuad, -kQuad, 0.0f, 1.0f),
            mk( kQuad, -kQuad, 1.0f, 1.0f), mk(-kQuad,  kQuad, 0.0f, 0.0f),
            mk( kQuad, -kQuad, 1.0f, 1.0f), mk( kQuad,  kQuad, 1.0f, 0.0f),
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle reg(kRTSize / 2, kRTSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        rt_->GetData(0, &reg, &px, 0, 1);
        return px;
    }

    void Case(GraphicsDevice& dev, const char* label, const Matrix& world, const Vector3& n)
    {
        const Vector3 wn = ExpectedWorldNormal(world, n);
        const float   ndotl = wn.Y > 0.0f ? wn.Y : 0.0f;      // L = (0,1,0)
        const int     want  = static_cast<int>(ndotl * 255.0f + 0.5f);

        const Color got = Render(dev, world, n);
        const int gotV = got.getRProperty();
        const bool ok = std::abs(gotV - want) <= 12;

        std::printf("[%s] SkinnedWorldNormal/%s: got=%d expected=%d "
                    "(world normal=(%.3f,%.3f,%.3f), N.L=%.3f)\n",
                    ok ? "PASS" : "FAIL", label, gotV, want, wn.X, wn.Y, wn.Z, ndotl);
        if (ok) ++pass_;
        else
        {
            ++fail_;
            std::printf("       expected InvTranspose(World3x3)*(skin*n); a result matching the "
                        "un-worlded normal means the world normal matrix is still missing "
                        "(Variant A), one matching raw-World means the inverse transpose is not "
                        "applied (Variant B)\n");
        }
        std::fflush(stdout);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        const std::vector<std::uint8_t> px = { 255, 255, 255, 255 };
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, px));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        const Vector3 nX(1.0f, 0.0f, 0.0f);
        const Vector3 nXY(0.70710678f, 0.70710678f, 0.0f);

        Case(dev, "identity World", Matrix::getIdentityProperty(), nX);
        Case(dev, "rotationZ(90)", Matrix::CreateRotationZ(MathHelper::PiOver2), nX);
        Case(dev, "scale(2,1,1)", Matrix::CreateScale(2.0f, 1.0f, 1.0f), nXY);
        Case(dev, "rotationZ(90)*scale(2,1,1)",
             Matrix::CreateScale(2.0f, 1.0f, 1.0f) * Matrix::CreateRotationZ(MathHelper::PiOver2),
             nXY);

        std::printf("SdlGpuSkinnedEffectWorldNormal: %d passed, %d failed\n", pass_, fail_);
        Exit();
    }

public:
    SdlGpuSkinnedEffectWorldNormalTest()
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

    SdlGpuSkinnedEffectWorldNormalTest game;
    game.Run();
    return game.getResult();
}
