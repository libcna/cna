// SPDX-License-Identifier: MS-PL
// Task 406: pixel test for SkinnedEffect's identity bone palette (EasyGL backend).
//
// The first real pixel/rendering test for SkinnedEffect in Phase 46 (Tasks 401-405 were all
// unit tests/audits, no GPU rendering exercised yet). Confirms the skinning math correctly
// degenerates to a no-op when every bone transform is Identity: for a vertex bound 100% to a
// single bone (weight=1, all other weights=0), `skinMat = weight * Bones[index] = 1 * Identity
// = Identity`, so `mul(Position, skinMat) = Position` unchanged -- the mesh should render at
// exactly its authored position, with zero deformation.
//
// Deliberately structured as a direct contrast with the pre-existing (Task 123)
// examples/skinned_effect_integration_test.cpp, which uses the identical quad geometry and
// vertex layout but sets bone 0 to a +0.5 X translation, shifting the quad from the left half
// of the screen into the centre. This test uses SkinnedEffect's own real default bone palette
// (every one of the 72 slots defaults to Matrix.Identity -- confirmed by Task 401's audit and
// Task 402's own `BoneTransformsDefaultToIdentity` test -- no `SetBoneTransforms` call needed
// at all) and confirms the quad does NOT move: it stays exactly in the left half of the screen,
// contrasting cleanly with Task 123's own already-verified translation behavior.
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

// GPU-compact skinned vertex: matches the EasyGL stride-52 layout (Task 123's own convention).
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

class SkinnedEffectIdentityBonesTest : public Game
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
        // Task 896 finding (mirrors the Bgfx sibling's Task 364/884 fix): once
        // GraphicsDevice's real default RasterizerState is pushed to every backend,
        // this quad's winding is culled unless explicitly disabled.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        // Deliberately no SetBoneTransforms call -- exercising the effect's own real default
        // bone palette (all 72 slots = Matrix.Identity), not an explicitly-set identity palette.
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();
        fx.Apply();

        // Quad covering NDC x: -1..0, y: -1..1 -- identical geometry to Task 123's own test.
        // All vertices bound 100% to bone 0 (w0=1, others=0; index i0=0), which defaults to
        // Identity, so the quad should NOT move from this authored position.
        const SkinnedGpuVertex verts[6] = {
            { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            { -1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0 },
            {  0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
            { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            {  0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
            {  0,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0 },
        };

        VertexBuffer vb(device, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // left  (NDC ~ -0.75) -> INSIDE the un-deformed quad -> textured/lit (red-dominant)
        // centre(NDC ~  0.00) -> INSIDE the un-deformed quad (right edge) -> textured/lit
        // right (NDC ~ +0.75) -> OUTSIDE the quad (it never moved here) -> green background
        const Rectangle leftReg(W / 8,     H / 2, 1, 1);
        const Rectangle centReg(3 * W / 8, H / 2, 1, 1); // NDC ~ -0.25, safely inside
        const Rectangle rightReg(7 * W / 8, H / 2, 1, 1);
        Color leftPx(0,0,0,0), centPx(0,0,0,0), rightPx(0,0,0,0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&centReg,  &centPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        const bool leftOk  = (leftPx.getRProperty()  > leftPx.getGProperty() && leftPx.getRProperty()  > 50);
        const bool centOk  = (centPx.getRProperty()  > centPx.getGProperty() && centPx.getRProperty()  > 50);
        const bool rightOk = (rightPx.getGProperty() > rightPx.getRProperty());

        if (leftOk && centOk && rightOk)
        {
            std::printf("[PASS] SkinnedEffectIdentityBones: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] SkinnedEffectIdentityBones: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n"
                        "       expected: left=textured, centre=textured (quad unmoved), right=green\n",
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
    SkinnedEffectIdentityBonesTest game;
    game.Run();
    return game.getResult();
}
