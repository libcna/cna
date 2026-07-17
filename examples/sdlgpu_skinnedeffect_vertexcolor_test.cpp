// SPDX-License-Identifier: MS-PL
// SkinnedEffect.VertexColorEnabled proof for the SDL_GPU graphics backend (stride-56
// skinnedColoredVertexShader_/skinned_colored3d.vert.glsl+skinned_colored3d.frag.glsl) -- proves
// the stride-56 skinned+Color vertex layout's Color attribute (location 5) is actually read and
// correctly gated by the VertexColorEnabled uniform, and that the multiply happens AFTER the
// specular term is added (not folded into fragTint, which only feeds the diffuse term -- see
// skinned_colored3d.frag.glsl's own doc comment for the real bug class this avoids).
//
// Mirrors easygl_skinnedeffect_vertexcolor_test.cpp's own established rationale for using a
// per-vertex color of pure black rather than trying to analytically reproduce
// EnableDefaultLighting()'s Blinn-Phong math across 3 lights (impractical by hand, same reasoning
// that test's own header comment gives): litRGB*texColor.rgb*vertexColor.rgb with
// vertexColor.rgb=(0,0,0) must zero out to (0,0,0,alpha) regardless of what the lit/textured
// color would otherwise be, independent of the lighting math -- a rigorously provable (not
// "ran once and pasted"), lighting-independent expected value. Quad A's own expected value is a
// deliberately qualitative "red-dominant, non-black" check (matching sdlgpu_skinned_test.cpp's
// own established convention for this backend's lit-shading results), since this is the "control"
// side of the test, not the property under test.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
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

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 64;

    // Stride-56 GPU-compact skinned+Color vertex: matches GetOrCreatePipelineSkinned3D's
    // hasVertexColor==true layout (the stride-52 SkinnedEffect layout with a per-vertex Color
    // appended at offset 52).
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

    void BuildQuad(std::vector<SkinnedColorGpuVertex>& out,
                    std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        const SkinnedColorGpuVertex tl{-1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0,  r,g,b,a};
        const SkinnedColorGpuVertex bl{-1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0,  r,g,b,a};
        const SkinnedColorGpuVertex br{ 1, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0,  r,g,b,a};
        const SkinnedColorGpuVertex tr{ 1,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0,  r,g,b,a};
        out.push_back(tl); out.push_back(bl); out.push_back(br);
        out.push_back(tl); out.push_back(br); out.push_back(tr);
    }
}

class SdlGpuSkinnedEffectVertexColorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> tex_;
    std::unique_ptr<VertexBuffer> vbNoColor_;   ///< quad, per-vertex color pure black
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, const Color& c)
    {
        std::printf("[%s] %s: sampled=(%d,%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", label,
                    c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());
        if (ok) ++passCount_;
    }

    Color RenderAndSampleCenter(GraphicsDevice& dev, SkinnedEffect& fx, VertexBuffer& vb)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetVertexBuffer(&vb);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle region(kRTSize / 2, kRTSize / 2, 1, 1);
        Color out(0, 0, 0, 0);
        rt_->GetData(0, &region, &out, 0, 1);
        return out;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        const std::vector<std::uint8_t> px = {255, 0, 0, 255};
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, px));

        // Same quad geometry both draws use -- only VertexColorEnabled (set per-draw on the
        // shared effect below) differs; the per-vertex Color itself is pure black in both cases,
        // so a bug that always ignores VertexColorEnabled (either always-on or always-off) is
        // guaranteed to fail at least one of the two checks below.
        std::vector<SkinnedColorGpuVertex> verts;
        BuildQuad(verts, 0, 0, 0, 255);
        vbNoColor_ = std::make_unique<VertexBuffer>(dev, static_cast<int>(verts.size()));
        vbNoColor_->SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedColorGpuVertex)));
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        SkinnedEffect fx(dev);
        fx.setTextureProperty(tex_.get());
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        const std::vector<Matrix> bones = {Matrix::getIdentityProperty()};
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();

        // Quad A: VertexColorEnabled=false -- the pure-black per-vertex color must be ignored,
        // rendering the same red-dominant lit/textured result EnableDefaultLighting() + a red
        // texture always produces on an identity-bone quad (qualitative check -- the exact
        // Blinn-Phong value across 3 lights is this test's "control" side, not what's under test).
        fx.VertexColorEnabled = false;
        const Color a = RenderAndSampleCenter(dev, fx, *vbNoColor_);
        const bool aOk = a.getRProperty() > a.getGProperty() && a.getRProperty() > a.getBProperty() && a.getRProperty() > 30;
        Check(aOk, "quadA-vertexcolor-disabled-stays-red-lit", a);

        // Quad B: VertexColorEnabled=true -- the same pure-black per-vertex color must now zero
        // the FINAL combined diffuse+specular+texture result exactly, regardless of the lighting
        // math (the property genuinely under test).
        fx.VertexColorEnabled = true;
        const Color b = RenderAndSampleCenter(dev, fx, *vbNoColor_);
        const bool bOk = b.getRProperty() == 0 && b.getGProperty() == 0 && b.getBProperty() == 0;
        Check(bOk, "quadB-vertexcolor-enabled-black-zeroes-result", b);

        std::printf("=== %d/2 PASS ===\n", passCount_);
        result_ = (passCount_ == 2) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuSkinnedEffectVertexColorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuSkinnedEffectVertexColorTest game;
    game.Run();
    return game.getResult();
}
