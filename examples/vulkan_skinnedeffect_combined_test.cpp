// SPDX-License-Identifier: MS-PL
// Task 409: cross-backend SkinnedEffect capstone (Vulkan backend). See
// examples/easygl_skinnedeffect_combined_test.cpp for the full derivation.
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

// Emits the 6 vertices (2 triangles) of a quad spanning authored NDC x: -1.0..-0.5, y: -1..1,
// with the given per-vertex weight/index skinning data applied uniformly to all 6 vertices.
static void appendQuad(std::vector<SkinnedGpuVertex>& out,
                        float w0, float w1, float w2, float w3,
                        uint8_t i0, uint8_t i1, uint8_t i2, uint8_t i3)
{
    const float xMin = -1.0f, xMax = -0.5f;
    const SkinnedGpuVertex tl{ xMin,  1, 0,  0,0,1,  0,0,  w0,w1,w2,w3,  i0,i1,i2,i3 };
    const SkinnedGpuVertex bl{ xMin, -1, 0,  0,0,1,  0,1,  w0,w1,w2,w3,  i0,i1,i2,i3 };
    const SkinnedGpuVertex br{ xMax, -1, 0,  0,0,1,  1,1,  w0,w1,w2,w3,  i0,i1,i2,i3 };
    const SkinnedGpuVertex tr{ xMax,  1, 0,  0,0,1,  1,0,  w0,w1,w2,w3,  i0,i1,i2,i3 };
    out.push_back(tl); out.push_back(bl); out.push_back(br);
    out.push_back(tl); out.push_back(br); out.push_back(tr);
}

class SkinnedEffectCombinedTest : public Game
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
        // Task 896 finding (mirrors the Bgfx sibling\'s Task 364/884 fix): this quad\'s
        // winding is culled under FNA\'s real default RasterizerState.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        std::vector<Matrix> bones = {
            Matrix::getIdentityProperty(),
            Matrix::CreateTranslation(0.75f, 0.0f, 0.0f),
            Matrix::CreateTranslation(1.0f, 0.0f, 0.0f),
            Matrix::CreateTranslation(2.0f, 0.0f, 0.0f),
        };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(2);
        fx.EnableDefaultLighting();
        fx.Apply();

        std::vector<SkinnedGpuVertex> verts;
        appendQuad(verts, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0); // quad A: identity -> stays -1.0..-0.5
        appendQuad(verts, 1.0f, 0.0f, 0.0f, 0.0f, 1, 0, 0, 0); // quad B: bone 1 -> -0.25..0.25
        appendQuad(verts, 0.5f, 0.5f, 0.0f, 0.0f, 2, 3, 0, 0); // quad C: bones 2+3 blend -> 0.5..1.0

        VertexBuffer vb(device, static_cast<int>(verts.size()));
        vb.SetDataRaw(verts.data(), static_cast<int>(verts.size()), static_cast<int>(sizeof(SkinnedGpuVertex)));
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, static_cast<int>(verts.size() / 3));

        const Rectangle aReg(W / 8,     H / 2, 1, 1); // NDC x ~ -0.75 -> quad A (identity)
        const Rectangle bReg(W / 2,     H / 2, 1, 1); // NDC x ~  0.00 -> quad B (single bone)
        const Rectangle cReg(7 * W / 8, H / 2, 1, 1); // NDC x ~ +0.75 -> quad C (two-bone blend)
        Color aPx(0,0,0,0), bPx(0,0,0,0), cPx(0,0,0,0);
        device.GetBackBufferData(&aReg, &aPx, 0, 1);
        device.GetBackBufferData(&bReg, &bPx, 0, 1);
        device.GetBackBufferData(&cReg, &cPx, 0, 1);

        const bool aOk = (aPx.getRProperty() > aPx.getGProperty() && aPx.getRProperty() > 50);
        const bool bOk = (bPx.getRProperty() > bPx.getGProperty() && bPx.getRProperty() > 50);
        const bool cOk = (cPx.getRProperty() > cPx.getGProperty() && cPx.getRProperty() > 50);

        if (aOk && bOk && cOk)
        {
            std::printf("[PASS] SkinnedEffectCombined: A(identity)=(%d,%d,%d) B(single-bone)=(%d,%d,%d) C(two-bone)=(%d,%d,%d)\n",
                        aPx.getRProperty(), aPx.getGProperty(), aPx.getBProperty(),
                        bPx.getRProperty(), bPx.getGProperty(), bPx.getBProperty(),
                        cPx.getRProperty(), cPx.getGProperty(), cPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] SkinnedEffectCombined: A(identity)=(%d,%d,%d) B(single-bone)=(%d,%d,%d) C(two-bone)=(%d,%d,%d)\n"
                        "       expected: all 3 textured (red-dominant)\n",
                        aPx.getRProperty(), aPx.getGProperty(), aPx.getBProperty(),
                        bPx.getRProperty(), bPx.getGProperty(), bPx.getBProperty(),
                        cPx.getRProperty(), cPx.getGProperty(), cPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    SkinnedEffectCombinedTest game;
    game.Run();
    return game.getResult();
}
