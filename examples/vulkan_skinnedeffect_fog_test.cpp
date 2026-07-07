// SPDX-License-Identifier: MS-PL
// Task 899: SkinnedEffect linear fog pixel integration test — Vulkan backend.
//
// skinned3d was one of 5 Vulkan pipelines sharing FillExtPushConst()'s fully-packed 128-byte
// push constant (zero spare bytes for fog). skinned3d's existing BoneBlock UBO (set=0,
// binding=1) also has zero spare capacity (exactly 72x64 bytes), so fog is forwarded via a
// SEPARATE new dynamic UBO at binding=2, extending descriptorSetLayoutSkinned_ from 2 bindings
// to 3.
//
// Formula (matches EasyGL/Bgfx's already-tested formula exactly, Task 900/899 bonus scope):
//   fogFactor = clamp((FogEnd - Z) / (FogEnd - FogStart), 0, 1)   (raw object-space Z, computed
//               from the PRE-SKIN vertex position, before the bone-skinning matrix multiply)
//   finalRGB  = mix(FogColor, geomRGB, fogFactor)
//
// Unlike examples/bgfx_skinnedeffect_fog_test.cpp (which isolates material color via
// EmissiveColor), Vulkan's skinned3d.frag.glsl has never read an emissive uniform at all (a
// separate, unreported gap outside this task's scope, analogous to the one Task 899's Bgfx bonus
// scope found and fixed for Bgfx) -- so this test isolates the pre-fog material color via
// DirectionalLight0 instead: an identity bone palette (weight=1 on bone 0, Identity, so the mesh
// is not deformed), a light pointing straight at the quad's +Z-facing normal (NdotL=1), a blue
// DirectionalLight0.DiffuseColor, AmbientLightColor left at its SkinnedEffect default (always 0 on
// Vulkan -- EmissiveColor/AmbientLightColor are never forwarded to the GPU here), and a white 1x1
// texture (identity factor) -- reducing skinned3d.frag's `(ambientColor + light0Diffuse*NdotL) *
// diffuseColor.rgb * tex.rgb` formula to exactly `light0Diffuse` when DiffuseColor is left at its
// default white and NdotL=1.
//
// Unlike the Bgfx sibling, no RasterizerState::CullNone override is needed here -- per this
// project's established Task 364/896 finding, Vulkan's default cull state already behaves like
// EasyGL's effectively-no-culling default (confirmed empirically: no other Vulkan SkinnedEffect
// pixel test in this family sets it, e.g. vulkan_skinnedeffect_identity_bones_test.cpp).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

// GPU-compact skinned vertex: matches the stride-52 layout (Task 123's own convention).
struct SkinnedGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

static const Color kWhite(255, 255, 255, 255);
static const Color kBlack(0, 0, 0, 255);
static const Color kBlue(0, 0, 255, 255);
static const Color kRed(255, 0, 0, 255);

class SkinnedEffectFogVulkanTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int  pass_ = 0;
    int  fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected, int tol = 30)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), tol)
            && closeTo(c.getGProperty(), expected.getGProperty(), tol)
            && closeTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    Color renderQuad(GraphicsDevice& dev, Texture2D& tex, float z, bool fogEnabled,
                      const Vector3& fogColor, float fogStart, float fogEnd)
    {
        SkinnedEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setTextureProperty(&tex);
        // Light points straight down -Z at the quad's +Z-facing normal -> NdotL=1.
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
        fx.setFogEnabledProperty(fogEnabled);
        fx.setFogColorProperty(fogColor);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);

        const SkinnedGpuVertex verts[6] = {
            { -1,  1, z,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            { -1, -1, z,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0 },
            {  1, -1, z,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
            { -1,  1, z,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            {  1, -1, z,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
            {  1,  1, z,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0 },
        };
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            fx.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = readCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        Texture2D tex(dev, 1, 1);
        tex.SetData(&kWhite, 1);

        // (a) Fog disabled: blue quad at Z=0 -> pure blue.
        const Color gotOff = renderQuad(dev, tex, 0.0f, false, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotOff, kBlue), "(a) fog OFF: blue quad -> pure blue", gotOff, "(0,0,255)");

        // (b) Fog 50%: Z=0.5, FogStart=0, FogEnd=1, FogColor=red -> mix(red,blue,0.5)=(128,0,128).
        const Color gotHalf = renderQuad(dev, tex, 0.5f, true, Vector3(1, 0, 0), 0.0f, 1.0f);
        check(matches(gotHalf, Color(128, 0, 128, 255)),
              "(b) fog 50%: Z=0.5 -> purple mix", gotHalf, "(128,0,128)");

        // (c) Full fog: FogEnd=0.5, Z=0.9 -> fogFactor=clamp(-0.8,0,1)=0 -> pure red.
        const Color gotFull = renderQuad(dev, tex, 0.9f, true, Vector3(1, 0, 0), 0.0f, 0.5f);
        check(matches(gotFull, kRed), "(c) full fog: FogEnd=0.5, Z=0.9 -> full red", gotFull, "(255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SkinnedEffectFogVulkanTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    SkinnedEffectFogVulkanTest game;
    game.Run();
    return game.getResult();
}
