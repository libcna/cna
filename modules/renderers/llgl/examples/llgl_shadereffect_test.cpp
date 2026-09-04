// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-27: custom ShaderEffect proof for the LLGL graphics renderer -- a REAL runtime
// GLSL->SPIR-V compile via libshaderc when the Vulkan module is loaded (LLGL's OpenGL module
// accepts the same GLSL text directly), not just a "didn't throw" smoke test.
//
// Adapted from examples/sdlgpu_shadereffect_test.cpp, which solved the identical "custom effect
// needs a runtime shader compile" problem for SDL_GPU. The uniform layout matches the native
// Vulkan renderer's own VulkanEffectRenderer push-constant contract (vpSize/uMatrix/uColor/uFloat0),
// just uploaded to a real constant buffer instead of a push constant -- see LlglEffectRenderer's
// own doc comment.
//
// Check A -- IsEffectValid() is true after CompileProgram() -- proves the runtime compile (GLSL
//   direct, or through libshaderc) and pipeline-layout-compatible shader creation both succeeded.
// Check B -- drawing a white 1x1 texture through the custom effect with SetUniformVec4(tint) reads
//   back the EXACT tint colour (white * tint = tint) -- proves the custom shader's own uniform is
//   genuinely read and applied, not the stock sprite shader's vertex-colour path.
// Check C -- the SAME draw with no custom effect (stock sprite shader, vertex colour = White)
//   reads back plain white, NOT the tint colour -- the discriminator that Check B's result came
//   from the custom shader actually being bound, not the pipeline/uniform being silently ignored.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 64;

    // Pixel-space -> NDC, matching this renderer's stock sprite2d.vert.glsl technique. pc.uColor
    // is the sole discriminator between Check B and Check C -- the vertex colour is deliberately
    // never referenced, so a tinted result can only come from THIS shader's own uniform.
    const char* kVertSrc = R"GLSL(
#version 450 core
layout(std140, binding = 1) uniform PC {
    vec4 vpSize_pad;
    mat4 uMatrix;
    vec4 uColor;
    vec4 uFloats;
} pc;
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 color;
layout(location = 0) out vec2 vTexCoord;
void main() {
    vec2 ndc = (position / pc.vpSize_pad.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    vTexCoord = texCoord;
}
)GLSL";

    const char* kFragSrc = R"GLSL(
#version 450 core
layout(binding = 2) uniform sampler2D colorMap;
layout(std140, binding = 1) uniform PC {
    vec4 vpSize_pad;
    mat4 uMatrix;
    vec4 uColor;
    vec4 uFloats;
} pc;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = texture(colorMap, vTexCoord) * pc.uColor;
}
)GLSL";
}

class LlglShaderEffectTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<RenderTarget2D> renderTarget;
        try
        {
            renderTarget = std::make_unique<RenderTarget2D>(
                device, kRTSize, kRTSize, false, SurfaceFormat::Color, DepthFormat::None);
        }
        catch (const std::exception&)
        {
            renderTarget.reset();
        }
        if (!ExpectTrue("RenderTarget2D construction succeeds", renderTarget != nullptr))
            return;

        const std::vector<std::uint8_t> whitePixels = {255, 255, 255, 255};
        std::unique_ptr<Texture2D> whiteTexture;
        try
        {
            whiteTexture = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(device, 1, 1, whitePixels));
        }
        catch (const std::exception&)
        {
            whiteTexture.reset();
        }
        if (!ExpectTrue("Texture2D::CreateFromPixels() succeeds for the source texture",
                        whiteTexture != nullptr))
        {
            return;
        }

        std::unique_ptr<ShaderEffect> effect;
        try
        {
            effect = std::make_unique<ShaderEffect>(device, kVertSrc, kFragSrc);
        }
        catch (const std::exception&)
        {
            effect.reset();
        }
        if (!ExpectTrue("ShaderEffect construction does not throw", effect != nullptr))
            return;

        ExpectTrue("IsEffectValid() is true after CompileProgram()", effect->IsEffectValid());
        if (!effect->IsEffectValid())
            return;

        effect->SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f); // red tint

        const auto renderThroughEffect = [&](Effect* customEffect) {
            device.SetRenderTarget(renderTarget.get());
            device.Clear(Color(0, 0, 0, 255));

            SpriteBatch spriteBatch(device);
            spriteBatch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr,
                              customEffect);
            spriteBatch.Draw(*whiteTexture, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1),
                             Color::White);
            spriteBatch.End();

            device.SetRenderTarget(nullptr);

            Color centre(0, 0, 0, 0);
            const Rectangle centreRegion(kRTSize / 2, kRTSize / 2, 1, 1);
            renderTarget->GetData(0, &centreRegion, &centre, 0, 1);
            return centre;
        };

        const Color tinted = renderThroughEffect(effect.get());
        ExpectTrue("custom effect tints white through the texture by uColor (red)",
                   tinted == Color(255, 0, 0, 255));

        const Color stock = renderThroughEffect(nullptr);
        ExpectTrue("no custom effect draws plain white (stock sprite shader, control case)",
                   stock == Color(255, 255, 255, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglShaderEffectTest>();
}
