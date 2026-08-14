// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-099 / GLTF-100: the vertex-stride decision table, and what each row loses.
//
// §2.3's table was a nested ternary chain, which has three problems a table does not. It cannot be
// enumerated, so no test can walk its rows and every assertion had to pick a combination by hand.
// It cannot be asked what a row *loses*, so a downgrade was a property of whichever branch was
// taken rather than a fact about the layout. And every renderer's own `ApplyLayout` is an implicit
// restatement of the same rule, with no way to check the two agree.
//
// This file holds the table to three things: it answers for every combination, it agrees with the
// ABI's own stride table about which slots each stride has, and its loss text is true -- a row
// claiming to lose a Normal must select a stride with no normal slot, and a row claiming to lose
// nothing must select one that has every slot the combination needs.

#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "GltfFixtureCorpus.hpp"

using CNA::Internal::GltfImport::SelectVertexLayoutEXT;
using CNA::Internal::GltfImport::VertexLayoutRequestEXT;
using CNA::Internal::GltfImport::VertexLayoutRuleEXT;
using CNA::Internal::GltfImport::VertexLayoutTableEXT;

namespace
{
    namespace fidelity = CNA::Internal::Graphics;
    using Usage = Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    /// True when the ABI's canonical layout for @p stride carries @p usage.
    bool StrideCarries(int stride, Usage usage, int usageIndex = 0)
    {
        const fidelity::InferredVertexLayout layout = fidelity::InferredLayoutForStride(
            stride, fidelity::UnlistedStrideLayout::RendererRefusesIt);
        if (!layout.known) { return false; }
        for (std::size_t i = 0; i < layout.count; ++i)
        {
            if (layout.elements[i].usage == usage &&
                layout.elements[i].usageIndex == usageIndex) { return true; }
        }
        return false;
    }

    /// A human-readable spelling of a combination, for a failure message.
    std::string Describe(const VertexLayoutRequestEXT& r)
    {
        std::string out = "skinned=";
        out += r.skinned ? "1" : "0";
        out += " colored=";
        out += r.colored ? "1" : "0";
        out += " usePbr=";
        out += r.usePbr ? "1" : "0";
        out += " useDualTexture=";
        out += r.useDualTexture ? "1" : "0";
        out += " hasSecondTexcoord=";
        out += r.hasSecondTexcoord ? "1" : "0";
        return out;
    }
}

TEST(GltfVertexLayoutTable, EveryCombinationOfTheFiveFlagsSelectsARowFromTheAbiTable)
{
    // Totality, over all 32 combinations rather than only the reachable rows the table spells. A selector
    // with no answer for a combination is not a hypothetical: `useDualTexture` is only ever set
    // alongside three other falses today, and a future change that set it elsewhere would fall
    // through a ternary chain silently.
    for (int bits = 0; bits < 32; ++bits)
    {
        const VertexLayoutRequestEXT request{
            (bits & 1) != 0, (bits & 2) != 0, (bits & 4) != 0, (bits & 8) != 0,
            (bits & 16) != 0};
        SCOPED_TRACE(Describe(request));
        const VertexLayoutRuleEXT& rule = SelectVertexLayoutEXT(request);

        // The stride it picked must be one the ABI actually knows, or every renderer refuses the
        // buffer at bind time -- which is a far later and far less legible failure.
        const fidelity::InferredVertexLayout layout = fidelity::InferredLayoutForStride(
            rule.stride, fidelity::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known)
            << "stride " << rule.stride << " is not in the vertex ABI table";
        EXPECT_TRUE(StrideCarries(rule.stride, Usage::Position))
            << "every layout must carry Position -- it is the one attribute glTF requires";
    }
}

TEST(GltfVertexLayoutTable, EveryRowSelectsAStrideThatCarriesWhatTheCombinationAsksFor)
{
    // The table's rows are claims about slots, and this is where they meet the ABI's own table.
    // A skinned row that selected a stride with no BlendIndices would produce a buffer the shader
    // reads garbage joints out of; a PBR row with no Tangent slot would normal-map with a
    // rest-pose basis. Neither fails at import.
    for (const VertexLayoutRuleEXT& rule : VertexLayoutTableEXT())
    {
        SCOPED_TRACE(Describe(rule.request) + " -> stride " + std::to_string(rule.stride));

        if (rule.request.skinned)
        {
            EXPECT_TRUE(StrideCarries(rule.stride, Usage::BlendIndices));
            EXPECT_TRUE(StrideCarries(rule.stride, Usage::BlendWeight));
        }
        if (rule.request.colored)
        {
            EXPECT_TRUE(StrideCarries(rule.stride, Usage::Color))
                << "a coloured primitive selected a layout with no colour slot, so its COLOR_0 "
                   "would be dropped without even the table saying so";
        }
        // A row that loses nothing must genuinely lose nothing: PBR needs a tangent, and anything
        // that is not the dual-texture layout needs a normal to be lit at all.
        if (rule.unrepresentable.empty())
        {
            EXPECT_TRUE(StrideCarries(rule.stride, Usage::Normal))
                << "the row claims to carry everything and has no normal slot";
            if (rule.request.usePbr)
            {
                EXPECT_TRUE(StrideCarries(rule.stride, Usage::Tangent))
                    << "a PBR row must carry a tangent, or a normal map lights with a rest-pose "
                       "basis and looks like a material bug";
                if (rule.request.hasSecondTexcoord)
                {
                    EXPECT_TRUE(StrideCarries(
                        rule.stride, Usage::TextureCoordinate, 1))
                        << "a two-UV PBR row must carry TextureCoordinate1";
                }
            }
        }
    }
}

TEST(GltfVertexLayoutTable, ARowClaimingToLoseANormalSelectsAStrideWithNoNormalSlot)
{
    // The other direction, and the one that keeps the loss text honest. Prose that drifted from
    // the layout would be worse than no prose: it would be a report an author believes.
    for (const VertexLayoutRuleEXT& rule : VertexLayoutTableEXT())
    {
        SCOPED_TRACE(Describe(rule.request) + " -> stride " + std::to_string(rule.stride));
        const bool claimsNormalLoss = rule.unrepresentable.find("Normal") != std::string::npos;
        EXPECT_EQ(claimsNormalLoss, !StrideCarries(rule.stride, Usage::Normal))
            << "the row's text and the ABI table disagree about whether stride " << rule.stride
            << " has a normal slot";
    }
}

TEST(GltfVertexLayoutTable, EveryRowThatLosesSomethingSaysWhatAndWhy)
{
    // A downgrade with no text is exactly what GLTF-100 exists to remove, and a text that only
    // names the casualty without the reason is nearly as bad -- an author who reads "the Normal
    // stream" and not why cannot tell a limitation from a bug.
    std::size_t lossyRows = 0;
    for (const VertexLayoutRuleEXT& rule : VertexLayoutTableEXT())
    {
        if (rule.unrepresentable.empty()) { continue; }
        ++lossyRows;
        SCOPED_TRACE(Describe(rule.request));
        EXPECT_NE(std::string::npos, rule.unrepresentable.find(':'))
            << "the text names a casualty but no reason: " << rule.unrepresentable;
        EXPECT_GT(rule.unrepresentable.size(), 30u) << rule.unrepresentable;
    }
    EXPECT_GT(lossyRows, 0u)
        << "no row loses anything, so this suite is asserting nothing -- CNA has at least three "
           "such combinations (GLTF-085, GLTF-086, GLTF-241)";
}

TEST(GltfVertexLayoutTable, TheCorpusAgreesWithTheTableOnEveryPrimitiveItExtracts)
{
    // The table and the importer could agree on paper and diverge in practice, because the
    // importer computes the four flags itself. Swept over the corpus so the two are checked
    // against real assets rather than against constructed requests.
    using namespace CNA::Internal::GltfImport;

    std::size_t compared = 0;
    std::set<int> stridesSeen;
    for (const std::string& id : CnaTest::GltfOracle::CorpusFixtureIds())
    {
        const CnaTest::GltfOracle::LoadedFixture fixture(id);
        if (!fixture.Ok() || CnaTest::GltfOracle::IsRejectionFixture(fixture.Expected()))
        {
            continue;
        }
        SCOPED_TRACE(id);
        const cgltf_data& data = fixture.Data();

        // Extracted WITH the file's own skeleton where it has one. Passing nullptr throughout
        // would make every primitive unskinned, so the four skinned rows of the table would go
        // unexercised by any real asset while the sweep still reported success -- which is the
        // shape of blind spot this file exists to close, not to reproduce.
        const SceneGraphOut scene = BuildSceneGraph(&data);
        std::vector<SkeletonResult> skeletons;
        for (cgltf_size sk = 0; sk < data.skins_count; ++sk)
        {
            try
            {
                skeletons.push_back(BuildSkeleton(
                    &data.skins[sk], scene,
                    Microsoft::Xna::Framework::Matrix::getIdentityProperty(), 1.0f));
            }
            catch (const std::exception&) { /* owned by the skin suites */ }
        }
        const SkeletonResult* skeleton = skeletons.empty() ? nullptr : &skeletons.front();

        for (cgltf_size m = 0; m < data.meshes_count; ++m)
        {
            for (cgltf_size p = 0; p < data.meshes[m].primitives_count; ++p)
            {
                MeshOut out;
                try
                {
                    out = ExtractMesh(&data, data.meshes[m].primitives[p], "probe", skeleton, 1.0f);
                }
                catch (const std::exception&) { continue; }

                const VertexLayoutRuleEXT& rule = SelectVertexLayoutEXT(
                    VertexLayoutRequestEXT{out.skinned, out.colored, out.usePbr,
                                            out.useDualTexture, out.hasSecondTexcoordEXT});
                EXPECT_EQ(rule.stride, out.stride)
                    << "the importer and the table disagree for " << Describe(
                           VertexLayoutRequestEXT{out.skinned, out.colored, out.usePbr,
                                                   out.useDualTexture,
                                                   out.hasSecondTexcoordEXT});
                EXPECT_EQ(rule.unrepresentable, out.unrepresentableForStrideEXT);
                stridesSeen.insert(out.stride);
                ++compared;
            }
        }
    }
    EXPECT_GT(compared, 0u) << "no primitive was compared -- the sweep proved nothing";
    // The corpus is meant to reach the whole ABI. Stated as a floor rather than an exact set, so
    // adding a fixture that reaches a new stride does not fail this test.
    EXPECT_GE(stridesSeen.size(), 5u)
        << "the corpus reaches only " << stridesSeen.size()
        << " strides, so most rows of the table are unexercised by any real asset";
}
