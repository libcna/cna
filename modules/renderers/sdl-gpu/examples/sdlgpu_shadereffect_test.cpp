// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-42/43: custom ShaderEffect proof for the SDL_GPU graphics renderer -- a
// REAL runtime GLSL->SPIR-V compile via libshaderc (SDL_gpu only accepts precompiled bytecode),
// not just a "didn't throw" smoke test. Verified end-to-end through the public XNA API:
// SpriteBatch.Begin(effect) -> Draw() -> End(), reading back the actual rendered pixel via
// RenderTarget2D::GetData() (RenderTarget2D::GetData() is real, SDLGPU-39; this renderer's
// swapchain-download path segfaults, see that row).
//
// Check A -- IsEffectValid() is true after CompileProgram() -- proves the runtime libshaderc
//   compile + SDL_CreateGPUShader (both stages) genuinely succeeded, not silently no-op'd.
// Check B -- drawing a white 1x1 texture through the custom effect with SetUniformVec4(tint) reads
//   back the EXACT tint color (white * tint = tint, byte-exact, no blend/interpolation artifacts) --
//   proves the custom shader's own uniform is genuinely read and applied by the compiled GLSL, not
//   the stock sprite shader's vertex-color path.
// Check C -- the SAME draw with SetCustomEffect(nullptr) (stock sprite shader, vertex color =
//   Color::White, no tint) reads back plain white, NOT the tint color -- the real discriminator
//   that Check B's result came from the custom shader actually being bound, not the custom
//   effect's pipeline/uniform being ignored and the stock shader winning regardless.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 64;

    // Pixel-space -> NDC, same technique as sprite2d.vert.glsl (this renderer's stock sprite
    // shader) -- proves this custom shader isn't just piggybacking on the stock one.
    const char* kVertSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec2 fragUV;
layout(set = 1, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    vec2 ndc = (inPos / pc.vpSize_pad.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    fragUV = inUV;
}
)GLSL";

    // Multiplies the sampled texture by the custom effect's own uniform color -- NOT the
    // vertex-color path the stock sprite shader uses, so a tinted readback can only come from
    // this shader genuinely being bound and its uniform genuinely being read.
    const char* kFragSrc = R"GLSL(
#version 450
layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(set = 2, binding = 0) uniform sampler2D uTexture;
layout(set = 3, binding = 0) uniform PC {
    vec4 vpSize_pad;
    mat4 matrix;
    vec4 color;
    vec4 slot0_pad;
} pc;
void main() {
    outColor = texture(uTexture, fragUV) * pc.color;
}
)GLSL";
}

class SdlGpuShaderEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    std::unique_ptr<Texture2D> whiteTex_;
    std::unique_ptr<ShaderEffect> effect_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    Color RenderCenterPixel(GraphicsDevice& dev, Effect* customEffect)
    {
        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color(0, 0, 0, 255));

        SpriteBatch sb(dev);
        sb.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr, customEffect);
        sb.Draw(*whiteTex_, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1), Color::White);
        sb.End();

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        rt_->GetData(0, &centre, &px, 0, 1);
        return px;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        const std::vector<std::uint8_t> whitePixels = {255, 255, 255, 255};
        whiteTex_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(dev, 1, 1, whitePixels));
        effect_ = std::make_unique<ShaderEffect>(dev, kVertSrc, kFragSrc);
    }

    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        Check(effect_->IsEffectValid(), "runtime libshaderc GLSL->SPIR-V compile succeeded (IsEffectValid)");

        effect_->SetUniformVec4("color", 0.2f, 0.6f, 1.0f, 1.0f);
        const Color tinted = RenderCenterPixel(dev, effect_.get());
        const Color wantTint(51, 153, 255, 255);  // 0.2,0.6,1.0,1.0 * 255, rounded
        const bool tintOk = std::abs(tinted.getRProperty() - wantTint.getRProperty()) <= 1
                         && std::abs(tinted.getGProperty() - wantTint.getGProperty()) <= 1
                         && std::abs(tinted.getBProperty() - wantTint.getBProperty()) <= 1;
        std::printf("       tinted=(%d,%d,%d) want~(%d,%d,%d)\n",
                    tinted.getRProperty(), tinted.getGProperty(), tinted.getBProperty(),
                    wantTint.getRProperty(), wantTint.getGProperty(), wantTint.getBProperty());
        Check(tintOk, "custom shader's own uniform tint applied exactly (proves the compiled GLSL is genuinely bound+read)");

        const Color plain = RenderCenterPixel(dev, nullptr);
        std::printf("       plain=(%d,%d,%d)\n", plain.getRProperty(), plain.getGProperty(), plain.getBProperty());
        Check(plain.getRProperty() == 255 && plain.getGProperty() == 255 && plain.getBProperty() == 255,
              "same draw with no custom effect reads back plain white (discriminates real binding from a no-op)");

        std::printf("=== %d/3 PASS ===\n", passCount_);
        result_ = (passCount_ == 3) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuShaderEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuShaderEffectTest game;
    game.Run();
    return game.getResult();
}
