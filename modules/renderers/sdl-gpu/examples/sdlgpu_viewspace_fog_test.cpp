// SPDX-License-Identifier: MS-PL
// REMED-GFX-010 (SdlGpu slice): transformed-camera stock-effect fog conformance.
//
// Proves SdlGpu fog is driven by VIEW-SPACE Z (FNA EffectHelpers.SetFogVector), not raw object-space
// Z. Every discriminating scene keeps the object-space Z of the sampled vertex fixed while moving the
// camera/world so the VIEW-SPACE Z changes; the old object-space shaders collapse them all to blue,
// the corrected view-space shaders track the camera (blue->purple->red). Reads back from a linear
// RenderTarget2D (the SdlGpu swapchain download path segfaults, SDLGPU-39).
//
// Families: BasicEffect colored3d (full battery) + SkinnedEffect pre-skin discriminator (a Z-translating
// bone + a Z-translating view; the SdlGpu SkinnedEffect fragment lights rather than adds emissive, so a
// white directional light + blue DiffuseColor drive the base). Pre-fix every discriminating scene FAILS.
// Exit 0 = all PASS, 1 = any FAIL, 77 = SKIP.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
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
    constexpr int kRTSize = 64;

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

class SdlGpuViewSpaceFogTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> white_;
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
        std::printf("[FAIL] %s: %s\n", label, why); ++fail_; std::fflush(stdout);
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

    Color RenderColored(GraphicsDevice& dev, const Ref::Scene& s)
    {
        BeginRT(dev);
        BasicEffect fx(dev);
        fx.setWorldProperty(s.world);
        fx.setViewProperty(s.view);
        fx.setProjectionProperty(Ref::Ortho());
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.VertexColorEnabled = false;
        fx.setDiffuseColorProperty(Ref::kGeomRGB);
        fx.setFogEnabledProperty(s.fogEnabled);
        fx.setFogColorProperty(Ref::kFogRGB);
        fx.setFogStartProperty(Ref::kFogStart);
        fx.setFogEndProperty(Ref::kFogEnd);
        fx.Apply();
        const Color ignored(0, 255, 0, 255);
        const float z = s.objZ;
        const VertexPositionColor quad[6] = {
            { Vector3(-1,  1, z), ignored }, { Vector3(-1, -1, z), ignored }, { Vector3(1, -1, z), ignored },
            { Vector3(-1,  1, z), ignored }, { Vector3( 1, -1, z), ignored }, { Vector3(1,  1, z), ignored },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        return EndRT(dev);
    }

    Color RenderSkinned(GraphicsDevice& dev, float boneZ, float viewZ, bool fog)
    {
        BeginRT(dev);
        SkinnedEffect fx(dev);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateTranslation(0.0f, 0.0f, viewZ));
        fx.setProjectionProperty(Ref::Ortho());
        fx.setTextureProperty(white_.get());
        fx.setDiffuseColorProperty(Ref::kGeomRGB); // blue base (lit by a white light -> blue)
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f)); // N.L=1 on +Z normal
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
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

        for (const Ref::Scene& s : Ref::Battery())
        {
            const Vector3 objPos(0.0f, 0.0f, s.objZ);
            const float viewKeep = Ref::ViewKeep(objPos, s.world, s.view, Ref::kFogStart, Ref::kFogEnd, s.fogEnabled);
            const float objKeep  = Ref::ObjectKeep(s.objZ, Ref::kFogStart, Ref::kFogEnd, s.fogEnabled);
            const bool disc = std::fabs(viewKeep - objKeep) > 0.15f;
            if (s.discriminating != disc) { Fail(s.label, "discrimination mismatch"); continue; }
            Check(s.label, RenderColored(dev, s), Ref::ColorFor(viewKeep));
        }

        {
            const float boneZ = -2.0f, viewZ = -2.0f;
            const Vector3 skinnedPos(0.0f, 0.0f, boneZ);
            const float viewKeep = Ref::ViewKeep(skinnedPos, Matrix::getIdentityProperty(),
                Matrix::CreateTranslation(0.0f, 0.0f, viewZ), Ref::kFogStart, Ref::kFogEnd, true);
            const float objKeep = Ref::ObjectKeep(0.0f, Ref::kFogStart, Ref::kFogEnd, true);
            if (std::fabs(viewKeep - objKeep) <= 0.15f) Fail("skinned pre-skin", "not discriminating");
            else Check("skinned pre-skin (purple)", RenderSkinned(dev, boneZ, viewZ, true), Ref::ColorFor(viewKeep));
            Check("skinned fog-disabled (blue)", RenderSkinned(dev, boneZ, viewZ, false), Ref::ColorFor(1.0f));
        }

        std::printf("=== SdlGpuViewSpaceFog: %d passed, %d failed ===\n", pass_, fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    SdlGpuViewSpaceFogTest()
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
    SdlGpuViewSpaceFogTest game;
    game.Run();
    return game.getResult();
}
