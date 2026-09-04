// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-250 (finding F-08): the renderer must state the shader dialect a
// custom ShaderEffect has to be written in.
//
// GetShaderDialectEXT() exists precisely so an application need not infer the dialect from the
// build identity. This renderer left it at the shared Unknown default while
// VulkanEffectRenderer::CompileProgram accepted nothing but SPIR-V words -- so the one query whose
// job is to stop a caller guessing answered "guess".
//
//   A  The dialect is declared, and it is GlslVulkan.
//   B  The declared dialect is ACTIONABLE, at least far enough to reject the wrong payload: GLSL
//      source text -- the thing a caller who read "GlslVulkan" would most plausibly send -- is
//      refused, with a compile error, rather than accepted and drawn from nothing.
//   C  And the reason B matters is recorded as an assertion rather than a comment. IGL's Vulkan
//      backend reports the SAME enumerator and takes GLSL source (igl_spritebatch_shadereffect_
//      test.cpp gates on exactly this value before handing it a GLSL string), while this renderer
//      takes compiled bytecode. So the enumerator does not distinguish source from bytecode, and
//      a caller acting on it alone is still guessing. VULKAN-264 owns narrowing that, which is a
//      C-ABI change. This leg pins the ambiguity so that if a future enumerator resolves it, this
//      test goes red and says so instead of quietly continuing to describe the old world.
//
// That a VALID SPIR-V pair is accepted and renders is not re-proved here -- Vulkan_ShaderEffect_SpirV
// owns that end to end, with a real tinted draw.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::ShaderDialectEXT;

namespace
{
    // Deliberately valid, ordinary Vulkan-flavoured GLSL: explicit locations, an explicit binding,
    // nothing a "GlslVulkan" renderer could object to on its own terms. IGL's Vulkan backend
    // compiles shaders of exactly this shape. This renderer cannot, because it wants the compiled
    // form, and that difference is the whole point of leg C.
    const char* kVulkanGlslVert = R"(#version 450
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 0) out vec2 vUV;
void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }
)";

    const char* kVulkanGlslFrag = R"(#version 450
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D SpriteTexture;
void main() { outColor = texture(SpriteTexture, vUV); }
)";

    constexpr int kSize = 32;
}

class VulkanShaderDialectContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        const ShaderDialectEXT dialect = dev.GetShaderDialectEXT();
        check(dialect != ShaderDialectEXT::Unknown,
              "A the renderer declares a shader dialect instead of leaving it Unknown",
              "ordinal " + std::to_string(static_cast<int>(dialect)));
        check(dialect == ShaderDialectEXT::GlslVulkan,
              "A the declared dialect is GlslVulkan",
              "ordinal " + std::to_string(static_cast<int>(dialect)) + ", expected "
                  + std::to_string(static_cast<int>(ShaderDialectEXT::GlslVulkan)));

        // B + C: the payload a caller acting on that answer would most plausibly send.
        ShaderEffect glslEffect(dev, std::string(kVulkanGlslVert), std::string(kVulkanGlslFrag));
        const bool accepted = glslEffect.IsEffectValid();
        const std::string error = glslEffect.GetCompileErrorEXT();
        check(!accepted,
              "B Vulkan-flavoured GLSL source is refused, not accepted and drawn from nothing",
              accepted ? "accepted" : ("refused: " + (error.empty() ? "(no message)" : error)));
        check(!accepted,
              "C the GlslVulkan enumerator does not distinguish source from bytecode -- IGL's "
              "Vulkan backend reports the same value and compiles this exact source (VULKAN-264)",
              accepted ? "this renderer now accepts GLSL source too; the ambiguity is gone and "
                         "VULKAN-264's premise needs re-reading"
                       : "still bytecode-only here, source-only there");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    VulkanShaderDialectContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanShaderDialectContractTest g;
    g.Run();
    return g.getResult();
}
