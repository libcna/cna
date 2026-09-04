// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-40: GraphicsDevice.BlendFactor must not be cached away by a Sokol pipeline
// created under a stale constant.
//
// Unlike Vulkan (VK_DYNAMIC_STATE_BLEND_CONSTANTS, set per-draw with vkCmdSetBlendConstants -- see
// VulkanRenderer's own "the BlendFactor *value* is dynamic" comment) or EasyGL (a plain
// glBlendColor call before every draw), sokol_gfx has no dynamic blend-constant API at all:
// sg_pipeline_desc.blend_color is baked into the pipeline object at sg_make_pipeline() time, the
// same "no dynamic call, so it lives in the pipeline key" situation SOKOL-23 already documents for
// sg_stencil_state.ref. Before this fix, SokolRenderer's PipelineKey/Pipeline3DKey/
// PipelineInstanced3DKey did not include the blend factor at all, so a second draw with an
// otherwise-identical pipeline state but a different GraphicsDevice.BlendFactor silently reused the
// first cached pipeline object -- and its stale baked-in constant.
//
// This test draws the exact same BlendState (ColorSourceBlend=BlendFactor,
// ColorDestinationBlend=Zero -- so the read-back pixel is exactly the constant, deliberately
// unaffected by whatever was drawn before it since dst is multiplied by zero) three times in a row,
// changing ONLY GraphicsDevice.BlendFactor between draws via setBlendFactorProperty -- never
// setBlendStateProperty again -- so every other PipelineKey field (blend enable/func/write mask,
// depth/stencil, sample count, colour-attachment count, vertex layout, index type) is bit-identical
// across all three draws and only a real per-draw cache-key check can tell them apart:
//   1. BlendFactor=(200,100,0)   -> expect (200,100,0)
//   2. BlendFactor=(0,100,200)   -> expect (0,100,200): the discriminating check -- a stale-cache
//      bug reuses draw 1's pipeline object and its baked (200,100,0) constant here.
//   3. BlendFactor=(200,100,0) again -> expect (200,100,0): the other direction of the same
//      transition, and confirms draw 1's pipeline is still independently correct, not corrupted.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

class BlendFactorPipelineCacheTest : public Game
{
    // Owned, not leaked: every other harness in examples/ -- including this renderer's own
    // sokol_2d_test/sokol_wireframe_test -- holds its GraphicsDeviceManager in a unique_ptr
    // member. This file was the only one that raw-`new`ed it, which ASan reported as a real
    // 488-byte leak plus the 40-byte EventHandler vector reachable only from it.
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label, const Color& got, const char* expected)
    {
        if (ok)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++pass_;
        }
        else
        {
            std::printf("[FAIL] %s: got=(%d,%d,%d) expected %s\n", label,
                got.getRProperty(), got.getGProperty(), got.getBProperty(), expected);
            ++fail_;
        }
    }

    static bool closeTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }

    static bool matches(const Color& c, const Color& expected)
    {
        return closeTo(c.getRProperty(), expected.getRProperty(), 15)
            && closeTo(c.getGProperty(), expected.getGProperty(), 15)
            && closeTo(c.getBProperty(), expected.getBProperty(), 15);
    }

    Color drawWithFactor(GraphicsDevice& dev, const VertexPositionColor (&quad)[6],
                          const Color& factor)
    {
        // Deliberately setBlendFactorProperty only -- never setBlendStateProperty again after the
        // first draw -- so no PipelineKey field other than the blend factor itself changes.
        dev.setBlendFactorProperty(factor);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);

        BlendState state;
        state.setColorSourceBlendProperty(Blend::BlendFactor);
        state.setColorDestinationBlendProperty(Blend::Zero);
        state.setAlphaSourceBlendProperty(Blend::One);
        state.setAlphaDestinationBlendProperty(Blend::Zero);
        // Established once via setBlendStateProperty; every subsequent factor change below goes
        // through setBlendFactorProperty alone, exactly like GraphicsDevice.BlendFactor in real XNA.
        dev.setBlendStateProperty(state);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color kSource(255, 255, 255, 255);
        const VertexPositionColor quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3(-1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f,  1.0f, 0.0f), kSource },
        };

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Color factorA(200, 100, 0, 255);
        const Color factorB(0, 100, 200, 255);

        const Color got1 = drawWithFactor(dev, quad, factorA);
        check(matches(got1, factorA), "1: BlendFactor=(200,100,0)", got1, "(200,100,0)");

        const Color got2 = drawWithFactor(dev, quad, factorB);
        check(matches(got2, factorB),
              "2: BlendFactor changed to (0,100,200), no other pipeline field changed",
              got2, "(0,100,200)");

        const Color got3 = drawWithFactor(dev, quad, factorA);
        check(matches(got3, factorA),
              "3: BlendFactor changed back to (200,100,0)", got3, "(200,100,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BlendFactorPipelineCacheTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BlendFactorPipelineCacheTest game;
    game.Run();
    return game.getResult();
}
