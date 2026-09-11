// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-267 (§23): the builds that are meant to go wrong, compared
// with XNA's own.
//
// XNAPP-265 asked whether the two builds accept the same sources and XNAPP-266 whether the files
// they produce mean the same thing. Neither asks the question a project asks next: when a build
// fails, does it fail the *same way*. A reimplementation that refuses everything agrees with XNA
// about every malformed file in the corpus and is still useless, and one that refuses what XNA only
// warns about turns a shipping project into a broken one.
//
// So three separate claims are checked per case, each able to fail on its own:
//
//   * the manifest's `expect` matches what the oracle actually recorded, so the corpus cannot drift
//     away from the measurement it describes;
//   * CNA's outcome matches XNA's, including *built with a warning*, which is what XNA does with an
//     unknown processor parameter, an unconvertible one, and a character region the font cannot
//     cover;
//   * every token in the case's `mentions` appears in both sides' diagnostics. The sentences are
//     each implementation's own and cannot be compared; the subject -- the parameter's name, the
//     missing file, the undeclared identifier -- belongs to the content, and a message that does
//     not name it is a message a developer cannot act on.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentTask.hpp"
#include "XnaDifferentialCorpus.hpp"

namespace Tasks = Microsoft::Xna::Framework::Content::Pipeline::Tasks;

namespace
{
    using namespace CNA::Tests::XnaDifferential;

    /** @brief What one side of the comparison did with one case. */
    struct Outcome
    {
        /** @brief Whether the build reported success. */
        bool built = false;
        /** @brief Whether it recorded at least one warning. */
        bool warned = false;
        /** @brief Every message it recorded, joined, for the `mentions` check and the report. */
        std::string said;

        /** @brief The `expect` spelling this outcome corresponds to. */
        [[nodiscard]] std::string Spelling() const
        {
            if (!built) { return "refused"; }
            return warned ? "built-with-warning" : "built";
        }
    };

    /** @brief One row of the error corpus, joined to what XNA answered for it. */
    struct ErrorCase
    {
        /** @brief The case's stable name. */
        std::string name;
        /** @brief The failure class the case is about, or `control` for a row meant to build. */
        std::string failureClass;
        /** @brief What the manifest says XNA did. */
        std::string expect;
        /** @brief Tokens both sides' diagnostics must name. */
        std::vector<std::string> mentions;
        /** @brief Source path under `tests/assets/xna40`. */
        std::string source;
        /** @brief XNA's importer name. */
        std::string importer;
        /** @brief XNA's processor name. */
        std::string processor;
        /** @brief XNA's target platform. */
        std::string platform;
        /** @brief XNA's graphics profile. */
        std::string profile;
        /** @brief The `ProcessorParameters_*` the case sets. */
        std::vector<std::pair<std::string, std::string>> parameters;
        /** @brief What the oracle recorded, and whether it recorded anything at all. */
        Outcome xna;
        /** @brief Whether the oracle has an answer for this case. */
        bool answered = false;
    };

    /** @brief The error corpus joined to the recorded XNA run. */
    std::vector<ErrorCase> ErrorCorpus()
    {
        const std::string manifest =
            ReadText(Locate("tools/xna-pipeline-oracle/differential/errors.json"));
        const std::string results = ReadText(
            Locate("tests/reference/xna40/differential-errors/differential-oracle.json"));

        std::map<std::string, Outcome> answered;
        for (const std::string& row : Objects(results, "cases"))
        {
            Outcome outcome;
            outcome.built = Field(row, "built") == "true";
            const std::vector<std::string> errors = Strings(row, "errors");
            const std::vector<std::string> warnings = Strings(row, "warnings");
            outcome.warned = !warnings.empty();
            for (const std::string& line : errors) { outcome.said += line + "\n"; }
            for (const std::string& line : warnings) { outcome.said += line + "\n"; }
            answered[Field(row, "case")] = outcome;
        }

        std::vector<ErrorCase> cases;
        for (const std::string& row : Objects(manifest, "cases"))
        {
            ErrorCase one;
            one.name = Field(row, "case");
            if (one.name.empty()) { continue; }
            one.failureClass = Field(row, "class");
            one.expect = Field(row, "expect");
            one.mentions = Strings(row, "mentions");
            one.source = Field(row, "source");
            one.importer = Field(row, "importer");
            one.processor = Field(row, "processor");
            one.platform = Field(row, "platform");
            one.profile = Field(row, "profile");
            one.parameters = Parameters(row);
            const auto found = answered.find(one.name);
            if (found != answered.end())
            {
                one.xna = found->second;
                one.answered = true;
            }
            cases.push_back(one);
        }
        return cases;
    }

    /** @brief Builds one error-corpus case through CNA's own BuildContent. */
    Outcome BuildWithCna(const ErrorCase& one, const OneSource& source)
    {
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

        Outcome outcome;
        try
        {
            outcome.built = task.Execute();
        }
        catch (const std::exception& error)
        {
            outcome.built = false;
            outcome.said += std::string("threw: ") + error.what() + "\n";
        }
        // All three levels, because all three are what a project reads: XNA's engine receives
        // errors, warnings and messages from its own task and the comparison is against that whole
        // record. The warning list is what decides `built-with-warning`, which is a different
        // outcome from `built` and the one XNA chooses more often than a reimplementation expects.
        for (const std::string& line : task.ErrorsEXT()) { outcome.said += line + "\n"; }
        for (const std::string& line : task.WarningsEXT()) { outcome.said += line + "\n"; }
        for (const std::string& line : task.MessagesEXT()) { outcome.said += line + "\n"; }
        outcome.warned = !task.WarningsEXT().empty();
        return outcome;
    }

    /** @brief Whether this case can be built here at all, and why not when it cannot. */
    std::string NotComparable(const ErrorCase& one, bool haveFxc)
    {
        const bool effect = one.source.size() > 3u &&
                            one.source.compare(one.source.size() - 3u, 3u, ".fx") == 0;
        if (effect && !haveFxc)
        {
            return "no effect compiler on this machine";
        }
        return {};
    }
}

// The manifest describes the run that was recorded. A case whose `expect` disagrees with the oracle
// is a corpus that has drifted from its own measurement, and every later comparison in this file
// would be against a claim rather than against XNA.
TEST(XnaErrorParityTest, TheManifestMatchesWhatTheOracleRecorded)
{
    const std::vector<ErrorCase> cases = ErrorCorpus();
    ASSERT_GE(cases.size(), 20u) << "the error corpus is missing or was not read";

    std::vector<std::string> drifted;
    for (const ErrorCase& one : cases)
    {
        if (!one.answered)
        {
            drifted.push_back(one.name + ": no recorded XNA answer. Re-run "
                                         "run-differential-oracle.sh errors");
            continue;
        }
        if (one.xna.Spelling() != one.expect)
        {
            drifted.push_back(one.name + ": the manifest expects '" + one.expect +
                              "' and XNA recorded '" + one.xna.Spelling() + "'");
        }
    }
    std::string report;
    for (const std::string& line : drifted) { report += "  " + line + "\n"; }
    EXPECT_TRUE(drifted.empty()) << drifted.size() << " row(s) drifted:\n" << report;
}

// Every class the manifest names is exercised by at least one case, and every case is meant to
// build or to fail for one of them. A class with no case is a claim the corpus does not support.
TEST(XnaErrorParityTest, EveryFailureClassHasACase)
{
    const std::string manifest =
        ReadText(Locate("tools/xna-pipeline-oracle/differential/errors.json"));
    const std::size_t at = manifest.find("\"classes\"");
    ASSERT_NE(at, std::string::npos) << "the manifest names no failure classes";
    const std::size_t close = manifest.find('}', at);
    const std::string classes = manifest.substr(at, close - at);

    std::map<std::string, int> used;
    int controls = 0;
    for (const ErrorCase& one : ErrorCorpus())
    {
        if (one.failureClass == "control") { ++controls; continue; }
        ++used[one.failureClass];
        EXPECT_NE(classes.find("\"" + one.failureClass + "\""), std::string::npos)
            << one.name << " is about '" << one.failureClass << "', which the manifest does not "
            << "describe";
    }
    EXPECT_GE(controls, 3) << "a refusal proves only that the route is broken unless something "
                              "next to it builds";

    // Past `"classes"` itself: its value is the object, not a string, and a scan that started at
    // the beginning would take the first class name for that key's value and then read every
    // description as a name.
    std::size_t from = classes.find('{');
    ASSERT_NE(from, std::string::npos);
    while (true)
    {
        const std::size_t start = classes.find('"', from);
        if (start == std::string::npos) { break; }
        const std::size_t end = classes.find('"', start + 1u);
        if (end == std::string::npos) { break; }
        const std::string name = classes.substr(start + 1u, end - start - 1u);
        // The description follows its name; only the names are checked, so the value is skipped.
        const std::size_t colon = classes.find(':', end);
        const std::size_t next = classes.find('"', colon);
        const std::size_t valueEnd = classes.find('"', next + 1u);
        from = valueEnd == std::string::npos ? classes.size() : valueEnd + 1u;
        EXPECT_NE(used.find(name), used.end())
            << "the manifest describes the failure class '" << name << "' and no case is about it";
    }
}

// The comparison itself: CNA fails the same way XNA does, or succeeds the same way.
TEST(XnaErrorParityTest, CnaFailsTheWayXnaFails)
{
    ScopedEnvironment environment;
    const bool haveFxc = ConfigureEffectCompiler(environment);

    std::vector<std::string> disagreements;
    std::vector<std::string> skipped;
    for (const ErrorCase& one : ErrorCorpus())
    {
        if (!one.answered) { continue; }
        const std::string why = NotComparable(one, haveFxc);
        if (!why.empty())
        {
            skipped.push_back(one.name + ": " + why);
            continue;
        }

        std::string label = one.name;
        std::replace(label.begin(), label.end(), '/', '_');
        OneSource source("err_" + label, one.source);
        const Outcome cna = BuildWithCna(one, source);

        if (cna.Spelling() != one.xna.Spelling())
        {
            disagreements.push_back(
                one.name + " [" + one.failureClass + "]: XNA " + one.xna.Spelling() + ", CNA " +
                cna.Spelling() + "\n    XNA said: " + one.xna.said.substr(0, 240) +
                "\n    CNA said: " + cna.said.substr(0, 240));
            continue;
        }
        for (const std::string& token : one.mentions)
        {
            if (!ContainsIgnoringCase(one.xna.said, token))
            {
                disagreements.push_back(one.name + " [" + one.failureClass + "]: XNA's own message "
                                        "does not name '" + token +
                                        "', so the case does not measure what it claims to\n"
                                        "    XNA said: " + one.xna.said.substr(0, 240));
                continue;
            }
            if (!ContainsIgnoringCase(cna.said, token))
            {
                disagreements.push_back(one.name + " [" + one.failureClass +
                                        "]: both " + one.xna.Spelling() + ", and CNA's message does "
                                        "not name '" + token + "'\n    XNA said: " +
                                        one.xna.said.substr(0, 240) + "\n    CNA said: " +
                                        cna.said.substr(0, 240));
            }
        }
    }

    std::string report;
    for (const std::string& line : disagreements) { report += "  " + line + "\n"; }
    for (const std::string& line : skipped) { report += "  (not compared) " + line + "\n"; }
    EXPECT_TRUE(disagreements.empty())
        << disagreements.size() << " case(s) where the two builds fail differently:\n" << report;
}
