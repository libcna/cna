// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    struct ImportedNumber
    {
        int value = 0;
    };

    struct ProcessedNumber
    {
        int value = 0;
    };

    constexpr const char* kImportedType = "test.imported-number.v1";
    constexpr const char* kProcessedType = "test.processed-number.v1";

    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_content_pipeline_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }

    class CollectingLogger final : public Pipeline::ContentBuildLogger
    {
    public:
        void Log(const Pipeline::ContentLogMessage& message) override
        {
            messages.push_back(message);
        }

        std::vector<Pipeline::ContentLogMessage> messages;
    };

    class NumberImporter final : public Pipeline::ContentImporter
    {
    public:
        explicit NumberImporter(std::string name = "test.NumberImporter",
                                std::string extension = ".num",
                                std::string returnedType = kImportedType,
                                std::filesystem::path dependency = {},
                                std::vector<std::string> outputTypes = {})
            : name_(std::move(name)), extension_(std::move(extension)),
              returnedType_(std::move(returnedType)), dependency_(std::move(dependency)),
              outputTypes_(std::move(outputTypes))
        {
            if (outputTypes_.empty()) { outputTypes_.push_back(kImportedType); }
        }

        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {name_, "1"};
        }

        [[nodiscard]] std::vector<std::string> SourceExtensions() const override
        {
            return {extension_};
        }

        [[nodiscard]] std::vector<std::string> OutputTypes() const override
        {
            return outputTypes_;
        }

        [[nodiscard]] Pipeline::ContentValue Import(
            Pipeline::ContentImporterContext& context) const override
        {
            if (!dependency_.empty())
            {
                (void)context.ResolveSourceDependency(dependency_);
            }
            context.LogInfo("read number");
            return Pipeline::ContentValue::Create(returnedType_, ImportedNumber{7});
        }

    private:
        std::string name_;
        std::string extension_;
        std::string returnedType_;
        std::filesystem::path dependency_;
        std::vector<std::string> outputTypes_;
    };

    class NumberProcessor final : public Pipeline::ContentProcessor
    {
    public:
        explicit NumberProcessor(std::string name = "test.NumberProcessor")
            : name_(std::move(name))
        {
        }

        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {name_, "2"};
        }

        [[nodiscard]] std::string InputType() const override
        {
            return kImportedType;
        }

        [[nodiscard]] std::string OutputType() const override
        {
            return kProcessedType;
        }

        void ValidateParameters(
            const Pipeline::ContentProcessorParameters& parameters) const override
        {
            for (const auto& [name, value] : parameters.Values())
            {
                if (name != "multiplier")
                {
                    throw std::invalid_argument("unknown parameter '" + name + "'.");
                }
                const std::uint64_t* multiplier = std::get_if<std::uint64_t>(&value);
                if (multiplier == nullptr || *multiplier == 0u || *multiplier > 100u)
                {
                    throw std::invalid_argument("multiplier must be an unsigned integer 1..100.");
                }
            }
        }

        [[nodiscard]] Pipeline::ContentValue Process(
            const Pipeline::ContentValue& input,
            Pipeline::ContentProcessorContext& context) const override
        {
            std::uint64_t multiplier = 1u;
            if (const auto* value = context.Parameters().Find("multiplier"))
            {
                multiplier = std::get<std::uint64_t>(*value);
            }
            context.AddContentBuildDependency("Shared/table");
            context.AddRuntimeReference("Runtime/lookup", 42u);
            context.LogWarning("test warning");
            return Pipeline::ContentValue::Create(
                kProcessedType,
                ProcessedNumber{input.Get<ImportedNumber>().value *
                                static_cast<int>(multiplier)});
        }

    private:
        std::string name_;
    };

    class NumberWriter final : public Pipeline::ContentTypeWriter
    {
    public:
        enum class OutputBehavior
        {
            PrimaryOnly,
            ValidAdditional,
            DuplicateName,
            TraversalName,
            EmptyAdditional,
            TooMany,
            EmptySchemaDeclarations,
            DuplicateSchemaDeclarations,
            UnsortedSchemaDeclarations,
            UndeclaredPrimaryIdentity,
        };

        explicit NumberWriter(std::string name = "test.NumberWriter",
                              OutputBehavior behavior = OutputBehavior::PrimaryOnly)
            : name_(std::move(name)), behavior_(behavior)
        {
        }

        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {name_, "3"};
        }

        [[nodiscard]] std::vector<Pipeline::ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override
        {
            if (behavior_ == OutputBehavior::EmptySchemaDeclarations) { return {}; }
            if (behavior_ == OutputBehavior::DuplicateSchemaDeclarations)
            {
                return {{42u, 1u, "Test.ProcessedNumber", {"Test.NumberCodec", "1"}},
                        {42u, 2u, "Test.ProcessedNumber", {"Test.NumberCodec", "2"}}};
            }
            if (behavior_ == OutputBehavior::UnsortedSchemaDeclarations)
            {
                return {{43u, 1u, "Test.NumberIndex", {"Test.NumberIndexCodec", "1"}},
                        {42u, 1u, "Test.ProcessedNumber", {"Test.NumberCodec", "1"}}};
            }
            if (behavior_ == OutputBehavior::UndeclaredPrimaryIdentity)
            {
                return {{43u, 1u, "Test.NumberIndex", {"Test.NumberIndexCodec", "1"}}};
            }
            return {{42u, 1u, "Test.ProcessedNumber", {"Test.NumberCodec", "1"}},
                    {43u, 1u, "Test.NumberIndex", {"Test.NumberIndexCodec", "1"}}};
        }

        [[nodiscard]] std::string InputType() const override
        {
            return kProcessedType;
        }

        [[nodiscard]] Pipeline::ContentWriteResult Write(
            const Pipeline::ContentValue& input, const std::string& logicalName) const override
        {
            const ProcessedNumber& number = input.Get<ProcessedNumber>();
            Pipeline::ContentWriteResult result{
                {static_cast<std::uint8_t>(number.value),
                 static_cast<std::uint8_t>(logicalName.size())},
                42u,
                "Test.ProcessedNumber"};
            if (behavior_ == OutputBehavior::ValidAdditional)
            {
                result.additionalOutputs.push_back(
                    {logicalName + "-index", {1u, 2u}, 43u, "Test.NumberIndex"});
            }
            else if (behavior_ == OutputBehavior::DuplicateName)
            {
                result.additionalOutputs.push_back(
                    {logicalName, {1u}, 43u, "Test.NumberIndex"});
            }
            else if (behavior_ == OutputBehavior::TraversalName)
            {
                result.additionalOutputs.push_back(
                    {"../escape", {1u}, 43u, "Test.NumberIndex"});
            }
            else if (behavior_ == OutputBehavior::EmptyAdditional)
            {
                result.additionalOutputs.push_back(
                    {logicalName + "-index", {}, 43u, "Test.NumberIndex"});
            }
            else if (behavior_ == OutputBehavior::TooMany)
            {
                for (std::size_t index = 0u; index < Pipeline::MaxContentBuildOutputs; ++index)
                {
                    result.additionalOutputs.push_back(
                        {logicalName + "-" + std::to_string(index), {1u}, 43u,
                         "Test.NumberIndex"});
                }
            }
            return result;
        }

    private:
        std::string name_;
        OutputBehavior behavior_ = OutputBehavior::PrimaryOnly;
    };

    class DeploymentProcessor final : public Pipeline::ContentProcessor
    {
    public:
        explicit DeploymentProcessor(
            std::vector<std::pair<std::filesystem::path, std::string>> files)
            : files_(std::move(files))
        {
        }

        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"test.DeploymentProcessor", "1"};
        }

        [[nodiscard]] std::string InputType() const override { return kImportedType; }
        [[nodiscard]] std::string OutputType() const override { return kProcessedType; }

        void ValidateParameters(const Pipeline::ContentProcessorParameters&) const override {}

        [[nodiscard]] Pipeline::ContentValue Process(
            const Pipeline::ContentValue& input,
            Pipeline::ContentProcessorContext& context) const override
        {
            for (const auto& [source, output] : files_)
            {
                context.AddDeploymentFile(source, output);
            }
            return Pipeline::ContentValue::Create(
                kProcessedType, ProcessedNumber{input.Get<ImportedNumber>().value});
        }

    private:
        std::vector<std::pair<std::filesystem::path, std::string>> files_;
    };

    class ExternalDeploymentProcessor final : public Pipeline::ContentProcessor
    {
    public:
        ExternalDeploymentProcessor(std::filesystem::path authored, std::string output)
            : authored_(std::move(authored)), output_(std::move(output)) {}

        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"test.ExternalDeploymentProcessor", "1"};
        }

        [[nodiscard]] std::string InputType() const override { return kImportedType; }
        [[nodiscard]] std::string OutputType() const override { return kProcessedType; }

        void ValidateParameters(const Pipeline::ContentProcessorParameters&) const override {}

        [[nodiscard]] Pipeline::ContentValue Process(
            const Pipeline::ContentValue& input,
            Pipeline::ContentProcessorContext& context) const override
        {
            const std::filesystem::path source =
                context.ResolveSourceDependency(authored_);
            context.AddDeploymentFile(source, output_);
            return Pipeline::ContentValue::Create(
                kProcessedType, ProcessedNumber{input.Get<ImportedNumber>().value});
        }

    private:
        std::filesystem::path authored_;
        std::string output_;
    };

    std::shared_ptr<Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        registry->RegisterImporter(std::make_shared<NumberImporter>());
        registry->RegisterProcessor(std::make_shared<NumberProcessor>());
        registry->RegisterWriter(std::make_shared<NumberWriter>());
        return registry;
    }

    Pipeline::ContentBuildRequest MakeRequest(const ScratchDirectory& scratch)
    {
        WriteText(scratch.Path() / "asset.num", "7");
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = scratch.Path();
        request.source = "asset.num";
        request.logicalName = "Numbers/asset";
        return request;
    }
}

TEST(ContentPipelineCoreTest, RegistryRejectsDuplicateStableComponentNames)
{
    Pipeline::ContentPipelineRegistry registry;
    registry.RegisterImporter(std::make_shared<NumberImporter>());
    EXPECT_THROW(registry.RegisterImporter(std::make_shared<NumberImporter>()), std::logic_error);

    registry.RegisterProcessor(std::make_shared<NumberProcessor>());
    EXPECT_THROW(registry.RegisterProcessor(std::make_shared<NumberProcessor>()), std::logic_error);

    registry.RegisterWriter(std::make_shared<NumberWriter>());
    EXPECT_THROW(registry.RegisterWriter(std::make_shared<NumberWriter>()), std::logic_error);

    Pipeline::ContentPipelineRegistry malformed;
    EXPECT_THROW(
        malformed.RegisterImporter(std::make_shared<NumberImporter>(
            "test.DuplicateOutputs", ".dup", kImportedType, std::filesystem::path{},
            std::vector<std::string>{kImportedType, kImportedType})),
        std::invalid_argument);
}

TEST(ContentPipelineCoreTest, SourceRootCapabilitiesValidateAliasesAndCanonicalOverlap)
{
    EXPECT_TRUE(Pipeline::ContentSourceRootAliasProblem("shared-textures").empty());
    EXPECT_FALSE(Pipeline::ContentSourceRootAliasProblem("").empty());
    EXPECT_FALSE(Pipeline::ContentSourceRootAliasProblem("Shared").empty());
    EXPECT_FALSE(Pipeline::ContentSourceRootAliasProblem("1shared").empty());
    EXPECT_FALSE(Pipeline::ContentSourceRootAliasProblem("shared_root").empty());

    Pipeline::ContentSourceRootCapabilities roots;
    EXPECT_TRUE(roots.Empty());
    roots.Add("shared", "../Shared");
    EXPECT_FALSE(roots.Empty());
    EXPECT_EQ(roots.Entries().size(), 1u);
    ASSERT_NE(roots.Find("shared"), nullptr);
    EXPECT_THROW(roots.Add("shared", "../Other"), std::invalid_argument);
    EXPECT_THROW(roots.Add("Bad", "../Other"), std::invalid_argument);
    EXPECT_THROW(roots.Add("other", {}), std::invalid_argument);

    ScratchDirectory scratch("external_root_validation");
    const std::filesystem::path source = scratch.Path() / "Source";
    const std::filesystem::path shared = scratch.Path() / "Shared";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(shared);
    const Pipeline::ContentSourceRootCapabilities resolved =
        Pipeline::ResolveContentSourceRootCapabilities(source, roots);
    EXPECT_EQ(*resolved.Find("shared"), std::filesystem::weakly_canonical(shared));

    Pipeline::ContentSourceRootCapabilities overlap;
    overlap.Add("nested", source / "nested");
    std::filesystem::create_directories(source / "nested");
    EXPECT_THROW(
        (void)Pipeline::ResolveContentSourceRootCapabilities(source, overlap),
        std::invalid_argument);

    WriteText(scratch.Path() / "not-a-root", "file");
    Pipeline::ContentSourceRootCapabilities fileRoot;
    fileRoot.Add("file", scratch.Path() / "not-a-root");
    EXPECT_THROW(
        (void)Pipeline::ResolveContentSourceRootCapabilities(source, fileRoot),
        std::invalid_argument);

    Pipeline::ContentSourceRootCapabilities duplicatePhysical;
    duplicatePhysical.Add("one", shared);
    duplicatePhysical.Add("two", shared);
    EXPECT_THROW(
        (void)Pipeline::ResolveContentSourceRootCapabilities(source, duplicatePhysical),
        std::invalid_argument);
}

TEST(ContentPipelineCoreTest, RegistryFreezesBeforeBuildAndRejectsEveryLaterRegistration)
{
    auto registry = MakeRegistry();
    EXPECT_FALSE(registry->IsFrozen());

    const Pipeline::ContentPipeline pipeline(registry);
    EXPECT_TRUE(registry->IsFrozen());
    registry->Freeze();
    EXPECT_TRUE(registry->IsFrozen());

    EXPECT_THROW(
        registry->RegisterImporter(
            std::make_shared<NumberImporter>("test.LateImporter", ".late")),
        std::logic_error);
    EXPECT_THROW(
        registry->RegisterProcessor(std::make_shared<NumberProcessor>("test.LateProcessor")),
        std::logic_error);
    EXPECT_THROW(registry->RegisterWriter(std::make_shared<NumberWriter>("test.LateWriter")),
                 std::logic_error);

    ScratchDirectory scratch("frozen_registry");
    const Pipeline::ContentBuildResult result = pipeline.Build(MakeRequest(scratch));
    EXPECT_EQ(result.logicalName, "Numbers/asset");
}

TEST(ContentPipelineCoreTest, FrozenRegistrySupportsConcurrentBuildCalls)
{
    auto registry = MakeRegistry();
    const Pipeline::ContentPipeline pipeline(registry);
    ScratchDirectory scratch("concurrent_builds");

    std::vector<std::future<Pipeline::ContentBuildResult>> builds;
    for (int index = 0; index < 16; ++index)
    {
        const std::string fileName = "asset-" + std::to_string(index) + ".num";
        WriteText(scratch.Path() / fileName, std::to_string(index));
        builds.push_back(std::async(std::launch::async, [&, index, fileName]
        {
            Pipeline::ContentBuildRequest request;
            request.sourceRoot = scratch.Path();
            request.source = fileName;
            request.logicalName = "Numbers/" + std::to_string(index);
            return pipeline.Build(request);
        }));
    }

    for (int index = 0; index < 16; ++index)
    {
        const Pipeline::ContentBuildResult result = builds[index].get();
        EXPECT_EQ(result.logicalName, "Numbers/" + std::to_string(index));
        EXPECT_EQ(result.output.assetTypeId, 42u);
    }
}

TEST(ContentPipelineCoreTest, RegistryNeverUsesRegistrationOrderToResolveAnAmbiguity)
{
    Pipeline::ContentPipelineRegistry registry;
    // Deliberately register reverse lexical order. The diagnostic must still be stable and no
    // "last registered wins" behavior may select either component.
    registry.RegisterImporter(
        std::make_shared<NumberImporter>("test.ZImporter", ".num"));
    registry.RegisterImporter(
        std::make_shared<NumberImporter>("test.AImporter", ".num"));

    try
    {
        (void)registry.ResolveImporter("asset.num");
        FAIL() << "ambiguous importer route resolved without an explicit name";
    }
    catch (const std::logic_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("ambiguous importer"), std::string::npos);
        EXPECT_LT(message.find("test.AImporter"), message.find("test.ZImporter"));
    }

    EXPECT_EQ(registry.ResolveImporter("asset.num", "test.AImporter")->Identity().name,
              "test.AImporter");
    EXPECT_THROW((void)registry.ResolveImporter("asset.wav", "test.AImporter"), std::logic_error);
}

TEST(ContentPipelineCoreTest, ProcessorAndWriterAmbiguitiesRequireExplicitStableNames)
{
    Pipeline::ContentPipelineRegistry registry;
    registry.RegisterProcessor(std::make_shared<NumberProcessor>("test.AProcessor"));
    registry.RegisterProcessor(std::make_shared<NumberProcessor>("test.BProcessor"));
    EXPECT_THROW((void)registry.ResolveProcessor(kImportedType), std::logic_error);
    EXPECT_EQ(registry.ResolveProcessor(kImportedType, "test.BProcessor")->Identity().name,
              "test.BProcessor");

    registry.RegisterWriter(std::make_shared<NumberWriter>("test.AWriter"));
    registry.RegisterWriter(std::make_shared<NumberWriter>("test.BWriter"));
    EXPECT_THROW((void)registry.ResolveWriter(kProcessedType), std::logic_error);
    EXPECT_EQ(registry.ResolveWriter(kProcessedType, "test.AWriter")->Identity().name,
              "test.AWriter");
}

TEST(ContentPipelineCoreTest, UnknownExtensionsAndMissingRoutesAreNamedClearly)
{
    Pipeline::ContentPipelineRegistry registry;
    registry.RegisterImporter(std::make_shared<NumberImporter>());
    EXPECT_TRUE(registry.HasImporterForSource("ASSET.NUM"));
    EXPECT_FALSE(registry.HasImporterForSource("asset.unknown"));
    EXPECT_THROW((void)registry.ResolveImporter("asset.unknown"), std::logic_error);
    EXPECT_THROW((void)registry.ResolveProcessor("unknown.imported.type"), std::logic_error);
    EXPECT_THROW((void)registry.ResolveWriter("unknown.processed.type"), std::logic_error);
}

TEST(ContentPipelineCoreTest, ValueChecksTheConcreteCppTypeWithoutPersistingRttiIdentity)
{
    const Pipeline::ContentValue value =
        Pipeline::ContentValue::Create("stable.example.v1", ImportedNumber{9});
    EXPECT_EQ(value.StableType(), "stable.example.v1");
    EXPECT_EQ(value.Get<ImportedNumber>().value, 9);
    EXPECT_THROW((void)value.Get<ProcessedNumber>(), std::logic_error);
}

TEST(ContentPipelineCoreTest, BuildReportsComponentsParametersDependenciesReferencesAndLogs)
{
    ScratchDirectory scratch("flow");
    Pipeline::ContentBuildRequest request = MakeRequest(scratch);
    WriteText(scratch.Path() / "sidecar.txt", "dependency");

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(
        std::make_shared<NumberImporter>("test.NumberImporter", ".num", kImportedType,
                                         "sidecar.txt"));
    registry->RegisterProcessor(std::make_shared<NumberProcessor>());
    registry->RegisterWriter(std::make_shared<NumberWriter>());

    CollectingLogger logger;
    request.logger = &logger;
    request.parameters.Set("multiplier", std::uint64_t{4u});

    const Pipeline::ContentBuildResult result = Pipeline::ContentPipeline(registry).Build(request);
    EXPECT_TRUE(result.built);
    EXPECT_EQ(result.logicalName, "Numbers/asset");
    EXPECT_EQ(result.importer,
              (Pipeline::ContentComponentIdentity{"test.NumberImporter", "1"}));
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"test.NumberProcessor", "2"}));
    EXPECT_EQ(result.writer, (Pipeline::ContentComponentIdentity{"test.NumberWriter", "3"}));
    ASSERT_EQ(result.output.bytes.size(), 2u);
    EXPECT_EQ(result.output.bytes[0], 28u);
    EXPECT_EQ(result.output.assetTypeId, 42u);

    ASSERT_EQ(result.dependencies.size(), 3u);
    EXPECT_EQ(result.dependencies[0].kind, Pipeline::ContentDependencyKind::PrimarySource);
    EXPECT_EQ(result.dependencies[1].kind, Pipeline::ContentDependencyKind::SourceFile);
    EXPECT_EQ(result.dependencies[2],
              (Pipeline::ContentDependency{Pipeline::ContentDependencyKind::ContentBuild,
                                           "Shared/table"}));

    ASSERT_EQ(result.runtimeReferences.size(), 1u);
    EXPECT_EQ(result.runtimeReferences[0],
              (Pipeline::RuntimeContentReference{"Runtime/lookup", 42u}));
    EXPECT_TRUE(std::none_of(
        result.dependencies.begin(), result.dependencies.end(),
        [](const Pipeline::ContentDependency& dependency)
        { return dependency.identity == "Runtime/lookup"; }))
        << "runtime XREF leaked into the build-dependency list";

    ASSERT_EQ(logger.messages.size(), 2u);
    EXPECT_EQ(result.messages, logger.messages);
    EXPECT_EQ(logger.messages[0].stage, Pipeline::ContentPipelineStage::Import);
    EXPECT_EQ(logger.messages[0].component, "test.NumberImporter");
    EXPECT_EQ(logger.messages[1].stage, Pipeline::ContentPipelineStage::Process);
    EXPECT_EQ(logger.messages[1].component, "test.NumberProcessor");
}

TEST(ContentPipelineCoreTest, ProcessorDeploymentFilesAreContainedDeduplicatedAndFingerprintable)
{
    ScratchDirectory scratch("deployment_files");
    Pipeline::ContentBuildRequest request = MakeRequest(scratch);
    WriteText(scratch.Path() / "media.bin", "streaming bytes");
    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<NumberImporter>());
    registry->RegisterProcessor(std::make_shared<DeploymentProcessor>(
        std::vector<std::pair<std::filesystem::path, std::string>>{
            {"media.bin", "Support/media.bin"},
            {"media.bin", "Support/media.bin"},
        }));
    registry->RegisterWriter(std::make_shared<NumberWriter>());

    const Pipeline::ContentBuildResult result =
        Pipeline::ContentPipeline(registry).Build(request);
    ASSERT_EQ(result.deploymentFiles.size(), 1u);
    EXPECT_EQ(result.deploymentFiles[0].source, scratch.Path() / "media.bin");
    EXPECT_EQ(result.deploymentFiles[0].outputPath, "Support/media.bin");
    ASSERT_EQ(result.dependencies.size(), 2u);
    EXPECT_EQ(result.dependencies[1],
              (Pipeline::ContentDependency{Pipeline::ContentDependencyKind::SourceFile,
                                           CNA::Internal::ContentPathToUtf8(
                                               scratch.Path() / "media.bin")}));
}

TEST(ContentPipelineCoreTest, ProcessorDeploymentFilesRejectConflictsAndPathEscapes)
{
    ScratchDirectory scratch("deployment_file_errors");
    ScratchDirectory outside("deployment_file_outside");
    Pipeline::ContentBuildRequest request = MakeRequest(scratch);
    WriteText(scratch.Path() / "a.bin", "a");
    WriteText(scratch.Path() / "b.bin", "b");
    WriteText(outside.Path() / "outside.bin", "outside");

    const auto build = [&](std::vector<std::pair<std::filesystem::path, std::string>> files)
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        registry->RegisterImporter(std::make_shared<NumberImporter>());
        registry->RegisterProcessor(
            std::make_shared<DeploymentProcessor>(std::move(files)));
        registry->RegisterWriter(std::make_shared<NumberWriter>());
        return Pipeline::ContentPipeline(registry).Build(request);
    };

    for (auto files :
         {std::vector<std::pair<std::filesystem::path, std::string>>{
              {"a.bin", "Support/shared.bin"}, {"b.bin", "Support/shared.bin"}},
          std::vector<std::pair<std::filesystem::path, std::string>>{
              {"a.bin", "../escape.bin"}},
          std::vector<std::pair<std::filesystem::path, std::string>>{
              {outside.Path() / "outside.bin", "Support/outside.bin"}},
          std::vector<std::pair<std::filesystem::path, std::string>>{
              {"missing.bin", "Support/missing.bin"}}})
    {
        try
        {
            static_cast<void>(build(std::move(files)));
            FAIL() << "invalid deployment mapping was accepted";
        }
        catch (const Pipeline::ContentPipelineError& error)
        {
            EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
            EXPECT_EQ(error.Component(), "test.DeploymentProcessor");
        }
    }
}

TEST(ContentPipelineCoreTest, ExplicitExternalDependencyCanBeHashedAndDeployedByStableAlias)
{
    ScratchDirectory scratch("external_dependency_deployment");
    const std::filesystem::path sourceRoot = scratch.Path() / "Source";
    const std::filesystem::path sharedA = scratch.Path() / "SharedA";
    const std::filesystem::path sharedB = scratch.Path() / "SharedB";
    const std::filesystem::path outputRoot = scratch.Path() / "Content";
    WriteText(sourceRoot / "asset.num", "7");
    WriteText(sharedA / "data" / "support.bin", "shared bytes");
    WriteText(sharedB / "data" / "support.bin", "shared bytes");
    std::filesystem::create_directories(outputRoot);

    Pipeline::ContentBuildRequest request;
    request.sourceRoot = sourceRoot;
    request.source = "asset.num";
    request.logicalName = "Numbers/asset";
    request.externalSourceRoots.Add("shared", sharedA);

    auto bypassRegistry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    bypassRegistry->RegisterImporter(std::make_shared<NumberImporter>());
    bypassRegistry->RegisterProcessor(std::make_shared<DeploymentProcessor>(
        std::vector<std::pair<std::filesystem::path, std::string>>{
            {sharedA / "data" / "support.bin", "Support/support.bin"}}));
    bypassRegistry->RegisterWriter(std::make_shared<NumberWriter>());
    EXPECT_THROW((void)Pipeline::ContentPipeline(bypassRegistry).Build(request),
                 Pipeline::ContentPipelineError)
        << "configured containment alone granted deployment without explicit resolution";

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<NumberImporter>());
    registry->RegisterProcessor(std::make_shared<ExternalDeploymentProcessor>(
        "@shared/data/support.bin", "Support/support.bin"));
    registry->RegisterWriter(std::make_shared<NumberWriter>());
    const Pipeline::ContentBuildResult result =
        Pipeline::ContentPipeline(registry).Build(request);

    ASSERT_EQ(result.dependencies.size(), 2u);
    EXPECT_EQ(result.dependencies[1].kind, Pipeline::ContentDependencyKind::SourceFile);
    EXPECT_EQ(result.dependencies[1].sourceRoot, "shared");
    EXPECT_EQ(CNA::Internal::ContentPathFromUtf8(result.dependencies[1].identity),
              std::filesystem::weakly_canonical(sharedA / "data" / "support.bin"));
    ASSERT_EQ(result.deploymentFiles.size(), 1u);
    EXPECT_EQ(result.deploymentFiles[0].sourceRoot, "shared");

    const Pipeline::ContentSourceRootCapabilities resolvedA =
        Pipeline::ResolveContentSourceRootCapabilities(sourceRoot,
                                                       request.externalSourceRoots);
    Pipeline::ContentBuildManifestEntry entry =
        Pipeline::MakeContentBuildManifestEntry(
            result, sourceRoot, outputRoot, outputRoot / "Numbers" / "asset.cnb", resolvedA);
    ASSERT_EQ(entry.dependencies.size(), 2u);
    EXPECT_EQ(entry.dependencies[1].sourceRoot, "shared");
    EXPECT_EQ(entry.dependencies[1].identity, "data/support.bin");
    ASSERT_EQ(entry.deploymentFiles.size(), 1u);
    EXPECT_EQ(entry.deploymentFiles[0].sourceRoot, "shared");
    EXPECT_EQ(entry.deploymentFiles[0].source, "data/support.bin");
    Pipeline::RefreshContentBuildDirectFingerprint(entry, sourceRoot, resolvedA);
    const std::string fingerprintA = entry.directFingerprint;

    Pipeline::ContentSourceRootCapabilities remapped;
    remapped.Add("shared", sharedB);
    const Pipeline::ContentSourceRootCapabilities resolvedB =
        Pipeline::ResolveContentSourceRootCapabilities(sourceRoot, remapped);
    EXPECT_EQ(Pipeline::ComputeContentBuildDirectFingerprint(
                  entry, sourceRoot, resolvedB),
              fingerprintA)
        << "physical root path leaked into semantic cache identity";

    WriteText(sharedB / "data" / "support.bin", "changed bytes");
    EXPECT_NE(Pipeline::ComputeContentBuildDirectFingerprint(
                  entry, sourceRoot, resolvedB),
              fingerprintA);
}

TEST(ContentPipelineCoreTest, ExternalReferencesRejectUnknownTraversalAbsoluteAndSymlinkEscape)
{
    ScratchDirectory scratch("external_dependency_rejection");
    const std::filesystem::path sourceRoot = scratch.Path() / "Source";
    const std::filesystem::path shared = scratch.Path() / "Shared";
    const std::filesystem::path outside = scratch.Path() / "Outside";
    WriteText(sourceRoot / "asset.num", "7");
    WriteText(shared / "safe.bin", "safe");
    WriteText(outside / "secret.bin", "secret");

    const auto build = [&](const std::string& dependency)
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        registry->RegisterImporter(std::make_shared<NumberImporter>(
            "test.ExternalImporter", ".num", kImportedType, dependency));
        registry->RegisterProcessor(std::make_shared<NumberProcessor>());
        registry->RegisterWriter(std::make_shared<NumberWriter>());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = sourceRoot;
        request.source = "asset.num";
        request.logicalName = "asset";
        request.externalSourceRoots.Add("shared", shared);
        return Pipeline::ContentPipeline(registry).Build(request);
    };

    for (const std::string& dependency : {
             "@unknown/safe.bin", "@shared/../Outside/secret.bin",
             "@shared//absolute.bin", "@shared/folder\\escape.bin",
             "/absolute/bypass.bin"})
    {
        EXPECT_THROW((void)build(dependency), Pipeline::ContentPipelineError)
            << dependency;
    }

    std::error_code error;
    std::filesystem::create_symlink(outside / "secret.bin", shared / "escape.bin", error);
    if (!error)
    {
        EXPECT_THROW((void)build("@shared/escape.bin"), Pipeline::ContentPipelineError);
    }
}

TEST(ContentPipelineCoreTest, BuildAcceptsBoundedExplicitlyNamedAdditionalOutputs)
{
    ScratchDirectory scratch("multiple_outputs");
    Pipeline::ContentBuildRequest request = MakeRequest(scratch);
    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<NumberImporter>());
    registry->RegisterProcessor(std::make_shared<NumberProcessor>());
    registry->RegisterWriter(std::make_shared<NumberWriter>(
        "test.MultiNumberWriter", NumberWriter::OutputBehavior::ValidAdditional));

    const Pipeline::ContentBuildResult result = Pipeline::ContentPipeline(registry).Build(request);
    ASSERT_EQ(result.output.additionalOutputs.size(), 1u);
    EXPECT_EQ(result.output.additionalOutputs.front().logicalName, "Numbers/asset-index");
    EXPECT_EQ(result.output.additionalOutputs.front().bytes,
              (std::vector<std::uint8_t>{1u, 2u}));
    EXPECT_EQ(result.output.additionalOutputs.front().assetTypeId, 43u);
}

TEST(ContentPipelineCoreTest, BuildRejectsUnsafeDuplicateEmptyAndUnboundedOutputsAtWriteStage)
{
    ScratchDirectory scratch("invalid_multiple_outputs");
    Pipeline::ContentBuildRequest request = MakeRequest(scratch);
    for (const NumberWriter::OutputBehavior behavior :
         {NumberWriter::OutputBehavior::DuplicateName,
          NumberWriter::OutputBehavior::TraversalName,
          NumberWriter::OutputBehavior::EmptyAdditional,
          NumberWriter::OutputBehavior::TooMany,
          NumberWriter::OutputBehavior::EmptySchemaDeclarations,
          NumberWriter::OutputBehavior::DuplicateSchemaDeclarations,
          NumberWriter::OutputBehavior::UnsortedSchemaDeclarations,
          NumberWriter::OutputBehavior::UndeclaredPrimaryIdentity})
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        registry->RegisterImporter(std::make_shared<NumberImporter>());
        registry->RegisterProcessor(std::make_shared<NumberProcessor>());
        registry->RegisterWriter(std::make_shared<NumberWriter>("test.BadOutputWriter", behavior));
        try
        {
            static_cast<void>(Pipeline::ContentPipeline(registry).Build(request));
            FAIL() << "invalid multi-output writer result was accepted";
        }
        catch (const Pipeline::ContentPipelineError& error)
        {
            EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Write);
            EXPECT_EQ(error.Component(), "test.BadOutputWriter");
        }
    }
}

TEST(ContentPipelineCoreTest, InvalidProcessorParametersFailAtTheProcessorBoundary)
{
    ScratchDirectory scratch("parameter");
    Pipeline::ContentBuildRequest request = MakeRequest(scratch);
    request.parameters.Set("unknown", true);

    try
    {
        (void)Pipeline::ContentPipeline(MakeRegistry()).Build(request);
        FAIL() << "unknown processor parameter was accepted";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
        EXPECT_EQ(error.Component(), "test.NumberProcessor");
        EXPECT_NE(std::string(error.what()).find("unknown parameter 'unknown'"),
                  std::string::npos);
    }
}

TEST(ContentPipelineCoreTest, AComponentCannotLieAboutItsStableOutputType)
{
    ScratchDirectory scratch("wrong_type");
    Pipeline::ContentBuildRequest request = MakeRequest(scratch);

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<NumberImporter>(
        "test.LyingImporter", ".num", "test.wrong-imported-type.v1"));
    registry->RegisterProcessor(std::make_shared<NumberProcessor>());
    registry->RegisterWriter(std::make_shared<NumberWriter>());

    try
    {
        (void)Pipeline::ContentPipeline(registry).Build(request);
        FAIL() << "importer returned a stable type other than the one it declared";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Import);
        EXPECT_EQ(error.Component(), "test.LyingImporter");
        EXPECT_NE(std::string(error.what()).find("declared output type"), std::string::npos);
    }
}

TEST(ContentPipelineCoreTest, PrimarySourceAndDependencyTraversalAreRejected)
{
    ScratchDirectory scratch("containment");
    const std::filesystem::path root = scratch.Path() / "root";
    std::filesystem::create_directories(root);
    WriteText(root / "asset.num", "7");
    WriteText(scratch.Path() / "outside.txt", "secret");

    Pipeline::ContentBuildRequest outsideRequest;
    outsideRequest.sourceRoot = root;
    outsideRequest.source = scratch.Path() / "outside.txt";
    outsideRequest.logicalName = "outside";
    EXPECT_THROW((void)Pipeline::ContentPipeline(MakeRegistry()).Build(outsideRequest),
                 Pipeline::ContentPipelineError);

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<NumberImporter>(
        "test.TraversingImporter", ".num", kImportedType, "../outside.txt"));
    registry->RegisterProcessor(std::make_shared<NumberProcessor>());
    registry->RegisterWriter(std::make_shared<NumberWriter>());

    Pipeline::ContentBuildRequest traversalRequest;
    traversalRequest.sourceRoot = root;
    traversalRequest.source = "asset.num";
    traversalRequest.logicalName = "asset";
    try
    {
        (void)Pipeline::ContentPipeline(registry).Build(traversalRequest);
        FAIL() << "dependency path traversal escaped the source root";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Import);
        EXPECT_NE(std::string(error.what()).find("outside source root"), std::string::npos);
    }
}

TEST(ContentPipelineCoreTest, ADependencySymlinkCannotEscapeTheSourceRoot)
{
    ScratchDirectory scratch("symlink");
    const std::filesystem::path root = scratch.Path() / "root";
    const std::filesystem::path outside = scratch.Path() / "outside";
    std::filesystem::create_directories(root);
    std::filesystem::create_directories(outside);
    WriteText(root / "asset.num", "7");
    WriteText(outside / "secret.txt", "secret");

    std::error_code ec;
    std::filesystem::create_directory_symlink(outside, root / "link", ec);
    if (ec)
    {
        GTEST_SKIP() << "directory symlinks are unavailable: " << ec.message();
    }

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<NumberImporter>(
        "test.SymlinkImporter", ".num", kImportedType, "link/secret.txt"));
    registry->RegisterProcessor(std::make_shared<NumberProcessor>());
    registry->RegisterWriter(std::make_shared<NumberWriter>());

    Pipeline::ContentBuildRequest request;
    request.sourceRoot = root;
    request.source = "asset.num";
    request.logicalName = "asset";
    EXPECT_THROW((void)Pipeline::ContentPipeline(registry).Build(request),
                 Pipeline::ContentPipelineError);
}
