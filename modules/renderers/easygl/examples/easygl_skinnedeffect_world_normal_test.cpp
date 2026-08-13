// SPDX-License-Identifier: MS-PL
// REMED-GFX-006 / plan_gltf.md GLTF-264: SkinnedEffect world- and joint-space normal transforms.
//
// FNA's stock path (SkinnedEffect.fx + Lighting.fxh):
//   Skin():                        vin.Normal = mul(vin.Normal, (float3x3)skinning);
//   ComputeCommonVSOutput...():    worldNormal = normalize(mul(normal, WorldInverseTranspose));
// assumes the bone palette has no non-uniform scale. glTF does allow scale in joint transforms;
// preserving a surface normal under that affine transform requires
//   normalize( InverseTranspose(World3x3)
//            * InverseTranspose(skinMatrix3x3) * objectNormal ).
//
// EasyGL's three skinned vertex programs (EnsureSkinnedProgram/EnsureSkinnedVertexLitProgram --
// audit Variant A, no world factor at all -- and EnsurePbrSkinnedProgram -- audit Variant B, raw
// World instead of the inverse-transpose) dropped or mis-applied that outer world normal matrix.
// Every pre-existing skinned test uses World = Identity, where the missing/mis-applied factor is
// the identity and the bug is invisible. This is the non-identity-World harness those tests
// structurally cannot cover -- a direct port of vulkan_skinnedeffect_world_normal_test.cpp, using
// EasyGL's own GetBackBufferData readback path (the same one every other EasyGL pixel test uses).
//
// Design notes that make the bug *numerically* visible (identical to the Vulkan harness):
//   * The quad lies in the XY plane facing the camera, and every World used here keeps it facing
//     the camera and covering the centre pixel -- so a failure is a lighting difference, never an
//     accidental "the geometry moved/turned edge-on" artifact.
//   * The vertex normal is deliberately NOT the face normal. It is an in-plane direction, so a
//     rotation ABOUT Z changes the world normal while leaving the quad's coverage untouched.
//   * The original cases use an identity bind pose to isolate the World factor. GLTF-264's added
//     cases hold World at identity and vary the bone scale, isolating the other factor.
//   * AmbientLightColor = 0 and a single directional light, so the pixel is a pure N.L readout.
//
// Cases and analytically-derived expectations (light chosen so -Direction = (0,1,0), diffuse
// white, so pixel intensity = 255 * max(N.L, 0)):
//
//   1. World = Identity,        n0 = (1,0,0):       correct N.L = 0     -> BLACK  (control)
//   2. World = RotationZ(90),   n0 = (1,0,0):       correct N   = (0,1,0) -> WHITE (255)
//   3. World = Scale(2,1,1),    n0 = norm(1,1,0):
//        correct   InvTranspose(diag(2,1,1))*n = norm(0.5,1,0) -> N.L = 0.894 -> ~228
//        raw-World (Variant B fix)             = norm(2,1,0)   -> N.L = 0.447 -> ~114
//        no-world  (Variant A)                 = norm(1,1,0)   -> N.L = 0.707 -> ~180
//      Three-way distinguishable, so this case rejects an incorrect raw-World "fix" too.
//   4. World = RotationZ(90) * Scale(2,1,1): CPU-derived by the same formula, cross-checked.
//   5. Bone = Scale(2,2,2), n0 = (0,.6,.8): uniform-scale control -> N.L=.6 -> ~153
//   6. Bone = Scale(1,2,1), n0 = (0,.6,.8):
//        correct inverse-transpose = norm(0,.3,.8) -> N.L=.351 -> ~90
//        wrong direct joint matrix  = norm(0,1.2,.8) -> N.L=.832 -> ~212
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
    Vector3 InverseTransposeNormal(const Matrix& transform, const Vector3& n)
    {
        const Matrix nm = Matrix::Transpose(Matrix::Invert(transform));
        Vector3 r(
            nm.M11 * n.X + nm.M21 * n.Y + nm.M31 * n.Z,
            nm.M12 * n.X + nm.M22 * n.Y + nm.M32 * n.Z,
            nm.M13 * n.X + nm.M23 * n.Y + nm.M33 * n.Z);
        const float len = std::sqrt(r.X * r.X + r.Y * r.Y + r.Z * r.Z);
        return len > 0.0f ? Vector3(r.X / len, r.Y / len, r.Z / len) : r;
    }

    Vector3 ExpectedNormal(const Matrix& world, const Matrix& bone, const Vector3& n)
    {
        return InverseTransposeNormal(world, InverseTransposeNormal(bone, n));
    }
}

class EasyGLSkinnedEffectWorldNormalTest : public Game
{
    Texture2D tex_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    // Renders the quad with the given World and object normal; returns the centre pixel.
    // preferPerPixel selects EnsureSkinnedProgram (per-pixel) vs EnsureSkinnedVertexLitProgram
    // (per-vertex/Gouraud) -- both are audit Variant A and both must be fixed. The quad's normal
    // and World are spatially constant, so the per-vertex Gouraud result at the centre equals the
    // per-pixel result to the byte, letting a single analytic expectation cover both programs.
    Color Render(GraphicsDevice& dev, const Matrix& world, const Matrix& bone,
                 const Vector3& n, bool preferPerPixel)
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
        // Pure N.L readout: no ambient, no emissive, no specular contribution.
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.setSpecularPowerProperty(1.0f);
        fx.setPreferPerPixelLightingProperty(preferPerPixel);
        fx.DirectionalLight0.setEnabledProperty(true);
        // Light travels along -Y, so -Direction = (0,1,0).
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);

        std::vector<Matrix> bones = { bone };
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

    void Case(GraphicsDevice& dev, const char* label, const Matrix& world, const Matrix& bone,
              const Vector3& n, bool preferPerPixel)
    {
        const Vector3 wn = ExpectedNormal(world, bone, n);
        const float   ndotl = wn.Y > 0.0f ? wn.Y : 0.0f;      // L = (0,1,0)
        const int     want  = static_cast<int>(ndotl * 255.0f + 0.5f);

        const Color got = Render(dev, world, bone, n, preferPerPixel);
        // The texture is white and DiffuseColor is white, so all three channels track N.L.
        const int gotV = got.getRProperty();
        const bool ok = std::abs(gotV - want) <= 12;

        std::printf("[%s] SkinnedWorldNormal/%s/%s: got=%d expected=%d "
                    "(world normal=(%.3f,%.3f,%.3f), N.L=%.3f)\n",
                    ok ? "PASS" : "FAIL", preferPerPixel ? "pixel-lit" : "vertex-lit", label,
                    gotV, want, wn.X, wn.Y, wn.Z, ndotl);
        if (ok) ++pass_;
        else
        {
            ++fail_;
            std::printf("       expected InvTranspose(World3x3)*"
                        "InvTranspose(Skin3x3)*normal; a direct skin 3x3 multiply is wrong under "
                        "a non-uniform joint scale (GLTF-264)\n");
        }
        std::fflush(stdout);
    }

    void AllCases(GraphicsDevice& dev, bool preferPerPixel)
    {
        const Vector3 nX(1.0f, 0.0f, 0.0f);
        const Vector3 nXY(0.70710678f, 0.70710678f, 0.0f);

        // 1. identity World -- control; must agree with the existing identity-World tests.
        const Matrix identity = Matrix::getIdentityProperty();
        Case(dev, "identity World", identity, identity, nX, preferPerPixel);

        // 2. pure rotation -- discriminates Variant A maximally (WHITE vs BLACK).
        Case(dev, "rotationZ(90)", Matrix::CreateRotationZ(MathHelper::PiOver2), identity,
             nX, preferPerPixel);

        // 3. non-uniform scale -- discriminates Variant A *and* a naive raw-World fix (Variant B).
        Case(dev, "scale(2,1,1)", Matrix::CreateScale(2.0f, 1.0f, 1.0f), identity,
             nXY, preferPerPixel);

        // 4. rotation + non-uniform scale.
        Case(dev, "rotationZ(90)*scale(2,1,1)",
             Matrix::CreateScale(2.0f, 1.0f, 1.0f) * Matrix::CreateRotationZ(MathHelper::PiOver2),
             identity, nXY, preferPerPixel);

        // GLTF-264 / skin-nonuniform-joint-scale: isolate the bone normal matrix with World=I.
        // The uniform case is a control where direct and inverse-transpose point the same way;
        // the non-uniform case separates them by 122 framebuffer levels.
        const Vector3 nYZ(0.0f, 0.6f, 0.8f);
        Case(dev, "uniform bone scale(2,2,2)", identity,
             Matrix::CreateScale(2.0f, 2.0f, 2.0f), nYZ, preferPerPixel);
        Case(dev, "non-uniform bone scale(1,2,1)", identity,
             Matrix::CreateScale(1.0f, 2.0f, 1.0f), nYZ, preferPerPixel);
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

        // Per-pixel-lit skinned program (EnsureSkinnedProgram) and per-vertex-lit sibling
        // (EnsureSkinnedVertexLitProgram) -- both audit Variant A, both must be fixed.
        AllCases(dev, true);
        AllCases(dev, false);

        std::printf("EasyGLSkinnedEffectWorldNormal: %d passed, %d failed\n", pass_, fail_);
        Exit();
    }

public:
    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EasyGLSkinnedEffectWorldNormalTest game;
    game.Run();
    return game.getResult();
}
