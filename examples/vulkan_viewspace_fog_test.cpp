// SPDX-License-Identifier: MS-PL
// REMED-GFX-010 (Vulkan slice): transformed-camera stock-effect fog conformance.
//
// Proves CNA fog is driven by VIEW-SPACE Z (FNA EffectHelpers.SetFogVector), not raw object-space Z.
// Every scene here keeps the object-space Z of the sampled vertex FIXED while moving the camera /
// world so the VIEW-SPACE Z changes; the old object-space shader collapses them all to "no fog"
// (blue), the correct view-space shader tracks the camera (blue->purple->red). Each discriminating
// scene asserts ViewKeep != ObjectKeep before checking the pixel, so the test can never silently
// pass on a non-discriminating scene.
//
// Families covered: BasicEffect colored3d (full battery) and SkinnedEffect (pre-skin discriminator:
// a Z-translating bone + a Z-translating view, so the sampled fog must use the POST-skin,
// view-transformed Z). See vulkan_viewspace_fog_effects_test.cpp for the remaining families.
//
// Pre-fix: every discriminating scene FAILS (renders blue instead of purple/red). Post-fix: all pass.
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"
#include "common/ViewSpaceFogRef.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace Ref = CNA::Examples::VSFogRef;

namespace
{
    constexpr int kSize = 64;

    // GPU-compact skinned vertex (Vulkan stride-52 convention, matches vulkan_skinnedeffect_fog_test).
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Match(const Color& c, const Color& e, int tol = 24)
    {
        return CloseTo(c.getRProperty(), e.getRProperty(), tol)
            && CloseTo(c.getGProperty(), e.getGProperty(), tol)
            && CloseTo(c.getBProperty(), e.getBProperty(), tol);
    }
}

class VulkanViewSpaceFogTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0, fail_ = 0;

    void Check(const char* label, const Color& got, const Color& want)
    {
        const bool ok = Match(got, want);
        std::printf("[%s] %s: got=(%d,%d,%d) expected=(%d,%d,%d)\n",
            ok ? "PASS" : "FAIL", label,
            got.getRProperty(), got.getGProperty(), got.getBProperty(),
            want.getRProperty(), want.getGProperty(), want.getBProperty());
        if (ok) ++pass_; else ++fail_;
        std::fflush(stdout);
    }

    void Fail(const char* label, const char* why)
    {
        std::printf("[FAIL] %s: %s\n", label, why);
        ++fail_;
        std::fflush(stdout);
    }

    Color ReadCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    // Render a full-screen colored3d quad (BasicEffect, DiffuseColor = blue) for one scene.
    // Taking the effect by reference lets the contract checks below exercise A→B→A mutations on
    // one public effect instance, rather than accidentally proving only constructor defaults.
    Color RenderColoredWithEffect(GraphicsDevice& dev, BasicEffect& fx, const Ref::Scene& s,
                                  float fogStart = Ref::kFogStart, float fogEnd = Ref::kFogEnd)
    {
        fx.setWorldProperty(s.world);
        fx.setViewProperty(s.view);
        fx.setProjectionProperty(Ref::Ortho());
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.VertexColorEnabled = true;
        fx.setDiffuseColorProperty(Ref::kGeomRGB);
        fx.setFogEnabledProperty(s.fogEnabled);
        fx.setFogColorProperty(Ref::kFogRGB);
        fx.setFogStartProperty(fogStart);
        fx.setFogEndProperty(fogEnd);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        fx.Apply();

        const Color vertexWhite(255, 255, 255, 255);
        const float z = s.objZ;
        const VertexPositionColor quad[6] = {
            { Vector3(-1,  1, z), vertexWhite }, { Vector3(-1, -1, z), vertexWhite },
            { Vector3(1, -1, z), vertexWhite }, { Vector3(-1,  1, z), vertexWhite },
            { Vector3(1, -1, z), vertexWhite }, { Vector3(1,  1, z), vertexWhite },
        };

        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
            got = ReadCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    Color RenderColored(GraphicsDevice& dev, const Ref::Scene& s)
    {
        BasicEffect fx(dev);
        return RenderColoredWithEffect(dev, fx, s);
    }

    // SkinnedEffect pre-skin discriminator: bone0 translates the vertex by (0,0,boneZ), the view
    // translates by (0,0,viewZ). Correct fog uses (objZ + boneZ + viewZ); the pre-skin object-space
    // bug uses only objZ.
    Color RenderSkinned(GraphicsDevice& dev, float boneZ, float viewZ, bool fog)
    {
        // The Vulkan SkinnedEffect fragment produces color from LIGHTING (litRGB =
        // (ambient+lightSum)*diffuse), not emissive, so drive the blue base with a directional light
        // hitting the +Z-facing normal at NdotL=1, plus a white texture (identity factor). The fog
        // keep value is independent of how the base blue is produced.
        Texture2D tex(dev, 1, 1);
        const Color white(255, 255, 255, 255);
        tex.SetData(&white, 1);

        SkinnedEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateTranslation(0.0f, 0.0f, viewZ));
        fx.setProjectionProperty(Ref::Ortho());
        fx.setTextureProperty(&tex);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(Ref::kGeomRGB); // blue
        fx.setFogEnabledProperty(fog);
        fx.setFogColorProperty(Ref::kFogRGB);
        fx.setFogStartProperty(Ref::kFogStart);
        fx.setFogEndProperty(Ref::kFogEnd);

        std::vector<Matrix> bones(SkinnedEffect::MaxBones, Matrix::getIdentityProperty());
        bones[0] = Matrix::CreateTranslation(0.0f, 0.0f, boneZ);
        fx.SetBoneTransforms(bones);
        fx.Apply();

        const float z = 0.0f;
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
            dev.Clear(Color(0, 0, 0, 255));
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            got = ReadCenter(dev);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) { return; }
        done = true;
        auto& dev = getGraphicsDeviceProperty();

        // ---- BasicEffect colored3d battery ----
        for (const Ref::Scene& s : Ref::Battery())
        {
            const Vector3 objPos(0.0f, 0.0f, s.objZ);
            const float viewKeep = Ref::ViewKeep(objPos, s.world, s.view, Ref::kFogStart, Ref::kFogEnd, s.fogEnabled);
            const float objKeep  = Ref::ObjectKeep(s.objZ, Ref::kFogStart, Ref::kFogEnd, s.fogEnabled);
            const bool actuallyDiscriminates = std::fabs(viewKeep - objKeep) > 0.15f;
            if (s.discriminating != actuallyDiscriminates)
            {
                Fail(s.label, s.discriminating ? "scene expected to discriminate but does not"
                                               : "scene expected NOT to discriminate but does");
                continue;
            }
            const Color got = RenderColored(dev, s);
            Check(s.label, got, Ref::ColorFor(viewKeep));
        }

        // ---- Public state lifetime and boundary contract, one BasicEffect instance ----
        // All draws occur in this one Draw() callback. This catches backends that retain an old
        // scalar/vector after a public effect mutation, including deferred/snapshotted paths.
        {
            const Matrix I = Matrix::getIdentityProperty();
            BasicEffect fx(dev);

            Ref::Scene half{"fog state A/B/A half", I, Matrix::CreateTranslation(0, 0, -4), 0.0f, false, true};
            Check("FogEnabled A=false (blue)",
                  RenderColoredWithEffect(dev, fx, half), Ref::ColorFor(1.0f));
            half.fogEnabled = true;
            Check("FogEnabled B=true (purple)",
                  RenderColoredWithEffect(dev, fx, half), Ref::ColorFor(0.5f));
            half.fogEnabled = false;
            Check("FogEnabled A=false again (blue)",
                  RenderColoredWithEffect(dev, fx, half), Ref::ColorFor(1.0f));

            Ref::Scene transformA{"transform A", I, Matrix::CreateTranslation(0, 0, -2), 0.0f, true, false};
            Ref::Scene transformB{"transform B", I, Matrix::CreateTranslation(0, 0, -6), 0.0f, true, true};
            Check("transform A near (blue)",
                  RenderColoredWithEffect(dev, fx, transformA), Ref::ColorFor(1.0f));
            Check("transform B far (red)",
                  RenderColoredWithEffect(dev, fx, transformB), Ref::ColorFor(0.0f));
            Check("transform A again (blue)",
                  RenderColoredWithEffect(dev, fx, transformA), Ref::ColorFor(1.0f));

            const struct Boundary { const char* label; float viewZ; float keep; } boundaries[] = {
                {"before FogStart", -1.0f, 1.0f},
                {"at FogStart",     -2.0f, 1.0f},
                {"between",         -4.0f, 0.5f},
                {"at FogEnd",       -6.0f, 0.0f},
                {"after FogEnd",    -7.0f, 0.0f},
            };
            for (const Boundary& b : boundaries)
            {
                Ref::Scene s{b.label, I, Matrix::CreateTranslation(0, 0, b.viewZ), 0.0f, true, false};
                Check(b.label, RenderColoredWithEffect(dev, fx, s), Ref::ColorFor(b.keep));
            }

            Ref::Scene zeroRange{"FogStart == FogEnd", I, Matrix::CreateTranslation(0, 0, -4),
                                 0.0f, true, false};
            Check("FogStart == FogEnd is fully fogged",
                  RenderColoredWithEffect(dev, fx, zeroRange, 4.0f, 4.0f), Ref::ColorFor(0.0f));
        }

        // ---- SkinnedEffect pre-skin discriminator ----
        // boneZ=-2, viewZ=-2, objZ=0 -> correct viewZ=-4 -> keep 0.5 -> purple.
        // pre-skin object-space bug -> objZ=0 -> keep 1 -> blue.
        {
            const float boneZ = -2.0f, viewZ = -2.0f;
            Matrix world = Matrix::getIdentityProperty();
            Matrix view  = Matrix::CreateTranslation(0.0f, 0.0f, viewZ);
            const Vector3 skinnedPos(0.0f, 0.0f, boneZ); // post-skin object-space position
            const float viewKeep = Ref::ViewKeep(skinnedPos, world, view, Ref::kFogStart, Ref::kFogEnd, true);
            const float objKeep  = Ref::ObjectKeep(0.0f, Ref::kFogStart, Ref::kFogEnd, true);
            if (std::fabs(viewKeep - objKeep) <= 0.15f)
                Fail("skinned pre-skin", "scene not discriminating");
            else
                Check("skinned pre-skin (purple)", RenderSkinned(dev, boneZ, viewZ, true), Ref::ColorFor(viewKeep));
            // fog-disabled control
            Check("skinned fog-disabled (blue)", RenderSkinned(dev, boneZ, viewZ, false), Ref::ColorFor(1.0f));
        }

        std::printf("=== VulkanViewSpaceFog: %d passed, %d failed ===\n", pass_, fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanViewSpaceFogTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    VulkanViewSpaceFogTest game;
    game.Run();
    return game.getResult();
}
