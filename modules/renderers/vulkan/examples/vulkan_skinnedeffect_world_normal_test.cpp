// SPDX-License-Identifier: MS-PL
// REMED-GFX-006: SkinnedEffect world-space normal transform, Vulkan renderer.
//
// FNA's authoritative path (SkinnedEffect.fx + Lighting.fxh):
//   Skin():                        vin.Normal = mul(vin.Normal, (float3x3)skinning);
//   ComputeCommonVSOutput...():    worldNormal = normalize(mul(normal, WorldInverseTranspose));
// so the correct world-space normal is
//   normalize( InverseTranspose(World3x3) * ( mat3(skinMatrix) * objectNormal ) ).
//
// Vulkan's skinned vertex shaders computed only `mat3(skinMatrix) * normal`, dropping the outer
// world normal matrix entirely (audit Variant A). Every pre-existing skinned test uses
// World = Identity, where the missing factor is the identity and the bug is invisible. This test
// supplies the non-identity World cases those tests structurally cannot cover.
//
// Design notes that make the bug *numerically* visible:
//   * The quad lies in the XY plane facing the camera, and every World used here keeps it facing
//     the camera and covering the centre pixel — so a failure is a lighting difference, never an
//     accidental "the geometry moved/turned edge-on" artifact.
//   * The vertex normal is deliberately NOT the face normal. It is an in-plane direction, so a
//     rotation ABOUT Z changes the world normal while leaving the quad's coverage untouched. With
//     the face normal (0,0,1) a Z-rotation would leave the normal fixed and discriminate nothing.
//   * Bones are an identity bind pose, so mat3(skinMatrix) = I and the measurement isolates the
//     World factor alone — the exact term under test.
//   * AmbientLightColor = 0 and a single directional light, so the pixel is a pure N·L readout.
//
// Cases and analytically-derived expectations (object normal n0 = (1,0,0), light chosen so that
// -Direction = (0,1,0), diffuse white, so pixel intensity = 255 * max(N·L, 0)):
//
//   1. World = Identity
//        correct N = (1,0,0)                    -> N·L = 0     -> BLACK
//        buggy   N = (1,0,0)                    -> N·L = 0     -> BLACK
//      Not discriminating by construction; included as the control that proves the harness agrees
//      with the pre-existing identity-World tests rather than silently changing their meaning.
//
//   2. World = RotationZ(90 deg)
//        correct N = RotZ90 * (1,0,0) = (0,1,0) -> N·L = 1     -> WHITE (255)
//        buggy   N = (1,0,0)                    -> N·L = 0     -> BLACK
//      Maximally discriminating for Variant A: fully lit vs fully unlit.
//
//   3. World = Scale(2,1,1)   (non-uniform)
//      with object normal n0 = normalize(1,1,0):
//        correct: InvTranspose(diag(2,1,1)) = diag(0.5,1,1)
//                 N = normalize(0.5,1,0) = (0.4472,0.8944,0)  -> N·L = 0.8944 -> ~228
//        raw-World (Variant B, and what a naive "just multiply by World" fix would give):
//                 N = normalize(2,1,0)   = (0.8944,0.4472,0)  -> N·L = 0.4472 -> ~114
//        buggy   (Variant A, no world at all):
//                 N = normalize(1,1,0)   = (0.7071,0.7071,0)  -> N·L = 0.7071 -> ~180
//      Three-way distinguishable, so this case also rejects an incorrect raw-World "fix".
//
//   4. World = RotationZ(90 deg) * Scale(2,1,1)
//      Combined case; expectation computed on the CPU by the same formula rather than hand-derived,
//      and cross-checked against the shader result.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
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
    constexpr float kQuad = 0.9f;

    // Upper-left 3x3 of the inverse transpose, computed on the CPU exactly as the shader must.
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

class VulkanSkinnedEffectWorldNormalTest : public Game
{
    Texture2D tex_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    // Renders the quad with the given World and object normal; returns the centre pixel.
    Color Render(GraphicsDevice& dev, const Matrix& world, const Vector3& n)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        // REMED-GFX-052/011: without CullNone this winding culls to black and the check is vacuous.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(dev);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(world);
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        // Pure N·L readout: no ambient, no emissive, no specular contribution.
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.setSpecularPowerProperty(1.0f);
        fx.DirectionalLight0.setEnabledProperty(true);
        // Light travels along -Y, so -Direction = (0,1,0).
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
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

        const auto& vp = dev.getViewportProperty();
        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    void Case(GraphicsDevice& dev, const char* label, const Matrix& world, const Vector3& n)
    {
        const Vector3 wn = ExpectedWorldNormal(world, n);
        const float   ndotl = wn.Y > 0.0f ? wn.Y : 0.0f;      // L = (0,1,0)
        const int     want  = static_cast<int>(ndotl * 255.0f + 0.5f);

        const Color got = Render(dev, world, n);
        // The texture is white and DiffuseColor is white, so all three channels track N·L.
        const int gotV = got.getRProperty();
        const bool ok = std::abs(gotV - want) <= 12;

        std::printf("[%s] SkinnedWorldNormal/%s: got=%d expected=%d "
                    "(world normal=(%.3f,%.3f,%.3f), N.L=%.3f)\n",
                    ok ? "PASS" : "FAIL", label, gotV, want, wn.X, wn.Y, wn.Z, ndotl);
        if (ok) ++pass_;
        else
        {
            ++fail_;
            std::printf("       expected the FNA transform InvTranspose(World3x3)*(skin*n); "
                        "a result matching the un-worlded normal means the outer world normal "
                        "matrix is still missing (Variant A)\n");
        }
        std::fflush(stdout);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> px = { 255, 255, 255, 255 };
        tex_ = Texture2D::CreateFromPixels(dev, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        const Vector3 nX(1.0f, 0.0f, 0.0f);
        const Vector3 nXY(0.70710678f, 0.70710678f, 0.0f);

        // 1. identity World — control; must agree with the existing identity-World tests.
        Case(dev, "identity World", Matrix::getIdentityProperty(), nX);

        // 2. pure rotation — discriminates Variant A maximally (WHITE vs BLACK).
        Case(dev, "rotationZ(90)", Matrix::CreateRotationZ(MathHelper::PiOver2), nX);

        // 3. non-uniform scale — discriminates Variant A *and* a naive raw-World fix (Variant B).
        Case(dev, "scale(2,1,1)", Matrix::CreateScale(2.0f, 1.0f, 1.0f), nXY);

        // 4. rotation + non-uniform scale.
        Case(dev, "rotationZ(90)*scale(2,1,1)",
             Matrix::CreateScale(2.0f, 1.0f, 1.0f) * Matrix::CreateRotationZ(MathHelper::PiOver2),
             nXY);

        std::printf("VulkanSkinnedEffectWorldNormal: %d passed, %d failed\n", pass_, fail_);
        Exit();
    }

public:
    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanSkinnedEffectWorldNormalTest game;
    game.Run();
    return game.getResult();
}
