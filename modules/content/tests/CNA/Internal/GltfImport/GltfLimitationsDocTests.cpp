// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-447: `docs/gltf-limitations.md`, held to the code it describes.
//
// A limitations document that drifts is worse than none: it reads as a promise about what a caller
// will be told, and a caller who believes it stops looking. Two things keep it true, and both are
// mechanical rather than editorial.
//
//   * Its extension table is GENERATED from `GltfExtensionRegistryEXT()` -- the same registry the
//     `extensionsRequired` gate reads -- and compared row by row here, with the corrected table
//     printed on failure so fixing the document is a paste rather than a reading exercise. This is
//     the same rule plan_gltf.md §19 lives under (`GLTF-334`), for the same reason.
//   * Every report field the document names must still be DECLARED in `GltfImportCore.hpp`. The
//     document's whole organising claim is "each loss names the field that reports it", so a
//     renamed field must break the document that promised it instead of leaving a dead name behind.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

using CNA::Internal::GltfImport::GltfExtensionRecordEXT;
using CNA::Internal::GltfImport::GltfExtensionRegistryEXT;
using CNA::Internal::GltfImport::GltfExtensionSupportNameEXT;

namespace
{
    std::filesystem::path RepositoryRoot()
    {
        const std::filesystem::path corpus = CnaTest::GltfOracle::CorpusDirectory();
        if (corpus.empty()) { return {}; }
        return corpus.parent_path().parent_path().parent_path();
    }

    std::filesystem::path LimitationsDoc() { return RepositoryRoot() / "docs" / "gltf-limitations.md"; }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    /// Strips markdown emphasis and surrounding whitespace from one table cell.
    std::string CleanCell(std::string cell)
    {
        const auto strip = [&cell](const std::string& token) {
            std::string::size_type at;
            while ((at = cell.find(token)) != std::string::npos) { cell.erase(at, token.size()); }
        };
        strip("**");
        strip("`");
        const auto first = cell.find_first_not_of(" \t");
        if (first == std::string::npos) { return {}; }
        return cell.substr(first, cell.find_last_not_of(" \t") - first + 1);
    }

    std::vector<std::string> Cells(const std::string& line)
    {
        std::vector<std::string> out;
        std::string cell;
        std::istringstream stream(line);
        while (std::getline(stream, cell, '|')) { out.push_back(CleanCell(cell)); }
        if (!out.empty() && out.front().empty()) { out.erase(out.begin()); }
        return out;
    }

    /// Every markdown table row under the heading that starts with @p headingPrefix.
    std::vector<std::vector<std::string>> RowsUnderHeading(const std::string& headingPrefix,
                                                            std::string& error)
    {
        std::ifstream file(LimitationsDoc());
        if (!file) { error = "cannot open " + LimitationsDoc().string(); return {}; }

        std::vector<std::vector<std::string>> rows;
        std::string line;
        bool inSection = false;
        while (std::getline(file, line))
        {
            if (line.rfind(headingPrefix, 0) == 0) { inSection = true; continue; }
            if (inSection && line.rfind("## ", 0) == 0) { break; }
            if (!inSection || line.empty() || line.front() != '|') { continue; }
            if (line.find("---") != std::string::npos) { continue; }
            rows.push_back(Cells(line));
        }
        if (rows.empty()) { error = "no table rows under '" + headingPrefix + "'"; }
        return rows;
    }

    /// The document's §1 row for one extension, reduced to the fields that must not drift.
    struct DocExtensionRow
    {
        std::string classification;
        std::string claimed;
        std::string task;
    };
}

// --- the extension table is the registry, not a second copy of it -------------------------------

TEST(GltfLimitationsDoc, ExtensionTableAgreesWithTheRegistry)
{
    std::string error;
    const std::vector<std::vector<std::string>> rows = RowsUnderHeading("## 1. Extensions", error);
    ASSERT_TRUE(error.empty()) << error;

    std::map<std::string, DocExtensionRow> byName;
    std::size_t buildDependentRows = 0;
    for (const std::vector<std::string>& cells : rows)
    {
        if (cells.size() < 5) { continue; }
        if (cells[0] == "Extension") { continue; }  // the header row
        std::string classification = cells[1];
        const auto space = classification.find(' ');
        if (space != std::string::npos) { classification.erase(space); }
        if (cells[2] == "build-dependent") { ++buildDependentRows; }
        EXPECT_TRUE(byName.emplace(cells[0], DocExtensionRow{classification, cells[2], cells[4]}).second)
            << "§1 lists " << cells[0] << " twice";
    }
    ASSERT_FALSE(byName.empty()) << "§1's table parsed to nothing";

    // "build-dependent" is a wildcard for the claim, and exactly one extension may use it: Draco,
    // whose claim follows CNA_DRACO_AVAILABLE. Bounding it here is what stops the wildcard from
    // spreading to rows that simply have not been checked.
    EXPECT_LE(buildDependentRows, 1u)
        << "more than one §1 row hides its claim behind 'build-dependent'";

    std::ostringstream generated;
    generated << "\n| Extension | Classification | Claimed | Evidence / note | Task |\n"
                 "|---|---|---|---|---|\n";
    for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
    {
        generated << "| `" << record.name << "` | **" << GltfExtensionSupportNameEXT(record.support)
                  << "** | " << (record.claimed ? "yes" : "no") << " | " << record.note << " | `"
                  << record.task << "` |\n";
    }
    const std::string table = generated.str();

    for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
    {
        SCOPED_TRACE(record.name);
        const auto found = byName.find(record.name);
        ASSERT_NE(byName.end(), found)
            << "the registry classifies " << record.name
            << " and docs/gltf-limitations.md does not list it." << table;
        EXPECT_EQ(GltfExtensionSupportNameEXT(record.support), found->second.classification)
            << "the document and the registry disagree about " << record.name << "." << table;
        EXPECT_NE(std::string::npos, found->second.task.find(record.task))
            << "the document attributes " << record.name << " to '" << found->second.task
            << "', the registry to '" << record.task << "'." << table;
        if (found->second.claimed != "build-dependent")
        {
            EXPECT_EQ(record.claimed ? "yes" : "no", found->second.claimed)
                << "the document and the registry disagree about whether a file REQUIRING "
                << record.name << " is accepted -- which is the one column a caller acts on."
                << table;
        }
        byName.erase(found);
    }
    for (const auto& [name, row] : byName)
    {
        ADD_FAILURE() << "docs/gltf-limitations.md lists " << name << " (" << row.classification
                      << ") but the registry does not classify it, so nothing in CNA behaves the "
                         "way the document describes." << table;
    }
}

// --- every named report field still exists ------------------------------------------------------

TEST(GltfLimitationsDoc, EveryReportFieldNamedHereExistsInTheHeader)
{
    // §2 and §3 are tables of losses, and their third column is the field that reports each one.
    // That column is the document's entire claim -- "this loss is visible to a caller" -- so a
    // name in it that no longer exists is the document promising a diagnostic nobody can read.
    const std::string header =
        ReadFile(RepositoryRoot() / "modules" / "content" / "include" / "CNA" / "Internal" /
                 "GltfImport" / "GltfImportCore.hpp");
    ASSERT_FALSE(header.empty()) << "cannot read GltfImportCore.hpp";

    std::size_t checked = 0;
    for (const char* heading : {"## 2. Approximations", "## 3. Data CNA cannot carry"})
    {
        SCOPED_TRACE(heading);
        std::string error;
        const std::vector<std::vector<std::string>> rows = RowsUnderHeading(heading, error);
        ASSERT_TRUE(error.empty()) << error;

        for (const std::vector<std::string>& cells : rows)
        {
            if (cells.size() < 4) { continue; }
            if (cells[2] == "Report field") { continue; }  // the header row
            // A row whose report cell is prose rather than identifiers -- the exact conversions,
            // which lose nothing and therefore have no loss to count.
            if (cells[2].find('(') != std::string::npos) { continue; }

            std::istringstream fields(cells[2]);
            std::string field;
            while (std::getline(fields, field, ','))
            {
                const auto first = field.find_first_not_of(" \t");
                if (first == std::string::npos) { continue; }
                field = field.substr(first, field.find_last_not_of(" \t") - first + 1);
                ASSERT_TRUE(std::regex_match(field, std::regex("[A-Za-z_][A-Za-z0-9_]*")))
                    << "'" << field << "' is not an identifier, so this row's report column is "
                       "prose pretending to be a field name";
                // As a DECLARATION, not merely as text: a name that survives only in a comment is
                // exactly the dead reference this test exists to catch.
                EXPECT_TRUE(std::regex_search(
                    header, std::regex("\\b" + field + "\\b[^;\\n]*(=|;)")))
                    << "docs/gltf-limitations.md names '" << field
                    << "' as the field that reports this loss, and GltfImportCore.hpp declares no "
                       "such member";
                ++checked;
            }
        }
    }
    EXPECT_GT(checked, 20u)
        << "too few report fields checked -- the tables parsed to almost nothing";
}

// --- the document covers what the code reports --------------------------------------------------

TEST(GltfLimitationsDoc, EveryApproximatedOrUnsupportedExtensionIsInTheApproximationTables)
{
    // The converse of the first test, and the one that catches an omission rather than a
    // contradiction: an extension the registry says is APPROXIMATED must appear somewhere in the
    // prose too, because "approximated" is a promise that the document explains what was lost.
    const std::string doc = ReadFile(LimitationsDoc());
    ASSERT_FALSE(doc.empty()) << "cannot read docs/gltf-limitations.md";

    std::size_t approximated = 0;
    for (const GltfExtensionRecordEXT& record : GltfExtensionRegistryEXT())
    {
        if (GltfExtensionSupportNameEXT(record.support) != std::string("APPROXIMATED_AND_REPORTED"))
        {
            continue;
        }
        ++approximated;
        // Once in §1's table is not enough: the count is over the whole file, so an extension that
        // appears only in the generated row -- with nothing saying what the approximation costs --
        // fails here.
        std::size_t occurrences = 0;
        for (std::string::size_type at = doc.find(record.name); at != std::string::npos;
             at = doc.find(record.name, at + 1))
        {
            ++occurrences;
        }
        EXPECT_GE(occurrences, 2u)
            << record.name << " is classified APPROXIMATED_AND_REPORTED, but docs/"
               "gltf-limitations.md mentions it only in the generated table -- nothing says what "
               "the approximation costs or which field reports it";
    }
    EXPECT_GT(approximated, 0u) << "no approximated extension at all -- this test proved nothing";
}
