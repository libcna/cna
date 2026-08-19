// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-465: the shared refusal that keeps a renderer out of the one state that is a
// defect rather than a limitation -- accepting a valid glTF asset and drawing it with different core
// semantics. Every renderer that does not evaluate glTF's COLOR_0 base-colour product calls this, so
// its predicate is worth pinning exactly: too wide and it refuses content that renders correctly
// today, too narrow and the silent degradation it exists to stop comes back.

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/VertexColourPbrSupport.hpp"

using CNA::Internal::Renderers::DrawCarriesEnabledVertexColourPbrEXT;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::RequireVertexColourPbrSupportEXT;

namespace
{
    GpuDrawParams PbrDraw()
    {
        GpuDrawParams params;
        params.pbr = true;
        params.vertexColorEnabled = true;
        return params;
    }
}

TEST(VertexColourPbrSupport, TheTwoColourCarryingPbrStridesAreRefusedWhenTheEffectEnabledTheColour)
{
    // Stride 60 is the rigid PBR record (GLTF-462) and stride 80 the skinned one (GLTF-463); those
    // are the only canonical layouts with a COLOR_0 slot a PBR shader is meant to multiply by.
    for (const std::size_t stride : {std::size_t{60}, std::size_t{80}})
    {
        SCOPED_TRACE(stride);
        EXPECT_TRUE(DrawCarriesEnabledVertexColourPbrEXT(PbrDraw(), stride));
        EXPECT_THROW(RequireVertexColourPbrSupportEXT(PbrDraw(), stride, "TESTRENDERER"),
                     std::runtime_error);
    }
}

TEST(VertexColourPbrSupport, TheRefusalNamesTheRendererTheSemanticAndTheApplicationsOwnOptOut)
{
    // A refusal a caller cannot act on is only marginally better than a wrong picture, so the message
    // has to carry three things: which renderer refused, what the missing semantic is, and the two
    // ways out (a renderer that implements it, or deliberately disabling the colour).
    try
    {
        RequireVertexColourPbrSupportEXT(PbrDraw(), 60, "TESTRENDERER");
        FAIL() << "the draw was accepted";
    }
    catch (const std::runtime_error& error)
    {
        const std::string what = error.what();
        EXPECT_NE(std::string::npos, what.find("TESTRENDERER"));
        EXPECT_NE(std::string::npos, what.find("COLOR_0"));
        EXPECT_NE(std::string::npos, what.find("3.9.2"));
        EXPECT_NE(std::string::npos, what.find("GLTF-465"));
        EXPECT_NE(std::string::npos, what.find("VertexColorEnabledEXT=false"));
    }
}

TEST(VertexColourPbrSupport, NothingThatRenderedBeforeThisGuardExistedStopsRendering)
{
    // The guard must be inert for every draw that is not exactly "a PBR draw whose vertex record
    // carries a colour the effect enabled". Each case here is content a renderer draws correctly
    // today, and refusing any of them would be a regression caused by the fix.
    const std::size_t colourCarrying = 60;

    // An UNCOLOURED dual-UV PBR primitive: the importer fills stride 60's slot with opaque white and
    // leaves the effect's flag false, so the identity is what the file actually asked for.
    GpuDrawParams uncoloured = PbrDraw();
    uncoloured.vertexColorEnabled = false;
    EXPECT_FALSE(DrawCarriesEnabledVertexColourPbrEXT(uncoloured, colourCarrying));
    EXPECT_NO_THROW(RequireVertexColourPbrSupportEXT(uncoloured, colourCarrying, "TESTRENDERER"));

    // A NON-PBR draw at the same stride: BasicEffect/SkinnedEffect vertex colour is a different,
    // long-supported feature and has nothing to do with §3.9.2's metallic-roughness product.
    GpuDrawParams basic = PbrDraw();
    basic.pbr = false;
    EXPECT_FALSE(DrawCarriesEnabledVertexColourPbrEXT(basic, colourCarrying));
    EXPECT_NO_THROW(RequireVertexColourPbrSupportEXT(basic, colourCarrying, "TESTRENDERER"));

    // Every other canonical stride, including the two PBR records with no colour slot at all (48 and
    // 68) and the two dual-UV ones (60's sibling 76, and 56).
    for (const std::size_t stride : {std::size_t{16}, std::size_t{20}, std::size_t{24},
                                     std::size_t{32}, std::size_t{48}, std::size_t{52},
                                     std::size_t{56}, std::size_t{68}, std::size_t{76}})
    {
        SCOPED_TRACE(stride);
        EXPECT_FALSE(DrawCarriesEnabledVertexColourPbrEXT(PbrDraw(), stride));
        EXPECT_NO_THROW(RequireVertexColourPbrSupportEXT(PbrDraw(), stride, "TESTRENDERER"));
    }
}

TEST(VertexColourPbrSupport, TheDrawParamDefaultAloneDoesNotTripTheGuard)
{
    // GpuDrawParams::vertexColorEnabled defaults TRUE, while PbrEffect/SkinnedPbrEffect's own
    // VertexColorEnabledEXT defaults FALSE and is always written by FillGpuDrawParams. A guard that
    // fired on the raw default would refuse draws no effect ever asked for -- so this pins that the
    // pbr flag is required as well, which only a PBR effect ever sets.
    GpuDrawParams defaults;
    EXPECT_TRUE(defaults.vertexColorEnabled);
    EXPECT_FALSE(defaults.pbr);
    EXPECT_FALSE(DrawCarriesEnabledVertexColourPbrEXT(defaults, 60));
    EXPECT_NO_THROW(RequireVertexColourPbrSupportEXT(defaults, 60, "TESTRENDERER"));
}
