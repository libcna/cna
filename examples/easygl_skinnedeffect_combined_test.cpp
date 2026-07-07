// SPDX-License-Identifier: MS-PL
// Task 409: cross-backend SkinnedEffect capstone (EasyGL backend).
//
// Combines Tasks 406-408's individually-verified pieces into ONE scene, drawn with a SINGLE
// bone-palette upload and a SINGLE DrawPrimitives call covering 3 quads whose vertices carry
// different weight/index data -- proving the pieces compose correctly together within one draw
// call, not just when each is tested in isolation (Tasks 406-408 each used their own dedicated
// draw call).
//
// All 3 quads share the identical authored geometry (NDC x: -1.0..-0.5, y: -1..1) and are
// distinguished only by their per-vertex bone weights/indices -- proving the vertex shader reads
// genuinely per-vertex skinning data within a single draw, not some global/uniform override:
//
//   Quad A (Task 406's identity-bone case): w0=1, i0=0 -> Bones[0]=Identity -> stays at
//     x: -1.0..-0.5.
//   Quad B (Task 407's single-bone translation case): w0=1, i0=1 -> Bones[1]=Translate(+0.75,0,0)
//     -> shifts to x: -0.25..0.25.
//   Quad C (Task 408's two-bone blend case): w0=w1=0.5, i0=2, i1=3 -> Bones[2]=Translate(+1.0,0,0),
//     Bones[3]=Translate(+2.0,0,0) -> blended shift = 0.5*1.0 + 0.5*2.0 = +1.5, distinct from
//     either individual bone -> shifts to x: 0.5..1.0.
//
// The 3 post-transform quads land in exactly the 3 non-overlapping regions this phase's own
// established read-back columns already probe (W/8, W/2, 7W/8) -- so the SAME 3 pixel columns
// used throughout Tasks 406-408 as "inside vs outside a single quad" checks now become
// "which quad is visible here" checks, cleanly proving 3 independent per-vertex bone
// applications coexist correctly in one draw call.
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

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        // Bone 0 = Identity (quad A). Bone 1 = Translate(+0.75,0,0) (quad B). Bones 2/3 =
        // Translate(+1.0,0,0)/Translate(+2.0,0,0), blended 0.5/0.5 = +1.5 net shift (quad C).
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

        // Same 3 pixel columns used throughout Tasks 406-408 (W/8, W/2, 7W/8); each now lands
        // inside a DIFFERENT quad's post-transform region.
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
