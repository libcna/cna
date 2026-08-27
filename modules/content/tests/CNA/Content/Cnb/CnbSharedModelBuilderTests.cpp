// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-100: the shared .cnj/.cnb/glTF model mesh builder.
//
// Two kinds of test here, and they defend different things.
//
// The behavioural half is already covered elsewhere and deliberately not repeated:
// CnbModelEquivalenceTest loads a corpus of fixtures through both the .cnj and the .cnb path and
// compares them field by field, so a divergence between the front-ends fails there.
//
// What that suite CANNOT catch is the duplication coming back. It compares outputs, so three
// copies that currently agree pass it -- and they passed it right up until this task, which is
// exactly how the drift risk survived so long. So the other half of this file is a source guard:
// the construction steps that used to be written out three times must now appear once.

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

#include "CNA/Content/Cnb/CnbModelData.hpp"

namespace
{
    std::string ReadContentManagerSource()
    {
        for (const char* prefix : {"", "../", "../../"})
        {
            const std::filesystem::path candidate =
                std::string(prefix) + "modules/content/src/Xna/ContentManager.cpp";
            if (std::filesystem::exists(candidate))
            {
                std::ifstream file(candidate);
                std::ostringstream ss;
                ss << file.rdbuf();
                return ss.str();
            }
        }
        return {};
    }

    std::size_t CountOccurrences(const std::string& haystack, const std::string& needle)
    {
        std::size_t count = 0;
        for (std::size_t at = haystack.find(needle); at != std::string::npos;
             at = haystack.find(needle, at + needle.size()))
        {
            ++count;
        }
        return count;
    }
}

TEST(CnbSharedModelBuilderTest, ThePartTopologyIsAppliedInExactlyOnePlace)
{
    // Before CNBF-100 the runtime glTF reader, the .cnj reader and the .cnb decoder each carried
    // the topology to the draw themselves. Three copies of one rule is three chances for one to
    // drift, and a drift here does not fail a build -- it makes .cnj and .cnb models draw
    // differently, which is a bug an output comparison only finds after someone ships it.
    const std::string source = ReadContentManagerSource();
    if (source.empty()) { GTEST_SKIP() << "ContentManager.cpp not found (run from source root)"; }

    EXPECT_EQ(CountOccurrences(source, "setPrimitiveTypeEXTProperty("), 1u)
        << "the part topology is applied in more than one place again; it belongs in "
           "BuildModelMeshPartGeometryEXT so all three model front-ends cannot disagree";
}

TEST(CnbSharedModelBuilderTest, TheSpecularSamplerSlotSplitIsWrittenInExactlyOnePlace)
{
    // Slots 0-4 are the ordinary texture samplers and 5+ are the specular ones. That boundary was
    // spelled out separately by every front-end, and it is the kind of constant that gets fixed
    // in one copy and not the others.
    const std::string source = ReadContentManagerSource();
    if (source.empty()) { GTEST_SKIP() << "ContentManager.cpp not found (run from source root)"; }

    EXPECT_EQ(CountOccurrences(source, "setSpecularSamplerStateEXTProperty("), 1u)
        << "the sampler slot split is written in more than one place again; it belongs in "
           "ApplyPartSamplerStatesEXT";
}

TEST(CnbSharedModelBuilderTest, TheSharedBuilderIsTheOneEveryModelFrontEndCalls)
{
    // The positive statement of the same property: all three front-ends reach the builder. A
    // fourth Model front-end that quietly builds its own parts would fail the two counts above,
    // but this makes the intended structure legible rather than only forbidding the wrong one.
    const std::string source = ReadContentManagerSource();
    if (source.empty()) { GTEST_SKIP() << "ContentManager.cpp not found (run from source root)"; }

    EXPECT_EQ(CountOccurrences(source, "BuiltModelPartEXT BuildModelMeshPartGeometryEXT("), 1u);
    EXPECT_EQ(CountOccurrences(source, "BuildModelMeshPartGeometryEXT(device, geometry)"), 3u)
        << "expected exactly three callers: the runtime glTF reader, the .cnj reader and the "
           ".cnb decoder";
    EXPECT_EQ(CountOccurrences(source, "ApplyPartSamplerStatesEXT(*part,"), 3u)
        << "expected the same three front-ends to apply sampler state through the shared helper";

    // SkinnedModelEXT also constructs a ModelMeshPart and is deliberately NOT routed through the
    // builder: it is a different asset type with a 16-bit-only index path, its own index range
    // validation and no topology or sampler concept at all. Recorded so its exemption reads as a
    // decision rather than an oversight.
    EXPECT_NE(source.find("SkinnedModel part '"), std::string::npos);
}

TEST(CnbSharedModelBuilderTest, ThePrimitiveCountOverrideExistsForTheCnbPathOnly)
{
    // The one genuine difference between the front-ends, and the reason the builder takes an
    // override rather than always deriving: a .cnb STATES its primitive count, and the decoder
    // has already cross-checked it against the topology (CNBF-H012), so re-deriving it here would
    // throw that check away. The other two have nothing to state.
    const std::string source = ReadContentManagerSource();
    if (source.empty()) { GTEST_SKIP() << "ContentManager.cpp not found (run from source root)"; }

    EXPECT_EQ(CountOccurrences(source, "geometry.primitiveCountOverride ="), 1u)
        << "only the .cnb path supplies a primitive count; the other two derive it";
}
