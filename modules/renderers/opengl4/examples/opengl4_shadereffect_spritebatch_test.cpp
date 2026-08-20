// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-32: real SpriteBatch::SetCustomEffect integration for the OpenGL4 graphics
// renderer -- discovered as a separate, newly-found gap while scoping GL4-30 (custom
// ShaderEffect/CreateEffectRenderer), not attempted there since it's a separate feature (driving
// 2D sprite rendering, not 3D DrawIndexedPrimitives). OpenGL4SpriteBatchRenderer previously had no
// SetCustomEffect() override (inherited the default no-op), so a custom ShaderEffect passed to
// SpriteBatch::Begin() was silently ignored -- every sprite still rendered with the built-in
// sprite program regardless.
//
// Fixed by: OpenGL4SpriteBatchRenderer gained a `customEffect_` field + a real SetCustomEffect()
// override (flushes any already-batched sprites under the previous effect before switching,
// mirroring EasyGLSpriteBatchRenderer::SetCustomEffect's own guard). FlushBatch() now binds the
// SAME compiled program the custom ShaderEffect itself owns (Effect::GetEffectRendererPtr(),
// overridden by ShaderEffect) instead of the built-in sprite program when one is set, then calls
// Effect::Apply() and sets "projection" -- this codebase's established custom-2D-effect
// uniform-name convention (see easygl_shader_effect_test.cpp), distinct from the built-in
// program's own private "uProjection"/"uTexture" internal naming.
//
// Ports easygl_shader_effect_test.cpp's own scene, methodology, and expected values exactly
// (desktop GLSL 410 core translation only -- no ES precision qualifiers, otherwise identical): a
// custom shader that outputs only the red channel of the sampled texel, applied to a white 1x1
// texture via SpriteBatch, producing a red-tinted sprite over an unmodified green background.
//
// Check A -- the sprite's centre pixel reads red (white texel x red-tint shader): proves the
//   custom effect's own compiled program is genuinely bound and driving the draw, not the
//   built-in sprite program (which would leave the sprite plain white, not red-tinted).
// Check B -- a background corner pixel (outside the sprite's destination rectangle) reads
//   unmodified green: proves the custom effect only affects the sprite's own pixels, not a
//   full-screen side effect.
//
// Exit code 0 = both PASS, 1 = either FAIL.

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

namespace
{
    // Attribute layout matches OpenGL4SpriteBatchRenderer's own VAO exactly: location 0 = vec2
    // position, location 1 = vec2 texcoord, location 2 = vec4 colour (all float, unnormalized).
    const char* kVertSrc = R"GLSL(
#version 410 core
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
)GLSL";

    // Red-tint fragment: output only the red channel of the sampled texel. White texture (r=1)
    // -> red output (1,0,0,1). "texture1" defaults to sampler unit 0 (GLSL sampler uniforms
    // default to 0) -- no explicit uniform1i binding needed, matching the EasyGL precedent.
    const char* kFragSrc = R"GLSL(
#version 410 core
in vec2 TexCoord;
in vec4 Color;
out vec4 FragColor;
uniform sampler2D texture1;
void main()
{
    vec4 t = texture(texture1, TexCoord);
    FragColor = vec4(t.r, 0.0, 0.0, t.a);
}
)GLSL";
}

class OpenGL4ShaderEffectSpriteBatchTest : public Game
{
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<Texture2D> tex_;
    bool done_  = false;
    int result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);

        const std::vector<uint8_t> white = { 255, 255, 255, 255 };
        tex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, white));
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
            std::printf("[FAIL] OpenGL4ShaderEffectSpriteBatch: GLSL compile failed\n");
            Exit();
            return;
        }

        device.Clear(Color(0, 255, 0, 255)); // green background
        device.SetDepthTestEnabled(false);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                   nullptr, nullptr, nullptr, &fx);
        sb_->Draw(*tex_,
                  Rectangle(W / 4, H / 4, W / 2, H / 2),
                  Rectangle(0, 0, 1, 1),
                  Color::White);
        sb_->End();

        const Rectangle centReg(W / 2, H / 2, 1, 1);
        const Rectangle bgReg(1, 1, 1, 1);
        Color centPx(0, 0, 0, 0);
        Color bgPx(0, 0, 0, 0);
        device.GetBackBufferData(&centReg, &centPx, 0, 1);
        device.GetBackBufferData(&bgReg,   &bgPx,   0, 1);

        const bool centOk = (centPx.getRProperty() >= 200 && centPx.getGProperty() <= 50);
        const bool bgOk   = (bgPx.getGProperty()   >= 200 && bgPx.getRProperty()   <= 50);

        std::printf("[%s] Check A: sprite centre reads red-tinted (%d,%d,%d) expected R>=200,G<=50\n",
                    centOk ? "PASS" : "FAIL",
                    centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());
        std::printf("[%s] Check B: background corner unmodified green (%d,%d,%d) expected G>=200,R<=50\n",
                    bgOk ? "PASS" : "FAIL",
                    bgPx.getRProperty(), bgPx.getGProperty(), bgPx.getBProperty());

        result_ = (centOk && bgOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    OpenGL4ShaderEffectSpriteBatchTest game;
    game.Run();
    return game.getResult();
}
