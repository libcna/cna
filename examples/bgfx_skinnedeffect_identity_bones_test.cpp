// SPDX-License-Identifier: MS-PL
// Task 406: pixel test for SkinnedEffect's identity bone palette (Bgfx renderer). See
// examples/easygl_skinnedeffect_identity_bones_test.cpp for the full derivation.
//
// Per Task 364's finding (tracked as Task 884, not fixed there or here): Bgfx's default
// RasterizerState cull state is the only one of the 3 renderers that actually matches FNA's
// real CullCounterClockwiseFace default, so it silently culls the standard NDC quad winding
// used throughout this pixel-test family unless RasterizerState::CullNone is set
// explicitly -- worked around here identically to prior Bgfx tests.
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

class BgfxSkinnedEffectIdentityBonesTest : public Game
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

    // Bgfx's readback only reliably reflects a single fresh GetBackBufferData() call per
    // rendered frame (every prior multi-region Bgfx test in this project reads exactly one
    // rectangle per draw+retry pass) -- so each check point gets its own full clear+draw+read
    // pass rather than sharing one frame across 3 reads.
    Color renderAndRead(GraphicsDevice& device, SkinnedEffect& fx, VertexBuffer& vb, const Rectangle& reg)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 255, 0, 255));
            // Task 375 finding: GraphicsDevice::SetDepthTestEnabled/SetBlend* are unconditional-
            // throw stubs on Bgfx -- not needed here since Clear() resets the depth buffer and
            // this draws a single full-screen quad at z=0 every frame.
            device.setBlendStateProperty(BlendState::Opaque);
            // See Task 364/884 finding: Bgfx's default cull state culls this quad's winding,
            // unlike EasyGL/Vulkan's own defaults.
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            fx.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break; // skip blank/black frames
        }
        return got;
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        SkinnedEffect fx(device);
        fx.setTextureProperty(&tex_);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setWeightsPerVertexProperty(1);
        fx.EnableDefaultLighting();

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

        const Rectangle leftReg(W / 8,     H / 2, 1, 1);
        const Rectangle centReg(3 * W / 8, H / 2, 1, 1);
        const Rectangle rightReg(7 * W / 8, H / 2, 1, 1);

        const Color leftPx  = renderAndRead(device, fx, vb, leftReg);
        const Color centPx  = renderAndRead(device, fx, vb, centReg);
        const Color rightPx = renderAndRead(device, fx, vb, rightReg);

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
    BgfxSkinnedEffectIdentityBonesTest game;
    game.Run();
    return game.getResult();
}
