// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-45/IGL-55: SpriteBatch.Begin(effect) -- a custom ShaderEffect driving the 2D
// sprite pipeline, not the general 3D draw path `igl_shadereffect_texture3d_test.cpp` already
// covers. This exercises a genuinely different call site: `IglRenderer::DrawSpriteBatchEXT()`
// (`IglDraw.cpp`), which builds its `PipelineKey`/`AcquirePipeline()` call and its
// `ApplyCustomEffectUniforms()` call independently of the 3D path's own -- both were touched by
// the same IGL-42..45 texture-binding fix (see `IglPipelineCache.cpp`'s `AcquirePipeline`), but
// only the 3D path had a test proving the fix actually reaches it; this closes that gap for 2D.
//
// The sprite's own texture (passed to `SpriteBatch::Draw`) is bound by the SHARED
// `BindEffectResources()` path (unconditional on every draw, custom or built-in) to texture unit
// 0 -- not by the custom effect's own `SetTexture()` (SpriteBatch owns which texture a sprite
// draws with; the custom effect only supplies the shader). For that unit-0 bind to resolve to a
// real GL sampler location under a custom shader, the effect still needs to declare its sampler's
// texture-unit assignment the established way: `SetUniformInt("SpriteTexture", 0)`, matching
// `igl_shadereffect_texture3d_test.cpp`'s own `SetUniformInt("VolumeSampler", 0)` convention.
//
// Scene: a white 8x8 sprite, tinted green by the custom shader's own `tint` uniform (not XNA's
// per-sprite colour parameter -- that is deliberately left white here, so a passing result proves
// the CUSTOM shader's tint reached the fragment stage, not the ordinary vertex-colour path every
// other 2D test already covers). A second, all-red decoy tint is set and then overwritten with
// green before the draw, ruling out a stale-uniform false positive the same way earlier tests in
// this family use a decoy texture.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kSpriteSize = 8;

    const char* kVertSrc = R"(#version 410 core
in vec3 aPosition;
in vec4 aColor;
in vec2 aTexCoord0;
out vec4 vColor;
out vec2 vTexCoord0;
uniform vec2 screenSize;
void main() {
    vec2 ndc = vec2(aPosition.x / screenSize.x * 2.0 - 1.0,
                    1.0 - aPosition.y / screenSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
    vTexCoord0 = aTexCoord0;
}
)";

    const char* kFragSrc = R"(#version 410 core
in vec4 vColor;
in vec2 vTexCoord0;
out vec4 FragColor;
uniform sampler2D SpriteTexture;
uniform vec4 tint;
void main() {
    FragColor = texture(SpriteTexture, vTexCoord0) * vColor * tint;
}
)";

    // The same effect for SPIR-V (plan_igl.md IGL-43). Three differences, each forced rather than
    // stylistic: every user input and output carries an explicit location (SPIR-V requires it, and
    // desktop GLSL 4.10 does not); the parameters are members of a std140 block at the binding CNA
    // reserves for a custom effect, because loose uniforms do not exist here; and the sampler is
    // bound by its own layout qualifier rather than by an int uniform naming its unit.
    const char* kVulkanVertSrc = R"(#version 460
layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec2 aTexCoord0;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTexCoord0;
layout(set = 1, binding = 2, std140) uniform CnaCustom {
    vec2 screenSize;
    vec2 cnaPad0;
    vec4 tint;
};
void main() {
    vec2 ndc = vec2(aPosition.x / screenSize.x * 2.0 - 1.0,
                    1.0 - aPosition.y / screenSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
    vTexCoord0 = aTexCoord0;
}
)";

    const char* kVulkanFragSrc = R"(#version 460
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord0;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D SpriteTexture;
layout(set = 1, binding = 2, std140) uniform CnaCustom {
    vec2 screenSize;
    vec2 cnaPad0;
    vec4 tint;
};
void main() {
    FragColor = texture(SpriteTexture, vTexCoord0) * vColor * tint;
}
)";

    /// True when this process resolved IGL's Vulkan backend, asked through the supported query.
    [[nodiscard]] bool IsVulkanDialect(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        return device.GetShaderDialectEXT() ==
               CNA::Internal::Renderers::ShaderDialectEXT::GlslVulkan;
    }
}

class IglSpriteBatchShaderEffectTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        const bool vulkan = IsVulkanDialect(device);

        ShaderEffect effect(device, vulkan ? kVulkanVertSrc : kVertSrc,
                            vulkan ? kVulkanFragSrc : kFragSrc);
        if (!ExpectTrue("the custom sprite ShaderEffect compiled", effect.IsEffectValid()))
            return;

        // Declared unconditionally: a backend whose uniforms are loose ignores it. The std140
        // offsets are the block's own -- a vec2 followed by a vec4 pads to 16, which is why the
        // shader carries an explicit cnaPad0 rather than leaving the rule implicit.
        static const char* const kNames[] = {"screenSize", "tint"};
        static const int kOffsets[] = {0, 16};
        effect.DeclareUniformBlockEXT(/*blockSizeBytes=*/32, kNames, kOffsets, 2);

        effect.SetUniformVec2("screenSize", static_cast<float>(kSize), static_cast<float>(kSize));
        if (!vulkan)
        {
            // An int uniform naming a texture unit is an OpenGL idea. On a SPIR-V target the
            // sampler is bound by its own layout(set, binding) qualifier, so setting it here would
            // be a parameter the shader has no member for -- which the renderer rejects by name
            // rather than dropping silently.
            effect.SetUniformInt("SpriteTexture", 0);
        }
        // A decoy tint, overwritten below -- rules out a stale/default uniform passing this test
        // for the wrong reason.
        effect.SetUniformVec4("tint", 1.0f, 0.0f, 0.0f, 1.0f);
        effect.SetUniformVec4("tint", 0.0f, 1.0f, 0.0f, 1.0f);

        auto white = std::make_unique<Texture2D>(device, kSpriteSize, kSpriteSize);
        const std::vector<Color> pixels(static_cast<std::size_t>(kSpriteSize * kSpriteSize),
                                        Color(static_cast<bytecs>(255), static_cast<bytecs>(255),
                                              static_cast<bytecs>(255), static_cast<bytecs>(255)));
        white->SetData(pixels.data(), static_cast<int>(pixels.size()));

        device.Clear(Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                           static_cast<bytecs>(20), static_cast<bytecs>(255)));

        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr,
                   &effect);
        batch.Draw(*white, Rectangle(28, 28, kSpriteSize, kSpriteSize),
                  Color(static_cast<bytecs>(255), static_cast<bytecs>(255),
                        static_cast<bytecs>(255), static_cast<bytecs>(255)));
        batch.End();

        ExpectPixel("the custom shader's own tint uniform reached the sprite, not vertex colour",
                    Rectangle(28 + kSpriteSize / 2, 28 + kSpriteSize / 2, 1, 1),
                    Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                          static_cast<bytecs>(0), static_cast<bytecs>(255)),
                    /*tolerance=*/10);
        ExpectPixel("outside the sprite stays the clear colour", Rectangle(4, 4, 1, 1),
                    Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                          static_cast<bytecs>(20), static_cast<bytecs>(255)));
    }

public:
    IglSpriteBatchShaderEffectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglSpriteBatchShaderEffectTest>();
}
