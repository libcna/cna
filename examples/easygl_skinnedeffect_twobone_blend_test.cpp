// SPDX-License-Identifier: MS-PL
// Task 408: pixel test for SkinnedEffect's two-bone weighted blend (EasyGL backend).
//
// Confirms FNA's real Skin(vin, boneCount) formula for boneCount=2:
//   skinMat = Bones[Indices[0]] * Weights[0] + Bones[Indices[1]] * Weights[1]
// Since matrix multiplication is linear in the matrix, mul(Position, skinMat) equals
// Weights[0] * mul(Position, Bones[0]) + Weights[1] * mul(Position, Bones[1]).
//
// Bone 0 = Translate(-0.5, 0, 0), Bone 1 = Translate(+1.5, 0, 0), weights split 0.5/0.5:
//   blended shift = 0.5*(-0.5) + 0.5*(+1.5) = +0.5
// This value is distinct from EITHER individual bone's own shift (-0.5 or +1.5), so it
// proves the shader is genuinely blending both weighted bones rather than only applying
// one of them (a bug that picked bone 0 alone would shift by -0.5; bone 1 alone by +1.5;
// neither matches the correct blended +0.5).
//
// The net effect (+0.5 shift) numerically matches Task 407's single-bone translation test,
// but is reached via a genuinely different mechanism -- 2 nonzero weighted bones instead of
// a single weight=1 bone -- so the same NDC quad geometry and read-back points apply.
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
// pos(12) + normal(12) + uv(8) + weights(16) + indices(4) = 52 bytes
struct SkinnedGpuVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

class SkinnedEffectTwoBoneBlendTest : public Game
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

        // Bone 0 = translate -0.5 along X, Bone 1 = translate +1.5 along X.
        // Weighted 0.5/0.5 blend => net shift = 0.5*(-0.5) + 0.5*(+1.5) = +0.5, distinct from
        // either bone's own individual shift.
        std::vector<Matrix> bones = {
            Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
            Matrix::CreateTranslation(1.5f, 0.0f, 0.0f),
        };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(2);
        fx.EnableDefaultLighting();
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): the standard NDC
        // quad winding used throughout this pixel-test family is culled once the real default
        // RasterizerState reaches the GPU.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        fx.Apply();

        // Quad covering NDC x: -1..0, y: -1..1. All vertices split 0.5/0.5 across bones 0/1;
        // net blended shift = +0.5, so the quad ends up at NDC x: -0.5..0.5 (screen centre) --
        // numerically identical to Task 407's result, reached via genuine 2-bone blending.
        const SkinnedGpuVertex verts[6] = {
            { -1,  1, 0,  0,0,1,  0,0,  0.5f,0.5f,0,0,  0,1,0,0 },
            { -1, -1, 0,  0,0,1,  0,1,  0.5f,0.5f,0,0,  0,1,0,0 },
            {  0, -1, 0,  0,0,1,  1,1,  0.5f,0.5f,0,0,  0,1,0,0 },
            { -1,  1, 0,  0,0,1,  0,0,  0.5f,0.5f,0,0,  0,1,0,0 },
            {  0, -1, 0,  0,0,1,  1,1,  0.5f,0.5f,0,0,  0,1,0,0 },
            {  0,  1, 0,  0,0,1,  1,0,  0.5f,0.5f,0,0,  0,1,0,0 },
        };

        VertexBuffer vb(device, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // left  (NDC ~ -0.75) -> OUTSIDE the shifted quad -> green background
        // centre(NDC ~  0.00) -> INSIDE the shifted quad   -> textured/lit (red-dominant)
        // right (NDC ~ +0.75) -> OUTSIDE the shifted quad -> green background
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
            std::printf("[PASS] SkinnedEffectTwoBoneBlend: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] SkinnedEffectTwoBoneBlend: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n"
                        "       expected: left=green, centre=textured (quad blend-shifted), right=green\n",
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
    SkinnedEffectTwoBoneBlendTest game;
    game.Run();
    return game.getResult();
}
