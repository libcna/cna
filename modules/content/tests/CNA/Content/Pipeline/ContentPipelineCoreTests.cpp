// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"

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
                                std::filesystem::path dependency = {})
            : name_(std::move(name)), extension_(std::move(extension)),
              returnedType_(std::move(returnedType)), dependency_(std::move(dependency))
        {
        }

        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {name_, "1"};
        }

        [[nodiscard]] std::vector<std::string> SourceExtensions() const override
        {
            return {extension_};
        }

        [[nodiscard]] std::string OutputType() const override
        {
            return kImportedType;
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
        explicit NumberWriter(std::string name = "test.NumberWriter")
            : name_(std::move(name))
        {
        }

        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {name_, "3"};
        }

        [[nodiscard]] std::string InputType() const override
        {
            return kProcessedType;
        }

        [[nodiscard]] Pipeline::ContentWriteResult Write(
            const Pipeline::ContentValue& input, const std::string& logicalName) const override
        {
            const ProcessedNumber& number = input.Get<ProcessedNumber>();
            return {{static_cast<std::uint8_t>(number.value),
                     static_cast<std::uint8_t>(logicalName.size())},
                    42u,
                    "Test.ProcessedNumber"};
        }

    private:
        std::string name_;
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
    EXPECT_EQ(logger.messages[0].stage, Pipeline::ContentPipelineStage::Import);
    EXPECT_EQ(logger.messages[0].component, "test.NumberImporter");
    EXPECT_EQ(logger.messages[1].stage, Pipeline::ContentPipelineStage::Process);
    EXPECT_EQ(logger.messages[1].component, "test.NumberProcessor");
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
