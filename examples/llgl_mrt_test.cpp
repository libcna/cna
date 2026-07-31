// SPDX-License-Identifier: MS-PL
// plan_llgl.md LLGL-26 follow-up: Multiple Render Targets (MRT) proof for the LLGL graphics
// backend, scoped to this backend's own first cut -- see LlglMRTBinding's doc comment in
// LlglGraphicsBackend.hpp for the full boundary. RenderTarget2D slots only (no RenderTargetCube
// faces), and 3D colour-only draws are refused while an MRT set is bound: real XNA MRT is
// meaningfully useful only through a custom multi-output ShaderEffect drawn via SpriteBatch, which
// is the one path this first cut supports.
//
// Check A -- SetRenderTargets({rt0,rt1}) + one SpriteBatch draw through a real 2-output custom
//   ShaderEffect writes a DIFFERENT value to each target from the SAME single draw call -- the
//   actual MRT discriminator, not just "didn't throw".
// Check B -- SetRenderTargets(nullptr,0) restores the back buffer, which keeps its own clear
//   colour untouched by the MRT draw (target isolation, matching llgl_rendertargetcube_test.cpp's
//   own back-buffer-independence check).
// Check C -- a 3D colour-only draw (DrawUserPrimitives) while the MRT set is still bound throws,
//   by name, matching QueuePrimitives' own documented scope boundary.
// Check D -- after SetRenderTargets(nullptr,0), an ordinary single-target SpriteBatch draw into
//   the back buffer still renders correctly -- proves releasing an MRT bind does not leave the
//   pipeline cache/render-pass selection in a state that corrupts an unrelated later draw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 32;

    // Same NDC technique + PC uniform-block shape as llgl_shadereffect_test.cpp's own custom
    // vertex shader.
    const char* kMrtVertSrc = R"GLSL(
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

    // Real 2-output fragment shader: outColorA is texture*tint (the same value
    // llgl_shadereffect_test.cpp already proves is genuinely read from pc.uColor); outColorB is a
    // channel-swapped derivative of the SAME value, computed by the SAME fragment invocation --
    // objectively different from outColorA, and only reaches its own distinct render target if
    // this backend's render pass genuinely has 2 simultaneous colour attachments.
    const char* kMrtFragSrc = R"GLSL(
#version 450 core
layout(binding = 2) uniform sampler2D colorMap;
layout(std140, binding = 1) uniform PC {
    vec4 vpSize_pad;
    mat4 uMatrix;
    vec4 uColor;
    vec4 uFloats;
} pc;
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 outColorA;
layout(location = 1) out vec4 outColorB;
void main() {
    vec4 base = texture(colorMap, vTexCoord) * pc.uColor;
    outColorA = base;
    outColorB = vec4(base.g, base.b, base.r, base.a);
}
)GLSL";
}

class LlglMrtTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<RenderTarget2D> rt0;
        std::unique_ptr<RenderTarget2D> rt1;
        try
        {
            rt0 = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, false,
                                                    SurfaceFormat::Color, DepthFormat::None);
            rt1 = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, false,
                                                    SurfaceFormat::Color, DepthFormat::None);
        }
        catch (const std::exception&)
        {
            rt0.reset();
            rt1.reset();
        }
        if (!ExpectTrue("two RenderTarget2D instances construct", rt0 != nullptr && rt1 != nullptr))
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

        std::unique_ptr<ShaderEffect> mrtEffect;
        try
        {
            mrtEffect = std::make_unique<ShaderEffect>(device, kMrtVertSrc, kMrtFragSrc);
        }
        catch (const std::exception&)
        {
            mrtEffect.reset();
        }
        if (!ExpectTrue("2-output ShaderEffect construction does not throw", mrtEffect != nullptr))
            return;
        if (!ExpectTrue("IsEffectValid() is true after CompileProgram()", mrtEffect->IsEffectValid()))
            return;

        mrtEffect->SetUniformVec4("uColor", 0.2f, 0.4f, 0.8f, 1.0f);

        // --- Check A: one draw, two distinct simultaneous outputs ------------------------------
        device.Clear(Color(30, 30, 30, 255));

        std::vector<RenderTargetBinding> bindings{RenderTargetBinding(rt0.get()),
                                                    RenderTargetBinding(rt1.get())};
        device.SetRenderTargets(bindings);
        device.Clear(Color(0, 0, 0, 0));

        SpriteBatch spriteBatch(device);
        spriteBatch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr,
                          mrtEffect.get());
        spriteBatch.Draw(*whiteTexture, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1),
                         Color::White);
        spriteBatch.End();

        // --- Check C: a 3D colour-only draw refuses to run while MRT is bound ------------------
        bool threw = false;
        try
        {
            const VertexPositionColor tri[3] = {
                { Vector3(-1.0f, -1.0f, 0.0f), Color::Green },
                { Vector3(0.0f, 1.0f, 0.0f), Color::Green },
                { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
            };
            BasicEffect fx(device);
            fx.VertexColorEnabled = true;
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
            fx.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        ExpectTrue("3D colour-only draw throws while multiple render targets are bound", threw);

        device.SetRenderTargets(std::vector<RenderTargetBinding>{});

        ExpectPixel("back buffer keeps its own clear colour after the MRT draw",
                    Rectangle(10, 10, 1, 1), Color(30, 30, 30, 255));

        // white * (0.2,0.4,0.8,1.0) = (51,102,204,255); outColorB = (g,b,r,a) of that.
        const Rectangle centre(kRTSize / 2, kRTSize / 2, 1, 1);
        Color got0(0, 0, 0, 0);
        Color got1(0, 0, 0, 0);
        rt0->GetData(0, &centre, &got0, 0, 1);
        rt1->GetData(0, &centre, &got1, 0, 1);
        ExpectTrue("slot 0 receives the custom effect's first fragment output (outColorA)",
                   got0 == Color(51, 102, 204, 255));
        ExpectTrue("slot 1 receives the SAME draw's second, distinct fragment output (outColorB)",
                   got1 == Color(102, 204, 51, 255));

        // --- Check D: an ordinary single-target draw still works after the MRT bind ends ------
        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        SpriteBatch stockBatch(device);
        stockBatch.Begin();
        stockBatch.Draw(*whiteTexture, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, 1, 1),
                        Color(255, 0, 0, 255));
        stockBatch.End();
        ExpectPixel("an ordinary single-target SpriteBatch draw still renders correctly after "
                    "the MRT bind ends", Rectangle(kRTSize / 2, kRTSize / 2, 1, 1),
                    Color(255, 0, 0, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglMrtTest>();
}
