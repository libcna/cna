// SPDX-License-Identifier: MS-PL
// Task 895: SkinnedEffect pixel test — WeightsPerVertex is a real GPU-enforced constraint
// (Vulkan backend).
//
// FNA reference (Graphics/Effect/StockEffects/HLSL/SkinnedEffect.fx's Skin(vin, boneCount)):
//   skinning = sum_{i=0}^{boneCount-1} Bones[Indices[i]] * Weights[i]
// Only the FIRST WeightsPerVertex (1, 2, or 4) weight/index pairs are ever read — slots beyond
// that are never touched by the real shader, regardless of what garbage data they contain.
//
// Before this task, CNA's skinning shaders always summed all 4 weight/index pairs
// unconditionally, ignoring WeightsPerVertex entirely (a "GPU no-op" per the property's own
// C++-side validation working, but never reaching the GPU).
//
// This is the same 2-bone blend as Task 408's own test (Bone 0 = Translate(-0.5,0,0), Bone 1 =
// Translate(+1.5,0,0), weights 0.5/0.5 => net shift +0.5, matching Task 407's single-bone
// result), but with WeightsPerVertex=2 explicitly set AND slots 2/3 deliberately populated with
// non-zero "garbage" weights (0.5 each) pointing at a 3rd bone with a huge, unmistakable
// translation (+100 on X). Task 408's own test never exercised this because its own slots 2/3
// were already zero-weighted, so summing all 4 vs. only the first 2 gave the same answer either
// way -- this test's garbage data makes the two cases genuinely diverge:
//   - Correct (WeightsPerVertex=2 honored): net shift = +0.5, quad lands at screen centre.
//   - Buggy (all 4 always summed): net shift = +0.5 + 0.5*100 + 0.5*100 = +100.5, pushing the
//     quad entirely off-screen -- the centre pixel reads background green instead of the quad.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

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

class VulkanSkinnedEffectWeightsPerVertexTest : public Game
{
    Texture2D tex_;
    bool      done_   = false;
    int       result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const std::vector<uint8_t> px = { 255, 0, 0, 255 };
        tex_ = Texture2D::CreateFromPixels(device, 1, 1, px);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        // Bone 0/1 match Task 408's own 2-bone blend exactly (net shift +0.5 when only these
        // 2 slots are honored). Bone 2 is a huge, unmistakable "garbage" translation that must
        // NOT contribute when WeightsPerVertex=2.
        std::vector<Matrix> bones = {
            Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
            Matrix::CreateTranslation(1.5f, 0.0f, 0.0f),
            Matrix::CreateTranslation(100.0f, 0.0f, 0.0f),
        };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(2);
        fx.EnableDefaultLighting();
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): the standard NDC
        // quad winding used throughout this pixel-test family is culled once the real default
        // RasterizerState reaches the GPU.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        fx.Apply();

        // Same NDC quad as Task 408's own test. Slots 2/3 carry non-zero "garbage" weights
        // pointing at bone 2 -- must be ignored when WeightsPerVertex=2.
        const SkinnedGpuVertex verts[6] = {
            { -1,  1, 0,  0,0,1,  0,0,  0.5f,0.5f,0.5f,0.5f,  0,1,2,2 },
            { -1, -1, 0,  0,0,1,  0,1,  0.5f,0.5f,0.5f,0.5f,  0,1,2,2 },
            {  0, -1, 0,  0,0,1,  1,1,  0.5f,0.5f,0.5f,0.5f,  0,1,2,2 },
            { -1,  1, 0,  0,0,1,  0,0,  0.5f,0.5f,0.5f,0.5f,  0,1,2,2 },
            {  0, -1, 0,  0,0,1,  1,1,  0.5f,0.5f,0.5f,0.5f,  0,1,2,2 },
            {  0,  1, 0,  0,0,1,  1,0,  0.5f,0.5f,0.5f,0.5f,  0,1,2,2 },
        };

        VertexBuffer vb(device, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // left  (NDC ~ -0.75) -> OUTSIDE the correctly-shifted quad -> green background
        // centre(NDC ~  0.00) -> INSIDE the correctly-shifted quad   -> textured/lit (red-dominant)
        // right (NDC ~ +0.75) -> OUTSIDE the correctly-shifted quad -> green background
        const Rectangle leftReg(W / 8,     H / 2, 1, 1);
        const Rectangle centReg(W / 2,     H / 2, 1, 1);
        const Rectangle rightReg(7 * W / 8, H / 2, 1, 1);
        Color leftPx(0,0,0,0), centPx(0,0,0,0), rightPx(0,0,0,0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&centReg,  &centPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        const bool leftOk  = (leftPx.getGProperty()  > leftPx.getRProperty());
        const bool centOk  = (centPx.getRProperty()  > centPx.getGProperty() && centPx.getRProperty()  > 50);
        const bool rightOk = (rightPx.getGProperty() > rightPx.getRProperty());

        if (leftOk && centOk && rightOk)
        {
            std::printf("[PASS] SkinnedEffectWeightsPerVertex: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] SkinnedEffectWeightsPerVertex: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n"
                        "       expected: left=green, centre=textured (quad at correct +0.5 shift), right=green.\n"
                        "       centre=green would mean slots 2/3's garbage bone-2 weights leaked in,\n"
                        "       pushing the quad off-screen -- WeightsPerVertex=2 was not honored.\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanSkinnedEffectWeightsPerVertexTest game;
    game.Run();
    return game.getResult();
}
