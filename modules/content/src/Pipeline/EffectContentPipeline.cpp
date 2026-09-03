// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-84: the achievable half of the Effect story.
//
// Compiling `.fx` source to shader bytecode is a separate major subsystem that this project has
// deliberately not taken on (plans/plan_fx.md: "Out of scope; CNA will not embed an HLSL source
// compiler"). What is achievable, and what this file does, is the other half: a project that
// already has a compiled Effect Framework binary can build it into an `Effect` .xnb through the
// ordinary pipeline instead of hand-assembling a container.

#include "CNA/Content/Pipeline/EffectContentPipeline.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        constexpr const char* kImporterName = "CNA.CompiledEffectImporter";
        constexpr const char* kProcessorName = "CNA.CompiledEffectProcessor";

        // The Effect Framework 9.1 signature, and the wrapper XNA 4.0's own Content Pipeline puts
        // in front of it. Both are read by CNA's runtime Effect preflight; this is the same pair
        // of tokens, checked at build time so a bad file is named by its source path.
        constexpr std::uint32_t kEffectFrameworkToken = 0xFEFF0901u;
        constexpr std::uint32_t kXna4EffectWrapperToken = 0xBCF00BCFu;

        [[nodiscard]] std::uint32_t ReadUInt32(const std::vector<std::uint8_t>& bytes,
                                               const std::size_t offset)
        {
            return static_cast<std::uint32_t>(bytes[offset]) |
                   (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8) |
                   (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16) |
                   (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24);
        }
    }

    bool IsCompiledEffectBinary(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.size() < 24u) { return false; }
        std::size_t tokenOffset = 0u;
        const std::uint32_t outerToken = ReadUInt32(bytes, 0u);
        if (outerToken == kXna4EffectWrapperToken)
        {
            const std::uint32_t wrappedOffset = ReadUInt32(bytes, 4u);
            if (wrappedOffset < 8u || wrappedOffset > bytes.size() - 8u ||
                (wrappedOffset & 3u) != 0u)
            {
                return false;
            }
            tokenOffset = wrappedOffset;
        }
        else if (outerToken != kEffectFrameworkToken)
        {
            return false;
        }
        return ReadUInt32(bytes, tokenOffset) == kEffectFrameworkToken;
    }

    ContentComponentIdentity CompiledEffectImporter::Identity() const
    {
        return {kImporterName, "1"};
    }

    std::vector<std::string> CompiledEffectImporter::SourceExtensions() const
    {
        // `.fxb` only. `.fx` is HLSL source, and accepting it here would promise a compiler this
        // project does not have; the refusal belongs at the routing layer, where a build says
        // "nothing imports .fx", rather than inside a component that then fails.
        return {".fxb"};
    }

    std::vector<std::string> CompiledEffectImporter::OutputTypes() const
    {
        return {ImportedCompiledEffectType};
    }

    ContentValue CompiledEffectImporter::Import(ContentImporterContext& context) const
    {
        std::ifstream stream(context.SourcePath(), std::ios::binary);
        if (!stream) { throw ContentLoadException("cannot open compiled effect source."); }
        ImportedCompiledEffect imported;
        imported.bytecode.assign(std::istreambuf_iterator<char>(stream),
                                 std::istreambuf_iterator<char>());

        if (!IsCompiledEffectBinary(imported.bytecode))
        {
            throw ContentLoadException(
                "this file does not begin with an Effect Framework 9.1 signature, so it is not a "
                "compiled effect. CNA serializes compiled bytecode and does not compile HLSL: "
                "build the .fx with a compiler that targets the fx_2_0 profile and point this "
                "route at the resulting .fxb.");
        }
        context.LogInfo("read a " + std::to_string(imported.bytecode.size()) +
                        "-byte compiled effect binary.");
        return ContentValue::Create(ImportedCompiledEffectType, std::move(imported));
    }

    ContentComponentIdentity CompiledEffectProcessor::Identity() const
    {
        return {kProcessorName, "1"};
    }

    std::string CompiledEffectProcessor::InputType() const { return ImportedCompiledEffectType; }

    std::string CompiledEffectProcessor::OutputType() const { return ProcessedCompiledEffectType; }

    void CompiledEffectProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        if (!parameters.Empty())
        {
            throw std::invalid_argument(
                "CompiledEffectProcessor does not accept processor parameters: the bytecode is "
                "passed through exactly as compiled, and any transformation of it would be a "
                "shader compiler's job.");
        }
    }

    ContentValue CompiledEffectProcessor::Process(const ContentValue& input,
                                                  ContentProcessorContext& context) const
    {
        ImportedCompiledEffect imported = input.Get<ImportedCompiledEffect>();
        context.LogInfo("prepared " + std::to_string(imported.bytecode.size()) +
                        " bytes of compiled effect bytecode for encoding.");
        return ContentValue::Create(ProcessedCompiledEffectType, std::move(imported));
    }

    void RegisterCompiledEffectContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<CompiledEffectImporter>());
        registry.RegisterProcessor(std::make_shared<CompiledEffectProcessor>());
    }
}
