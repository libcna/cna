// SPDX-License-Identifier: MS-PL
// WEBGPU-142: a custom-WGSL ShaderEffect on the SpriteBatch route.
//
// SpriteBatch.Begin(..., &effect) routes every sprite through a custom WGSL ShaderEffect (the same
// WebGPUEffectRenderer the 3D route uses). This effect samples the sprite texture and multiplies by
// the vertex colour and a `uTint` uniform. Sprite vertices are already NDC, so the vertex shader
// only passes them through (no matrices).
//
// Check A -- draw a WHITE texture with a red `uTint`: the batch renders RED. Proves the custom WGSL
//   runs on the sprite path (the stock sprite pipeline would ignore uTint and render white), samples
//   the texture and reads the uniform.
// Check B -- the same, blue `uTint`: renders BLUE. Proves the uniform drives the pixel.
// Check C -- a batch with NO effect (stock Begin): renders WHITE. Proves the effect is per-Begin and
//   is cleared, so the stock sprite path still works afterwards.
//
// Only pure-channel colours are asserted, so the backbuffer's colour space cannot shift the result.
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // Sprite vertices are already NDC (position/uv/color at @location 0/1/2); the vertex shader
    // passes them through. The fragment samples the sprite texture at the reserved @binding(1)/(2)
    // and multiplies by the vertex colour and the uTint uniform block at @binding(0).
    const char* const kVertWgsl = R"WGSL(
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@vertex fn vs_main(
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f
) -> VOut {
    var o: VOut;
    o.position = vec4f(position, 1.0);
    o.uv = uv;
    o.color = color;
    return o;
}
)WGSL";

    const char* const kFragWgsl = R"WGSL(
struct U { uTint: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;
@fragment fn fs_main(
    @location(0) uv: vec2f,
    @location(1) color: vec4f
) -> @location(0) vec4f {
    return textureSample(tex, texSampler, uv) * color * u.uTint;
}
)WGSL";

    const char* const kUniformNames[] = {"uTint"};
    const int kUniformOffsets[] = {0};

    bool colorNear(Color a, Color b, int tol = 16)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tol &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tol &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tol;
    }

    Color readCenter(GraphicsDevice& dev)
    {
        const Rectangle region(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }
}

class WebGpuSpriteBatchShaderEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    SpriteBatch* sb_ = nullptr;
    Texture2D white_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        sb_ = new SpriteBatch(getGraphicsDeviceProperty());
        white_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                             std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const Rectangle full(0, 0, kSize, kSize);

        ShaderEffect fx(dev, kVertWgsl, kFragWgsl);
        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] SpriteBatch ShaderEffect failed to compile: %s\n",
                        fx.GetCompileErrorEXT().c_str());
            std::printf("=== 0/3 PASS ===\n");
            result_ = 1;
            Exit();
            return;
        }
        fx.DeclareUniformBlockEXT(16, kUniformNames, kUniformOffsets, 1);

        auto drawTinted = [&](float r, float g, float b)
        {
            dev.Clear(Color::Black);
            fx.SetUniformVec4("uTint", r, g, b, 1.0f);
            sb_->Begin(SpriteSortMode::Deferred, &BlendState::Opaque, nullptr, nullptr, nullptr, &fx);
            sb_->Draw(white_, full, Color::White);
            sb_->End();
            return readCenter(dev);
        };

        // Check A: the custom WGSL runs, samples the texture and applies a red tint.
        check(colorNear(drawTinted(1.0f, 0.0f, 0.0f), Color::Red),
              "SpriteBatch custom WGSL, uTint=red -> red (the stock path would render white)");

        // Check B: a different tint drives a different pixel.
        check(colorNear(drawTinted(0.0f, 0.0f, 1.0f), Color::Blue),
              "SpriteBatch custom WGSL, uTint=blue -> blue");

        // Check C: a batch with NO effect renders the white texture -- the effect is per-Begin.
        {
            dev.Clear(Color::Black);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            sb_->Draw(white_, full, Color::White);
            sb_->End();
            check(colorNear(readCenter(dev), Color::White),
                  "a SpriteBatch with no effect renders the stock white texture (effect is per-Begin)");
        }

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    WebGpuSpriteBatchShaderEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    ~WebGpuSpriteBatchShaderEffectTest() override { delete sb_; }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuSpriteBatchShaderEffectTest game;
    game.Run();
    return game.getResult();
}
