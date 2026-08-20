// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-038: the vendored cgltf stays byte-identical to upstream, and every fault found
// in it is answered on CNA's side of the call.
//
// A patched vendored header is invisible at exactly the moment it matters. The next upgrade is a
// file replacement, and a local edit inside it is either silently lost -- the fault returns, now
// with a test that no longer explains itself -- or silently kept, and the upgrade is not the
// upgrade anyone thought they made. Neither failure announces itself, which is why this is a test
// rather than a paragraph.
//
// The rule and its rationale are `docs/gltf-conventions.md`; this file is the half a compiler can
// check.

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

namespace
{
    std::filesystem::path RepositoryRoot()
    {
        const std::filesystem::path corpus = CnaTest::GltfOracle::CorpusDirectory();
        if (corpus.empty()) { return {}; }
        return corpus.parent_path().parent_path().parent_path();
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }
}

TEST(GltfVendoredCgltf, TheVendoredHeaderCarriesNoCnaEdits)
{
    const std::filesystem::path header = RepositoryRoot() / "third_party" / "cgltf" / "cgltf.h";
    const std::string source = ReadFile(header);
    ASSERT_FALSE(source.empty()) << "cannot read " << header;
    // A sanity check on the check: if the file ever stops being cgltf, the markers below would
    // trivially be absent and this test would pass over nothing.
    EXPECT_NE(std::string::npos, source.find("cgltf_parse"))
        << "this is not the cgltf header any more";

    // Any of these in the vendored file means someone edited it, which the upgrade path cannot
    // survive. The task-id pattern is deliberately narrow (`GLTF-` followed by digits) so a
    // legitimate upstream mention of, say, "gltf-2.0" is not mistaken for a CNA marker.
    for (const char* marker : {"CNA", "plan_gltf", "CNAEXT"})
    {
        EXPECT_EQ(std::string::npos, source.find(marker))
            << "third_party/cgltf/cgltf.h contains '" << marker
            << "', so it has been patched. Per GLTF-038 the vendored header stays byte-identical "
               "to upstream and faults are answered on CNA's side of the call -- a patch here is "
               "lost or silently kept by the next upgrade, and neither announces itself.";
    }
    EXPECT_FALSE(std::regex_search(source, std::regex("GLTF-[0-9]{3}")))
        << "third_party/cgltf/cgltf.h cites a plans/plan_gltf.md task, so it has been patched (GLTF-038)";

    // The LICENSE must travel with it: the header is MIT, and vendoring it without its licence is
    // the other way this directory goes wrong.
    EXPECT_TRUE(std::filesystem::exists(header.parent_path() / "LICENSE"))
        << "the vendored cgltf has no LICENSE beside it";
}

TEST(GltfVendoredCgltf, EveryKnownCgltfFaultStillHasItsCnaSideAnswer)
{
    // The other half of the policy: a workaround that is deleted -- because it looked redundant,
    // or because an upgrade was assumed to have retired it -- must fail here rather than at some
    // renderer's expense. Each name below is the CNA-side answer to a fault the campaign proved in
    // the vendored reader, and each is checked as a DECLARATION in the importer rather than as
    // free text, so a comment mentioning one does not satisfy it.
    const std::string importer =
        ReadFile(RepositoryRoot() / "modules" / "content" / "src" / "GltfImport" /
                 "GltfImportCore.cpp");
    ASSERT_FALSE(importer.empty()) << "cannot read GltfImportCore.cpp";

    struct Answer
    {
        const char* symbol;
        const char* fault;
        const char* task;
    };
    const std::vector<Answer> answers{
        {"ApplySparseOverridesTightly",
         "sparse values read at the base accessor's stride rather than tightly packed", "GLTF-062"},
        {"ClampNormalizedSigned",
         "§3.6.2.2's max(c/N, -1) clamp omitted for signed normalized components", "GLTF-056"},
        {"RequiredSpan",
         "an accessor span computed with wrapping arithmetic passes cgltf_validate", "GLTF-039"},
    };
    for (const Answer& answer : answers)
    {
        SCOPED_TRACE(answer.symbol);
        EXPECT_TRUE(std::regex_search(
            importer, std::regex(std::string("\\b") + answer.symbol + "\\s*\\(")))
            << "GltfImportCore.cpp no longer defines or calls " << answer.symbol
            << ", which is CNA's answer to a proven cgltf fault (" << answer.fault
            << ", " << answer.task << "). If an upgrade really did retire it, retire the row in "
               "docs/gltf-conventions.md and known_bugs.md in the same commit.";
    }
}
