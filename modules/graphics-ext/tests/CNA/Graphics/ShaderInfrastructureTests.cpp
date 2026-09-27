// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-210, MOD-219, MOD-220: the shader-side infrastructure the passes share.
//
// All three exist because of failures that produce a picture rather than an error. A shader
// compiled twice costs twice and looks identical. A shader that failed to compile makes its pass
// copy its input, which looks like a weak effect rather than a missing one. A pyramid sampled with
// point filtering looks like bloom, just blockier. So each is checked by counting or by reading
// what was reported, never by looking at the result.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/ShaderDiagnosticEXT.hpp"
#include "RetroGraphicsTestSupport.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::Graphics::ShaderCodeEXT;
using CNA::Graphics::ShaderBindingRequirementEXT;
using CNA::Graphics::ShaderBindingTypeEXT;
using CNA::Graphics::ShaderPackageEXT;
using CNA::Graphics::ShaderPackageSelectionEXT;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;

constexpr const char* kVertex = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

constexpr const char* kFragment = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() { FragColor = texture(texture1, TexCoord); }
)";

class FallbackShaderEffect final : public ShaderEffect
{
public:
    FallbackShaderEffect(GraphicsDevice& device, const ShaderPackageEXT& package)
        : ShaderEffect(device, package, kVertex, kFragment)
    {
    }
};

/// Not GLSL at all. Every compiler rejects it, which is the point: MOD-219 is about what happens
/// when a shader fails, and a subtly-wrong shader might compile on some driver.
constexpr const char* kBroken = "this is not a shader; it is a sentence.";

// =====================================================================================
// MOD-2211: explicit, owned source/binary payloads
// =====================================================================================

TEST(ShaderCodeEXTTest, TextOwnsEveryFieldAndSurvivesCopyAndMove)
{
    std::string entryPoint = "main";
    std::string label = "post/tonemap.frag";
    std::string source = "void main() {}";
    ShaderCodeEXT code(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
        entryPoint, label, source);

    entryPoint.clear();
    label.clear();
    source.clear();
    EXPECT_EQ(code.getLanguage(), CNA::ShaderLanguageEXT::GlslEs);
    EXPECT_EQ(code.getStage(), CNA::ShaderStageEXT::Fragment);
    EXPECT_EQ(code.getEntryPoint(), "main");
    EXPECT_EQ(code.getSourceLabel(), "post/tonemap.frag");
    EXPECT_TRUE(code.isText());
    EXPECT_FALSE(code.isBinary());
    EXPECT_EQ(code.getPayloadByteSize(), 14U);
    EXPECT_EQ(code.getText(), "void main() {}");
    EXPECT_THROW((void)code.getBytes(), std::logic_error);

    ShaderCodeEXT copy = code;
    ShaderCodeEXT moved = std::move(copy);
    EXPECT_EQ(moved.getText(), code.getText());
    EXPECT_NE(moved.getText().data(), code.getText().data());
}

TEST(ShaderCodeEXTTest, BinaryOwnsEveryFieldAndSurvivesCallerMutation)
{
    std::vector<std::uint8_t> bytes = {0x03, 0x02, 0x23, 0x07};
    ShaderCodeEXT code(
        CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Compute,
        "main", "compute/cull.spv", bytes);

    bytes[0] = 0;
    EXPECT_EQ(code.getLanguage(), CNA::ShaderLanguageEXT::SpirV);
    EXPECT_EQ(code.getStage(), CNA::ShaderStageEXT::Compute);
    EXPECT_EQ(code.getEntryPoint(), "main");
    EXPECT_EQ(code.getSourceLabel(), "compute/cull.spv");
    EXPECT_FALSE(code.isText());
    EXPECT_TRUE(code.isBinary());
    EXPECT_EQ(code.getPayloadByteSize(), 4U);
    EXPECT_EQ(code.getBytes(), (std::vector<std::uint8_t>{0x03, 0x02, 0x23, 0x07}));
    EXPECT_THROW((void)code.getText(), std::logic_error);
}

TEST(ShaderCodeEXTTest, EveryDeclaredLanguageAcceptsOnlyItsPayloadForm)
{
    constexpr std::array textLanguages = {
        CNA::ShaderLanguageEXT::GlslDesktop,
        CNA::ShaderLanguageEXT::GlslEs,
        CNA::ShaderLanguageEXT::GlslVulkan,
        CNA::ShaderLanguageEXT::Hlsl,
        CNA::ShaderLanguageEXT::Msl,
        CNA::ShaderLanguageEXT::Wgsl
    };
    for (const auto language : textLanguages)
    {
        EXPECT_NO_THROW(ShaderCodeEXT(
            language, CNA::ShaderStageEXT::Vertex, "main", "text", "source"));
        EXPECT_THROW(ShaderCodeEXT(
            language, CNA::ShaderStageEXT::Vertex, "main", "binary",
            std::vector<std::uint8_t>{1}), std::invalid_argument);
    }

    constexpr std::array binaryLanguages = {
        CNA::ShaderLanguageEXT::SpirV,
        CNA::ShaderLanguageEXT::Dxil
    };
    for (const auto language : binaryLanguages)
    {
        const std::vector<std::uint8_t> bytes = language == CNA::ShaderLanguageEXT::SpirV
            ? std::vector<std::uint8_t>{1, 2, 3, 4}
            : std::vector<std::uint8_t>{1};
        EXPECT_NO_THROW(ShaderCodeEXT(
            language, CNA::ShaderStageEXT::Vertex, "main", "binary", bytes));
        EXPECT_THROW(ShaderCodeEXT(
            language, CNA::ShaderStageEXT::Vertex, "main", "text", "source"),
            std::invalid_argument);
    }
}

TEST(ShaderCodeEXTTest, UnknownFutureAndSentinelLanguagesAreRejected)
{
    constexpr std::array invalidLanguages = {
        CNA::ShaderLanguageEXT::Unknown,
        CNA::ShaderLanguageEXT::Count,
        static_cast<CNA::ShaderLanguageEXT>(999)
    };
    for (const auto language : invalidLanguages)
    {
        EXPECT_THROW(ShaderCodeEXT(
            language, CNA::ShaderStageEXT::Vertex, "main", "text", "source"),
            std::invalid_argument);
        EXPECT_THROW(ShaderCodeEXT(
            language, CNA::ShaderStageEXT::Vertex, "main", "binary",
            std::vector<std::uint8_t>{1, 2, 3, 4}), std::invalid_argument);
    }
}

TEST(ShaderCodeEXTTest, UnknownFutureAndSentinelStagesAreRejected)
{
    constexpr std::array invalidStages = {
        CNA::ShaderStageEXT::Unknown,
        CNA::ShaderStageEXT::Count,
        static_cast<CNA::ShaderStageEXT>(999)
    };
    for (const auto stage : invalidStages)
    {
        EXPECT_THROW(ShaderCodeEXT(
            CNA::ShaderLanguageEXT::GlslEs, stage, "main", "text", "source"),
            std::invalid_argument);
        EXPECT_THROW(ShaderCodeEXT(
            CNA::ShaderLanguageEXT::SpirV, stage, "main", "binary",
            std::vector<std::uint8_t>{1, 2, 3, 4}), std::invalid_argument);
    }
}

TEST(ShaderCodeEXTTest, EmptyEntryPointAndPayloadsAreRejectedButAnEmptyLabelIsAllowed)
{
    EXPECT_THROW(ShaderCodeEXT(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
        "", "text", "source"), std::invalid_argument);
    EXPECT_THROW(ShaderCodeEXT(
        CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Compute,
        "", "binary", std::vector<std::uint8_t>{1, 2, 3, 4}),
        std::invalid_argument);
    EXPECT_THROW(ShaderCodeEXT(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
        "main", "text", ""), std::invalid_argument);
    EXPECT_THROW(ShaderCodeEXT(
        CNA::ShaderLanguageEXT::Dxil, CNA::ShaderStageEXT::Compute,
        "main", "binary", std::vector<std::uint8_t>{}), std::invalid_argument);

    const ShaderCodeEXT unlabeled(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
        "main", "", "source");
    EXPECT_TRUE(unlabeled.getSourceLabel().empty());
}

TEST(ShaderCodeEXTTest, SpirVRequiresWholeWordsWhileDxilRemainsByteAddressed)
{
    for (const std::size_t byteCount : {1U, 2U, 3U, 5U, 6U, 7U})
    {
        EXPECT_THROW(ShaderCodeEXT(
            CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Compute,
            "main", "bad.spv", std::vector<std::uint8_t>(byteCount, 0)),
            std::invalid_argument);
    }
    EXPECT_NO_THROW(ShaderCodeEXT(
        CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Compute,
        "main", "one-word.spv", std::vector<std::uint8_t>(4, 0)));
    EXPECT_NO_THROW(ShaderCodeEXT(
        CNA::ShaderLanguageEXT::Dxil, CNA::ShaderStageEXT::Compute,
        "main", "byte.dxil", std::vector<std::uint8_t>{0}));
}

// =====================================================================================
// MOD-2212: owned multi-language packages and their stage/binding contract
// =====================================================================================

TEST(ShaderPackageEXTTest, OwnsMultiLanguageVariantsStagesAndBindingsAcrossCopyAndMove)
{
    std::vector<ShaderCodeEXT> variants;
    variants.emplace_back(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex,
        "main", "effect.vert", "glsl vertex");
    variants.emplace_back(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
        "main", "effect.frag", "glsl fragment");
    variants.emplace_back(
        CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Vertex,
        "main", "effect.vert.spv", std::vector<std::uint8_t>{1, 2, 3, 4});
    variants.emplace_back(
        CNA::ShaderLanguageEXT::SpirV, CNA::ShaderStageEXT::Fragment,
        "main", "effect.frag.spv", std::vector<std::uint8_t>{5, 6, 7, 8});
    variants.emplace_back(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Vertex,
        "VSMain", "effect.hlsl", "hlsl vertex");
    variants.emplace_back(
        CNA::ShaderLanguageEXT::Wgsl, CNA::ShaderStageEXT::Fragment,
        "fragmentMain", "effect.wgsl", "wgsl fragment");

    std::vector<CNA::ShaderStageEXT> stages = {
        CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment};
    std::vector<ShaderBindingRequirementEXT> bindings;
    bindings.emplace_back(
        "sceneTexture", 0, ShaderBindingTypeEXT::SampledTexture2D,
        CNA::ShaderStageEXT::Fragment);
    ShaderPackageEXT package(variants, stages, bindings);
    variants.clear();
    stages.clear();
    bindings.clear();

    ASSERT_EQ(package.getVariants().size(), 6U);
    EXPECT_EQ(package.getVariants()[0].getText(), "glsl vertex");
    EXPECT_EQ(package.getVariants()[2].getBytes()[0], 1U);
    EXPECT_EQ(package.getRequiredStages(),
              (std::vector<CNA::ShaderStageEXT>{
                  CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment}));
    EXPECT_TRUE(package.requiresStage(CNA::ShaderStageEXT::Vertex));
    EXPECT_TRUE(package.requiresStage(CNA::ShaderStageEXT::Fragment));
    EXPECT_FALSE(package.requiresStage(CNA::ShaderStageEXT::Compute));
    EXPECT_FALSE(package.requiresStage(static_cast<CNA::ShaderStageEXT>(999)));
    ASSERT_EQ(package.getBindingRequirements().size(), 1U);
    const auto& binding = package.getBindingRequirements()[0];
    EXPECT_EQ(binding.getName(), "sceneTexture");
    EXPECT_EQ(binding.getBinding(), 0);
    EXPECT_EQ(binding.getType(), ShaderBindingTypeEXT::SampledTexture2D);
    EXPECT_EQ(binding.getStage(), CNA::ShaderStageEXT::Fragment);

    ShaderPackageEXT copy = package;
    ShaderPackageEXT moved = std::move(copy);
    EXPECT_EQ(moved.getVariants()[3].getSourceLabel(), "effect.frag.spv");
    EXPECT_NE(moved.getVariants().data(), package.getVariants().data());
}

TEST(ShaderPackageEXTTest, BindingKindsHaveStableUniqueOrdinals)
{
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::SampledTexture2D), 0);
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::SampledTextureCube), 1);
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::SampledTexture3D), 2);
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::SampledTexture2DArray), 3);
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::StorageBuffer), 4);
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::StorageTexture2D), 5);
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::ConstantBuffer), 6);
    EXPECT_EQ(static_cast<int>(ShaderBindingTypeEXT::Count), 7);
}

TEST(ShaderPackageEXTTest, BindingRequirementRejectsInvalidFields)
{
    EXPECT_THROW(ShaderBindingRequirementEXT(
        "", 0, ShaderBindingTypeEXT::SampledTexture2D, CNA::ShaderStageEXT::Fragment),
        std::invalid_argument);
    EXPECT_THROW(ShaderBindingRequirementEXT(
        "texture", -1, ShaderBindingTypeEXT::SampledTexture2D,
        CNA::ShaderStageEXT::Fragment), std::invalid_argument);
    EXPECT_THROW(ShaderBindingRequirementEXT(
        "texture", 0, ShaderBindingTypeEXT::Count, CNA::ShaderStageEXT::Fragment),
        std::invalid_argument);
    EXPECT_THROW(ShaderBindingRequirementEXT(
        "texture", 0, static_cast<ShaderBindingTypeEXT>(999),
        CNA::ShaderStageEXT::Fragment), std::invalid_argument);
    EXPECT_THROW(ShaderBindingRequirementEXT(
        "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
        CNA::ShaderStageEXT::Unknown), std::invalid_argument);
    EXPECT_THROW(ShaderBindingRequirementEXT(
        "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
        static_cast<CNA::ShaderStageEXT>(999)), std::invalid_argument);
}

TEST(ShaderPackageEXTTest, EmptyVariantsOrRequiredStagesAreRejected)
{
    const ShaderCodeEXT code(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex,
        "main", "vertex", "source");
    EXPECT_THROW(ShaderPackageEXT(
        {}, std::vector<CNA::ShaderStageEXT>{CNA::ShaderStageEXT::Vertex}),
        std::invalid_argument);
    EXPECT_THROW(ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{code}, {}), std::invalid_argument);
}

TEST(ShaderPackageEXTTest, RequiredStagesMustBeValidUniqueAndRepresented)
{
    const ShaderCodeEXT vertex(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex,
        "main", "vertex", "source");
    EXPECT_THROW(ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{vertex},
        std::vector<CNA::ShaderStageEXT>{CNA::ShaderStageEXT::Unknown}),
        std::invalid_argument);
    EXPECT_THROW(ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{vertex},
        std::vector<CNA::ShaderStageEXT>{static_cast<CNA::ShaderStageEXT>(999)}),
        std::invalid_argument);
    EXPECT_THROW(ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{vertex},
        std::vector<CNA::ShaderStageEXT>{
            CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Vertex}),
        std::invalid_argument);
    EXPECT_THROW(ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{vertex},
        std::vector<CNA::ShaderStageEXT>{
            CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment}),
        std::invalid_argument);
}

TEST(ShaderPackageEXTTest, VariantsAndBindingsCannotUseUndeclaredStages)
{
    const ShaderCodeEXT vertex(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex,
        "main", "vertex", "source");
    const ShaderCodeEXT fragment(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
        "main", "fragment", "source");
    EXPECT_THROW(ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{vertex, fragment},
        std::vector<CNA::ShaderStageEXT>{CNA::ShaderStageEXT::Vertex}),
        std::invalid_argument);
    EXPECT_THROW(ShaderPackageEXT(
        std::vector<ShaderCodeEXT>{vertex},
        std::vector<CNA::ShaderStageEXT>{CNA::ShaderStageEXT::Vertex},
        std::vector<ShaderBindingRequirementEXT>{ShaderBindingRequirementEXT(
            "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
            CNA::ShaderStageEXT::Fragment)}), std::invalid_argument);
}

TEST(ShaderPackageEXTTest, BindingsAreUniquePerStageAndConsistentAcrossStages)
{
    const ShaderCodeEXT vertex(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex,
        "main", "vertex", "source");
    const ShaderCodeEXT fragment(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Fragment,
        "main", "fragment", "source");
    const std::vector<ShaderCodeEXT> variants = {vertex, fragment};
    const std::vector<CNA::ShaderStageEXT> stages = {
        CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment};

    EXPECT_THROW(ShaderPackageEXT(
        variants, stages,
        std::vector<ShaderBindingRequirementEXT>{
            ShaderBindingRequirementEXT(
                "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment),
            ShaderBindingRequirementEXT(
                "textureAgain", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)}), std::invalid_argument);
    EXPECT_THROW(ShaderPackageEXT(
        variants, stages,
        std::vector<ShaderBindingRequirementEXT>{
            ShaderBindingRequirementEXT(
                "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Vertex),
            ShaderBindingRequirementEXT(
                "other", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)}), std::invalid_argument);
    EXPECT_THROW(ShaderPackageEXT(
        variants, stages,
        std::vector<ShaderBindingRequirementEXT>{
            ShaderBindingRequirementEXT(
                "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Vertex),
            ShaderBindingRequirementEXT(
                "texture", 0, ShaderBindingTypeEXT::StorageBuffer,
                CNA::ShaderStageEXT::Fragment)}), std::invalid_argument);
    EXPECT_NO_THROW(ShaderPackageEXT(
        variants, stages,
        std::vector<ShaderBindingRequirementEXT>{
            ShaderBindingRequirementEXT(
                "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Vertex),
            ShaderBindingRequirementEXT(
                "texture", 0, ShaderBindingTypeEXT::SampledTexture2D,
                CNA::ShaderStageEXT::Fragment)}));
}

// =====================================================================================
// MOD-2213: deterministic live-device shader-package selection
// =====================================================================================

TEST(ShaderPackageSelectionEXTTest, StablePreferenceIgnoresDeclarationOrderAndOwnsSelectedCode)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    std::vector<ShaderCodeEXT> variants;
    const auto add = [&variants](const CNA::ShaderLanguageEXT language,
                                 const CNA::ShaderStageEXT stage,
                                 const std::string& label) {
        if (language == CNA::ShaderLanguageEXT::SpirV
            || language == CNA::ShaderLanguageEXT::Dxil)
        {
            variants.emplace_back(
                language, stage, "main", label,
                std::vector<std::uint8_t>{1, 2, 3, 4});
        }
        else
            variants.emplace_back(language, stage, "main", label, "source");
    };

    constexpr std::array declarationOrder = {
        CNA::ShaderLanguageEXT::Wgsl,
        CNA::ShaderLanguageEXT::Msl,
        CNA::ShaderLanguageEXT::Hlsl,
        CNA::ShaderLanguageEXT::GlslVulkan,
        CNA::ShaderLanguageEXT::GlslEs,
        CNA::ShaderLanguageEXT::GlslDesktop,
        CNA::ShaderLanguageEXT::Dxil,
        CNA::ShaderLanguageEXT::SpirV
    };
    for (const auto language : declarationOrder)
    {
        add(language, CNA::ShaderStageEXT::Vertex, "vertex");
        add(language, CNA::ShaderStageEXT::Fragment, "fragment");
    }

    ShaderPackageSelectionEXT selection = ShaderPackageEXT(
        std::move(variants),
        {CNA::ShaderStageEXT::Fragment, CNA::ShaderStageEXT::Vertex}).selectFor(device);

    constexpr std::array preference = {
        CNA::ShaderLanguageEXT::SpirV,
        CNA::ShaderLanguageEXT::Dxil,
        CNA::ShaderLanguageEXT::GlslDesktop,
        CNA::ShaderLanguageEXT::GlslEs,
        CNA::ShaderLanguageEXT::GlslVulkan,
        CNA::ShaderLanguageEXT::Hlsl,
        CNA::ShaderLanguageEXT::Msl,
        CNA::ShaderLanguageEXT::Wgsl
    };
    auto expected = CNA::ShaderLanguageEXT::Unknown;
    if (device.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
    {
        for (const auto language : preference)
        {
            if (device.SupportsShaderLanguageEXT(language, CNA::ShaderStageEXT::Vertex)
                && device.SupportsShaderLanguageEXT(language, CNA::ShaderStageEXT::Fragment))
            {
                expected = language;
                break;
            }
        }
    }

    EXPECT_EQ(selection.isUsable(), expected != CNA::ShaderLanguageEXT::Unknown);
    EXPECT_EQ(selection.getLanguage(), expected);
    if (selection.isUsable())
    {
        ASSERT_EQ(selection.getCode().size(), 2U);
        EXPECT_EQ(selection.getCode()[0].getStage(), CNA::ShaderStageEXT::Fragment);
        EXPECT_EQ(selection.getCode()[1].getStage(), CNA::ShaderStageEXT::Vertex);
        ASSERT_NE(selection.findStage(CNA::ShaderStageEXT::Vertex), nullptr);
        EXPECT_EQ(selection.findStage(CNA::ShaderStageEXT::Vertex)->getSourceLabel(), "vertex");
        EXPECT_EQ(selection.findStage(CNA::ShaderStageEXT::Compute), nullptr);
        EXPECT_NE(selection.getDiagnostic().find(": selected"), std::string::npos);
    }
}

TEST(ShaderPackageSelectionEXTTest, DuplicateLiveStageIsAmbiguousAndNeverChosen)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    constexpr std::array preference = {
        CNA::ShaderLanguageEXT::SpirV,
        CNA::ShaderLanguageEXT::Dxil,
        CNA::ShaderLanguageEXT::GlslDesktop,
        CNA::ShaderLanguageEXT::GlslEs,
        CNA::ShaderLanguageEXT::GlslVulkan,
        CNA::ShaderLanguageEXT::Hlsl,
        CNA::ShaderLanguageEXT::Msl,
        CNA::ShaderLanguageEXT::Wgsl
    };
    auto language = CNA::ShaderLanguageEXT::Unknown;
    for (const auto candidate : preference)
    {
        if (device.SupportsShaderLanguageEXT(candidate, CNA::ShaderStageEXT::Vertex))
        {
            language = candidate;
            break;
        }
    }
    if (language == CNA::ShaderLanguageEXT::Unknown
        || !device.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
        GTEST_SKIP() << "this renderer has no selectable graphics shader language";

    std::vector<ShaderCodeEXT> variants;
    const auto add = [&variants, language](const char* label) {
        if (language == CNA::ShaderLanguageEXT::SpirV
            || language == CNA::ShaderLanguageEXT::Dxil)
            variants.emplace_back(
                language, CNA::ShaderStageEXT::Vertex, "main", label,
                std::vector<std::uint8_t>{1, 2, 3, 4});
        else
            variants.emplace_back(
                language, CNA::ShaderStageEXT::Vertex, "main", label, "source");
    };
    add("first");
    add("second");

    const auto selection = ShaderPackageEXT(
        std::move(variants), {CNA::ShaderStageEXT::Vertex}).selectFor(device);
    EXPECT_FALSE(selection.isUsable());
    EXPECT_EQ(selection.getLanguage(), CNA::ShaderLanguageEXT::Unknown);
    EXPECT_TRUE(selection.getCode().empty());
    EXPECT_EQ(selection.findStage(CNA::ShaderStageEXT::Vertex), nullptr);
    EXPECT_NE(selection.getDiagnostic().find("ambiguous duplicate Vertex"), std::string::npos);
    EXPECT_NE(selection.getDiagnostic().find("first"), std::string::npos);
    EXPECT_NE(selection.getDiagnostic().find("second"), std::string::npos);
}

TEST(ShaderPackageSelectionEXTTest, FragmentStorageRequiresAnAvailableBufferRoute)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    if (device.SupportsCapability(CNA::GraphicsCapability::ComputeShaders))
        GTEST_SKIP() << "this renderer has a storage-buffer creation route";

    CNA::ShaderLanguageEXT language = CNA::ShaderLanguageEXT::Unknown;
    for (const auto candidate : {CNA::ShaderLanguageEXT::SpirV,
                                 CNA::ShaderLanguageEXT::Wgsl,
                                 CNA::ShaderLanguageEXT::Hlsl,
                                 CNA::ShaderLanguageEXT::GlslDesktop,
                                 CNA::ShaderLanguageEXT::GlslEs})
    {
        if (device.SupportsShaderLanguageEXT(candidate, CNA::ShaderStageEXT::Fragment))
        {
            language = candidate;
            break;
        }
    }
    if (language == CNA::ShaderLanguageEXT::Unknown)
        GTEST_SKIP() << "this renderer has no selectable fragment language";

    std::vector<ShaderCodeEXT> variants;
    if (language == CNA::ShaderLanguageEXT::SpirV)
        variants.emplace_back(language, CNA::ShaderStageEXT::Fragment, "main", "fragment",
                              std::vector<std::uint8_t>{1, 2, 3, 4});
    else
        variants.emplace_back(language, CNA::ShaderStageEXT::Fragment, "main", "fragment", "source");
    const auto selection = ShaderPackageEXT(
        std::move(variants), {CNA::ShaderStageEXT::Fragment},
        {ShaderBindingRequirementEXT("lights", 6, ShaderBindingTypeEXT::StorageBuffer,
                                     CNA::ShaderStageEXT::Fragment)}).selectFor(device);

    EXPECT_FALSE(selection.isUsable());
    EXPECT_NE(selection.getDiagnostic().find("requires ComputeShaders"), std::string::npos);
}

TEST(ShaderPackageSelectionEXTTest, RequiredVertexStorageBindingIsCapabilityChecked)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    CNA::ShaderLanguageEXT language = CNA::ShaderLanguageEXT::Unknown;
    for (const auto candidate : {CNA::ShaderLanguageEXT::SpirV,
                                 CNA::ShaderLanguageEXT::Wgsl,
                                 CNA::ShaderLanguageEXT::Hlsl,
                                 CNA::ShaderLanguageEXT::GlslDesktop,
                                 CNA::ShaderLanguageEXT::GlslEs})
    {
        if (device.SupportsShaderLanguageEXT(candidate, CNA::ShaderStageEXT::Vertex))
        {
            language = candidate;
            break;
        }
    }
    if (language == CNA::ShaderLanguageEXT::Unknown)
        GTEST_SKIP() << "this renderer has no selectable vertex language";

    std::vector<ShaderCodeEXT> variants;
    if (language == CNA::ShaderLanguageEXT::SpirV)
        variants.emplace_back(
            language, CNA::ShaderStageEXT::Vertex, "main", "vertex",
            std::vector<std::uint8_t>{1, 2, 3, 4});
    else
        variants.emplace_back(
            language, CNA::ShaderStageEXT::Vertex, "main", "vertex", "source");
    const auto selection = ShaderPackageEXT(
        std::move(variants), {CNA::ShaderStageEXT::Vertex},
        {ShaderBindingRequirementEXT(
            "instances", 2147483647, ShaderBindingTypeEXT::StorageBuffer,
            CNA::ShaderStageEXT::Vertex)}).selectFor(device);

    EXPECT_FALSE(selection.isUsable());
    EXPECT_NE(
        selection.getDiagnostic().find("exceeds vertex storage-buffer bindings"),
        std::string::npos);
}

TEST(ShaderPackageSelectionEXTTest, ConstantBuffersHaveOnlyThePublishedComputeRoute)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    CNA::ShaderLanguageEXT language = CNA::ShaderLanguageEXT::Unknown;
    for (const auto candidate : {CNA::ShaderLanguageEXT::SpirV,
                                 CNA::ShaderLanguageEXT::Wgsl,
                                 CNA::ShaderLanguageEXT::Hlsl,
                                 CNA::ShaderLanguageEXT::GlslDesktop,
                                 CNA::ShaderLanguageEXT::GlslEs})
    {
        if (device.SupportsShaderLanguageEXT(candidate, CNA::ShaderStageEXT::Vertex))
        {
            language = candidate;
            break;
        }
    }
    if (language == CNA::ShaderLanguageEXT::Unknown)
        GTEST_SKIP() << "this renderer has no selectable vertex language";

    std::vector<ShaderCodeEXT> variants;
    if (language == CNA::ShaderLanguageEXT::SpirV)
        variants.emplace_back(
            language, CNA::ShaderStageEXT::Vertex, "main", "vertex",
            std::vector<std::uint8_t>{1, 2, 3, 4});
    else
        variants.emplace_back(
            language, CNA::ShaderStageEXT::Vertex, "main", "vertex", "source");
    const auto selection = ShaderPackageEXT(
        std::move(variants), {CNA::ShaderStageEXT::Vertex},
        {ShaderBindingRequirementEXT(
            "Parameters", 0, ShaderBindingTypeEXT::ConstantBuffer,
            CNA::ShaderStageEXT::Vertex)}).selectFor(device);

    EXPECT_FALSE(selection.isUsable());
    EXPECT_NE(
        selection.getDiagnostic().find("has no portable non-compute binding route yet"),
        std::string::npos);
}

TEST(ShaderPackageOverloadTest, ShaderEffectRejectsMismatchedCodeAndWrongPackageStages)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    const ShaderCodeEXT vertex(
        CNA::ShaderLanguageEXT::GlslEs, CNA::ShaderStageEXT::Vertex,
        "main", "effect.vert", "source");
    const ShaderCodeEXT fragment(
        CNA::ShaderLanguageEXT::GlslDesktop, CNA::ShaderStageEXT::Fragment,
        "main", "effect.frag", "source");
    EXPECT_THROW(ShaderEffect(device, vertex, fragment), std::invalid_argument);
    EXPECT_THROW(
        ShaderEffect(
            device, ShaderPackageEXT(
                        {vertex}, {CNA::ShaderStageEXT::Vertex})),
        std::invalid_argument);
}

TEST(ShaderPackageOverloadTest, LegacyShaderEffectReportsNoExplicitSelectedLanguage)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    ShaderEffect effect(device, kVertex, kFragment);
    EXPECT_EQ(effect.GetSelectedShaderLanguageEXT(), CNA::ShaderLanguageEXT::Unknown);
}

TEST(ShaderPackageOverloadTest, NativeHlslPackageCompilesOnADeclaredHlslRenderer)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    if (!device.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::Hlsl,
                                          CNA::ShaderStageEXT::Vertex) ||
        !device.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::Hlsl,
                                          CNA::ShaderStageEXT::Fragment))
        GTEST_SKIP() << "this renderer does not consume HLSL graphics stages";

    const ShaderPackageEXT package(
        {
            ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Vertex,
                          "main", "native.vert.hlsl",
                          "float4 main(float4 position : POSITION) : SV_POSITION { return position; }"),
            ShaderCodeEXT(CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Fragment,
                          "main", "native.frag.hlsl",
                          "float4 main() : SV_Target { return float4(1, 0, 0, 1); }"),
        },
        {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});
    ShaderEffect effect(device, package);
    EXPECT_EQ(effect.GetSelectedShaderLanguageEXT(), CNA::ShaderLanguageEXT::Hlsl);
    EXPECT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();
}

TEST(ShaderPackageOverloadTest, AnHlslDialectDeclaresItsExecutableGraphicsStages)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    if (device.GetShaderDialectEXT() !=
        CNA::Internal::Renderers::ShaderDialectEXT::Hlsl)
        GTEST_SKIP() << "this renderer does not compile HLSL shader sources";

    EXPECT_TRUE(device.SupportsShaderLanguageEXT(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Vertex));
    EXPECT_TRUE(device.SupportsShaderLanguageEXT(
        CNA::ShaderLanguageEXT::Hlsl, CNA::ShaderStageEXT::Fragment));
    EXPECT_FALSE(device.SupportsShaderLanguageEXT(
        CNA::ShaderLanguageEXT::Dxil, CNA::ShaderStageEXT::Vertex));
}

TEST(ShaderPackageOverloadTest, DerivedEffectCanRetainLegacyFallbackWhenNoVariantExists)
{
    CnaTest::RetroGraphics::HiDefDevice device;
    CNA::ShaderLanguageEXT unsupported = CNA::ShaderLanguageEXT::Unknown;
    for (const auto candidate : {CNA::ShaderLanguageEXT::GlslDesktop,
                                 CNA::ShaderLanguageEXT::GlslEs,
                                 CNA::ShaderLanguageEXT::GlslVulkan,
                                 CNA::ShaderLanguageEXT::Hlsl,
                                 CNA::ShaderLanguageEXT::Msl,
                                 CNA::ShaderLanguageEXT::Wgsl,
                                 CNA::ShaderLanguageEXT::SpirV,
                                 CNA::ShaderLanguageEXT::Dxil})
    {
        if (!device.SupportsShaderLanguageEXT(candidate, CNA::ShaderStageEXT::Vertex)
            && !device.SupportsShaderLanguageEXT(candidate, CNA::ShaderStageEXT::Fragment))
        {
            unsupported = candidate;
            break;
        }
    }
    if (unsupported == CNA::ShaderLanguageEXT::Unknown)
        GTEST_SKIP() << "this renderer accepts every declared graphics shader language";

    const auto makeCode = [unsupported](const CNA::ShaderStageEXT stage) {
        if (unsupported == CNA::ShaderLanguageEXT::SpirV
            || unsupported == CNA::ShaderLanguageEXT::Dxil)
        {
            return ShaderCodeEXT(
                unsupported, stage, "main", "unsupported.bin",
                std::vector<std::uint8_t>{1, 2, 3, 4});
        }
        return ShaderCodeEXT(
            unsupported, stage, "main", "unsupported.txt", "unsupported source");
    };
    const ShaderPackageEXT package(
        {makeCode(CNA::ShaderStageEXT::Vertex),
         makeCode(CNA::ShaderStageEXT::Fragment)},
        {CNA::ShaderStageEXT::Vertex, CNA::ShaderStageEXT::Fragment});

    std::unique_ptr<FallbackShaderEffect> effect;
    EXPECT_NO_THROW(effect = std::make_unique<FallbackShaderEffect>(device, package));
    ASSERT_NE(effect, nullptr);
    EXPECT_EQ(effect->GetSelectedShaderLanguageEXT(), CNA::ShaderLanguageEXT::Unknown);
}

// =====================================================================================
// MOD-210: compiled once per name, per device
// =====================================================================================

} // namespace
#endif // CNA_CNAEXT
