// SPDX-License-Identifier: MS-PL
// Task 123: EasyGL integration test — SkinnedEffect 2-bone transform + mesh deformation.
//
// Creates a quad spanning the left half of the screen (NDC x: -1..0), with all
// vertices bound 100% to bone 0.  Bone 0 is set to translate(+0.5, 0, 0), which
// shifts the quad into the centre of the screen (NDC x: -0.5..0.5).
// Background is cleared to green.
//
// Expected results after GPU skinning:
//   - Pixel at left quarter (NDC ≈ -0.75) → green  (quad moved away)
//   - Pixel at centre       (NDC ≈  0.00) → red    (quad now covers centre)
//   - Pixel at right-most   (NDC ≈ +0.75) → green  (quad doesn't reach there)
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

// GPU-compact skinned vertex: matches the EasyGL stride-52 layout.
// pos(12) + normal(12) + uv(8) + weights(16) + indices(4) = 52 bytes
struct SkinnedGpuVertex
{
    float px, py, pz;            // position
    float nx, ny, nz;            // normal (unused by test but required by layout)
    float u, v;                  // texture coord
    float w0, w1, w2, w3;       // bone weights
    uint8_t i0, i1, i2, i3;    // bone indices
};
static_assert(sizeof(SkinnedGpuVertex) == 52, "skinned vertex must be 52 bytes");

class SkinnedIntegrationTest : public Game
{
    Texture2D   tex_;
    bool        done_   = false;
    int         result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();

        // 1×1 solid red texture (UV irrelevant for the position test, just needs a valid texture)
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

        device.Clear(Color(0, 255, 0, 255));   // background = green
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // Set up SkinnedEffect.
        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        // Bone 0 = translate +0.5 along X.
        // Bone 1 = identity (not used in this test but palette must have at least 1 entry).
        std::vector<Matrix> bones = {
            Matrix::CreateTranslation(0.5f, 0.0f, 0.0f),  // bone 0 — shifts quad right
            Matrix::getIdentityProperty(),                 // bone 1 — not referenced
        };
        fx.SetBoneTransforms(bones);
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();  // needed: without lights the Phong shader outputs black
        fx.Apply();

        // Quad covering NDC x: -1..0, y: -1..1.
        // After bone 0 (translate +0.5): quad ends up at NDC x: -0.5..0.5.
        // All vertices bound 100% to bone 0 (w0=1, others=0; index i0=0).
        const SkinnedGpuVertex verts[6] = {
            // tri 0
            { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            { -1, -1, 0,  0,0,1,  0,1,  1,0,0,0,  0,0,0,0 },
            {  0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
            // tri 1
            { -1,  1, 0,  0,0,1,  0,0,  1,0,0,0,  0,0,0,0 },
            {  0, -1, 0,  0,0,1,  1,1,  1,0,0,0,  0,0,0,0 },
            {  0,  1, 0,  0,0,1,  1,0,  1,0,0,0,  0,0,0,0 },
        };

        VertexBuffer vb(device, 6);
        vb.SetDataRaw(verts, 6, static_cast<int>(sizeof(SkinnedGpuVertex)));
        device.SetVertexBuffer(&vb);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);

        // Read back three pixels.
        const Rectangle leftReg(W / 8,     H / 2, 1, 1);  // NDC ≈ -0.75 — outside quad
        const Rectangle centReg(W / 2,     H / 2, 1, 1);  // NDC ≈  0.00 — inside skinned quad
        const Rectangle rightReg(7 * W / 8, H / 2, 1, 1); // NDC ≈ +0.75 — outside quad
        Color leftPx(0,0,0,0), centPx(0,0,0,0), rightPx(0,0,0,0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&centReg,  &centPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        // left → green background, centre → red texture (lit, so R can be < 255),
        // right → green background.  Use R > G as the discriminator for the red channel.
        const bool leftOk  = (leftPx.getGProperty()  > leftPx.getRProperty());
        const bool centOk  = (centPx.getRProperty()  > centPx.getGProperty() && centPx.getRProperty() > 50);
        const bool rightOk = (rightPx.getGProperty() > rightPx.getRProperty());

        if (leftOk && centOk && rightOk)
        {
            std::printf("[PASS] SkinnedIntegration: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] SkinnedIntegration: left=(%d,%d,%d) centre=(%d,%d,%d) right=(%d,%d,%d)\n"
                        "       expected: left=green, centre=red, right=green\n",
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
    SkinnedIntegrationTest game;
    game.Run();
    return game.getResult();
}
