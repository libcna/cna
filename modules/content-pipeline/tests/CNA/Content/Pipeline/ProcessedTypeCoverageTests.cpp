// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-61: every processed type the production pipeline can reach either
// has a writer for each container, or a recorded reason it cannot.
//
// The row this closes listed nine types by hand, and by the time it was read the list was wrong in
// two ways at once: `Effect` had been added and was missing from it, and its claim that "a
// schema-1 Model is refused with a diagnostic naming XNAP-56" described code that no longer
// exists -- no source in this repository mentions `XNAP-56`, and the XNB Model writer has an
// explicit schema-1 path. A hand-maintained list of what a registry contains is a second copy of
// the registry, and the second copy is the one that goes stale.
//
// So nothing here is listed. The inventory is derived from the registry the real `cna-content`
// builds, and the assertions are about its *shape*:
//
//   * every processed type reachable in production has a writer for each container, or a
//     documented absence saying why it cannot;
//   * every importer's declared output type has a processor that accepts it, so no route dead-ends
//     after import;
//   * every writer's input type is some processor's output, so no writer sits unreachable.

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Internal/HostProcess.hpp"
#include "CNA/Content/Pipeline/EffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/EffectSourceContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    /** @brief The containers the production compiler can be asked for. */
    constexpr Pipeline::ContentOutputFormat kFormats[] = {
        Pipeline::ContentOutputFormat::Cnb,
        Pipeline::ContentOutputFormat::Xnb,
    };

    /**
     * @brief Builds the registry `cna-content` itself builds.
     *
     * Both halves: the built-in source routes, and the XNB output writers the coordinator adds
     * once it has parsed the command line. Anything less would be a different pipeline from the
     * one this is supposed to be measuring.
     */
    [[nodiscard]] std::shared_ptr<Pipeline::ContentPipelineRegistry> ProductionRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterBuiltInContentPipeline(*registry);
        Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
        return registry;
    }

    /** @brief Renders the whole inventory, so a failure shows what the registry actually holds. */
    [[nodiscard]] std::string Inventory(const Pipeline::ContentPipelineRegistry& registry)
    {
        std::ostringstream text;
        text << "\n-- importers --\n";
        for (const auto& importer : registry.Importers())
        {
            text << "  " << importer->Identity().name << "/" << importer->Identity().version
                 << "  extensions:";
            for (const std::string& extension : importer->SourceExtensions())
            {
                text << " " << extension;
            }
            text << "  ->";
            for (const std::string& output : importer->OutputTypes()) { text << " " << output; }
            text << "\n";
        }
        text << "-- processors --\n";
        for (const auto& processor : registry.Processors())
        {
            text << "  " << processor->Identity().name << "/" << processor->Identity().version
                 << "  " << processor->InputType() << " -> " << processor->OutputType() << "\n";
        }
        text << "-- writers --\n";
        for (const auto& writer : registry.Writers())
        {
            text << "  " << writer->Identity().name << "/" << writer->Identity().version << "  "
                 << Pipeline::ContentOutputFormatName(writer->OutputFormat()) << " <- "
                 << writer->InputType() << "\n";
        }
        text << "-- documented writer absences --\n";
        for (const auto& [format, type, reason] : registry.AbsentWriters())
        {
            text << "  " << Pipeline::ContentOutputFormatName(format) << " " << type << ": "
                 << reason.substr(0u, 90u) << "...\n";
        }
        return text.str();
    }

    /** @brief Every type some registered processor produces. */
    [[nodiscard]] std::set<std::string> ProcessedTypes(
        const Pipeline::ContentPipelineRegistry& registry)
    {
        std::set<std::string> types;
        for (const auto& processor : registry.Processors())
        {
            types.insert(processor->OutputType());
        }
        return types;
    }

    /** @brief Whether a writer resolves for @p type in @p format. */
    [[nodiscard]] bool HasWriter(const Pipeline::ContentPipelineRegistry& registry,
                                 const std::string& type,
                                 const Pipeline::ContentOutputFormat format)
    {
        try
        {
            return registry.ResolveWriter(type, {}, format) != nullptr;
        }
        catch (const std::logic_error&)
        {
            return false;
        }
    }
} // namespace

TEST(ProcessedTypeCoverageTest, EveryProcessedTypeEitherHasAWriterOrARecordedReasonItCannot)
{
    const auto registry = ProductionRegistry();
    const std::set<std::string> types = ProcessedTypes(*registry);
    ASSERT_GE(types.size(), 9u) << "the production registry looks empty" << Inventory(*registry);

    for (const std::string& type : types)
    {
        for (const Pipeline::ContentOutputFormat format : kFormats)
        {
            if (HasWriter(*registry, type, format)) { continue; }
            const std::string reason = registry->AbsentWriterReason(format, type);
            EXPECT_FALSE(reason.empty())
                << "the production pipeline can produce '" << type << "' and no "
                << Pipeline::ContentOutputFormatName(format)
                << " writer accepts it, with no recorded reason. Either register a writer, or "
                   "record why one cannot exist with "
                   "ContentPipelineRegistry::DocumentAbsentWriter(). A type that silently has no "
                   "route is exactly what this test exists to prevent."
                << Inventory(*registry);
        }
    }
}

TEST(ProcessedTypeCoverageTest, NoRecordedAbsenceContradictsARegisteredWriter)
{
    const auto registry = ProductionRegistry();
    for (const auto& [format, type, reason] : registry->AbsentWriters())
    {
        EXPECT_FALSE(reason.empty())
            << Pipeline::ContentOutputFormatName(format) << " " << type;
        EXPECT_FALSE(HasWriter(*registry, type, format))
            << Pipeline::ContentOutputFormatName(format) << " '" << type
            << "' has both a writer and a recorded reason it cannot have one."
            << Inventory(*registry);
    }
}

TEST(ProcessedTypeCoverageTest, EveryImportedTypeHasAProcessorThatAcceptsIt)
{
    const auto registry = ProductionRegistry();
    std::set<std::string> accepted;
    for (const auto& processor : registry->Processors())
    {
        accepted.insert(processor->InputType());
    }

    for (const auto& importer : registry->Importers())
    {
        for (const std::string& output : importer->OutputTypes())
        {
            EXPECT_TRUE(accepted.contains(output))
                << importer->Identity().name << " declares output type '" << output
                << "', which no processor accepts, so that route dead-ends after import."
                << Inventory(*registry);
        }
    }
}

TEST(ProcessedTypeCoverageTest, EveryWriterAcceptsATypeSomeProcessorActuallyProduces)
{
    const auto registry = ProductionRegistry();
    const std::set<std::string> types = ProcessedTypes(*registry);

    for (const auto& writer : registry->Writers())
    {
        EXPECT_TRUE(types.contains(writer->InputType()))
            << writer->Identity().name << " writes '" << writer->InputType()
            << "', which no processor in this configuration produces, so it is unreachable."
            << Inventory(*registry);
    }
}

TEST(ProcessedTypeCoverageTest, TheEffectRoutesReachTheOneWriterFromBothTheirSources)
{
    // The route the row that opened XNAP-61 predates, and the one it did not mention. Both
    // produce the same processed type on purpose: nothing downstream can tell a compiled `.fxb`
    // from a compiled `.fx`, which is what stops there being two Effect writers.
    const auto registry = ProductionRegistry();
    const std::shared_ptr<const Pipeline::ContentProcessor> fromSource =
        registry->ResolveProcessor(Pipeline::ImportedEffectSourceType);
    const std::shared_ptr<const Pipeline::ContentProcessor> fromCompiled =
        registry->ResolveProcessor(Pipeline::ImportedCompiledEffectType);

    ASSERT_NE(fromSource, nullptr);
    ASSERT_NE(fromCompiled, nullptr);
    EXPECT_EQ(fromSource->OutputType(), fromCompiled->OutputType());
    EXPECT_EQ(registry->ResolveWriter(fromSource->OutputType(), {},
                                      Pipeline::ContentOutputFormat::Xnb),
              registry->ResolveWriter(fromCompiled->OutputType(), {},
                                      Pipeline::ContentOutputFormat::Xnb));
}

// -- The claim this row carried, tested rather than described -----------------------------------

TEST(ProcessedTypeCoverageTest, ASchemaOneModelBuildsToXnbAndSaysWhatItCannotCarry)
{
    // XNAP-61's row said "`Model` accepts only the exact schema-2 canonical form; a schema-1 Model
    // is refused with a diagnostic naming `XNAP-56`". No source in this repository mentions
    // XNAP-56 any more and the XNB Model writer has an explicit schema-1 path, so the sentence
    // outlived its code. A glTF carrying animation clips stays schema 1 (the clips are embedded),
    // which makes it the exact case that claim was about.
    const std::filesystem::path fixture =
        std::filesystem::current_path() / "tests" / "assets" / "gltf" / "anim-two-clips.glb";
    ASSERT_TRUE(std::filesystem::is_regular_file(fixture))
        << "the schema-1 Model fixture is missing; this test must not pass by not running";

    const std::filesystem::path scratch =
        std::filesystem::temp_directory_path() /
        ("cna_schema1_" + std::to_string(::getpid()));
    std::filesystem::create_directories(scratch / "src");
    std::filesystem::copy_file(fixture, scratch / "src" / "clips.glb",
                               std::filesystem::copy_options::overwrite_existing);

    const CNA::Internal::HostProcessResult built = CNA::Internal::RunHostProcess(
        CNA_CONTENT_TOOL_PATH,
        {"build", (scratch / "src").string(), "-o", (scratch / "out").string(),
         "--format", "xnb"});
    const std::string log = built.standardOutput + built.standardError;

    EXPECT_TRUE(built.started) << built.failure;
    EXPECT_EQ(built.exitCode, 0) << log;
    EXPECT_TRUE(std::filesystem::is_regular_file(scratch / "out" / "clips.xnb")) << log;
    // What it genuinely cannot carry is said out loud, which is the honest version of the refusal
    // the row described: an XNA Model has no animation storage, and the CNB output keeps them.
    EXPECT_NE(log.find("animation clip"), std::string::npos) << log;

    std::error_code error;
    std::filesystem::remove_all(scratch, error);
}
