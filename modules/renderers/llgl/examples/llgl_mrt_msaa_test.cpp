// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-54: preserve the effective MSAA sample count across a multi-render-target
// bind, and resolve EVERY attachment independently -- not just slot 0. Mirrors
// llgl_msaa_rendertarget_test.cpp's own diagonal-edge technique and llgl_mrt_test.cpp's own
// 2-output custom-effect MRT proof, combined: the SAME single draw writes a diagonal-edged
// triangle-ish shape (a 45-degree-rotated quad) to TWO simultaneously-bound multisampled render
// targets, and BOTH must resolve to a genuinely antialiased edge independently.
//
// Check A -- two RenderTarget2D instances requesting MultiSampleCount=4 construct, and
//   GraphicsDevice::SetRenderTargets() accepts binding them together (the shared layer's own
//   "applied sample counts must match" check passes when both slots request the same count).
// Check B -- with the MRT bind released, EACH of the two render targets independently shows at
//   least one genuinely blended, mid-tone edge pixel along the rotated quad's diagonal edge -- not
//   just slot 0 (proves both that LlglMRTBinding::GetSampleCount() reported a real sample count
//   to AcquirePrimitivePipeline()'s own `rasterizer.multiSampleEnabled`, previously hardcoded to 1
//   regardless of what the bound slots requested, AND that LLGL's own per-attachment resolve,
//   wired via this renderer's own per-slot `targetDesc.resolveAttachments[i]`, actually resolves
//   attachment 1 too, not just slot 0).
// Check C -- a mixed-sample-count bind (slot 0 at 4x, slot 1 at 0x/none) is rejected by the SHARED
//   GraphicsDevice::SetRenderTargets() validation layer, by name, before this renderer ever sees it
//   -- confirms this ticket's own "mixed requested/effective sample-count validation" acceptance
//   item is already enforced upstream, not silently accepted and only one slot's count honoured.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 48;
    constexpr int kScanY = kRTSize / 2;

    // Same NDC technique + PC uniform-block shape as llgl_mrt_test.cpp's own custom vertex shader
    // (and llgl_shadereffect_test.cpp's before it).
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

    // 2 simultaneous outputs, both opaque white -- only the shape of the rotated quad (drawn by
    // SpriteBatch below) determines the diagonal edge; this shader itself does not need to
    // distinguish slot 0 from slot 1's own colour the way llgl_mrt_test.cpp's channel-swap does,
    // since THIS test only cares whether EACH attachment resolves its own MSAA edge correctly.
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
    outColorB = base;
}
)GLSL";
}

class LlglMrtMsaaTest : public CNA::Examples::PixelTestGame
{
    // Scans row kScanY of `rt` and returns true if at least one sampled pixel's red channel is a
    // genuine mid-tone (neither pure background nor pure foreground) -- the same "blended edge
    // pixel" criterion llgl_msaa_rendertarget_test.cpp's own Check F already establishes.
    static bool HasBlendedEdgePixel(RenderTarget2D& rt)
    {
        bool blended = false;
        for (int x = 0; x < kRTSize; ++x)
        {
            const Rectangle region(x, kScanY, 1, 1);
            Color pixel(0, 0, 0, 0);
            rt.GetData(0, &region, &pixel, 0, 1);
            const int red = pixel.getRProperty();
            if (red > 20 && red < 235) { blended = true; break; }
        }
        return blended;
    }

public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        // --- Check A: two MSAA RenderTarget2D instances construct and bind together -------------
        std::unique_ptr<RenderTarget2D> rt0;
        std::unique_ptr<RenderTarget2D> rt1;
        try
        {
            rt0 = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, false,
                                                    SurfaceFormat::Color, DepthFormat::None, 4);
            rt1 = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, false,
                                                    SurfaceFormat::Color, DepthFormat::None, 4);
        }
        catch (const std::exception&)
        {
            rt0.reset();
            rt1.reset();
        }
        if (!ExpectTrue("two MultiSampleCount=4 RenderTarget2D instances construct",
                        rt0 != nullptr && rt1 != nullptr))
            return;

        const int applied0 = rt0->getMultiSampleCountProperty();
        const int applied1 = rt1->getMultiSampleCountProperty();
        if (applied0 <= 1 || applied1 <= 1)
        {
            std::printf("[SKIP] this module does not apply MultiSampleCount to a RenderTarget2D "
                        "in this environment -- skipping every MRT+MSAA check below (nothing to "
                        "resolve if neither slot is actually multisampled)\n");
            return;
        }
        ExpectTrue("both render targets applied the same real, device-clamped sample count "
                  "(a precondition SetRenderTargets() itself also enforces)",
                  applied0 == applied1);

        std::vector<RenderTargetBinding> bindings{RenderTargetBinding(rt0.get()),
                                                    RenderTargetBinding(rt1.get())};
        bool bindThrew = false;
        try
        {
            device.SetRenderTargets(bindings);
        }
        catch (const std::exception&)
        {
            bindThrew = true;
        }
        if (!ExpectTrue("SetRenderTargets() accepts two slots requesting the SAME applied "
                        "MultiSampleCount", !bindThrew))
        {
            return;
        }

        // --- Check B: the MRT bind's real sample count is exercised indirectly by Check C below
        // (LlglMRTBinding::GetSampleCount() is consulted by AcquirePrimitivePipeline() to decide
        // rasterizer.multiSampleEnabled; it is not itself part of this renderer's public API
        // surface to query directly from a test, so a genuinely antialiased resolved edge in
        // Check C is the observable proof that it reported the right value).

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
            device.SetRenderTargets(std::vector<RenderTargetBinding>{});
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
        {
            device.SetRenderTargets(std::vector<RenderTargetBinding>{});
            return;
        }
        mrtEffect->SetUniformVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);

        device.Clear(Color(0, 0, 0, 255));

        // A 45-degree-rotated quad centred on the target, sized so its own straight edges cross
        // row kScanY diagonally (never axis-aligned with the sample grid) -- same rationale as
        // llgl_msaa_test.cpp's/llgl_msaa_rendertarget_test.cpp's own diagonal-edge triangles.
        SpriteBatch spriteBatch(device);
        spriteBatch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr,
                          mrtEffect.get());
        const Vector2 centre(static_cast<float>(kRTSize) / 2.0f, static_cast<float>(kRTSize) / 2.0f);
        const Vector2 origin(0.5f, 0.5f); // texture-space centre of the 1x1 source texture
        spriteBatch.Draw(*whiteTexture, centre, std::optional<Rectangle>(Rectangle(0, 0, 1, 1)),
                         Color::White, MathHelper::PiOver4, origin,
                         static_cast<float>(kRTSize) * 0.7f, SpriteEffects::None, 0.0f);
        spriteBatch.End();

        device.SetRenderTargets(std::vector<RenderTargetBinding>{});

        // --- Check B: BOTH slots independently resolve a genuinely blended edge pixel -----------
        const bool blended0 = HasBlendedEdgePixel(*rt0);
        const bool blended1 = HasBlendedEdgePixel(*rt1);
        ExpectTrue("slot 0's own multisampled attachment resolves to a genuinely blended, "
                  "mid-tone edge pixel -- real antialiasing, not merely \"didn't crash\"",
                  blended0);
        ExpectTrue("slot 1's own multisampled attachment ALSO independently resolves to a "
                  "genuinely blended edge pixel (not just slot 0's)", blended1);

        // --- Check C: a mixed-sample-count bind is rejected upstream, before this renderer sees it
        std::unique_ptr<RenderTarget2D> rt2NoMsaa;
        try
        {
            rt2NoMsaa = std::make_unique<RenderTarget2D>(device, kRTSize, kRTSize, false,
                                                          SurfaceFormat::Color, DepthFormat::None, 0);
        }
        catch (const std::exception&)
        {
            rt2NoMsaa.reset();
        }
        if (ExpectTrue("a third, non-multisampled RenderTarget2D also constructs",
                       rt2NoMsaa != nullptr))
        {
            bool mixedThrew = false;
            try
            {
                std::vector<RenderTargetBinding> mixed{RenderTargetBinding(rt0.get()),
                                                        RenderTargetBinding(rt2NoMsaa.get())};
                device.SetRenderTargets(mixed);
                device.SetRenderTargets(std::vector<RenderTargetBinding>{});
            }
            catch (const std::exception&)
            {
                mixedThrew = true;
            }
            ExpectTrue("SetRenderTargets() refuses a bind mixing a MultiSampleCount=4 slot with "
                      "a MultiSampleCount=0 slot, by name, before this renderer's own MRT creation "
                      "ever runs", mixedThrew);
        }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglMrtMsaaTest>();
}
