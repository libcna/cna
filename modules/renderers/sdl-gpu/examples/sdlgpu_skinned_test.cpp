// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-34: SkinnedEffect proof for the SDL_GPU graphics renderer, and the
// empirical spike this task itself needed: whether SDL_gpu's push-uniform-data mechanism can
// carry a full 72-bone (4608-byte) palette. Verified: SDL_PushGPUVertexUniformData follows std140
// UBO layout conventions (per its own doc comment), not a hardware push-constant register file, so
// it is not subject to Vulkan's typical 128/256-byte push-constant limit -- all 3 checks below pass
// with the real 4608-byte BoneBlock uniform.
//
// Checks A/B are directly ported from this project's own existing
// vulkan_skinnedeffect_translation_bone_test.cpp / vulkan_skinnedeffect_twobone_blend_test.cpp
// (same geometry/bone values), using RenderTarget2D::GetData() instead of GetBackBufferData()
// since this renderer's swapchain-download path segfaults (see plans/plan_sdlgpu.md's SDLGPU-39 row).
//
// Check A -- a single translation bone (index 0, weight 1): a textured quad shifted right by the
//   bone's translation -- left/right sample points show the cleared background, only the centre
//   (where the shifted quad now covers) shows the red texture.
// Check B -- a two-bone 50/50 weighted blend (indices 0 and 1, weights 0.5/0.5): the same
//   left/centre/right pattern, proving a genuine weighted SUM of two distinct bone matrices, not
//   just the first bone's transform.
// Check C -- the actual empirical spike: a translation bone at index 71 (the LAST slot of the
//   72-matrix palette, all other 71 slots Identity) produces the exact same shift as Check A's
//   index-0 bone -- proves the full 4608-byte BoneBlock uniform is transmitted intact and
//   indexable at the far end, not truncated or corrupted for a large push.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

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

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 64;

    // GPU-compact skinned vertex: matches the stride-52 layout this project's other renderers
    // already use for SkinnedEffect tests.
    struct SkinnedGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");
}

class SdlGpuSkinnedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> tex_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label, const Color& l, const Color& c, const Color& r)
    {
        std::printf("[%s] %s: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", label,
                    l.getRProperty(), l.getGProperty(), l.getBProperty(),
                    c.getRProperty(), c.getGProperty(), c.getBProperty(),
                    r.getRProperty(), r.getGProperty(), r.getBProperty());
        if (ok) ++passCount_;
    }

    // Renders the given skinned quad and returns the left/centre/right samples along the
    // vertical-centre row, matching this project's existing SkinnedEffect pixel-test convention.
    void RenderAndSample(GraphicsDevice& dev, const std::vector<Matrix>& bones, int weightsPerVertex,
                         const SkinnedGpuVertex (&verts)[6], Color& left, Color& centre, Color& right)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 255, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(dev);
        fx.setTextureProperty(tex_.get());
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(weightsPerVertex);
        fx.EnableDefaultLighting();
        fx.Apply();

        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle leftReg(kRTSize / 8, kRTSize / 2, 1, 1);
        const Rectangle centReg(kRTSize / 2, kRTSize / 2, 1, 1);
        const Rectangle rightReg(7 * kRTSize / 8, kRTSize / 2, 1, 1);
        left = Color(0, 0, 0, 0); centre = Color(0, 0, 0, 0); right = Color(0, 0, 0, 0);
        rt_->GetData(0, &leftReg, &left, 0, 1);
        rt_->GetData(0, &centReg, &centre, 0, 1);
        rt_->GetData(0, &rightReg, &right, 0, 1);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        const std::vector<std::uint8_t> px = {255, 0, 0, 255};
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, px));
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        // Check A: single translation bone at index 0, weight 1.
        {
            const std::vector<Matrix> bones = {Matrix::CreateTranslation(0.5f, 0.0f, 0.0f)};
            const SkinnedGpuVertex verts[6] = {
                {-1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0},
                {-1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0},
                { 0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0},
                {-1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0},
                { 0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0},
                { 0,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0},
            };
            Color l(0, 0, 0, 0), c(0, 0, 0, 0), r(0, 0, 0, 0);
            RenderAndSample(dev, bones, 1, verts, l, c, r);
            const bool ok = (l.getGProperty() > l.getRProperty())
                         && (c.getRProperty() > c.getGProperty() && c.getRProperty() > 50)
                         && (r.getGProperty() > r.getRProperty());
            Check(ok, "single translation bone (index 0) shifts the quad right", l, c, r);
        }

        // Check B: two-bone 50/50 weighted blend (indices 0 and 1).
        {
            const std::vector<Matrix> bones = {
                Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
                Matrix::CreateTranslation(1.5f, 0.0f, 0.0f),
            };
            const SkinnedGpuVertex verts[6] = {
                {-1,  1, 0,  0,0,1,  0,0,  0.5f,0.5f,0,0,  0,1,0,0},
                {-1, -1, 0,  0,0,1,  0,1,  0.5f,0.5f,0,0,  0,1,0,0},
                { 0, -1, 0,  0,0,1,  1,1,  0.5f,0.5f,0,0,  0,1,0,0},
                {-1,  1, 0,  0,0,1,  0,0,  0.5f,0.5f,0,0,  0,1,0,0},
                { 0, -1, 0,  0,0,1,  1,1,  0.5f,0.5f,0,0,  0,1,0,0},
                { 0,  1, 0,  0,0,1,  1,0,  0.5f,0.5f,0,0,  0,1,0,0},
            };
            Color l(0, 0, 0, 0), c(0, 0, 0, 0), r(0, 0, 0, 0);
            RenderAndSample(dev, bones, 2, verts, l, c, r);
            const bool ok = (l.getGProperty() > l.getRProperty())
                         && (c.getRProperty() > c.getGProperty() && c.getRProperty() > 50)
                         && (r.getGProperty() > r.getRProperty());
            Check(ok, "two-bone 50/50 weighted blend (indices 0,1) shifts the quad right", l, c, r);
        }

        // Check C: the empirical spike -- a translation bone at index 71 (the LAST slot of the
        // full 72-matrix palette, all others Identity) produces the exact same shift as Check A's
        // index-0 bone. Proves the full 4608-byte BoneBlock uniform is transmitted intact and
        // indexable at the far end.
        {
            std::vector<Matrix> bones(72, Matrix::getIdentityProperty());
            bones[71] = Matrix::CreateTranslation(0.5f, 0.0f, 0.0f);
            const SkinnedGpuVertex verts[6] = {
                {-1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  71,0,0,0},
                {-1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  71,0,0,0},
                { 0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  71,0,0,0},
                {-1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  71,0,0,0},
                { 0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  71,0,0,0},
                { 0,  1, 0,  0,0,1,  1,0,  1,0,0,0,  71,0,0,0},
            };
            Color l(0, 0, 0, 0), c(0, 0, 0, 0), r(0, 0, 0, 0);
            RenderAndSample(dev, bones, 1, verts, l, c, r);
            const bool ok = (l.getGProperty() > l.getRProperty())
                         && (c.getRProperty() > c.getGProperty() && c.getRProperty() > 50)
                         && (r.getGProperty() > r.getRProperty());
            Check(ok, "translation bone at index 71 (full 72-bone palette) shifts the quad right", l, c, r);
        }

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuSkinnedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuSkinnedTest game;
    game.Run();
    return game.getResult();
}
