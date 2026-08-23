// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-DECL-GUARD -- the declaration-fidelity rule itself.
//
// The rendering oracle in VertexDeclarationLayoutTests.cpp proves the OUTCOME on whichever renderer
// this build selected. This file proves the DECISION, on every build, without a device: the
// predicate is a pure header-only function over a declaration and a description of the native
// layout a renderer actually programs, so the whole matrix REMED-GFX-217 and REMED-GFX-218 care
// about can be asserted directly rather than inferred from pixels.
//
// WHY THE RULE IS ASYMMETRIC. The exit triage rejected "the declaration must equal the stride
// table's entry": Vulkan renders a position-only stride-12 declaration correctly today because a
// declaration that names no colour supplies no colour for a stock program to consume, and an
// equality rule would reject that. Only what the declaration ACTUALLY DECLARES has to survive, and
// every leg below is written to fail if that asymmetry is ever quietly tightened into equality --
// the accepted cases are as load-bearing as the rejected ones.

#include <string>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

using CNA::Internal::Graphics::DeclaredVertexLayout;
using CNA::Internal::Graphics::DescribeUnrepresentableVertexDeclaration;
using CNA::Internal::Graphics::InferredLayoutForStride;
using CNA::Internal::Graphics::InferredVertexLayout;
using CNA::Internal::Graphics::RequireDeclarationMatchesStockProgram;
using CNA::Internal::Graphics::RequireFaithfulVertexDeclaration;
using CNA::Internal::Graphics::StockProgramInput;
using CNA::Internal::Graphics::UnlistedStrideLayout;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    /// A declaration built element-by-element, so a leg can state exactly the shape it means.
    std::vector<VertexElement> Elements(std::initializer_list<VertexElement> elements)
    {
        return std::vector<VertexElement>(elements);
    }

    VertexElement Pos(int offset = 0, VertexElementFormat f = VertexElementFormat::Vector3)
    {
        return VertexElement(offset, f, VertexElementUsage::Position, 0);
    }

    VertexElement Col(int offset, VertexElementFormat f = VertexElementFormat::Color,
                      int usageIndex = 0)
    {
        return VertexElement(offset, f, VertexElementUsage::Color, usageIndex);
    }

    VertexElement Uv(int offset, VertexElementFormat f = VertexElementFormat::Vector2,
                     int usageIndex = 0)
    {
        return VertexElement(offset, f, VertexElementUsage::TextureCoordinate, usageIndex);
    }

    VertexElement Nrm(int offset)
    {
        return VertexElement(offset, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    }

    VertexElement Tan(int offset)
    {
        return VertexElement(offset, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0);
    }

    VertexElement Weight(int offset)
    {
        return VertexElement(offset, VertexElementFormat::Vector4,
                             VertexElementUsage::BlendWeight, 0);
    }

    VertexElement Indices(int offset)
    {
        return VertexElement(offset, VertexElementFormat::Byte4,
                             VertexElementUsage::BlendIndices, 0);
    }

    /// Runs the predicate against the canonical layout for @p stride under @p fallback.
    std::string Verdict(const std::vector<VertexElement>& elements, int stride,
                        UnlistedStrideLayout fallback = UnlistedStrideLayout::RendererRefusesIt)
    {
        return DescribeUnrepresentableVertexDeclaration(
            elements, stride, stride, InferredLayoutForStride(stride, fallback));
    }
}

// ---------------------------------------------------------------------------
// Every built-in declaration must stay representable on every renderer in scope. These seven are
// the layouts SpriteBatch, the stock BasicEffect routes, SkinnedEffect, PbrEffect and every
// currently-green rendering test go through, so a rule that rejects one of them is a rule that
// broke the framework rather than a rule that made it safe.
// ---------------------------------------------------------------------------

TEST(VertexDeclarationFidelityTest, EveryBuiltInDeclarationIsRepresentable)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    struct BuiltIn { const char* name; const VertexDeclaration* declaration; };
    const BuiltIn builtIns[] = {
        {"VertexPositionColor", &VertexPositionColor::getVertexDeclarationStatic()},
        {"VertexPositionTexture", &VertexPositionTexture::getVertexDeclarationStatic()},
        {"VertexPositionColorTexture", &VertexPositionColorTexture::getVertexDeclarationStatic()},
        {"VertexPositionNormalTexture", &VertexPositionNormalTexture::getVertexDeclarationStatic()},
        {"VertexPositionNormalTangentTexture",
         &VertexPositionNormalTangentTexture::getVertexDeclarationStatic()},
        {"VertexPositionNormalTextureSkinned",
         &VertexPositionNormalTextureSkinned::getVertexDeclarationStatic()},
        {"VertexPositionNormalTangentTextureSkinned",
         &VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic()},
    };
    for (const BuiltIn& b : builtIns)
    {
        const int stride = b.declaration->getVertexStrideProperty();
        for (const UnlistedStrideLayout fallback :
             {UnlistedStrideLayout::RendererRefusesIt, UnlistedStrideLayout::PositionColorFallback,
              UnlistedStrideLayout::PositionOnlyFallback})
        {
            const std::string failure =
                Verdict(b.declaration->GetVertexElements(), stride, fallback);
            EXPECT_TRUE(failure.empty())
                << b.name << " (stride " << stride
                << ") was refused by the declaration guard: " << failure;
        }
    }
}

// The stride-56 SkinnedEffect+VertexColorEnabled layout has no built-in type of its own -- it is
// the stride-52 skinned stream with a packed colour appended at 52, which every renderer's own
// stride table already carries.
TEST(VertexDeclarationFidelityTest, SkinnedVertexColorStride56IsRepresentable)
{
    EXPECT_EQ("", Verdict(Elements({Pos(), Nrm(12), Uv(24), Weight(32), Indices(48), Col(52)}), 56));
}

// ---------------------------------------------------------------------------
// The asymmetry. A declaration that names FEWER semantics than the native layout binds is
// representable: nothing it declared is being reinterpreted.
// ---------------------------------------------------------------------------

TEST(VertexDeclarationFidelityTest, PositionOnlyStride12SurvivesThePositionColorFallback)
{
    // Vulkan's measured behaviour: its colored pipeline binds Position@0 and Color@12 and strides
    // by the buffer's own 12 bytes, and a position-only declaration renders correctly through it.
    EXPECT_EQ("", Verdict(Elements({Pos()}), 12, UnlistedStrideLayout::PositionColorFallback));
    // WebGPU's and Vulkan's instanced modules are position-only for the same stride.
    EXPECT_EQ("", Verdict(Elements({Pos()}), 12, UnlistedStrideLayout::PositionOnlyFallback));
}

TEST(VertexDeclarationFidelityTest, PositionOnlyAtAListedStrideIsRepresentable)
{
    // Position alone at stride 16, 20, 24 and 32: the native layout binds more, the declaration
    // names less, and the extra native fetches touch no declared byte.
    for (const int stride : {16, 20, 24, 32})
        EXPECT_EQ("", Verdict(Elements({Pos()}), stride)) << "stride " << stride;
}

TEST(VertexDeclarationFidelityTest, ARendererThatRefusesTheStrideKeepsItsOwnRejection)
{
    // Software, WebGPU's ordinary route and the three D3D renderers throw on an out-of-table stride
    // themselves. The guard must abstain there rather than substitute its own message for an
    // established boundary -- including for a declaration it could not otherwise represent.
    EXPECT_EQ("", Verdict(Elements({Pos(), Col(12)}), 28));
    EXPECT_EQ("", Verdict(Elements({Col(0), Pos(4)}), 28));
}

// ---------------------------------------------------------------------------
// The collisions. Same stride, different composition -- the shapes REMED-GFX-217 measured
// rendering silently wrong on all seven renderers.
// ---------------------------------------------------------------------------

TEST(VertexDeclarationFidelityTest, SameStrideDifferentSemanticOrderIsRefused)
{
    // colorPosition16: Color at 0, Position at 4, against a native Position@0 + Color@12.
    const std::string failure = Verdict(Elements({Col(0), Pos(4)}), 16);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("Color0@0"))
        << "the diagnostic must name the element that cannot be represented: " << failure;
}

TEST(VertexDeclarationFidelityTest, SameStrideSameSemanticsDifferentOffsetsIsRefused)
{
    // positionTextureColor24: the same three semantics as the stride-24 table entry, with the
    // colour moved to the end and the texture coordinate moved forward.
    const std::string failure = Verdict(Elements({Pos(), Uv(12), Col(20)}), 24);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("offset 16")) << failure;
}

TEST(VertexDeclarationFidelityTest, PaddedEquivalentAtAnotherTableEntrysStrideIsRefused)
{
    // positionColorPadded32: Position+Color's own semantics carried at stride 32, where the table
    // entry is Position+Normal+TexCoord. The colour has nowhere native to come from.
    const std::string failure = Verdict(Elements({Pos(), Col(12)}), 32);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("does not bind at all")) << failure;
}

TEST(VertexDeclarationFidelityTest, SameStrideSameSemanticsDifferentFormatIsRefused)
{
    // A colour declared as raw Byte4 where the native fetch normalizes it: same bytes, different
    // values, and nothing downstream could tell.
    const std::string failure =
        Verdict(Elements({Pos(), Col(12, VertexElementFormat::Byte4)}), 16);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("Color")) << failure;
}

TEST(VertexDeclarationFidelityTest, AnotherUsageIndexIsNotTheSameSemantic)
{
    // Color1 is not Color0. A native layout that binds only Color0 cannot supply it.
    const std::string failure = Verdict(Elements({Pos(), Col(12, VertexElementFormat::Color, 1)}), 16);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("Color1@12")) << failure;
}

TEST(VertexDeclarationFidelityTest, ASemanticNoNativeLayoutBindsIsRefused)
{
    const std::string failure = Verdict(
        Elements({Pos(), VertexElement(12, VertexElementFormat::Single,
                                       VertexElementUsage::PointSize, 0)}),
        16);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("PointSize")) << failure;
}

// ---------------------------------------------------------------------------
// The structural rules: record advance, range and overlap.
// ---------------------------------------------------------------------------

TEST(VertexDeclarationFidelityTest, AStrideThatDisagreesWithTheBufferIsRefused)
{
    // R0: the renderer advances records by the stride the buffer was uploaded with. A declaration
    // that claims another one describes a different record, whatever its elements say.
    const std::string failure = DescribeUnrepresentableVertexDeclaration(
        Elements({Pos(), Col(12)}), /*declaredStride=*/16, /*nativeRecordStride=*/24,
        InferredLayoutForStride(16, UnlistedStrideLayout::RendererRefusesIt));
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("wrong address")) << failure;
}

TEST(VertexDeclarationFidelityTest, AnElementReachingPastTheStrideIsRefused)
{
    // R3, checked before the layout comparison: a Vector3 at offset 12 of a 20-byte record ends at
    // 24. The native layout is irrelevant -- the record itself cannot hold it.
    const std::string failure =
        Verdict(Elements({Pos(), VertexElement(12, VertexElementFormat::Vector3,
                                               VertexElementUsage::Normal, 0)}),
                20);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("does not fit")) << failure;
}

TEST(VertexDeclarationFidelityTest, OverlappingDeclaredElementsAreRefused)
{
    // R4: two declared elements claiming the same bytes cannot both be read as declared.
    const std::string failure = Verdict(Elements({Pos(), Col(10)}), 16);
    EXPECT_NE("", failure);
    EXPECT_NE(std::string::npos, failure.find("same bytes")) << failure;
}

TEST(VertexDeclarationFidelityTest, ANativeFetchReachingIntoDeclaredBytesIsRefused)
{
    // R1: the native stride-32 layout reads a Normal out of bytes 12..24. A declaration that gives
    // 12..16 to a colour has those bytes reinterpreted even though it names no Normal at all.
    const std::string failure = Verdict(Elements({Pos(), Col(12), Uv(24)}), 32);
    EXPECT_NE("", failure);
}

TEST(VertexDeclarationFidelityTest, AnEmptyDeclarationIsNotSecondGuessed)
{
    // A buffer whose declaration was never propagated keeps exactly the behaviour it had before
    // this guard existed: there is nothing declared to reinterpret.
    EXPECT_EQ("", Verdict({}, 16));
    EXPECT_EQ("", DescribeUnrepresentableVertexDeclaration({}, 16, 999,
                                                           InferredLayoutForStride(16, UnlistedStrideLayout::RendererRefusesIt)));
}

TEST(VertexDeclarationFidelityTest, ABufferWithNoUploadYetIsNotSecondGuessed)
{
    EXPECT_EQ("", DescribeUnrepresentableVertexDeclaration(
                      Elements({Col(0), Pos(4)}), 16, /*nativeRecordStride=*/0,
                      InferredLayoutForStride(16, UnlistedStrideLayout::RendererRefusesIt)));
}

// ---------------------------------------------------------------------------
// The throwing wrapper: the exception class, and the fact that it names what went wrong.
// ---------------------------------------------------------------------------

TEST(VertexDeclarationFidelityTest, AnUnrepresentableDeclarationThrowsNotSupported)
{
    DeclaredVertexLayout declared;
    declared.Remember(VertexDeclaration(16, {Col(0), Pos(4)}));
    try
    {
        RequireFaithfulVertexDeclaration(declared, 16, UnlistedStrideLayout::RendererRefusesIt,
                                         "TestRenderer", "ordinary-indexed");
        FAIL() << "an unrepresentable declaration was accepted";
    }
    catch (const System::NotSupportedException& e)
    {
        const std::string what = e.what();
        EXPECT_NE(std::string::npos, what.find("TestRenderer")) << what;
        EXPECT_NE(std::string::npos, what.find("ordinary-indexed")) << what;
        EXPECT_NE(std::string::npos, what.find("Color0@0")) << what;
    }
}

TEST(VertexDeclarationFidelityTest, ARepresentableDeclarationThrowsNothing)
{
    DeclaredVertexLayout declared;
    declared.Remember(VertexDeclaration(16, {Pos(), Col(12)}));
    EXPECT_NO_THROW(RequireFaithfulVertexDeclaration(
        declared, 16, UnlistedStrideLayout::RendererRefusesIt, "TestRenderer", "ordinary-indexed"));
}

TEST(VertexDeclarationFidelityTest, RememberingIsIdempotentAndRecoversAfterARejection)
{
    // A -> B (rejected) -> A. The predicate is pure, so nothing about the rejected declaration may
    // survive into the next verdict.
    DeclaredVertexLayout declared;
    declared.Remember(VertexDeclaration(16, {Pos(), Col(12)}));
    EXPECT_NO_THROW(RequireFaithfulVertexDeclaration(
        declared, 16, UnlistedStrideLayout::RendererRefusesIt, "TestRenderer", "ordinary-indexed"));

    declared.Remember(VertexDeclaration(16, {Col(0), Pos(4)}));
    EXPECT_THROW(RequireFaithfulVertexDeclaration(
                     declared, 16, UnlistedStrideLayout::RendererRefusesIt, "TestRenderer",
                     "ordinary-indexed"),
                 System::NotSupportedException);

    declared.Remember(VertexDeclaration(16, {Pos(), Col(12)}));
    EXPECT_NO_THROW(RequireFaithfulVertexDeclaration(
        declared, 16, UnlistedStrideLayout::RendererRefusesIt, "TestRenderer", "ordinary-indexed"));
    EXPECT_EQ(16, declared.GetStride());
    EXPECT_EQ(2u, declared.GetElements().size());
}

// ---------------------------------------------------------------------------
// REMED-GFX-218's rule: an ordered attribute-location comparison, not a stride table. Its
// asymmetry is different -- a declaration shorter than the program's input list leaves the
// remaining inputs unbound, which is a missing input rather than a reinterpretation.
// ---------------------------------------------------------------------------

namespace
{
    constexpr StockProgramInput kPos{VertexElementUsage::Position, 0,
                                     VertexElementFormat::Vector3, "aPos"};
    constexpr StockProgramInput kColor{VertexElementUsage::Color, 0, VertexElementFormat::Color,
                                       "aColor"};
    constexpr StockProgramInput kUv{VertexElementUsage::TextureCoordinate, 0,
                                    VertexElementFormat::Vector2, "aUV"};
    constexpr StockProgramInput kNormal{VertexElementUsage::Normal, 0,
                                        VertexElementFormat::Vector3, "aNormal"};
    constexpr StockProgramInput kColored[] = {kPos, kColor};
    constexpr StockProgramInput kLit[] = {kPos, kNormal, kUv};
}

TEST(VertexDeclarationFidelityTest, StockProgramAcceptsTheDeclarationItsLocationsDescribe)
{
    EXPECT_NO_THROW(RequireDeclarationMatchesStockProgram(
        Elements({Pos(), Col(12)}), kColored, 2, "EasyGL", "colored3d"));
    EXPECT_NO_THROW(RequireDeclarationMatchesStockProgram(
        Elements({Pos(), Nrm(12), Uv(24)}), kLit, 3, "EasyGL", "lit_textured3d"));
}

TEST(VertexDeclarationFidelityTest, StockProgramAcceptsAShorterDeclaration)
{
    // A position-only declaration leaves aColor unbound, which reads as GL's constant attribute
    // value -- a missing input, not a reinterpretation of declared bytes, and exactly what
    // EasyGL renders correctly today.
    EXPECT_NO_THROW(RequireDeclarationMatchesStockProgram(
        Elements({Pos()}), kColored, 2, "EasyGL", "colored3d"));
    EXPECT_NO_THROW(
        RequireDeclarationMatchesStockProgram({}, kColored, 2, "EasyGL", "colored3d"));
}

TEST(VertexDeclarationFidelityTest, StockProgramAcceptsReorderedSemantics)
{
    EXPECT_NO_THROW(RequireDeclarationMatchesStockProgram(
        Elements({Uv(12), Nrm(20), Pos(0)}), kLit, 3, "EasyGL", "lit_textured3d"));
}

TEST(VertexDeclarationFidelityTest, StockProgramIgnoresUnusedSemantic)
{
    EXPECT_NO_THROW(RequireDeclarationMatchesStockProgram(
        Elements({Pos(), Col(12)}), kLit, 3, "EasyGL", "lit_textured3d"));
}

TEST(VertexDeclarationFidelityTest, StockProgramRefusesWrongFormatForConsumedSemantic)
{
    EXPECT_THROW(RequireDeclarationMatchesStockProgram(
                     Elements({Pos(), VertexElement(12, VertexElementFormat::Vector4,
                                                    VertexElementUsage::Normal, 0)}),
                     kLit, 3, "EasyGL", "lit_textured3d"),
                 System::NotSupportedException);
}

// --- plans/plan_gltf.md GLTF-155: the importer's strides and the canonical table are one set ----------
//
// The table exists so there is a single source of truth for what a byte stride means. That is only
// worth anything if every producer's strides are actually in it: a stride the importer emits but
// the table does not know is one no renderer can bind, and one the table knows but no producer
// emits is a row nobody validates.
//
// The importer's own selection is
//     skinned ? (colored ? 56 : (usePbr ? 68 : 52))
//             : (colored ? 24 : (usePbr ? 48 : (useDualTexture ? 20 : 32)))
// plus stride 16 from the .cnj path's position+colour layout. Restating it here is deliberate:
// this test is the coupling, so it has to name both sides.

TEST(VertexDeclarationFidelity, EveryStrideTheGltfImporterCanEmitIsInTheCanonicalTable)
{
    for (const int stride : {16, 20, 24, 32, 48, 52, 56, 60, 68, 76})
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        ASSERT_TRUE(layout.known)
            << "the glTF importer can emit this stride and no renderer would be able to bind it";
        EXPECT_GT(layout.count, 0u);

        // Position at offset 0 is the one invariant every producer and consumer relies on
        // (GLTF-150), so it is asserted per stride rather than assumed once.
        bool positionAtZero = false;
        for (std::size_t i = 0; i < layout.count; ++i)
        {
            if (layout.elements[i].usage ==
                    Microsoft::Xna::Framework::Graphics::VertexElementUsage::Position &&
                layout.elements[i].usageIndex == 0)
            {
                positionAtZero = layout.elements[i].offset == 0;
            }
        }
        EXPECT_TRUE(positionAtZero) << "Position must be the first element of every stride";
    }
}

TEST(VertexDeclarationFidelity, AStrideTheTableDoesNotKnowIsReportedAsUnknownRatherThanGuessed)
{
    // The property the importer's new refusal depends on: an unlisted stride must come back
    // `known == false`, not with a plausible-looking guessed layout. A table that invented one
    // would turn GLTF-157's loud error back into a silent wrong bind.
    for (const int stride : {13, 40, 44, 64, 1024})
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_FALSE(layout.known);
    }
}
