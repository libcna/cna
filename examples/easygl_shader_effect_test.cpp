// SPDX-License-Identifier: MS-PL
// Task 132: EasyGL integration test — ShaderEffect (GLSL) with SpriteBatch.
//
// Renders a white 1×1 texture through a custom GLSL ShaderEffect that outputs
// only the red channel of the sampled texel, producing a red-tinted sprite
// over a green background.
//
// Vertex shader matches the SpriteBatch attribute layout exactly:
//   location 0 = vec2 aPos      (pixel coords)
//   location 1 = vec2 aTexCoord
//   location 2 = vec4 aColor
//   uniform mat4 projection     (set by SpriteBatch on the compiled program)
//
// Fragment shader:
//   FragColor = vec4(texture(texture1, TexCoord).r, 0.0, 0.0, 1.0)
//   → white texel → red output  (verifies custom GLSL replaces the built-in shader)
//
// The sampler2D 'texture1' uniform defaults to texture unit 0, which is where
// SpriteBatch binds the sprite texture, so no explicit uniform-integer set is needed.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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

// ---------------------------------------------------------------------------
// GLSL ES 3.0 shaders — attribute layout matches EasyGLSpriteBatchRenderer.
// ---------------------------------------------------------------------------

static const char* kVertSrc = R"(#version 300 es
precision highp float;

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

out vec2 TexCoord;
out vec4 Color;

uniform mat4 projection;

void main()
{
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}
)";

// Red-tint fragment: output only the red channel of the sampled texel.
// White texture (r=1) → red output (1, 0, 0, 1).
static const char* kFragSrc = R"(#version 300 es
precision mediump float;

in vec2 TexCoord;
in vec4 Color;

out vec4 FragColor;

uniform sampler2D texture1;

void main()
{
    vec4 t = texture(texture1, TexCoord);
    FragColor = vec4(t.r, 0.0, 0.0, t.a);
}
)";

class EasyGLShaderEffectTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D                    tex_;
    bool                         done_   = false;
    int                          result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        // 1×1 solid-white texture — the red-tint shader will colourise it.
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
            std::printf("[FAIL] EasyGLShaderEffect: GLSL compile failed\n");
            Exit();
            return;
        }

        device.Clear(Color(0, 255, 0, 255)); // green background
        device.SetDepthTestEnabled(false);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                   nullptr, nullptr, nullptr, &fx);
        sb_->Draw(tex_,
                  Rectangle(W / 4, H / 4, W / 2, H / 2),
                  Rectangle(0, 0, 1, 1),
                  Color::White);
        sb_->End();

        // Centre pixel should be red (white texture × red-tint shader).
        // Corner pixel should be green (uncleared background).
        const Rectangle centReg(W / 2, H / 2, 1, 1);
        const Rectangle bgReg(1, 1, 1, 1);
        Color centPx(0, 0, 0, 0);
        Color bgPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);
        device.GetBackBufferData(&bgReg,   &bgPx,   0, 1);

        const bool centOk = (centPx.getRProperty() >= 200 && centPx.getGProperty() <= 50);
        const bool bgOk   = (bgPx.getGProperty()   >= 200 && bgPx.getRProperty()   <= 50);

        if (centOk && bgOk)
        {
            std::printf("[PASS] EasyGLShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] EasyGLShaderEffect: centre=(%d,%d,%d) bg=(%d,%d,%d)\n"
                        "       expected: centre=red (R>=200,G<=50), bg=green (G>=200,R<=50)\n",
                        centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty(),
                        bgPx.getRProperty(),   bgPx.getGProperty(),   bgPx.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    EasyGLShaderEffectTest game;
    game.Run();
    return game.getResult();
}
