// SPDX-License-Identifier: MS-PL
// plans/plan_pre_sdlgpu_closeout.md PSG-0006: a draw that outlives the effect that queued it.
//
// This renderer records a frame and replays it later, and a queued sprite or custom-effect draw
// holds its `ShaderEffect` as a RAW pointer. Disposing the effect before the frame flushes
// therefore left the replay reading freed memory. It is not a theoretical window: it is the
// `CNAEXT_LeakLoop` segfault, where the freed object's `valid_` flag still read true, its program
// layout read null, and the pipeline built from it dereferenced nothing. AddressSanitizer named it
// exactly -- `heap-use-after-free ... in IssueSpriteWithCustomEffect`.
//
// XNA submits a draw to Direct3D 9 at the call, so disposing the effect afterwards cannot take the
// draw back. The renderer's answer is `RetainEffectForQueuedDrawsEXT`: from the effect's own
// destructor, the effect's GPU state moves to an heir the renderer owns until the queued commands
// are discarded, so the draw replays as issued. Only if the heir cannot be allocated does
// `ForgetEffectEXT` drop the draws, and count them.
//
// The test is deterministic -- no frame counts, no soak. It arranges exactly the window:
//
//     Begin -> Draw(sprite, effect) -> End      the draw is QUEUED, not issued
//     dispose the effect                        the raw pointer in the queued draw dangles
//     force the flush                           the replay would read freed memory
//
// Check A -- the flush completes and the process survives.
// Check B -- the draw was kept: nothing was counted as dropped. Whether this file's own
//            descriptor-contract sprite shader produces pixels is not its question (Check D's note);
//            deferred_effect_query_lifetime E2-E4 prove the kept draw's pixels, on every renderer.
// Check C -- an effect disposed with NO queued draw counts nothing either.
// Check D -- an ORDINARY sprite drawn afterwards still reaches the target, so the effect's
//            departure left the command queue usable.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = this renderer runs no ShaderEffect source.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kSize = 32;

    int passCount = 0;
    int totalCount = 0;

    void check(const bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    // A descriptor-contract sprite effect: the fragment samples the sprite's own texture at
    // group 0, which is the contract WMG-0009 states, and tints it so the draw is observable.
    const char* const kVertexWgsl = R"WGSL(
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(@location(0) position: vec3f,
                   @location(1) uv: vec2f,
                   @location(2) color: vec4f) -> VOut {
    // SpriteBatch hands this route clip-space positions already, exactly as
    // webgpu_spritebatch_shadereffect_test's shader does; treating them as NDC 2D renders nothing.
    var out: VOut;
    out.position = vec4f(position, 1.0);
    out.uv = uv;
    return out;
}
)WGSL";

    const char* const kFragmentWgsl = R"WGSL(
@group(0) @binding(0) var spriteTexture: texture_2d<f32>;
@group(0) @binding(32) var spriteSampler: sampler;
@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    return textureSample(spriteTexture, spriteSampler, uv);
}
)WGSL";
}

class WebGpuEffectOutlivedByDrawTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;
    bool skipped_ = false;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(dev.GetRenderer());

        Texture2D white(dev, 1, 1);
        const Color opaque = Color::White;
        white.SetData(&opaque, 1);
        SpriteBatch batch(dev);
        RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);

        // ---- C first: destroying an effect with nothing queued must count nothing -----------
        const int beforeIdle = renderer.GetDroppedDrawsForDestroyedEffectsEXT();
        {
            ShaderEffect unused(dev, kVertexWgsl, kFragmentWgsl);
            if (!unused.IsEffectValid())
            {
                std::printf("SKIP: this renderer did not compile the ShaderEffect: %s\n",
                            unused.GetCompileErrorEXT().c_str());
                skipped_ = true;
                Exit();
                return;
            }
        }
        const int afterIdle = renderer.GetDroppedDrawsForDestroyedEffectsEXT();
        check(afterIdle == beforeIdle,
              "Check C: destroying an effect with no queued draw counts nothing");

        // ---- A and B: the real window ------------------------------------------------------
        const int before = renderer.GetDroppedDrawsForDestroyedEffectsEXT();
        dev.SetRenderTarget(&target);
        dev.Clear(Color(0, 0, 0, 255));
        {
            // Queued inside this scope and disposed at its end, while the frame is still
            // unflushed -- exactly the window that used to read freed memory at replay.
            ShaderEffect doomed(dev, kVertexWgsl, kFragmentWgsl);
            batch.Begin(SpriteSortMode::Immediate, nullptr, nullptr, nullptr, nullptr, &doomed);
            batch.Draw(white, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
        }
        // The flush. Before PSG-0006 this is where the replay dereferenced the freed effect.
        dev.SetRenderTarget(nullptr);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
        const int after = renderer.GetDroppedDrawsForDestroyedEffectsEXT();

        std::printf("    dropped draws for destroyed effects: %d -> %d\n", before, after);
        check(true, "Check A: the flush completed and the process survived the freed effect");
        check(after == before,
              "Check B: the draw queued before the effect was disposed was kept, not dropped");

        // ---- D: the queue is not poisoned --------------------------------------------------
        // Deliberately an ORDINARY sprite, with no effect at all. The claim under test is that
        // forgetting one effect left the command queue usable, and an ordinary sprite is the
        // cleanest way to ask that: whether a descriptor-contract sprite EFFECT produces pixels on
        // this renderer is a different question, unproven here, and one this test must not depend
        // on to answer its own.
        dev.SetRenderTarget(&target);
        dev.Clear(Color(0, 0, 0, 255));
        batch.Begin();
        batch.Draw(white, Rectangle(0, 0, kSize, kSize), Color::White);
        batch.End();
        dev.SetRenderTarget(nullptr);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

        const Color centre = pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
        std::printf("    centre after an ordinary sprite: (%d,%d,%d,%d)\n",
                    centre.getRProperty(), centre.getGProperty(), centre.getBProperty(),
                    centre.getAProperty());
        check(centre.getRProperty() > 200 && centre.getGProperty() > 200 &&
                  centre.getBProperty() > 200,
              "Check D: an ordinary draw after the drop still reaches the target");

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuEffectOutlivedByDrawTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] bool wasSkipped() const { return skipped_; }
};

int main()
{
    WebGpuEffectOutlivedByDrawTest game;
    game.Run();

    if (game.wasSkipped()) return 77;
    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
