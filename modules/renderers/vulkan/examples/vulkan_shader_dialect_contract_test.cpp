// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-250 (finding F-08): the renderer must state the shader dialect a
// custom ShaderEffect has to be written in.
//
// GetShaderDialectEXT() exists precisely so an application need not infer the dialect from the
// build identity. This renderer left it at the shared Unknown default while
// VulkanEffectRenderer::CompileProgram accepted nothing but SPIR-V words -- so the one query whose
// job is to stop a caller guessing answered "guess".
//
//   A  The dialect is declared, and it is the bytecode-specific SpirV identity.
//   B  The declared dialect is actionable: GLSL source text is refused with a SPIR-V diagnostic,
//      rather than accepted and drawn from nothing.
//   C  IGL's Vulkan backend remains source-based and reports GlslVulkan, so this renderer must not
//      report that identity. VULKAN-264 appends SpirV without changing any existing ordinal.
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
#include "CNA/ShaderLanguageEXT.hpp"

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
        check(dialect == ShaderDialectEXT::SpirV,
              "A the declared dialect is already-compiled SpirV bytecode",
              "ordinal " + std::to_string(static_cast<int>(dialect)) + ", expected "
                  + std::to_string(static_cast<int>(ShaderDialectEXT::SpirV)));
        check(dev.SupportsShaderLanguageEXT(
                  CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Vertex)
                  && dev.SupportsShaderLanguageEXT(
                      CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Fragment),
              "A2 the live renderer accepts explicit SPIR-V graphics-stage payloads",
              "vertex and fragment queried independently");
        check(dev.SupportsShaderLanguageEXT(
                  CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Compute)
                  == dev.SupportsCapability(CNA::GraphicsCapability::ComputeShaders),
              "A3 SPIR-V compute language support follows the implemented compute path",
              dev.SupportsCapability(CNA::GraphicsCapability::ComputeShaders)
                  ? "compute is available and SPIR-V is accepted"
                  : "compute is unavailable and the pair is refused");
        check(!dev.SupportsShaderLanguageEXT(
                  CNA::ShaderLanguageEXT::GlslVulkan, CNA::ShaderStageEXT::Vertex)
                  && !dev.SupportsShaderLanguageEXT(
                      CNA::ShaderLanguageEXT::Unknown, CNA::ShaderStageEXT::Vertex)
                  && !dev.SupportsShaderLanguageEXT(
                      static_cast<CNA::ShaderLanguageEXT>(999), CNA::ShaderStageEXT::Fragment)
                  && !dev.SupportsShaderLanguageEXT(
                      CNA::ShaderLanguageEXT::SpirV, static_cast<CNA::ShaderStageEXT>(999)),
              "A4 Vulkan GLSL, unknown and invalid payload pairs are refused",
              "support is not inferred from the Vulkan renderer identity");

        // B + C: the payload a caller acting on that answer would most plausibly send.
        ShaderEffect glslEffect(dev, std::string(kVulkanGlslVert), std::string(kVulkanGlslFrag));
        const bool accepted = glslEffect.IsEffectValid();
        const std::string error = glslEffect.GetCompileErrorEXT();
        check(!accepted,
              "B Vulkan-flavoured GLSL source is refused, not accepted and drawn from nothing",
              accepted ? "accepted" : ("refused: " + (error.empty() ? "(no message)" : error)));
        // plan_vulkan.md VULKAN-256: leg B was passing for the wrong reason. The refusal read
        // "SPIR-V size must be a multiple of 4 bytes" -- an accident of THIS source's length, not
        // a judgement about its content. One byte more and the same text went to
        // vkCreateShaderModule as if it were bytecode. This leg pads the source to a multiple of
        // four with a trailing comment, so the size check cannot fire and only a real check can.
        {
            std::string paddedVert(kVulkanGlslVert);
            std::string paddedFrag(kVulkanGlslFrag);
            while (paddedVert.size() % 4 != 0) paddedVert += ' ';
            while (paddedFrag.size() % 4 != 0) paddedFrag += ' ';
            ShaderEffect padded(dev, paddedVert, paddedFrag);
            const bool paddedAccepted = padded.IsEffectValid();
            const std::string paddedError = padded.GetCompileErrorEXT();
            check(!paddedAccepted && paddedError.find("SPIR-V") != std::string::npos,
                  "B2 GLSL source whose LENGTH is a multiple of four is refused too, and the "
                  "message says what the payload has to be",
                  paddedAccepted ? "accepted -- the refusal was an accident of length"
                                 : ("refused: " + (paddedError.empty() ? "(no message)"
                                                                      : paddedError)));
        }

        check(dialect != ShaderDialectEXT::GlslVulkan,
              "C bytecode-only Vulkan is distinct from IGL's GLSL-consuming Vulkan backend",
              dialect == ShaderDialectEXT::SpirV
                  ? "SpirV here; GlslVulkan remains the source identity"
                  : "unexpected ordinal " + std::to_string(static_cast<int>(dialect)));

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
