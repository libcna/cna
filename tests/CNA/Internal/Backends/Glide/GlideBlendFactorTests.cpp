#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideBlendFactor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"

using CNA::Internal::Backends::Glide::GlideBlendFactorNeedsAuxiliaryAlpha;
using CNA::Internal::Backends::Glide::GlideBlendFactorsNeedAuxiliaryAlpha;
using CNA::Internal::Backends::Glide::GlideBlendSlot;
using CNA::Internal::Backends::Glide::kGlideBlendAlphaSaturate;
using CNA::Internal::Backends::Glide::kGlideBlendDestinationAlpha;
using CNA::Internal::Backends::Glide::kGlideBlendInverseDestinationAlpha;
using CNA::Internal::Backends::Glide::kGlideBlendInverseSourceAlpha;
using CNA::Internal::Backends::Glide::kGlideBlendInverseSourceColor;
using CNA::Internal::Backends::Glide::kGlideBlendOne;
using CNA::Internal::Backends::Glide::kGlideBlendSourceAlpha;
using CNA::Internal::Backends::Glide::kGlideBlendSourceColor;
using CNA::Internal::Backends::Glide::kGlideBlendZero;
using CNA::Internal::Backends::Glide::ToGlideBlendFactor;
using Microsoft::Xna::Framework::Graphics::Blend;

TEST(GlideBlendFactorTest, RgbSourceSlotAcceptsItsNineDocumentedFactors)
{
    EXPECT_EQ(ToGlideBlendFactor(Blend::Zero, GlideBlendSlot::RgbSource), kGlideBlendZero);
    EXPECT_EQ(ToGlideBlendFactor(Blend::One, GlideBlendSlot::RgbSource), kGlideBlendOne);
    EXPECT_EQ(ToGlideBlendFactor(Blend::SourceAlpha, GlideBlendSlot::RgbSource), kGlideBlendSourceAlpha);
    EXPECT_EQ(ToGlideBlendFactor(Blend::InverseSourceAlpha, GlideBlendSlot::RgbSource), kGlideBlendInverseSourceAlpha);
    EXPECT_EQ(ToGlideBlendFactor(Blend::DestinationAlpha, GlideBlendSlot::RgbSource), kGlideBlendDestinationAlpha);
    EXPECT_EQ(ToGlideBlendFactor(Blend::InverseDestinationAlpha, GlideBlendSlot::RgbSource), kGlideBlendInverseDestinationAlpha);
    EXPECT_EQ(ToGlideBlendFactor(Blend::SourceAlphaSaturation, GlideBlendSlot::RgbSource), kGlideBlendAlphaSaturate);
    // Glide's *_sf slot references the OTHER buffer's colour: XNA DestinationColor maps here.
    EXPECT_EQ(ToGlideBlendFactor(Blend::DestinationColor, GlideBlendSlot::RgbSource), kGlideBlendSourceColor);
    EXPECT_EQ(ToGlideBlendFactor(Blend::InverseDestinationColor, GlideBlendSlot::RgbSource), kGlideBlendInverseSourceColor);
}

TEST(GlideBlendFactorTest, RgbSourceSlotRejectsOwnColourFactors)
{
    EXPECT_THROW(ToGlideBlendFactor(Blend::SourceColor, GlideBlendSlot::RgbSource), std::runtime_error);
    EXPECT_THROW(ToGlideBlendFactor(Blend::InverseSourceColor, GlideBlendSlot::RgbSource), std::runtime_error);
}

TEST(GlideBlendFactorTest, RgbDestinationSlotAcceptsItsEightDocumentedFactors)
{
    EXPECT_EQ(ToGlideBlendFactor(Blend::Zero, GlideBlendSlot::RgbDestination), kGlideBlendZero);
    EXPECT_EQ(ToGlideBlendFactor(Blend::One, GlideBlendSlot::RgbDestination), kGlideBlendOne);
    EXPECT_EQ(ToGlideBlendFactor(Blend::SourceAlpha, GlideBlendSlot::RgbDestination), kGlideBlendSourceAlpha);
    EXPECT_EQ(ToGlideBlendFactor(Blend::InverseSourceAlpha, GlideBlendSlot::RgbDestination), kGlideBlendInverseSourceAlpha);
    EXPECT_EQ(ToGlideBlendFactor(Blend::DestinationAlpha, GlideBlendSlot::RgbDestination), kGlideBlendDestinationAlpha);
    EXPECT_EQ(ToGlideBlendFactor(Blend::InverseDestinationAlpha, GlideBlendSlot::RgbDestination), kGlideBlendInverseDestinationAlpha);
    // Glide's *_df slot references the OTHER buffer's colour: XNA SourceColor maps here.
    EXPECT_EQ(ToGlideBlendFactor(Blend::SourceColor, GlideBlendSlot::RgbDestination), kGlideBlendSourceColor);
    EXPECT_EQ(ToGlideBlendFactor(Blend::InverseSourceColor, GlideBlendSlot::RgbDestination), kGlideBlendInverseSourceColor);
}

TEST(GlideBlendFactorTest, RgbDestinationSlotRejectsOwnColourAndAlphaSaturate)
{
    EXPECT_THROW(ToGlideBlendFactor(Blend::DestinationColor, GlideBlendSlot::RgbDestination), std::runtime_error);
    EXPECT_THROW(ToGlideBlendFactor(Blend::InverseDestinationColor, GlideBlendSlot::RgbDestination), std::runtime_error);
    EXPECT_THROW(ToGlideBlendFactor(Blend::SourceAlphaSaturation, GlideBlendSlot::RgbDestination), std::runtime_error);
}

TEST(GlideBlendFactorTest, AlphaSlotsShareTheSameLegalDomainShapeAsTheMatchingRgbSlot)
{
    // The Glide 3.0 Reference's legal-value tables for alpha_sf/alpha_df are structurally
    // identical to rgb_sf/rgb_df (same nine vs. eight symbolic constants), so the alpha slots
    // must accept and reject exactly the same XNA Blend values as their RGB counterparts.
    EXPECT_EQ(ToGlideBlendFactor(Blend::DestinationColor, GlideBlendSlot::AlphaSource), kGlideBlendSourceColor);
    EXPECT_EQ(ToGlideBlendFactor(Blend::SourceAlphaSaturation, GlideBlendSlot::AlphaSource), kGlideBlendAlphaSaturate);
    EXPECT_THROW(ToGlideBlendFactor(Blend::SourceColor, GlideBlendSlot::AlphaSource), std::runtime_error);

    EXPECT_EQ(ToGlideBlendFactor(Blend::SourceColor, GlideBlendSlot::AlphaDestination), kGlideBlendSourceColor);
    EXPECT_THROW(ToGlideBlendFactor(Blend::DestinationColor, GlideBlendSlot::AlphaDestination), std::runtime_error);
    EXPECT_THROW(ToGlideBlendFactor(Blend::SourceAlphaSaturation, GlideBlendSlot::AlphaDestination), std::runtime_error);
}

TEST(GlideBlendFactorTest, SourceColorAndDestinationColorShareANativeCodeButNotASlot)
{
    // The native code for GR_BLEND_SRC_COLOR/GR_BLEND_DST_COLOR (0x2) is the same regardless of
    // which XNA Blend value produced it; only the argument slot disambiguates its meaning.
    EXPECT_EQ(ToGlideBlendFactor(Blend::DestinationColor, GlideBlendSlot::RgbSource),
              ToGlideBlendFactor(Blend::SourceColor, GlideBlendSlot::RgbDestination));
    EXPECT_EQ(ToGlideBlendFactor(Blend::InverseDestinationColor, GlideBlendSlot::RgbSource),
              ToGlideBlendFactor(Blend::InverseSourceColor, GlideBlendSlot::RgbDestination));
}

TEST(GlideBlendFactorTest, BlendFactorConstantIsUnsupportedInEverySlot)
{
    EXPECT_THROW(ToGlideBlendFactor(Blend::BlendFactor, GlideBlendSlot::RgbSource), std::runtime_error);
    EXPECT_THROW(ToGlideBlendFactor(Blend::InverseBlendFactor, GlideBlendSlot::RgbDestination), std::runtime_error);
    EXPECT_THROW(ToGlideBlendFactor(Blend::BlendFactor, GlideBlendSlot::AlphaSource), std::runtime_error);
    EXPECT_THROW(ToGlideBlendFactor(Blend::InverseBlendFactor, GlideBlendSlot::AlphaDestination), std::runtime_error);
}

TEST(GlideBlendFactorTest, OnlyDestinationAlphaAndAlphaSaturateFactorsNeedTheAuxiliaryBuffer)
{
    EXPECT_TRUE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendDestinationAlpha));
    EXPECT_TRUE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendInverseDestinationAlpha));
    EXPECT_TRUE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendAlphaSaturate));
    EXPECT_FALSE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendZero));
    EXPECT_FALSE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendOne));
    EXPECT_FALSE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendSourceAlpha));
    EXPECT_FALSE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendInverseSourceAlpha));
    EXPECT_FALSE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendSourceColor));
    EXPECT_FALSE(GlideBlendFactorNeedsAuxiliaryAlpha(kGlideBlendInverseSourceColor));
}

TEST(GlideBlendFactorTest, AnyOfTheFourFactorsNeedingTheAuxiliaryBufferTripsTheCombinedCheck)
{
    EXPECT_TRUE(GlideBlendFactorsNeedAuxiliaryAlpha(
        kGlideBlendOne, kGlideBlendZero, kGlideBlendOne, kGlideBlendAlphaSaturate));
    EXPECT_TRUE(GlideBlendFactorsNeedAuxiliaryAlpha(
        kGlideBlendDestinationAlpha, kGlideBlendZero, kGlideBlendOne, kGlideBlendZero));
    EXPECT_FALSE(GlideBlendFactorsNeedAuxiliaryAlpha(
        kGlideBlendOne, kGlideBlendInverseSourceAlpha, kGlideBlendOne, kGlideBlendInverseSourceAlpha));
}
