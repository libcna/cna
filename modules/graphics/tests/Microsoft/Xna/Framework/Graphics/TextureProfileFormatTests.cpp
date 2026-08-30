// SPDX-License-Identifier: MS-PL
// REMED-GFX-242: which SurfaceFormats a GraphicsProfile permits for a Texture2D.
//
// CNA used to decide texture-format legality by renderer capability alone, so a renderer able to
// carry a HiDef-only format offered it to a Reach game as well. XNA decides by profile. These
// cases pin the profile half on its own -- no device, no renderer, so they run everywhere and stay
// meaningful in a configuration where no renderer can carry the format anyway.
//
// The expectations are MEASURED, not transcribed: spikes/xna-pixel-center-spike/ builds a
// Texture2D in each format against the real XNA 4.0 runtime, at both profiles. Reach accepted
// exactly nine and refused eleven with NotSupportedException; HiDef refused none.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;

namespace
{
    /// The nine XNA accepted at Reach.
    constexpr SurfaceFormat kReachAccepts[] = {
        SurfaceFormat::Color,           SurfaceFormat::Bgr565,
        SurfaceFormat::Bgra5551,        SurfaceFormat::Bgra4444,
        SurfaceFormat::Dxt1,            SurfaceFormat::Dxt3,
        SurfaceFormat::Dxt5,            SurfaceFormat::NormalizedByte2,
        SurfaceFormat::NormalizedByte4,
    };

    /// The eleven XNA refused at Reach with NotSupportedException.
    constexpr SurfaceFormat kReachRefuses[] = {
        SurfaceFormat::Rgba1010102, SurfaceFormat::Rg32,        SurfaceFormat::Rgba64,
        SurfaceFormat::Alpha8,      SurfaceFormat::Single,      SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,     SurfaceFormat::HalfSingle,  SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4, SurfaceFormat::HdrBlendable,
    };
}

TEST(TextureProfileFormat, ReachAcceptsTheNineFormatsXnaAcceptsThere)
{
    for (const SurfaceFormat fmt : kReachAccepts)
    {
        SCOPED_TRACE(static_cast<int>(fmt));
        EXPECT_TRUE(Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::Reach, fmt));
    }
}

TEST(TextureProfileFormat, ReachRefusesTheElevenFormatsXnaRefusesThere)
{
    for (const SurfaceFormat fmt : kReachRefuses)
    {
        SCOPED_TRACE(static_cast<int>(fmt));
        EXPECT_FALSE(Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::Reach, fmt));
    }
}

TEST(TextureProfileFormat, HiDefRefusesNoneOfThem)
{
    for (const SurfaceFormat fmt : kReachAccepts)
    {
        SCOPED_TRACE(static_cast<int>(fmt));
        EXPECT_TRUE(Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::HiDef, fmt));
    }
    for (const SurfaceFormat fmt : kReachRefuses)
    {
        SCOPED_TRACE(static_cast<int>(fmt));
        EXPECT_TRUE(Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::HiDef, fmt));
    }
}

TEST(TextureProfileFormat, TheTwoProfilesDisagreeOnExactlyTheElevenMeasuredFormats)
{
    // Guards the tables against being widened or narrowed silently: the count is the measurement.
    int disagreements = 0;
    for (int raw = 0; raw <= static_cast<int>(SurfaceFormat::HdrBlendable); ++raw)
    {
        const auto fmt = static_cast<SurfaceFormat>(raw);
        if (Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::HiDef, fmt) !=
            Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::Reach, fmt))
            ++disagreements;
    }
    EXPECT_EQ(disagreements, 11);
}

TEST(TextureProfileFormat, ACnaExtensionFormatIsNotTheProfilesBusiness)
{
    // The *EXT formats are CNA's own and XNA has no opinion on them, so the profile must not be
    // the thing that refuses one -- that stays the renderer's call.
    EXPECT_TRUE(Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::Reach,
                                                     SurfaceFormat::ColorSrgbEXT));
    EXPECT_TRUE(Texture::IsFormatAllowedByProfileEXT(GraphicsProfile::Reach,
                                                     SurfaceFormat::Bc7EXT));
}

// --- REMED-GFX-245: the other two resource kinds ---------------------------------------------

TEST(TextureProfileFormat, ACubeNeverCarriesTheSignedNormalizedFormatsOnEitherProfile)
{
    // Measured, and the "either profile" half is the whole point: HiDef refuses these two for a
    // cube on a device that carries them as a Texture2D without complaint, so it is the resource
    // kind saying no rather than the hardware. spikes/xna-pixel-center-spike/ leg LEG-G.
    for (const GraphicsProfile profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        SCOPED_TRACE(static_cast<int>(profile));
        EXPECT_FALSE(Texture::IsCubeFormatAllowedByProfileEXT(profile,
                                                              SurfaceFormat::NormalizedByte2));
        EXPECT_FALSE(Texture::IsCubeFormatAllowedByProfileEXT(profile,
                                                              SurfaceFormat::NormalizedByte4));
        EXPECT_TRUE(Texture::IsCubeFormatAllowedByProfileEXT(profile, SurfaceFormat::Color));
        EXPECT_TRUE(Texture::IsCubeFormatAllowedByProfileEXT(profile, SurfaceFormat::Dxt1));
    }
}

TEST(TextureProfileFormat, TheCubeGateIsDeliberatelyNarrowerThanXnasOwnCubeList)
{
    // XNA's Reach tier also refuses the eleven HiDef-only formats for a cube. CNA does NOT enforce
    // that, because MOD-107 guarantees a float cube on the default profile for image-based
    // lighting. This case exists so the omission is a recorded decision rather than an oversight
    // someone later "fixes" -- see REMED-GFX-245.
    EXPECT_TRUE(Texture::IsCubeFormatAllowedByProfileEXT(GraphicsProfile::Reach,
                                                         SurfaceFormat::HdrBlendable));
    EXPECT_TRUE(Texture::IsCubeFormatAllowedByProfileEXT(GraphicsProfile::Reach,
                                                         SurfaceFormat::Single));
}

TEST(TextureProfileFormat, NothingRendersIntoABlockCompressedSurface)
{
    // The render-target list is the Texture2D list minus the three compressed formats, at both
    // profiles. The predicate is exposed and measured even though RenderTarget2D does not yet
    // consult it: XNA SUBSTITUTES Color rather than refusing, and MOD-115 deliberately refuses
    // instead, so wiring it in is the owner's call. REMED-GFX-245.
    for (const GraphicsProfile profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        SCOPED_TRACE(static_cast<int>(profile));
        for (const SurfaceFormat fmt : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3,
                                        SurfaceFormat::Dxt5})
            EXPECT_FALSE(Texture::IsRenderTargetFormatAllowedByProfileEXT(profile, fmt));
        EXPECT_TRUE(Texture::IsRenderTargetFormatAllowedByProfileEXT(profile, SurfaceFormat::Color));
    }
    // And the profile tier still applies on top of that.
    EXPECT_FALSE(Texture::IsRenderTargetFormatAllowedByProfileEXT(GraphicsProfile::Reach,
                                                                  SurfaceFormat::HdrBlendable));
    EXPECT_TRUE(Texture::IsRenderTargetFormatAllowedByProfileEXT(GraphicsProfile::HiDef,
                                                                 SurfaceFormat::HdrBlendable));
}
