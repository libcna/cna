// SPDX-License-Identifier: MS-PL
// Task 408: pixel test for SkinnedEffect's two-bone weighted blend (LLGL renderer). See
// examples/easygl_skinnedeffect_twobone_blend_test.cpp for the full derivation.
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

class LlglSkinnedEffectTwoBoneBlendTest : public Game
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
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): this quad's
        // winding is culled under FNA's real default RasterizerState.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        std::vector<Matrix> bones = {
            Matrix::CreateTranslation(-0.5f, 0.0f, 0.0f),
            Matrix::CreateTranslation(1.5f, 0.0f, 0.0f),
        };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(2);
        fx.EnableDefaultLighting();
        fx.Apply();

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
    LlglSkinnedEffectTwoBoneBlendTest game;
    game.Run();
    return game.getResult();
}
