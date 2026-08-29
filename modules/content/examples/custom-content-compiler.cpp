// SPDX-License-Identifier: MS-PL

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;

namespace
{
    constexpr const char* kAssetTypeName = "ExampleGame.Greeting";
    constexpr const char* kImportedType = "ExampleGame.Pipeline.ImportedGreeting";
    constexpr const char* kProcessedType = "ExampleGame.Pipeline.ProcessedGreeting";
    constexpr Cnb::CnbChunkId kTextChunk = Cnb::MakeChunkId('T', 'X', 'T', '0');

    struct ImportedGreeting
    {
        std::string text;
    };

    struct ProcessedGreeting
    {
        std::string text;
    };

    std::uint32_t GreetingAssetTypeId()
    {
        return Cnb::CnbAssetTypeIdFromName(kAssetTypeName);
    }

    std::vector<std::uint8_t> EncodeGreetingToCnb(const ProcessedGreeting& greeting,
                                                  const std::string& logicalName)
    {
        Cnb::CnbByteWriter payload;
        payload.WriteString(greeting.text);

        Cnb::CnbWriter writer(GreetingAssetTypeId(), 1u);
        writer.SetMetadata(kAssetTypeName, logicalName);
        writer.AddChunk(kTextChunk, payload.Take(), Cnb::CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }

    class GreetingImporter final : public Pipeline::ContentImporter
    {
    public:
        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"ExampleGame.GreetingImporter", "1"};
        }

        [[nodiscard]] std::vector<std::string> SourceExtensions() const override
        {
            return {".greeting"};
        }

        [[nodiscard]] std::vector<std::string> OutputTypes() const override
        {
            return {kImportedType};
        }

        [[nodiscard]] Pipeline::ContentValue Import(
            Pipeline::ContentImporterContext& context) const override
        {
            std::ifstream source(context.SourcePath(), std::ios::binary);
            if (!source)
            {
                throw std::runtime_error("cannot open greeting source.");
            }
            std::string text{std::istreambuf_iterator<char>(source),
                             std::istreambuf_iterator<char>()};
            if (source.bad())
            {
                throw std::runtime_error("cannot read greeting source completely.");
            }
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            {
                text.pop_back();
            }
            if (text.empty()) { throw std::runtime_error("greeting source must not be empty."); }
            context.LogInfo("read the custom greeting source.");
            return Pipeline::ContentValue::Create(kImportedType,
                                                  ImportedGreeting{std::move(text)});
        }
    };

    class GreetingProcessor final : public Pipeline::ContentProcessor
    {
    public:
        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"ExampleGame.GreetingProcessor", "2"};
        }

        [[nodiscard]] std::string InputType() const override { return kImportedType; }

        [[nodiscard]] std::string OutputType() const override { return kProcessedType; }

        void ValidateParameters(
            const Pipeline::ContentProcessorParameters& parameters) const override
        {
            for (const auto& [name, value] : parameters.Values())
            {
                if ((name != "prefix" && name != "dependsOn") ||
                    !std::holds_alternative<std::string>(value))
                {
                    throw std::invalid_argument(
                        "GreetingProcessor accepts only the string parameters 'prefix' and "
                        "'dependsOn'.");
                }
            }
        }

        [[nodiscard]] Pipeline::ContentValue Process(
            const Pipeline::ContentValue& input,
            Pipeline::ContentProcessorContext& context) const override
        {
            std::string text;
            if (const Pipeline::ContentProcessorParameterValue* prefix =
                    context.Parameters().Find("prefix"))
            {
                text = std::get<std::string>(*prefix);
            }
            text += input.Get<ImportedGreeting>().text;
            if (const Pipeline::ContentProcessorParameterValue* dependency =
                    context.Parameters().Find("dependsOn"))
            {
                context.AddContentBuildDependency(std::get<std::string>(*dependency));
            }
            context.LogInfo("applied the custom greeting policy.");
            return Pipeline::ContentValue::Create(kProcessedType,
                                                  ProcessedGreeting{std::move(text)});
        }
    };

    class GreetingWriter final : public Pipeline::ContentTypeWriter
    {
    public:
        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"ExampleGame.GreetingWriter", "2"};
        }

        [[nodiscard]] std::string InputType() const override { return kProcessedType; }

        [[nodiscard]] Pipeline::ContentWriteResult Write(
            const Pipeline::ContentValue& input,
            const std::string& logicalName) const override
        {
            const ProcessedGreeting& greeting = input.Get<ProcessedGreeting>();
            Pipeline::ContentWriteResult result{
                EncodeGreetingToCnb(greeting, logicalName), GreetingAssetTypeId(), kAssetTypeName};
            const std::string replyLogicalName = "Generated/" + logicalName + "-reply";
            const ProcessedGreeting reply{"Reply: " + greeting.text};
            result.additionalOutputs.push_back(
                {replyLogicalName, EncodeGreetingToCnb(reply, replyLogicalName),
                 GreetingAssetTypeId(), kAssetTypeName});
            return result;
        }
    };

    template<typename Character>
    int Run(int argc, Character** argv)
    {
        std::vector<std::filesystem::path> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0u);
        for (int index = 1; index < argc; ++index) { arguments.emplace_back(argv[index]); }

        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterBuiltInContentPipeline(*registry);
        registry->RegisterImporter(std::make_shared<GreetingImporter>());
        registry->RegisterProcessor(std::make_shared<GreetingProcessor>());
        registry->RegisterWriter(std::make_shared<GreetingWriter>());
        return Pipeline::RunContentCompiler(arguments, std::move(registry));
    }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    return Run(argc, argv);
}
#else
int main(int argc, char** argv)
{
    return Run(argc, argv);
}
#endif
