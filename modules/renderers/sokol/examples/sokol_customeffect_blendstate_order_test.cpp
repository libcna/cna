// SPDX-License-Identifier: MS-PL
// plans/plan_sokol.md SOKOL-41: SpriteBatch's raw-GL custom-effect draw path
// (SokolSpriteBatchRenderer::DrawSpriteRunEXT's custom branch) must apply the device's real
// BlendState, not silently keep whatever GL_BLEND state a previous draw happened to leave behind.
//
// Before this fix, the custom branch bound a program/texture/VAO and drew with glDrawElements
// with no glEnable/glDisable(GL_BLEND), glBlendFuncSeparate, glBlendEquationSeparate or
// glBlendColor call of its own -- it inherited whatever the GL context's blend state happened to
// be from the last sg_apply_pipeline() call (a stock sprite/3D draw) or, on the very first draw of
// a session, OpenGL's own spec default (blend disabled).
//
// A constant-colour fragment shader (ignores texture and vertex colour: always outputs
// (255, 0, 0) at 50% alpha) makes the two possible outcomes -- "blended" vs "fully overwritten" --
// unambiguous regardless of what either GL_BLEND state predicted.
//
// Uses BlendState::NonPremultiplied (srcFactor=SourceAlpha, dstFactor=InverseSourceAlpha), not
// BlendState::AlphaBlend (srcFactor=One, dstFactor=InverseSourceAlpha, i.e. premultiplied) --
// the tint/shader colours below are plain straight alpha, not premultiplied, and AlphaBlend
// against straight-alpha input produces a fully-opaque-looking result instead of a genuine 50/50
// mix (the same "first draft used the premultiplied factors against straight-alpha data" mistake
// plans/plan_sokol.md SOKOL-21's own history note records for a different test).
//
// Check A -- sanity: a STOCK NonPremultiplied sprite over black genuinely blends (not a
//   hardcoded assumption -- establishes the harness itself is exercising real blending).
// Check B (KEY, stock -> custom direction) -- immediately after that stock NonPremultiplied draw
//   (which leaves GL_BLEND enabled with alpha factors), a CUSTOM-effect draw under
//   BlendState::Opaque must fully overwrite with the shader's raw output colour, not blend.
// Check C (custom -> stock direction) -- immediately after a custom-effect Opaque draw (leaves
//   GL_BLEND disabled), a STOCK NonPremultiplied draw over it still genuinely blends.
// Check D (KEY, the other blend direction) -- a CUSTOM-effect draw under
//   BlendState::NonPremultiplied over a fresh black background genuinely blends (not fully
//   overwritten either) -- proves the custom path can correctly ENABLE blending too, not just
//   always leave it however it found it.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

    const char* kVertSrc = R"(#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); }
)";

    // Ignores the sampled texture and vertex colour entirely -- always red at 50% alpha, so the
    // read-back pixel unambiguously reveals whether blending was applied or not.
    const char* kFragSrc = R"(#version 410 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 0.0, 0.0, 0.5); }
)";

    Color ReadCenter(GraphicsDevice& dev)
    {
        const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);
        return got;
    }

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
}

class CustomEffectBlendStateOrderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    Texture2D whiteTex_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

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

    void DrawStockNonPremultipliedWhite(GraphicsDevice& dev)
    {
        // Alpha=128 (~0.5), full-screen: over black this blends to ~(128,128,128); over an
        // existing colour C it blends to ~(C+255)/2 per channel where the white texel contributes.
        sb_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied);
        sb_->Draw(whiteTex_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1),
                  Color(255, 255, 255, 128));
        sb_->End();
    }

    void DrawCustomEffect(GraphicsDevice& dev, ShaderEffect& fx, const BlendState& blend)
    {
        sb_->Begin(SpriteSortMode::Deferred, blend, nullptr, nullptr, nullptr, &fx);
        sb_->Draw(whiteTex_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1), Color::White);
        sb_->End();
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
            std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        ShaderEffect fx(dev, kVertSrc, kFragSrc);
        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] GLSL compile failed\n");
            Exit();
            return;
        }

        // --- Check A: sanity, stock NonPremultiplied genuinely blends over black -----------
        dev.Clear(Color(0, 0, 0, 255));
        DrawStockNonPremultipliedWhite(dev);
        const Color gotA = ReadCenter(dev);
        check(CloseTo(gotA.getRProperty(), 128, 20) && CloseTo(gotA.getGProperty(), 128, 20),
              "A: stock NonPremultiplied sanity (white@0.5 over black ~= grey)",
              gotA, "~(128,128,128)");

        // --- Check B (KEY): custom Opaque right after stock blend must NOT blend -----------
        // GL_BLEND is left enabled (alpha factors) by check A's stock draw -- a pre-fix custom
        // draw would inherit that and blend its (255,0,0,0.5) output with the grey background
        // instead of fully overwriting it.
        DrawCustomEffect(dev, fx, BlendState::Opaque);
        const Color gotB = ReadCenter(dev);
        check(gotB.getRProperty() >= 245 && gotB.getGProperty() <= 10 && gotB.getBProperty() <= 10,
              "B: custom Opaque after stock blend fully overwrites (stock->custom)",
              gotB, "(255,0,0)");

        // --- Check C: stock NonPremultiplied right after custom Opaque still blends --------
        // GL_BLEND is left disabled by check B's custom draw.
        DrawStockNonPremultipliedWhite(dev);
        const Color gotC = ReadCenter(dev);
        check(gotC.getRProperty() >= 245 && CloseTo(gotC.getGProperty(), 128, 20)
                  && CloseTo(gotC.getBProperty(), 128, 20),
              "C: stock blend after custom Opaque still blends (custom->stock)",
              gotC, "~(255,128,128)");

        // --- Check D (KEY): custom NonPremultiplied over black must genuinely blend --------
        dev.Clear(Color(0, 0, 0, 255));
        DrawCustomEffect(dev, fx, BlendState::NonPremultiplied);
        const Color gotD = ReadCenter(dev);
        check(CloseTo(gotD.getRProperty(), 128, 20) && gotD.getGProperty() <= 10,
              "D: custom NonPremultiplied over black genuinely blends, not fully overwritten",
              gotD, "~(128,0,0)");

        std::printf("\nResult: %d/%d PASS\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    CustomEffectBlendStateOrderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    CustomEffectBlendStateOrderTest game;
    game.Run();
    return game.getResult();
}
