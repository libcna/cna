// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-473. A fixed-function renderer binds each client array at a literal byte offset,
// and that literal is right only for the records its route was written for. OPENGLES1 routed every
// draw it has no fixed-function equivalent for -- PbrEffect, SkinnedEffect, a custom ShaderEffect,
// instancing, an unmet dual-texture or environment-map precondition -- into its colour path, which
// binds a colour at offset 12. Offset 12 carries a colour in exactly two of CNA's canonical records
// and carries the NORMAL in every PBR and skinned one, so a valid core glTF vertex-coloured
// metallic-roughness primitive (stride 60) was drawn with per-vertex colours read out of the bytes
// of its own normals.
//
// The decision itself is pure -- stride in, verdict out -- so it is pinned here as a table over
// EVERY canonical stride rather than as an assertion about the one that was reported. A fix that
// special-cased stride 60 would pass a stride-60 test and leave 52, 56, 68, 76 and 80 corrupt.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>

#include "CNA/Internal/Renderers/Common/FixedFunctionArrayLayoutSupport.hpp"

using CNA::Internal::Renderers::FixedFunctionClientArrayMatchesCanonicalLayoutEXT;
using CNA::Internal::Renderers::RequireFixedFunctionClientArrayEXT;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    /// Every stride the canonical table lists, so the tables below cannot quietly omit one.
    constexpr std::array<int, 11> kCanonicalStrides{{16, 20, 24, 32, 48, 52, 56, 60, 68, 76, 80}};

    /// The offset OpenGLES1's colour path passes to glColorPointer, unconditionally.
    constexpr int kColourPathOffset = 12;

    bool ColourArrayAccepted(int stride)
    {
        return FixedFunctionClientArrayMatchesCanonicalLayoutEXT(
            stride, VertexElementUsage::Color, 0, kColourPathOffset);
    }
}

TEST(FixedFunctionArrayLayout, OnlyTheTwoRecordsThatActuallyCarryAColourAtTwelveAcceptTheColourPath)
{
    // The whole of GLTF-473 in one table. Stride 16 (VertexPositionColor) and stride 24
    // (VertexPositionColorTexture) are the only canonical records with a Color at offset 12; every
    // other one keeps something else there, and reading it produces a perfectly valid RGBA value
    // that is not the surface's colour -- which is why nothing caught this by looking at output.
    constexpr std::array<int, 2> accepted{{16, 24}};

    for (const int stride : kCanonicalStrides)
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        const bool shouldAccept =
            std::find(accepted.begin(), accepted.end(), stride) != accepted.end();
        EXPECT_EQ(shouldAccept, ColourArrayAccepted(stride))
            << (shouldAccept
                    ? "this record does carry a colour at offset 12, so refusing it would break a "
                      "draw that renders correctly today"
                    : "this record does not carry a colour at offset 12, so accepting it lets the "
                      "colour path read some other semantic's bytes as an RGBA value");
    }
}

TEST(FixedFunctionArrayLayout, EveryPbrAndSkinnedRecordIsRefusedByTheColourPathNotOnlyStrideSixty)
{
    // Stated separately and by name, because the reported defect was stride 60 and a fix that
    // special-cased it would leave the rest of the family reading normals as colours. Offset 12 is
    // the NORMAL in all seven, so the wrong value is not even noise -- it is the same wrong value
    // every frame, which reads as a deliberate tint.
    for (const int stride : {48, 52, 56, 60, 68, 76, 80})
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        EXPECT_FALSE(ColourArrayAccepted(stride));
        const CNA::Internal::Graphics::CanonicalSemanticOffsetEXT normalAt =
            CNA::Internal::Graphics::CanonicalOffsetOfSemanticEXT(
                stride, VertexElementUsage::Normal, 0);
        ASSERT_TRUE(normalAt.present) << "the fixture for this test is wrong, not the guard";
        EXPECT_EQ(kColourPathOffset, normalAt.offset)
            << "offset 12 is what the colour path reads; if this record no longer keeps its NORMAL "
               "there, this test is asserting the wrong thing";
    }
}

TEST(FixedFunctionArrayLayout, TheTextureArmRefusesEveryRecordWhoseUvIsNotEightBytesFromTheEnd)
{
    // The colour array is the reported half; this is the other one. OpenGLES1's ordinary route
    // derives its UV offset as `stride - 8`, which is where UV0 sits in every record that route was
    // written for and is NOT where it sits in any of the dual-UV or skinned ones -- stride 60 keeps
    // UV0 at 40, not 52. Same class of bug, same guard, and it is only reachable at all because a
    // caller can bind those strides to a non-PBR effect.
    struct TextureCase { int stride; bool accepted; };
    constexpr std::array<TextureCase, 10> cases{{
        {20, true}, {24, true}, {32, true}, {48, true},
        {52, false}, {56, false}, {60, false}, {68, false}, {76, false}, {80, false},
    }};
    for (const TextureCase& c : cases)
    {
        SCOPED_TRACE("stride " + std::to_string(c.stride));
        EXPECT_EQ(c.accepted, FixedFunctionClientArrayMatchesCanonicalLayoutEXT(
                                  c.stride, VertexElementUsage::TextureCoordinate, 0, c.stride - 8));
    }
}

TEST(FixedFunctionArrayLayout, TheNormalArmAcceptsExactlyTheRecordsThatCarryANormalAtTwelve)
{
    // The third array, for completeness: OpenGLES1 binds glNormalPointer at offset 12 and gates it
    // on stride 32 today. The guard states the rule the gate is an instance of, so widening the gate
    // later cannot silently start reading a record with no normal in it.
    for (const int stride : kCanonicalStrides)
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        const bool hasNormalAtTwelve = (stride != 16 && stride != 20 && stride != 24);
        EXPECT_EQ(hasNormalAtTwelve, FixedFunctionClientArrayMatchesCanonicalLayoutEXT(
                                         stride, VertexElementUsage::Normal, 0, 12));
    }
}

TEST(FixedFunctionArrayLayout, AStrideTheCanonicalTableDoesNotListIsLeftAlone)
{
    // OpenGLES1's own dual-UV record is 28 bytes and is not in the shared table at all. The guard
    // must abstain there rather than refuse: it has no offset to compare against, and refusing a
    // renderer-local layout on the strength of not recognising it would break a route that works.
    for (const int stride : {12, 28, 40, 64, 128})
    {
        SCOPED_TRACE("stride " + std::to_string(stride));
        EXPECT_TRUE(FixedFunctionClientArrayMatchesCanonicalLayoutEXT(
            stride, VertexElementUsage::Color, 0, 12));
        EXPECT_TRUE(FixedFunctionClientArrayMatchesCanonicalLayoutEXT(
            stride, VertexElementUsage::TextureCoordinate, 0, stride - 8));
    }
}

TEST(FixedFunctionArrayLayout, ARefusalNamesTheRendererTheOffsetsTheTaskAndTheWayOut)
{
    // A refusal a caller cannot act on is barely better than the wrong picture. GLTF-473 was found
    // precisely because the message the renderer DID produce for an adjacent failure named the
    // wrong thing, so the content of this one is asserted rather than assumed.
    try
    {
        RequireFixedFunctionClientArrayEXT(
            60, VertexElementUsage::Color, 0, kColourPathOffset, "glColorPointer", "OPENGLES1",
            "ordinary-nonindexed fallback",
            "PbrEffect/SkinnedPbrEffect (ES 1.1 has no programmable pipeline)");
        FAIL() << "a stride-60 record has no colour at offset 12 and must be refused";
    }
    catch (const System::NotSupportedException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(std::string::npos, message.find("OPENGLES1")) << message;
        EXPECT_NE(std::string::npos, message.find("glColorPointer")) << message;
        EXPECT_NE(std::string::npos, message.find("stride-60")) << message;
        EXPECT_NE(std::string::npos, message.find("Normal0"))
            << "the message must say what the record actually keeps at offset 12, or a reader "
               "cannot tell this from an ordinary unsupported-stride refusal: " << message;
        EXPECT_NE(std::string::npos, message.find("offset 56"))
            << "and where the colour really is: " << message;
        EXPECT_NE(std::string::npos, message.find("GLTF-473")) << message;
        EXPECT_NE(std::string::npos, message.find("PbrEffect")) << message;
        EXPECT_NE(std::string::npos, message.find("Use a renderer that implements it")) << message;
    }
}

TEST(FixedFunctionArrayLayout, ADrawThatWouldHaveBeenCorrectIsStillPermitted)
{
    // The control. Every refusal guard risks refusing more than it was written for, and this one is
    // installed on routes that render real content today: a VertexPositionColor draw is exactly what
    // OpenGLES1's colour path exists for, and it must keep working with no effect named.
    EXPECT_NO_THROW(RequireFixedFunctionClientArrayEXT(
        16, VertexElementUsage::Color, 0, 12, "glColorPointer", "OPENGLES1", "colored-nonindexed",
        nullptr));
    EXPECT_NO_THROW(RequireFixedFunctionClientArrayEXT(
        24, VertexElementUsage::Color, 0, 12, "glColorPointer", "OPENGLES1", "colored-indexed",
        nullptr));
    EXPECT_NO_THROW(RequireFixedFunctionClientArrayEXT(
        32, VertexElementUsage::Normal, 0, 12, "glNormalPointer", "OPENGLES1",
        "ordinary-nonindexed", nullptr));
    EXPECT_NO_THROW(RequireFixedFunctionClientArrayEXT(
        48, VertexElementUsage::TextureCoordinate, 0, 40, "glTexCoordPointer", "OPENGLES1",
        "ordinary-indexed", nullptr));
}
