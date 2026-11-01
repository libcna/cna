// SPDX-License-Identifier: MS-PL
// Task 1077: ShaderEffect uniforms set via SetUniformXxx() must actually reach the real draw
// when the effect is used through SpriteBatch::Begin(..., &fx) (EasyGL renderer).
//
// Found while prototyping Task 946 (BloomSample's shader-conversion proof): every one of
// BloomSample's 3 pixel shaders needs custom uniforms (BloomThreshold; SampleOffsets/
// SampleWeights arrays; BloomIntensity/BaseIntensity/BloomSaturation/BaseSaturation), and a probe
// test showed none of them ever reached the real draw.
//
// Root cause: EasyGLSpriteBatchRenderer::FlushBatch() used to compile its OWN separate GL program
// (customProgram_) from the ShaderEffect's raw GLSL source text, instead of using the
// ShaderEffect's own already-compiled program (effectRenderer_). ShaderEffect::SetUniformXxx()
// writes to effectRenderer_'s program via glUniform*, which only affects whichever program is
// CURRENTLY BOUND (glUseProgram) -- but FlushBatch() always bound its own separately-compiled
// customProgram_ for the real draw, so the uniform value was silently discarded.
//
// Fixed by having FlushBatch() bind the SAME compiled program the effect itself owns
// (Effect::GetEffectRendererPtr(), overridden by ShaderEffect) instead of maintaining a redundant
// second copy.
//
// This test sets a uMyTint uniform via ShaderEffect::SetUniformVec4() (blue) and confirms the
// SpriteBatch-drawn quad actually reads back blue, not the GLSL-default-initialized black that
// results when the uniform is silently orphaned on a never-bound program.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static const char* kVertSrc = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)";

// If uMyTint is actually applied, output = uMyTint (blue). If the uniform is a no-op
// (never reaches the bound program), GLSL default-initializes it to 0 -> output = black.
static const char* kFragSrc = R"(#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;
uniform vec4 uMyTint;
void main() { FragColor = vec4(uMyTint.rgb, 1.0); }
)";

static const char* kFlipFragSrc = R"(#version 300 es
precision mediump float;
out vec4 FragColor;
uniform vec4 uRtFlipV;
void main() { FragColor = vec4(uRtFlipV.xyz, 1.0); }
)";

class EasyGLShaderEffectSpriteBatchUniformTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D tex_;
    bool done_ = false;
    int result_ = 1;

    bool CheckTextureUniformProgram()
    {
        auto& device = getGraphicsDeviceProperty();
        ShaderEffect target(device, kVertSrc, kFlipFragSrc);
        ShaderEffect other(device, kVertSrc, kFlipFragSrc);
        if (!target.IsEffectValid() || !other.IsEffectValid()) return false;

        RenderTarget2D rendered(device, 8, 8);
        device.SetRenderTarget(&rendered);
        device.Clear(Color::White);
        device.SetRenderTarget(nullptr);

        const auto check = [&](ShaderEffect& effect, const Color& expected, const char* step)
        {
            device.Clear(Color::Magenta);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                       nullptr, nullptr, nullptr, &effect);
            sb_->Draw(tex_, Rectangle(0, 0, 8, 8), Color::White);
            sb_->End();
            const Rectangle region(4, 4, 1, 1);
            Color pixel;
            device.GetBackBufferData(&region, &pixel, 0, 1);
            const bool ok = pixel == expected;
            std::printf("[%s] %s: pixel=(%d,%d,%d), expected=(%d,%d,%d)\n",
                        ok ? "PASS" : "FAIL", step,
                        pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty(),
                        expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
            return ok;
        };

        // SetTexture is legal before Apply, including while another effect is current.
        // Both shaders declare the same uniform so a misdirected write also corrupts pixels.
        other.SetUniformVec4("uRtFlipV", 0, 1, 0, 0);
        target.SetTexture(0, rendered);
        bool ok = check(target, Color::Red, "SetTexture before Apply reaches its own program");
        ok = check(other, Color::Lime, "SetTexture preserves the other program") && ok;

        other.Apply();
        target.SetTexture(0, tex_);
        ok = check(target, Color::Black, "switching to uploaded rows clears the flip") && ok;
        ok = check(other, Color::Lime, "clearing the flip preserves the other program") && ok;

        // No state change: the previously uploaded flags must survive without another write.
        other.Apply();
        target.SetTexture(0, tex_);
        ok = check(target, Color::Black, "unchanged texture keeps the uploaded flags") && ok;
        return ok;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        tex_ = Texture2D::CreateFromPixels(device, 1, 1, white);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        ShaderEffect fx(device, kVertSrc, kFragSrc);
        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] EasyGLShaderEffectSpriteBatchUniform: GLSL compile error\n");
            Exit();
            return;
        }

        // Set uMyTint = blue BEFORE the SpriteBatch flush that actually draws.
        fx.Apply();
        fx.SetUniformVec4("uMyTint", 0.0f, 0.0f, 1.0f, 1.0f);

        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                   nullptr, nullptr, nullptr, &fx);
        sb_->Draw(tex_, Rectangle(W / 4, H / 4, W / 2, H / 2), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();

        const Rectangle centReg(W / 2, H / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);

        const bool ok = (centPx.getBProperty() >= 200 && centPx.getRProperty() <= 50);

        if (ok)
        {
            std::printf("[PASS] EasyGLShaderEffectSpriteBatchUniform: centre=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EasyGLShaderEffectSpriteBatchUniform: centre=(%d,%d,%d)\n"
                        "       expected: blue (B>=200,R<=50) -- uMyTint uniform did not reach the real draw\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        }
        if (!CheckTextureUniformProgram()) result_ = 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLShaderEffectSpriteBatchUniformTest game;
    game.getGraphicsDeviceProperty().SetGraphicsProfileEXT(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    game.Run();
    return game.getResult();
}
