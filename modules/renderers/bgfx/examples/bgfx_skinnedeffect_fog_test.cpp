// SPDX-License-Identifier: MS-PL
// Task 899: SkinnedEffect linear fog pixel integration test — Bgfx renderer.
//
// Bgfx's skinned3d shader pipeline never implemented fog at all (Task 900 fixed the C++-side
// FillGpuDrawParams() forwarding for SkinnedEffect on all 3 renderers, but only added the GPU-side
// shader fog blend to EasyGL; Vulkan/Bgfx were left for this task). The fog uniforms
// (u_fogColor/u_fogParams) were already set unconditionally per-draw in DrawPrimitivesEx (Task
// 888) -- only the shader itself (vs/fs_skinned3d.sc) was missing the fog varying/blend, added
// here mirroring vs/fs_lit_textured3d.sc's already-proven pattern (fog computed from the
// PRE-SKIN raw object-space Z, matching EasyGL's Task 900 formula exactly). Direct port of
// examples/easygl_skinnedeffect_fog_test.cpp (Task 900).
//
// REMED-GFX-005: fog factor corrected to FNA/EasyGL's Task-1111 form (the prior Task 888/899
// (FogEnd - Z) formula was the mirror image and wrong):
//   geomFraction = clamp((Z + FogEnd) / (FogEnd - FogStart), 0, 1)   (raw object-space Z)
//   finalRGB     = mix(FogColor, geomRGB, geomFraction)
//
// Uses an identity bone palette (weight=1 on bone 0, which defaults to Identity, so the mesh is
// not deformed) with a white 1x1 texture and EmissiveColor as the material color (no directional
// light contribution, since DirectionalLight0 is left at its own default-disabled state), so the
// pre-fog color reduces to exactly EmissiveColor -- isolating fog cleanly from skinning/lighting.
//
// Per Task 364/896's finding: Bgfx's default RasterizerState cull state culls this test family's
// standard quad winding unless RasterizerState::CullNone is set explicitly.
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
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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

// GPU-compact skinned vertex: matches the EasyGL stride-52 layout (Task 123's own convention).
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

class BgfxSkinnedEffectFogTest : public Game
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
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 1.0f)); // blue
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
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
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

        // REMED-GFX-005: retargeted from FogStart=0/FogEnd=1 (which collapses to no-fog under the
        // corrected (Z+FogEnd)/(FogEnd-FogStart) formula) to FogStart=0/FogEnd=-0.9. Asserted pixels
        // unchanged; the corrected formula gives half/full fog while the mirror gives no fog.
        // (b) Fog 50%: Z=0.45, FogStart=0, FogEnd=-0.9 -> (0.45-0.9)/(-0.9)=0.5 -> mix(red,blue,0.5).
        const Color gotHalf = renderQuad(dev, tex, 0.45f, true, Vector3(1, 0, 0), 0.0f, -0.9f);
        check(matches(gotHalf, Color(128, 0, 128, 255)),
              "(b) fog 50%: Z=0.45 -> purple mix", gotHalf, "(128,0,128)");

        // (c) Full fog: FogStart=0, FogEnd=-0.9, Z=0.9 -> (0.9-0.9)/(-0.9)=0 -> pure red.
        const Color gotFull = renderQuad(dev, tex, 0.9f, true, Vector3(1, 0, 0), 0.0f, -0.9f);
        check(matches(gotFull, kRed), "(c) full fog: FogEnd=-0.9, Z=0.9 -> full red", gotFull, "(255,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxSkinnedEffectFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxSkinnedEffectFogTest game;
    game.Run();
    return game.getResult();
}
