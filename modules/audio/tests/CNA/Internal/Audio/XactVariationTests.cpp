// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/XactVariation.hpp"
#include <limits>
#include <vector>

namespace {
using namespace CNA::Internal::Audio;
struct LiteralVariationRandom
{
    std::vector<uint32_t> rolls, bounds;
    std::vector<float> units;
    std::size_t integers = 0, reals = 0;
    XactVariationRandom Source()
    {
        return {
            [this](uint32_t bound) {
                bounds.push_back(bound);
                EXPECT_LT(integers, rolls.size());
                const auto roll = integers < rolls.size() ? rolls[integers++] : 0;
                EXPECT_LT(roll, bound);
                return roll;
            },
            [this] {
                EXPECT_LT(reals, units.size());
                return reals < units.size() ? units[reals++] : 0.0f;
            }
        };
    }
};
XsbWaveRef ThreeCandidates(XsbTrackVariationType type)
{
    XsbWaveRef wave{};
    wave.trackVariationType = type;
    wave.trackVariationEntries = {{0,0,1}, {1,0,2}, {2,0,3}};
    return wave;
}

TEST(XactVariationState, OrderedWrapsAndConsumesNoRandom)
{
    // FACT_internal.c:747-751,201-206: initial -1, then +1 modulo3.
    auto wave = ThreeCandidates(XsbTrackVariationType::Ordered);
    XactVariationState state; LiteralVariationRandom input;
    for (const auto expected : {0u,1u,2u,0u,1u})
    {
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_EQ(state.candidate,expected);
    }
    EXPECT_EQ(input.integers,0u); EXPECT_EQ(input.reals,0u);
}

TEST(XactVariationState, OrderedFromRandomDrawsOnlyItsInitialOffset)
{
    // Weights1,2,3 -> buckets[0,1),[1,3),[3,6). Roll2 ->1, then increment ->2.
    // FACT_internal.c:753-771 and201-206.
    auto wave = ThreeCandidates(XsbTrackVariationType::OrderedFromRandom);
    XactVariationState state; LiteralVariationRandom input{{2}};
    for (const auto expected : {2u,0u,1u,2u})
    { AdvanceXactVariation(wave,state,input.Source()); EXPECT_EQ(state.candidate,expected); }
    EXPECT_EQ(input.bounds,(std::vector<uint32_t>{6}));
}

TEST(XactVariationState, RandomKeepsFirstActivationDoubleDrawThenOnePerLoop)
{
    // FACT_internal.c:753-771,208-221; initial5 discarded, rolls0,1,3,2 ->0,1,2,1.
    auto wave = ThreeCandidates(XsbTrackVariationType::Random);
    XactVariationState state; LiteralVariationRandom input{{5,0,1,3,2}};
    for (const auto expected : {0u,1u,2u,1u})
    { AdvanceXactVariation(wave,state,input.Source()); EXPECT_EQ(state.candidate,expected); }
    EXPECT_EQ(input.bounds,(std::vector<uint32_t>{6,6,6,6,6}));
}

TEST(XactVariationState, NoRepeatAndShuffleExcludeActualPreviousCandidate)
{
    // FACT_internal.c:224-244 treats both types alike. Initial5 ->2 excluded;
    // roll1/3 ->1; exclude1: roll3/4 ->2; exclude2: roll0/3 ->0;
    // exclude0: roll0/5 ->1. A repeat of1 after two steps is allowed, no shuffle bag.
    for (auto type : {XsbTrackVariationType::RandomNoRepeats,XsbTrackVariationType::Shuffle})
    {
        auto wave = ThreeCandidates(type);
        XactVariationState state; LiteralVariationRandom input{{5,1,3,0,0}};
        for (const auto expected : {1u,2u,0u,1u})
        { AdvanceXactVariation(wave,state,input.Source()); EXPECT_EQ(state.candidate,expected); }
        EXPECT_EQ(input.bounds,(std::vector<uint32_t>{6,3,4,3,5}));
    }
}

TEST(XactVariationState, IntegerBoundariesCannotSelectZeroWeightOrOutOfRange)
{
    // Discrete integer convention retained from TQ-022: weights0,1,2,
    // bound3 (exclusive); rolls0,1,2 map exactly1,2,2, never entry0.
    const std::vector<XsbTrackVariationEntry> entries{{0,0,0},{1,0,1},{2,0,2}};
    LiteralVariationRandom input{{0,1,2}};
    for (const auto expected : {1u,2u,2u})
        EXPECT_EQ(PickXactCandidate(entries,entries.size(),input.Source()),expected);
    EXPECT_EQ(input.bounds,(std::vector<uint32_t>{3,3,3}));
}

TEST(XactVariationState, DegenerateCandidatesKeepExistingFallbackWithoutInvalidDraw)
{
    // Compatibility controls for pre-existing CNA fallback, not native-XACT evidence.
    for (auto type : {XsbTrackVariationType::RandomNoRepeats,XsbTrackVariationType::Shuffle})
    {
        auto wave=ThreeCandidates(type); wave.trackVariationEntries={{0,0,0}};
        XactVariationState state; LiteralVariationRandom input;
        for (int i=0;i<3;++i)
        { AdvanceXactVariation(wave,state,input.Source()); EXPECT_EQ(state.candidate,0u); }
        EXPECT_TRUE(input.bounds.empty());
    }
}

TEST(XactVariationState, PitchAndVolumeHoldReplaceAndAddInAuthoredUnits)
{
    // FACT_internal.c:309-374, FACT_internal.h:334-342. Pitch100+.25*400=200;
    // next100+.75*400=400. Volume-2000+.25*2000=-1500; next=-500.
    for (const uint16_t mode : {uint16_t(0x0005),uint16_t(0x0300),uint16_t(0x0305)})
    {
        XsbWaveRef wave{}; wave.effectVariationFlags=0x3000|mode;
        wave.effectMinPitch=100; wave.effectMaxPitch=500;
        wave.effectMinVolume=-2000; wave.effectMaxVolume=0;
        XactVariationState state; LiteralVariationRandom input{{},{},{.25f,.25f,.75f,.75f}};
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_FLOAT_EQ(state.pitchCentsDelta,200); EXPECT_FLOAT_EQ(state.volumeCentibels,-1500);
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_FLOAT_EQ(state.pitchCentsDelta,mode==0x0005 ? 200 : mode==0x0300 ? 400 : 600);
        EXPECT_FLOAT_EQ(state.volumeCentibels,mode==0x0005 ? -1500 : mode==0x0300 ? -500 : -2000);
        EXPECT_EQ(input.reals,4u); // active axis draws even when NEW is absent
    }
}

TEST(XactVariationState, PitchTruncatesSpanBeforeAddingNegativeMinimum)
{
    // FACT_internal.c:311-314: int16(.25*(18-(-17)))+(-17) =8-17=-9.
    XsbWaveRef wave{}; wave.effectVariationFlags=0x1100;
    wave.effectMinPitch=-17; wave.effectMaxPitch=18;
    XactVariationState state; LiteralVariationRandom input{{},{},{.25f,1,0}};
    AdvanceXactVariation(wave,state,input.Source()); EXPECT_FLOAT_EQ(state.pitchCentsDelta,-9);
    AdvanceXactVariation(wave,state,input.Source()); EXPECT_FLOAT_EQ(state.pitchCentsDelta,18);
    AdvanceXactVariation(wave,state,input.Source()); EXPECT_FLOAT_EQ(state.pitchCentsDelta,-17);
}

TEST(XactVariationState, FilterDrawsQBeforeFrequencyAndUsesNewGate)
{
    // FACT_internal.c:381-424. Q=2+.5*4=4 ->1/4, f=1000+.75*4000=4000.
    // Next Q=2 ->1/2, f=2000. Either NEW mask bit enables replacement.
    // FAudio's frequency/Q ADD branch is TODO: those bits remain a fallback, no native claim.
    for (const uint16_t mode : {uint16_t(0),uint16_t(0x0400),uint16_t(0x0800),uint16_t(0x0c50)})
    {
        XsbWaveRef wave{}; wave.effectVariationFlags=0xc000|mode;
        wave.effectMinQFactor=2; wave.effectMaxQFactor=6;
        wave.effectMinFrequency=1000; wave.effectMaxFrequency=5000;
        XactVariationState state; LiteralVariationRandom input{{},{},{.5f,.75f,0,.25f}};
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_TRUE(state.hasFilterOverride); EXPECT_FLOAT_EQ(state.filterOneOverQ,.25f);
        EXPECT_FLOAT_EQ(state.filterFrequencyHz,4000);
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_FLOAT_EQ(state.filterOneOverQ,mode ? .5f : .25f);
        EXPECT_FLOAT_EQ(state.filterFrequencyHz,mode ? 2000 : 4000); EXPECT_EQ(input.reals,4u);
    }
}

TEST(XactVariationState, DisabledAxesConsumeNoDrawsEvenWithLowFlagBits)
{
    // FACT_internal.c:309,341,381 top-level gates dominate NEW and ADD bits.
    XsbWaveRef wave{}; wave.effectVariationFlags=0x0f55;
    XactVariationState state; LiteralVariationRandom input;
    for (int i=0;i<3;++i) AdvanceXactVariation(wave,state,input.Source());
    EXPECT_FLOAT_EQ(state.pitchCentsDelta,0); EXPECT_FLOAT_EQ(state.volumeCentibels,0);
    EXPECT_FALSE(state.hasFilterOverride); EXPECT_EQ(input.reals,0u);
}

TEST(XactVariationState, EventsOwnIndependentSelectionAndEffectHistory)
{
    // Independent FACTEventInstance valuei (FACT_internal.c:724-771); no global last pick.
    auto wave=ThreeCandidates(XsbTrackVariationType::Ordered);
    wave.effectVariationFlags=0x1104; wave.effectMinPitch=100; wave.effectMaxPitch=100;
    XactVariationState a,b; LiteralVariationRandom input{{},{},{0,0,0}};
    AdvanceXactVariation(wave,a,input.Source()); AdvanceXactVariation(wave,a,input.Source());
    AdvanceXactVariation(wave,b,input.Source());
    EXPECT_EQ(a.candidate,1u); EXPECT_EQ(b.candidate,0u);
    EXPECT_FLOAT_EQ(a.pitchCentsDelta,200); EXPECT_FLOAT_EQ(b.pitchCentsDelta,100);
}
TEST(XactVariationState, EachNewBitChangesOnlyItsOwnAxes)
{
    // FACT_internal.h:338-343, FACT_internal.c:309-424. All four axes draw;
    // after two activations only the named NEW gate may change its prior value.
    for (uint16_t mode : {uint16_t(0x0100),uint16_t(0x0200),uint16_t(0x0400)})
    {
        XsbWaveRef wave{}; wave.effectVariationFlags=0xf000|mode;
        wave.effectMinPitch=100; wave.effectMaxPitch=500;
        wave.effectMinVolume=-2000; wave.effectMaxVolume=0;
        wave.effectMinQFactor=2; wave.effectMaxQFactor=6;
        wave.effectMinFrequency=1000; wave.effectMaxFrequency=5000;
        XactVariationState state;
        LiteralVariationRandom input{{},{},{.25f,.25f,.5f,.75f,.75f,.75f,0,.25f}};
        AdvanceXactVariation(wave,state,input.Source());
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_FLOAT_EQ(state.pitchCentsDelta,mode==0x0100 ? 400 : 200);
        EXPECT_FLOAT_EQ(state.volumeCentibels,mode==0x0200 ? -500 : -1500);
        EXPECT_FLOAT_EQ(state.filterOneOverQ,mode==0x0400 ? .5f : .25f);
        EXPECT_FLOAT_EQ(state.filterFrequencyHz,mode==0x0400 ? 2000 : 4000);
        EXPECT_EQ(input.reals,8u);
    }
}
TEST(XactVariationState, AuthoredVolumeBaseParticipatesOnceInHoldReplaceAndAdd)
{
    // Native first draw: (-1500+100)-50=-1450. Next draw is -500;
    // HOLD keeps -1450, NEW replaces with -450, NEW+ADD gives -1950.
    for (const uint16_t mode : {uint16_t(0x0001),uint16_t(0x0200),uint16_t(0x0201)})
    {
        XsbWaveRef wave{};wave.effectVariationFlags=0x2000|mode;
        wave.soundVolumeCentibels=100;wave.trackVolumeCentibels=-50;
        wave.effectMinVolume=-2000;wave.effectMaxVolume=0;
        XactVariationState state;LiteralVariationRandom input{{},{},{.25f,.75f}};
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_FLOAT_EQ(state.volumeCentibels,-1450);
        AdvanceXactVariation(wave,state,input.Source());
        EXPECT_FLOAT_EQ(state.volumeCentibels,mode==0x0001?-1450:mode==0x0200?-450:-1950);
        EXPECT_EQ(input.reals,2u);
    }
    XsbWaveRef plain{};plain.soundVolumeCentibels=100;plain.trackVolumeCentibels=-50;
    XactVariationState state;LiteralVariationRandom input;
    AdvanceXactVariation(plain,state,input.Source());EXPECT_FLOAT_EQ(state.volumeCentibels,50);
    EXPECT_EQ(input.reals,0u);
}

TEST(XactVariationState, SilentAuthoredSoundOrTrackRemainsSilentThroughEveryEffectMode)
{
    for (const bool silentSound : {false,true})
        for (const uint16_t flags : {uint16_t(0),uint16_t(0x2001),uint16_t(0x2200),uint16_t(0x2201)})
        {
            SCOPED_TRACE(silentSound);
            SCOPED_TRACE(flags);
            XsbWaveRef wave{};wave.effectVariationFlags=flags;
            wave.soundVolumeCentibels=silentSound?-std::numeric_limits<float>::infinity():100;
            wave.trackVolumeCentibels=silentSound?-50:-std::numeric_limits<float>::infinity();
            wave.effectMinVolume=wave.effectMaxVolume=2000;
            XactVariationState state;LiteralVariationRandom input{{},{},{0,0}};
            for (int iteration=0;iteration<2;++iteration)
            {
                AdvanceXactVariation(wave,state,input.Source());
                EXPECT_EQ(state.volumeCentibels,-std::numeric_limits<float>::infinity());
            }
            EXPECT_EQ(input.reals,flags?2u:0u);
        }
}
} // namespace
