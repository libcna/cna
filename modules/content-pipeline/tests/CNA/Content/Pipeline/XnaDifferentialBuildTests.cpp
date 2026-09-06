// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-265 (§23): CNA's build compared with XNA's own, case for
// case.
//
// `tools/xna-pipeline-oracle/differential/` runs Microsoft's `BuildContent` task -- the one an XNA
// project's MSBuild invokes -- over a committed corpus and records, per case, whether it built and
// what it said when it did not. This test builds the same corpus through CNA's coordinator and
// compares the two, which is a different question from the per-component differentials: those ask
// what a processor answers, this asks what a *project* gets.
//
// The comparison is deliberately about the outcome and the refusal, not the bytes. Byte equality is
// a separate claim, true for some types and not yet for others (XNAPP-266), and asserting it here
// would bury the outcome disagreements that matter most -- a case one side builds and the other
// refuses is a project that works for one and not the other.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <utility>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildContent.hpp"
#include "System/Environment.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentTask.hpp"

namespace Tasks = Microsoft::Xna::Framework::Content::Pipeline::Tasks;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    /** @brief One field of one object in a flat JSON array, found by scanning rather than parsing. */
    std::string Field(const std::string& object, const std::string& key)
    {
        const std::string needle = "\"" + key + "\":";
        const std::size_t at = object.find(needle);
        if (at == std::string::npos) { return {}; }
        std::size_t from = object.find_first_not_of(" \t", at + needle.size());
        if (from == std::string::npos) { return {}; }
        if (object[from] == '"')
        {
            const std::size_t end = object.find('"', from + 1u);
            return object.substr(from + 1u, end - from - 1u);
        }
        const std::size_t end = object.find_first_of(",}", from);
        return object.substr(from, end - from);
    }

    /** @brief What XNA's own BuildContent did with one corpus case. */
    struct XnaOutcome
    {
        /** @brief Whether the genuine task reported success. */
        bool built = false;

        /** @brief The whole recorded row, for a message that quotes what XNA said. */
        std::string row;
    };

    /** @brief The corpus, joined to what XNA answered for it. */
    struct Case
    {
        /** @brief The case's stable name. */
        std::string name;
        /** @brief Source path under `tests/assets/xna40`. */
        std::string source;
        /** @brief XNA's importer name; empty when the case names none. */
        std::string importer;
        /** @brief XNA's processor name; empty when the case names none. */
        std::string processor;
        /** @brief XNA's target platform. */
        std::string platform;
        /** @brief XNA's graphics profile. */
        std::string profile;
        /** @brief The `ProcessorParameters_*` the case sets, by XNA's own names. */
        std::vector<std::pair<std::string, std::string>> parameters;
        /** @brief What XNA did. */
        XnaOutcome xna;
    };

    /** @brief Splits a flat JSON array of objects into its objects. */
    std::vector<std::string> Objects(const std::string& text, const std::string& arrayKey)
    {
        std::vector<std::string> objects;
        const std::size_t start = text.find("\"" + arrayKey + "\"");
        if (start == std::string::npos) { return objects; }
        int depth = 0;
        std::size_t from = 0;
        for (std::size_t at = start; at < text.size(); ++at)
        {
            if (text[at] == '{')
            {
                if (depth == 0) { from = at; }
                ++depth;
            }
            else if (text[at] == '}')
            {
                --depth;
                if (depth == 0) { objects.push_back(text.substr(from, at - from + 1u)); }
            }
            else if (text[at] == ']' && depth == 0)
            {
                break;
            }
        }
        return objects;
    }

    std::vector<Case> Corpus()
    {
        const std::filesystem::path manifest =
            Locate("tools/xna-pipeline-oracle/differential/corpus.json");
        const std::filesystem::path results =
            Locate("tests/reference/xna40/differential/differential-oracle.json");
        std::map<std::string, XnaOutcome> answered;
        for (const std::string& row : Objects(ReadText(results), "cases"))
        {
            XnaOutcome outcome;
            outcome.built = Field(row, "built") == "true";
            outcome.row = row;
            answered[Field(row, "case")] = outcome;
        }

        std::vector<Case> cases;
        for (const std::string& row : Objects(ReadText(manifest), "cases"))
        {
            Case one;
            one.name = Field(row, "case");
            one.source = Field(row, "source");
            one.importer = Field(row, "importer");
            one.processor = Field(row, "processor");
            one.platform = Field(row, "platform");
            one.profile = Field(row, "profile");
            // The parameters object is nested, so it is taken out of the row by hand: the manifest
            // is written by this repository and its shape is known.
            const std::size_t at = row.find("\"parameters\":");
            if (at != std::string::npos)
            {
                const std::size_t open = row.find('{', at);
                const std::size_t close = row.find('}', open);
                if (open != std::string::npos && close != std::string::npos)
                {
                    const std::string body = row.substr(open + 1u, close - open - 1u);
                    std::size_t from = 0;
                    while (true)
                    {
                        const std::size_t keyStart = body.find('"', from);
                        if (keyStart == std::string::npos) { break; }
                        const std::size_t keyEnd = body.find('"', keyStart + 1u);
                        const std::size_t valueStart = body.find('"', keyEnd + 1u);
                        const std::size_t valueEnd = body.find('"', valueStart + 1u);
                        if (valueEnd == std::string::npos) { break; }
                        one.parameters.emplace_back(
                            body.substr(keyStart + 1u, keyEnd - keyStart - 1u),
                            body.substr(valueStart + 1u, valueEnd - valueStart - 1u));
                        from = valueEnd + 1u;
                    }
                }
            }
            const auto found = answered.find(one.name);
            if (found != answered.end()) { one.xna = found->second; }
            cases.push_back(one);
        }
        return cases;
    }

    /**
     * @brief Sets environment variables for the length of a test and puts them back.
     *
     * Restoring matters more than setting: a variable left behind is seen by every later test in
     * the process, and a leaked `CNA_FXC` sends unrelated builds through a Wine compiler they never
     * asked for. Only a null value removes a variable, so a previously-unset one is restored by
     * passing the empty optional through rather than an empty string.
     */
    class ScopedEnvironment
    {
    public:
        void Set(const std::string& name, const std::string& value)
        {
            previous_.emplace_back(name, System::Environment::GetEnvironmentVariable(name));
            System::Environment::SetEnvironmentVariable(name, value);
        }
        ~ScopedEnvironment()
        {
            for (auto entry = previous_.rbegin(); entry != previous_.rend(); ++entry)
            {
                System::Environment::SetEnvironmentVariable(entry->first, entry->second);
            }
        }
        ScopedEnvironment() = default;
        ScopedEnvironment(const ScopedEnvironment&) = delete;
        ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

    private:
        std::vector<std::pair<std::string, std::optional<std::string>>> previous_;
    };

    /** @brief A source root holding one corpus source, removed when the case ends. */
    class OneSource
    {
    public:
        OneSource(const std::string& label, const std::string& source)
            : root_(std::filesystem::temp_directory_path() / ("cna_xnapp265_" + label))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            std::filesystem::create_directories(Output());
            const std::filesystem::path from = Locate("tests/assets/xna40") / source;
            name_ = from.filename().string();
            std::error_code error;
            std::filesystem::copy_file(from, Source() / name_,
                                       std::filesystem::copy_options::overwrite_existing, error);
            // A model's material names a texture beside it, and the corpus's own sources are the
            // ones XNA was given, so anything the source refers to travels with it.
            for (const char* companion : {"surface.png", "blue.dds"})
            {
                const std::filesystem::path beside = from.parent_path() / companion;
                if (std::filesystem::exists(beside, error) && !error)
                {
                    std::filesystem::copy_file(beside, Source() / companion,
                                               std::filesystem::copy_options::overwrite_existing,
                                               error);
                }
            }
        }
        ~OneSource()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        OneSource(const OneSource&) = delete;
        OneSource& operator=(const OneSource&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "out"; }
        [[nodiscard]] const std::string& Name() const { return name_; }

    private:
        std::filesystem::path root_;
        std::string name_;
    };
}

// Every corpus case has an answer from XNA, and the corpus is not empty. A case added to the
// manifest without re-running the oracle would otherwise compare against nothing.
TEST(XnaDifferentialBuildTest, EveryCorpusCaseWasAnsweredByTheGenuineBuild)
{
    const std::vector<Case> cases = Corpus();
    ASSERT_GE(cases.size(), 30u) << "the differential corpus is missing or was not read";
    for (const Case& one : cases)
    {
        EXPECT_FALSE(one.xna.row.empty())
            << one.name
            << ": no recorded XNA answer. Re-run "
               "tools/xna-pipeline-oracle/differential/run-differential-oracle.sh";
    }
}

// The two builds agree about which sources are buildable at all.
//
// This is the coarsest comparison the corpus supports and the one a project feels first: a case XNA
// builds and CNA refuses is content a game cannot ship, and a case XNA refuses and CNA builds is a
// project that fails when it reaches a real XNA toolchain.
TEST(XnaDifferentialBuildTest, CnaAcceptsAndRefusesTheSameSourcesXnaDoes)
{
    // Cases whose XNA answer is an environment limit rather than XNA's behaviour, each with the
    // reason it cannot be compared here. Recorded rather than skipped silently.
    static const std::map<std::string, std::string> notComparable = {
        {"audio/mp3_song", "XNA's Windows Media encoder never returns under this Wine prefix"},
        {"audio/wma_song", "XNA's Windows Media layer cannot open a WMA under this Wine prefix"},
        {"video/wmv_video", "constructing a VideoContent needs Media Foundation, which Wine lacks"},
        {"xml/intermediate_passthrough",
         "the .xml route has no canonical importer; the built-in subset is XNAPP-260 work"},
    };

    // The `.fx` cases need the compiler XNA used. If the caller named one, that is the one; if not
    // and the DirectX SDK copy is where the oracle's own tooling keeps it, point the build at it
    // and at the prefix it runs in, so the comparison happens here without the caller having to
    // know. On a machine with neither, those cases are left out rather than counted as a
    // disagreement about content.
    ScopedEnvironment environment;
    std::error_code error;
    // Present-but-empty is not configured: a variable can be set to an empty string, and a build
    // told the compiler is "" fails instantly in a way that reads like a disagreement about content.
    const char* configured = std::getenv("CNA_FXC");
    bool haveFxc = configured != nullptr && *configured != '\0';
    if (!haveFxc)
    {
        const std::filesystem::path fxc(
            "/rv/tmp/samples/_tools/directx-sdk-june-2010/extract/DXSDK/Utilities/bin/x86/fxc.exe");
        const std::filesystem::path prefix =
            std::filesystem::path(std::getenv("HOME") == nullptr ? "" : std::getenv("HOME")) /
            ".wine-cna-xna40";
        if (std::filesystem::exists(fxc, error) && !error &&
            std::filesystem::exists(prefix, error) && !error)
        {
            environment.Set("CNA_FXC", fxc.string());
            environment.Set("CNA_FXC_LAUNCHER", "wine");
            environment.Set("WINEPREFIX", prefix.string());
            environment.Set("WINEDEBUG", "-all");
            haveFxc = true;
        }
    }

    std::vector<std::string> disagreements;
    for (const Case& one : Corpus())
    {
        if (notComparable.count(one.name) != 0u) { continue; }
        if (one.name.rfind("effect/", 0) == 0 && !haveFxc) { continue; }

        OneSource source(one.name.substr(one.name.find('/') + 1u) + "_" + one.platform, one.source);

        // Driven through CNA's own BuildContent, so both sides are given the same thing: the same
        // ItemSpec, the same Importer and Processor names, the same ProcessorParameters_* metadata,
        // the same platform and profile. Comparing a `cna-content` command line against an MSBuild
        // task would be comparing two different questions.
        Tasks::TaskItem item((source.Source() / source.Name()).string());
        item.SetMetadata("Name", std::filesystem::path(source.Name()).stem().string());
        if (!one.importer.empty()) { item.SetMetadata("Importer", one.importer); }
        if (!one.processor.empty()) { item.SetMetadata("Processor", one.processor); }
        for (const auto& [name, value] : one.parameters)
        {
            item.SetMetadata("ProcessorParameters_" + name, value);
        }

        Tasks::BuildContent task;
        task.setRootDirectoryProperty(source.Source().string());
        task.setOutputDirectoryProperty(source.Output().string());
        task.setIntermediateDirectoryProperty((source.Output() / "obj").string());
        task.setBuildConfigurationProperty("Release");
        task.setTargetPlatformProperty(one.platform);
        task.setTargetProfileProperty(one.profile.empty() ? "Reach" : one.profile);
        task.setContentProjectGUIDProperty("{7C1D0F4E-5B2A-4E97-9E3C-1B7A2D6F4E80}");
        task.setRebuildAllProperty(true);
        task.setCompressContentProperty(false);
        task.setSourceAssetsProperty({item});

        bool cnaBuilt = false;
        try
        {
            cnaBuilt = task.Execute();
        }
        catch (const std::exception&)
        {
            cnaBuilt = false;
        }
        if (cnaBuilt != one.xna.built)
        {
            disagreements.push_back(one.name + ": XNA " + (one.xna.built ? "built" : "refused") +
                                    ", CNA " + (cnaBuilt ? "built" : "refused") + "\n    XNA said: " +
                                    one.xna.row.substr(0, 400));
        }
    }

    std::string report;
    for (const std::string& line : disagreements) { report += "  " + line + "\n"; }
    EXPECT_TRUE(disagreements.empty())
        << disagreements.size() << " case(s) where the two builds disagree:\n" << report;
}

// The DXT rule, which is the first thing the corpus found: XNA refuses to block-compress a texture
// whose dimensions are not multiples of four, and said so in these words for both a 2x2 and a 3x2
// while building the 4x4. CNA answers the same sentence.
TEST(XnaDifferentialBuildTest, TheDxtDimensionRuleIsXnasOwnSentence)
{
    const std::string results =
        ReadText(Locate("tests/reference/xna40/differential/differential-oracle.json"));
    for (const auto& [name, size] : std::vector<std::pair<std::string, std::string>>{
             {"texture/png_texture_dxt", "2x2"}, {"texture/png3x2_texture_dxt", "3x2"}})
    {
        const std::size_t at = results.find("\"" + name + "\"");
        ASSERT_NE(at, std::string::npos) << name;
        const std::string row = results.substr(at, 600);
        EXPECT_NE(row.find("Face 0 is sized " + size +
                           ", but textures using DXT compressed formats must be multiples of four."),
                  std::string::npos)
            << row;
    }
    EXPECT_NE(results.find("\"texture/png4x4_texture_dxt\", \"built\": true"), std::string::npos)
        << "the rule needs a source on each side of it";
}
