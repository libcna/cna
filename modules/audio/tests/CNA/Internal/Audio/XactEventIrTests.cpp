// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/XactTypes.hpp"

#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <variant>
#include <vector>

using namespace CNA::Internal::Audio;

namespace
{
    using Bytes = std::vector<uint8_t>;
    void U8(Bytes& b, uint8_t v) { b.push_back(v); }
    void U16(Bytes& b, uint16_t v) { U8(b, v & 255); U8(b, v >> 8); }
    void U32(Bytes& b, uint32_t v) { U16(b, v & 65535); U16(b, v >> 16); }
    void F32(Bytes& b, float v)
    {
        uint32_t bits;
        static_assert(sizeof(bits) == sizeof(v));
        std::memcpy(&bits, &v, sizeof(bits));
        U32(b, bits);
    }
    void Set16(Bytes& b, std::size_t p, uint16_t v)
    {
        b.at(p) = v & 255; b.at(p + 1) = v >> 8;
    }
    void Set32(Bytes& b, std::size_t p, uint32_t v)
    {
        Set16(b, p, v & 65535); Set16(b, p + 2, v >> 16);
    }

    // Oracle: FACT_internal.c:2300-2308. Bits 21..31 are not timestamp bits.
    Bytes Header(uint8_t type, uint16_t time = 40, uint16_t random = 7)
    {
        Bytes b;
        U32(b, 0xA0000000u | (uint32_t(time) << 5) | type);
        U16(b, random); U8(b, 255);
        return b;
    }

    void Effect(Bytes& b)
    {
        // FACT_internal.c:2369-2379, 2398-2408; signed cents and volume-byte formula.
        U16(b, static_cast<uint16_t>(-1200)); U16(b, 600);
        U8(b, 128); U8(b, 255);
        F32(b, 200.0f); F32(b, 8000.0f); F32(b, 0.5f); F32(b, 2.0f);
        U16(b, 0x30F3);
    }

    Bytes Play(uint8_t type = 1, uint16_t index = 0x1234, uint8_t bank = 3,
               uint8_t loops = 2, uint16_t time = 40, uint16_t random = 7,
               bool emptyCandidates = false)
    {
        Bytes b = Header(type, time, random);
        U8(b, 0x81);
        if (type == 1 || type == 4) { U16(b, index); U8(b, bank); }
        U8(b, loops); U16(b, 0x2345); U16(b, 0x3456);
        if (type == 4 || type == 6) Effect(b);
        if (type == 3 || type == 6)
        {
            // count=2, RandomNoRepeats=3, table-type=wave=0, HAS_VARIATION=0x40.
            U32(b, emptyCandidates ? 0x00430000 : 0x00430002);
            for (auto v : {0x11, 0x22, 0x33, 0x44}) U8(b, v);
            if (!emptyCandidates)
            {
                U16(b, 0x1234); U8(b, 3); U8(b, 12); U8(b, 29);
                U16(b, 0x4567); U8(b, 4); U8(b, 29); U8(b, 66);
            }
        }
        return b;
    }

    Bytes Stop(uint8_t flags = 3)
    {
        Bytes b = Header(0); U8(b, flags); return b;
    }
    Bytes Marker(uint8_t type = 9, uint32_t marker = 0x12345678,
                 uint16_t repeats = 65535, uint16_t frequency = 37)
    {
        Bytes b = Header(type, 65535, 65535); U32(b, marker);
        if (type == 18) { U16(b, repeats); U16(b, frequency); }
        return b;
    }
    Bytes Value(uint8_t type, bool ramp)
    {
        // Oracle: FACT_internal.c:2441-2467. Repeating ramps have NO repeat trailer.
        Bytes b = Header(type, 5, 11); U8(b, ramp ? 0xA1 : 0xA0);
        if (ramp)
        {
            F32(b, -6.25f); F32(b, 0.125f); F32(b, -0.5f); U16(b, 1234);
        }
        else
        {
            U8(b, 0x0D); F32(b, -12.5f); F32(b, 3.25f);
            for (uint8_t v = 1; v <= 5; ++v) U8(b, v);
            if (type == 16 || type == 17) { U16(b, 65535); U16(b, 37); }
        }
        return b;
    }

    struct Bank
    {
        Bytes bytes;
        std::vector<std::size_t> eventCounts;
        std::vector<std::size_t> trackCodes;
    };

    // Literal LE bank with absolute track offsets. FACT_internal.c:2617-2768.
    // Header=74+name64 => sound138; complex base10; track metadata5(v43)/9(modern).
    // Optional RPC(7)+DSP(7) precede ALL track metadata; following sound begins after
    // the LAST event block. No CNA serializer or output contributes expected values.
    Bank MakeBank(const std::vector<std::vector<Bytes>>& tracks, bool blocks = false,
                  bool followingSound = false,uint16_t version=46,bool gap=false)
    {
        Bank result;
        auto& b = result.bytes;
        b.resize(138, 0);
        b[0] = 'S'; b[1] = 'D'; b[2] = 'B'; b[3] = 'K'; Set16(b, 4, version);
        Set16(b,6,43);b[18]=3;
        Set16(b, 28, followingSound ? 2 : 1);
        for (auto p : {34u, 38u, 42u, 50u, 58u, 66u}) Set32(b, p, 0xFFFFFFFFu);
        Set32(b,58,0);
        Set32(b, 70, 138);
        U8(b, blocks ? 0x13 : 0x01); U16(b, 0x1234); U8(b, 128);
        U16(b, static_cast<uint16_t>(-300)); U8(b, 9); U16(b, 0);
        U8(b, static_cast<uint8_t>(tracks.size()));
        if (blocks)
        {
            U16(b, 7); U8(b, 1); U32(b, 0x11223344); // RPC
            U16(b, 7); U8(b, 1); U32(b, 0x55667788); // DSP
        }
        for (std::size_t t = 0; t < tracks.size(); ++t)
        {
            U8(b, 128); result.trackCodes.push_back(b.size()); U32(b, 0);
            if(version!=43)
            {U16(b, t == 0 ? 0x0605 : 0x0301); U16(b, t == 0 ? 8000 : 4000);}
        }
        for (std::size_t t = 0; t < tracks.size(); ++t)
        {
            if(t && gap)b.insert(b.end(),5,0xEE);
            Set32(b, result.trackCodes[t], static_cast<uint32_t>(b.size()));
            result.eventCounts.push_back(b.size());
            U8(b, static_cast<uint8_t>(tracks[t].size()));
            for (const auto& event : tracks[t]) b.insert(b.end(), event.begin(), event.end());
        }
        if (followingSound)
        {
            const uint32_t soundCode = static_cast<uint32_t>(b.size());
            U8(b, 0); U16(b, 0xABCD); U8(b, 128); U16(b, 450); U8(b, 13); U16(b, 0);
            U16(b, 0x3456); U8(b, 4);
            Set16(b, 19, 1); Set32(b, 34, static_cast<uint32_t>(b.size()));
            U8(b, 4); U32(b, soundCode); // cue must resolve the following sound's exact offset
        }
        return result;
    }

    void CheckWave(const XsbWaveRef& wave, uint16_t index, uint8_t bank, uint8_t loops)
    {
        EXPECT_EQ(wave.waveIndex, index); EXPECT_EQ(wave.wavebankIndex, bank);
        EXPECT_EQ(wave.loopCount, loops);
        // cb(128)=3969*log10(128/28240)+8715=-586.9655945;
        // gain=10^(cb/2000)=.5087649718, sound*track=.2588417965.
        // 1e-6 absolute permits f32 intermediate rounding and libm variation.
        EXPECT_NEAR(wave.volume, 0.2588417965f, 1e-6f);
        EXPECT_EQ(wave.filterType, 2); EXPECT_EQ(wave.filterFrequencyHz, 8000);
        EXPECT_EQ(wave.filterQFactorRaw, 6);
    }
}

TEST(XactEventIrTest, KeepsEveryEventInFileOrderAndProjectsFirstWave)
{
    const auto xsb = ParseXsb(MakeBank({{Marker(), Play(), Stop(),
        Play(1, 0x4567, 4, 254, 5, 11), Marker(18)}}).bytes);
    ASSERT_EQ(xsb.sounds.size(), 1u);
    const auto& sound = xsb.sounds[0];
    ASSERT_EQ(sound.tracks.size(), 1u); ASSERT_EQ(sound.waves.size(), 1u);
    const auto& track = sound.tracks[0];
    EXPECT_TRUE(track.complete); EXPECT_EQ(track.declaredEventCount, 5);
    EXPECT_EQ(track.code, 157u); // 138+10+9
    EXPECT_NEAR(track.volume, 0.2588417965f, 1e-6f);
    EXPECT_EQ(track.filterType, 2); EXPECT_EQ(track.filterFrequencyHz, 8000);
    EXPECT_EQ(track.filterQFactorRaw, 6);
    ASSERT_EQ(track.events.size(), 5u);
    const std::array<uint8_t, 5> types{9, 1, 0, 1, 18};
    const std::array<uint16_t, 5> times{65535, 40, 40, 5, 65535};
    const std::array<uint16_t, 5> random{65535, 7, 7, 11, 65535};
    for (std::size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(static_cast<uint8_t>(track.events[i].type), types[i]);
        EXPECT_EQ(track.events[i].timestamp, times[i]);
        EXPECT_EQ(track.events[i].randomOffset, random[i]);
    }
    EXPECT_EQ(track.events[1].info, 0xA0000501u); // 0xA0000000 | (40*32) | 1
    ASSERT_TRUE(std::holds_alternative<XsbPlayWaveEvent>(track.events[1].payload));
    ASSERT_TRUE(std::holds_alternative<XsbPlayWaveEvent>(track.events[3].payload));
    const auto& first = std::get<XsbPlayWaveEvent>(track.events[1].payload);
    const auto& second = std::get<XsbPlayWaveEvent>(track.events[3].payload);
    CheckWave(first.wave, 0x1234, 3, 2); CheckWave(second.wave, 0x4567, 4, 254);
    EXPECT_EQ(first.flags, 0x81); EXPECT_EQ(first.position, 0x2345); EXPECT_EQ(first.angle, 0x3456);
    EXPECT_EQ(second.flags, 0x81); EXPECT_EQ(second.position, 0x2345); EXPECT_EQ(second.angle, 0x3456);
    EXPECT_TRUE(first.wave.trackVariationEntries.empty()); EXPECT_EQ(first.wave.effectVariationFlags, 0);
    EXPECT_EQ(first.variationInfo, 0u);
    CheckWave(sound.waves[0], 0x1234, 3, 2);
    EXPECT_TRUE(sound.waves[0].trackVariationEntries.empty());
    EXPECT_EQ(sound.waves[0].effectVariationFlags, 0);
    ASSERT_TRUE(std::holds_alternative<XsbStopEvent>(track.events[2].payload));
    EXPECT_EQ(std::get<XsbStopEvent>(track.events[2].payload).flags, 3); // immediate + whole cue
}

TEST(XactEventIrTest, RetainsAllFourWaveLayoutsAfterTheFirstPlay)
{
    // FACT_internal.c:2314-2434; the later event must retain all fields, not just be consumed.
    for (uint8_t type : {1, 3, 4, 6})
    {
        SCOPED_TRACE(int(type));
        const auto xsb = ParseXsb(MakeBank({{Play(1, 9, 2, 0), Play(type), Marker()}}).bytes);
        const auto& sound = xsb.sounds.at(0);
        ASSERT_EQ(sound.tracks.at(0).events.size(), 3u);
        const auto& event = sound.tracks[0].events[1];
        EXPECT_EQ(static_cast<uint8_t>(event.type), type);
        ASSERT_TRUE(std::holds_alternative<XsbPlayWaveEvent>(event.payload));
        const auto& play = std::get<XsbPlayWaveEvent>(event.payload);
        CheckWave(play.wave, 0x1234, 3, 2);
        EXPECT_EQ(play.flags, 0x81); EXPECT_EQ(play.position, 0x2345); EXPECT_EQ(play.angle, 0x3456);
        CheckWave(sound.waves.at(0), 9, 2, 0); // compatibility remains the FIRST event
        if (type == 3 || type == 6)
        {
            EXPECT_EQ(play.variationInfo, 0x00430002u);
            EXPECT_EQ(play.variationReserved, (std::array<uint8_t, 4>{0x11, 0x22, 0x33, 0x44}));
            EXPECT_EQ(play.wave.trackVariationType, XsbTrackVariationType::RandomNoRepeats);
            const auto& entries = play.wave.trackVariationEntries;
            ASSERT_EQ(entries.size(), 2u);
            EXPECT_EQ(entries[0].waveIndex, 0x1234); EXPECT_EQ(entries[0].wavebankIndex, 3);
            EXPECT_EQ(entries[0].minWeight, 12); EXPECT_EQ(entries[0].maxWeight, 29);
            EXPECT_EQ(entries[0].weight, 17); // 29-12
            EXPECT_EQ(entries[1].waveIndex, 0x4567); EXPECT_EQ(entries[1].wavebankIndex, 4);
            EXPECT_EQ(entries[1].minWeight, 29); EXPECT_EQ(entries[1].maxWeight, 66);
            EXPECT_EQ(entries[1].weight, 37); // 66-29
        }
        else
        {
            EXPECT_TRUE(play.wave.trackVariationEntries.empty()); EXPECT_EQ(play.variationInfo, 0u);
        }
        if (type == 4 || type == 6)
        {
            EXPECT_EQ(play.wave.effectVariationFlags, 0x30F3);
            EXPECT_EQ(play.wave.effectMinPitch, -1200); EXPECT_EQ(play.wave.effectMaxPitch, 600);
            EXPECT_EQ(play.minVolumeByte, 128); EXPECT_EQ(play.maxVolumeByte, 255);
            // FACT read_volbyte: 3969*log10(b/28240)+8715, rounded once to f32.
            // 1e-3 centibel absolute covers f32/libm rounding; no CNA output is used.
            EXPECT_NEAR(play.wave.effectMinVolume, -586.9655945f, 1e-3f);
            EXPECT_NEAR(play.wave.effectMaxVolume, 601.0760121f, 1e-3f);
            EXPECT_FLOAT_EQ(play.wave.effectMinFrequency, 200.0f);
            EXPECT_FLOAT_EQ(play.wave.effectMaxFrequency, 8000.0f);
            EXPECT_FLOAT_EQ(play.wave.effectMinQFactor, 0.5f);
            EXPECT_FLOAT_EQ(play.wave.effectMaxQFactor, 2.0f);
        }
        else
        {
            EXPECT_EQ(play.wave.effectVariationFlags, 0);
            EXPECT_EQ(play.wave.effectMinPitch, 0); EXPECT_EQ(play.wave.effectMaxPitch, 0);
            EXPECT_FLOAT_EQ(play.wave.effectMinVolume, 0); EXPECT_FLOAT_EQ(play.wave.effectMaxVolume, 0);
            EXPECT_FLOAT_EQ(play.wave.effectMinFrequency, 0); EXPECT_FLOAT_EQ(play.wave.effectMaxFrequency, 0);
            EXPECT_FLOAT_EQ(play.wave.effectMinQFactor, 0); EXPECT_FLOAT_EQ(play.wave.effectMaxQFactor, 0);
        }
        ASSERT_TRUE(std::holds_alternative<XsbMarkerEvent>(sound.tracks[0].events[2].payload));
        EXPECT_EQ(std::get<XsbMarkerEvent>(sound.tracks[0].events[2].payload).marker, 0x12345678u);
    }
}

TEST(XactEventIrTest, KeepsEmptyCandidateTablesAndInfiniteLoopSentinel)
{
    // Zero candidates consume zero entries; do not invent a candidate or lose following bytes.
    for (uint8_t type : {3, 6})
    {
        const auto xsb = ParseXsb(MakeBank({{Play(type, 0, 0, 255, 40, 7, true), Marker()}}).bytes);
        const auto& sound = xsb.sounds.at(0);
        ASSERT_EQ(sound.tracks.at(0).events.size(), 2u);
        const auto& play = std::get<XsbPlayWaveEvent>(sound.tracks[0].events[0].payload);
        EXPECT_EQ(play.variationInfo, 0x00430000u); EXPECT_TRUE(play.wave.trackVariationEntries.empty());
        CheckWave(play.wave, 0xFFFF, 0xFF, 255); CheckWave(sound.waves.at(0), 0xFFFF, 0xFF, 255);
        EXPECT_EQ(std::get<XsbMarkerEvent>(sound.tracks[0].events[1].payload).marker, 0x12345678u);
    }
}

TEST(XactEventIrTest, FirstVariationProjectionKeepsItsCandidatesAndEffectRanges)
{
    // Existing first-activation compatibility, with payload oracle FACT_internal.c:2314-2434.
    for (uint8_t type : {3, 4, 6})
    {
        SCOPED_TRACE(int(type));
        const auto xsb = ParseXsb(MakeBank({{Marker(), Play(type), Play(1, 99, 2, 254)}}).bytes);
        const auto& wave = xsb.sounds.at(0).waves.at(0);
        CheckWave(wave, 0x1234, 3, 2);
        if (type == 3 || type == 6)
        {
            ASSERT_EQ(wave.trackVariationEntries.size(), 2u);
            EXPECT_EQ(wave.trackVariationType, XsbTrackVariationType::RandomNoRepeats);
            EXPECT_EQ(wave.trackVariationEntries[0].waveIndex, 0x1234);
            EXPECT_EQ(wave.trackVariationEntries[0].wavebankIndex, 3);
            EXPECT_EQ(wave.trackVariationEntries[0].weight, 17);
            EXPECT_EQ(wave.trackVariationEntries[1].waveIndex, 0x4567);
            EXPECT_EQ(wave.trackVariationEntries[1].wavebankIndex, 4);
            EXPECT_EQ(wave.trackVariationEntries[1].weight, 37);
        }
        else EXPECT_TRUE(wave.trackVariationEntries.empty());
        if (type == 4 || type == 6)
        {
            EXPECT_EQ(wave.effectVariationFlags, 0x30F3);
            EXPECT_EQ(wave.effectMinPitch, -1200); EXPECT_EQ(wave.effectMaxPitch, 600);
            EXPECT_NEAR(wave.effectMinVolume, -586.9655945f, 1e-3f);
            EXPECT_NEAR(wave.effectMaxVolume, 601.0760121f, 1e-3f);
            EXPECT_FLOAT_EQ(wave.effectMinFrequency, 200); EXPECT_FLOAT_EQ(wave.effectMaxFrequency, 8000);
            EXPECT_FLOAT_EQ(wave.effectMinQFactor, 0.5f); EXPECT_FLOAT_EQ(wave.effectMaxQFactor, 2);
        }
        else EXPECT_EQ(wave.effectVariationFlags, 0);
    }
}

TEST(XactEventIrTest, PreservesEverySelectionAlgorithmAndHasVariationBit)
{
    // FACT_internal.c:2337-2343 / FACT_internal.h:135-146: algorithm mask=7, has-variation=0x40.
    for (uint32_t algorithm = 0; algorithm <= 4; ++algorithm)
        for (uint32_t flags : {0u, 0x00400000u})
        {
            auto event = Play(3);
            const uint32_t info = flags | (algorithm << 16) | 2;
            Set32(event, 13, info); // header(7)+complex-wave prefix(6)
            const auto xsb = ParseXsb(MakeBank({{event}}).bytes);
            const auto& play = std::get<XsbPlayWaveEvent>(xsb.sounds.at(0).tracks.at(0).events.at(0).payload);
            EXPECT_EQ(play.variationInfo, info);
            EXPECT_EQ(static_cast<uint8_t>(play.wave.trackVariationType), algorithm);
            ASSERT_EQ(play.wave.trackVariationEntries.size(), 2u);
        }
}

TEST(XactEventIrTest, RetainsEquationAndRampFormsOfEveryValueKind)
{
    // FACT_internal.c:2436-2467. All values are binary-exact; .125*100=12.5 exactly.
    for (uint8_t type : {7, 8, 16, 17})
    {
        SCOPED_TRACE(int(type));
        const auto xsb = ParseXsb(MakeBank({{Value(type, false), Value(type, true), Marker()}}).bytes);
        const auto& track = xsb.sounds.at(0).tracks.at(0);
        ASSERT_EQ(track.events.size(), 3u);
        for (std::size_t i = 0; i < 2; ++i)
        {
            EXPECT_EQ(static_cast<uint8_t>(track.events[i].type), type);
            EXPECT_EQ(track.events[i].timestamp, 5); EXPECT_EQ(track.events[i].randomOffset, 11);
            ASSERT_TRUE(std::holds_alternative<XsbValueEvent>(track.events[i].payload));
        }
        const auto& value = std::get<XsbValueEvent>(track.events[0].payload);
        EXPECT_EQ(value.settings, 0xA0);
        ASSERT_TRUE(std::holds_alternative<XsbEquation>(value.value));
        const auto& eq = std::get<XsbEquation>(value.value);
        EXPECT_EQ(eq.flags, 0x0D); EXPECT_FLOAT_EQ(eq.value1, -12.5f); EXPECT_FLOAT_EQ(eq.value2, 3.25f);
        EXPECT_EQ(eq.reserved, (std::array<uint8_t, 5>{1, 2, 3, 4, 5}));
        EXPECT_EQ(value.repeats, type >= 16 ? 65535 : 0); EXPECT_EQ(value.frequency, type >= 16 ? 37 : 0);
        const auto& rampValue = std::get<XsbValueEvent>(track.events[1].payload);
        EXPECT_EQ(rampValue.settings, 0xA1);
        ASSERT_TRUE(std::holds_alternative<XsbRamp>(rampValue.value));
        const auto& ramp = std::get<XsbRamp>(rampValue.value);
        EXPECT_FLOAT_EQ(ramp.initialValue, -6.25f); EXPECT_FLOAT_EQ(ramp.initialSlope, 12.5f);
        EXPECT_FLOAT_EQ(ramp.slopeDelta, -0.5f); EXPECT_EQ(ramp.duration, 1234);
        EXPECT_EQ(rampValue.repeats, 0); EXPECT_EQ(rampValue.frequency, 0);
        EXPECT_EQ(std::get<XsbMarkerEvent>(track.events[2].payload).marker, 0x12345678u);
        EXPECT_EQ(xsb.sounds[0].waves.at(0).waveIndex, 0xFFFF);
    }
}

TEST(XactEventIrTest, RetainsMarkerAndStopPayloadsIncludingZeroAndMaximumRepeats)
{
    const auto xsb = ParseXsb(MakeBank({{Stop(0), Stop(1), Stop(2), Stop(3),
        Marker(), Marker(18, 0xFEDCBA98, 0, 0), Marker(18, 0xFFFFFFFF, 65535, 65535)}}).bytes);
    const auto& events = xsb.sounds.at(0).tracks.at(0).events;
    ASSERT_EQ(events.size(), 7u);
    for (std::size_t i = 0; i < 4; ++i)
    {
        EXPECT_EQ(events[i].type, XsbEventType::Stop);
        EXPECT_EQ(std::get<XsbStopEvent>(events[i].payload).flags, i);
    }
    const auto& once = std::get<XsbMarkerEvent>(events[4].payload);
    EXPECT_EQ(events[4].type, XsbEventType::Marker);
    EXPECT_EQ(once.marker, 0x12345678u); EXPECT_EQ(once.repeats, 0); EXPECT_EQ(once.frequency, 0);
    const auto& zero = std::get<XsbMarkerEvent>(events[5].payload);
    EXPECT_EQ(zero.marker, 0xFEDCBA98u); EXPECT_EQ(zero.repeats, 0); EXPECT_EQ(zero.frequency, 0);
    const auto& maximum = std::get<XsbMarkerEvent>(events[6].payload);
    EXPECT_EQ(events[6].type, XsbEventType::MarkerRepeating);
    EXPECT_EQ(maximum.marker, 0xFFFFFFFFu); EXPECT_EQ(maximum.repeats, 65535);
    EXPECT_EQ(maximum.frequency, 65535);
}

TEST(XactEventIrTest, RetainsTrackBoundariesAndFollowingSoundAfterRpcAndDsp)
{
    const auto bank = MakeBank({{Play(), Play(1, 0x4567, 4, 254)}, {Value(8, false), Marker()}}, true, true);
    const auto xsb = ParseXsb(bank.bytes);
    ASSERT_EQ(xsb.sounds.size(), 2u);
    const auto& sound = xsb.sounds[0];
    EXPECT_EQ(sound.categoryIndex, 0x1234); EXPECT_EQ(sound.pitchCents, -300); EXPECT_EQ(sound.priority, 9);
    EXPECT_EQ(sound.rpcCodes, (std::vector<uint32_t>{0x11223344}));
    ASSERT_EQ(sound.tracks.size(), 2u); ASSERT_EQ(sound.waves.size(), 2u);
    EXPECT_EQ(sound.tracks[0].code, 180u); // 138+10+7+7+2*9
    EXPECT_EQ(sound.tracks[1].code, 213u); // 180+1+2*(7+9)
    ASSERT_EQ(sound.tracks[0].events.size(), 2u); ASSERT_EQ(sound.tracks[1].events.size(), 2u);
    CheckWave(sound.waves[0], 0x1234, 3, 2);
    EXPECT_EQ(sound.tracks[1].filterType, 0); EXPECT_EQ(sound.tracks[1].filterFrequencyHz, 4000);
    EXPECT_EQ(sound.tracks[1].filterQFactorRaw, 3);
    EXPECT_NEAR(sound.tracks[1].volume, 0.2588417965f, 1e-6f);
    EXPECT_EQ(sound.waves[1].waveIndex, 0xFFFF); EXPECT_EQ(sound.waves[1].wavebankIndex, 0xFF);
    EXPECT_EQ(sound.waves[1].filterType, 0); EXPECT_EQ(sound.waves[1].filterFrequencyHz, 4000);
    EXPECT_EQ(sound.waves[1].filterQFactorRaw, 3);
    const auto& next = xsb.sounds[1];
    EXPECT_EQ(next.categoryIndex, 0xABCD); EXPECT_EQ(next.pitchCents, 450); EXPECT_EQ(next.priority, 13);
    ASSERT_EQ(next.waves.size(), 1u); EXPECT_EQ(next.waves[0].waveIndex, 0x3456);
    EXPECT_EQ(next.waves[0].wavebankIndex, 4);
    ASSERT_EQ(xsb.cues.size(), 1u); EXPECT_TRUE(xsb.cues[0].isSingleSound);
    EXPECT_EQ(xsb.cues[0].soundIndex, 1u); // catches wrong shared cursor/soundCodeMap, too
}

TEST(XactEventIrTest, SynthesizesSimpleSoundAsOneZeroTimePlayWave)
{
    // FACT_internal.c:2646-2664: simple sound synthesizes one event and no filter.
    const auto xsb = ParseXsb(MakeBank({{}}, false, true).bytes);
    const auto& sound = xsb.sounds.at(1);
    ASSERT_EQ(sound.tracks.size(), 1u);
    const auto& track = sound.tracks[0];
    EXPECT_TRUE(track.complete); EXPECT_EQ(track.code, 0u); EXPECT_EQ(track.declaredEventCount, 1);
    EXPECT_EQ(track.filterType, 0xFF); EXPECT_EQ(track.filterFrequencyHz, 0);
    EXPECT_EQ(track.filterQFactorRaw, 0); EXPECT_NEAR(track.volume, 0.5087649718f, 1e-6f);
    ASSERT_EQ(track.events.size(), 1u);
    const auto& event = track.events[0];
    EXPECT_EQ(event.type, XsbEventType::PlayWave); EXPECT_EQ(event.info, 0u);
    EXPECT_EQ(event.timestamp, 0); EXPECT_EQ(event.randomOffset, 0);
    const auto& play = std::get<XsbPlayWaveEvent>(event.payload);
    EXPECT_EQ(play.flags, 0); EXPECT_EQ(play.position, 0); EXPECT_EQ(play.angle, 0);
    EXPECT_EQ(play.wave.waveIndex, 0x3456); EXPECT_EQ(play.wave.wavebankIndex, 4);
    EXPECT_EQ(play.wave.loopCount, 0); EXPECT_EQ(play.wave.filterType, 0xFF);
    EXPECT_TRUE(play.wave.trackVariationEntries.empty()); EXPECT_EQ(play.wave.effectVariationFlags, 0);
    EXPECT_NEAR(play.wave.volume, 0.5087649718f, 1e-6f);
}

TEST(XactEventIrTest, KeepsEmptyTracksAndZeroTrackSounds)
{
    // Empty event lists remain real tracks; no PlayWave is invented.
    const auto xsb = ParseXsb(MakeBank({{}, {Marker()}}).bytes);
    const auto& sound = xsb.sounds.at(0);
    ASSERT_EQ(sound.tracks.size(), 2u); ASSERT_EQ(sound.waves.size(), 2u);
    EXPECT_TRUE(sound.tracks[0].complete); EXPECT_EQ(sound.tracks[0].declaredEventCount, 0);
    EXPECT_TRUE(sound.tracks[0].events.empty()); EXPECT_EQ(sound.waves[0].waveIndex, 0xFFFF);
    EXPECT_EQ(sound.waves[0].wavebankIndex, 0xFF);
    ASSERT_EQ(sound.tracks[1].events.size(), 1u); EXPECT_EQ(sound.waves[1].waveIndex, 0xFFFF);
    const auto empty = ParseXsb(MakeBank({}).bytes);
    ASSERT_EQ(empty.sounds.size(), 1u);
    EXPECT_TRUE(empty.sounds[0].tracks.empty()); EXPECT_TRUE(empty.sounds[0].waves.empty());
}

TEST(XactEventIrTest, RetainsAll255EventsWithoutCounterWrapOrDroppingTheLast)
{
    // eventCount is u8; use its entire range and independently authored marker arithmetic.
    std::vector<Bytes> authored;
    for (uint32_t i = 0; i < 255; ++i) authored.push_back(Marker(9, 0x01020300u + i));
    const auto xsb = ParseXsb(MakeBank({authored}).bytes);
    const auto& track = xsb.sounds.at(0).tracks.at(0);
    EXPECT_TRUE(track.complete); EXPECT_EQ(track.declaredEventCount, 255);
    ASSERT_EQ(track.events.size(), 255u);
    for (std::size_t i = 0; i < 255; ++i)
    {
        EXPECT_EQ(track.events[i].type, XsbEventType::Marker);
        EXPECT_EQ(std::get<XsbMarkerEvent>(track.events[i].payload).marker, 0x01020300u + i);
    }
}

TEST(XactEventIrTest, UnknownKindIsExplicitlyIncompleteAndKeepsLegacyStopScan)
{
    // Compatibility oracle: existing XactParserTest.ComplexTrackStopsScanningAtUnrecognizedEventType.
    // Unknown payloads have no recoverable size; do not claim this prefix is a complete sequence.
    const auto xsb = ParseXsb(MakeBank({{Header(2, 9, 13), Play()}}).bytes);
    const auto& sound = xsb.sounds.at(0);
    const auto& track = sound.tracks.at(0);
    EXPECT_FALSE(track.complete); EXPECT_EQ(track.declaredEventCount, 2);
    ASSERT_EQ(track.events.size(), 1u);
    EXPECT_EQ(static_cast<uint8_t>(track.events[0].type), 2);
    EXPECT_EQ(track.events[0].timestamp, 9); EXPECT_EQ(track.events[0].randomOffset, 13);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(track.events[0].payload));
    EXPECT_EQ(sound.waves.at(0).waveIndex, 0xFFFF); EXPECT_EQ(sound.waves[0].wavebankIndex, 0xFF);
}

TEST(XactEventIrTest, RejectsEveryTruncatedSupportedHeaderAndPayload)
{
    // Ctx's documented bounded-input contract; tails end at EOF, not unrelated valid bytes.
    std::vector<Bytes> events{Stop(), Play(1), Play(3), Play(4), Play(6), Marker(), Marker(18)};
    for (uint8_t type : {7, 8, 16, 17})
    {
        events.push_back(Value(type, false)); events.push_back(Value(type, true));
    }
    for (std::size_t kind = 0; kind < events.size(); ++kind)
    {
        const auto bank = MakeBank({{events[kind]}});
        const auto valid = ParseXsb(bank.bytes);
        ASSERT_EQ(valid.sounds.at(0).tracks.at(0).events.size(), 1u);
        EXPECT_TRUE(valid.sounds[0].tracks[0].complete);
        for (std::size_t length = bank.eventCounts[0]; length < bank.bytes.size(); ++length)
        {
            SCOPED_TRACE(kind);
            SCOPED_TRACE(length);
            const Bytes truncated(bank.bytes.begin(), bank.bytes.begin() + length);
            EXPECT_THROW(ParseXsb(truncated), std::runtime_error);
        }
    }
}

TEST(XactEventIrTest, RejectsBadSeparatorCountCandidateExtentAndAbsoluteTrackOffset)
{
    // Separator=FF is FACT_internal.c:2306-2308; remaining cases are CNA's bounded-reader contract.
    auto bank = MakeBank({{Play()}});
    bank.bytes.at(bank.eventCounts[0] + 1 + 6) = 0;
    EXPECT_THROW(ParseXsb(bank.bytes), std::runtime_error);
    bank = MakeBank({{Play()}}); bank.bytes.at(bank.eventCounts[0]) = 2;
    EXPECT_THROW(ParseXsb(bank.bytes), std::runtime_error);
    bank = MakeBank({{Play(3)}});
    Set16(bank.bytes, bank.eventCounts[0] + 1 + 7 + 6, 65535); // 7-byte header + 6-byte wave prefix
    EXPECT_THROW(ParseXsb(bank.bytes), std::runtime_error);
    bank = MakeBank({{Play()}}); Set32(bank.bytes, bank.trackCodes[0], 0xFFFFFFFFu);
    EXPECT_THROW(ParseXsb(bank.bytes), std::runtime_error);
}

// Oracle: FACT_internal.c:2670-2693 reads sound RPCs then one list per track.
TEST(XactEventIr, TrackRpcCodesRemainDistinctFromSoundRpcAndDspCodes)
{
    auto bank=MakeBank({{Play(1)}, {Play(1)}} ,true);
    auto& b=bank.bytes;
    b.at(138)=0x17; // COMPLEX|HAS_RPC|HAS_TRACK_RPC|HAS_DSP
    Bytes lists; U8(lists,1); U32(lists,0x99887766); U8(lists,1); U32(lists,0x55443322);
    // Sound header 138+10, sound RPC block 7 => track RPC lists at155.
    b.insert(b.begin()+155,lists.begin(),lists.end()); Set16(b,148,17);
    for(auto p:bank.trackCodes)
    {
        // Each track-code field and the event block it points at move by10.
        const auto at=p+10;
        const uint32_t old=uint32_t(b.at(at))|(uint32_t(b.at(at+1))<<8)|
            (uint32_t(b.at(at+2))<<16)|(uint32_t(b.at(at+3))<<24);
        Set32(b,at,old+10);
    }
    const auto x=ParseXsb(b);
    ASSERT_EQ(x.sounds.size(),1u); ASSERT_EQ(x.sounds[0].tracks.size(),2u);
    EXPECT_EQ(x.sounds[0].rpcCodes,(std::vector<uint32_t>{0x11223344}));
    EXPECT_EQ(x.sounds[0].tracks[0].rpcCodes,(std::vector<uint32_t>{0x99887766}));
    EXPECT_EQ(x.sounds[0].tracks[1].rpcCodes,(std::vector<uint32_t>{0x55443322}));
    ASSERT_EQ(x.sounds[0].tracks[0].events.size(),1u);
    ASSERT_EQ(x.sounds[0].tracks[1].events.size(),1u);
}

TEST(XactEventIr, SimpleSoundKeepsItsSyntheticTracksRpcList)
{
    auto bank=MakeBank({}); auto& b=bank.bytes; b.resize(138);
    U8(b,4); U16(b,0); U8(b,128); U16(b,0); U8(b,0); U16(b,0);
    U16(b,7); U8(b,0); // simple wave reference: wave7, bank0
    U16(b,7); U8(b,1); U32(b,0x12345678); // track-only RPC block
    const auto x=ParseXsb(b);
    ASSERT_EQ(x.sounds.size(),1u); ASSERT_EQ(x.sounds[0].tracks.size(),1u);
    EXPECT_TRUE(x.sounds[0].rpcCodes.empty());
    EXPECT_EQ(x.sounds[0].tracks[0].rpcCodes,(std::vector<uint32_t>{0x12345678}));
}

namespace {
Bytes LegacyEffectPlay(uint8_t family,uint16_t version,uint8_t oldFlags)
{
    Bytes b=Header(family,4,7);U8(b,0x81);
    if(family==4){U16(b,0x1234);U8(b,3);}
    U8(b,2);U16(b,0x2345);U16(b,0x3456);
    U16(b,1000);U16(b,static_cast<uint16_t>(-1000));U8(b,128);U8(b,255);
    if(version==43)U8(b,oldFlags);
    else {F32(b,200);F32(b,8000);F32(b,.5f);F32(b,2);U16(b,0x30F3);}
    if(family==6)
    {U32(b,0x00430001);U32(b,0x44332211);U16(b,0x4567);U8(b,4);U8(b,12);U8(b,29);}
    return b;
}
uint16_t LegacyFlags(unsigned value)
{
    // Independent bit table, verified against all256 native variation_flags_from_3_0 inputs.
    constexpr std::array<uint16_t,8> flags{1,0,4,0,0x100,0x200,0x1000,0x2000};
    uint16_t result=0;
    for(unsigned bit=0;bit<flags.size();++bit)if(value&(1u<<bit))result|=flags[bit];
    return result;
}
}

TEST(XactLegacyXsb, TrackMetadataSkipsAbsentFiltersAndKeepsTheFollowingSound)
{
    for(const uint16_t version:{43,44,45,46})
    {
        SCOPED_TRACE(version);
        const auto bank=MakeBank({{Play()},{Play()}},false,true,version,true);
        const auto parsed=ParseXsb(bank.bytes);ASSERT_EQ(parsed.sounds.size(),2u);
        ASSERT_EQ(parsed.sounds[0].tracks.size(),2u);
        for(unsigned t=0;t<2;++t)
        {
            const auto& track=parsed.sounds[0].tracks[t];
            EXPECT_EQ(track.code,bank.eventCounts[t]);ASSERT_EQ(track.events.size(),1u);
            EXPECT_EQ(track.filterType,version==43?255:t?0:2);
            EXPECT_EQ(track.filterQFactorRaw,version==43?0:t?3:6);
            EXPECT_EQ(track.filterFrequencyHz,version==43?0:t?4000:8000);
            const auto& play=std::get<XsbPlayWaveEvent>(track.events[0].payload);
            EXPECT_EQ(play.wave.waveIndex,0x1234);
        }
        EXPECT_EQ(parsed.sounds[1].pitchCents,450);
        ASSERT_EQ(parsed.sounds[1].waves.size(),1u);EXPECT_EQ(parsed.sounds[1].waves[0].waveIndex,0x3456);
    }
}

TEST(XactLegacyXsb, EffectFlagsMapEveryLegacyByteWithoutConsumingTheNextTrack)
{
    // The same259 sound/track layouts and inputs were parsed by native CreateSoundBank/Destroy.
    for(const uint16_t version:{43,44,45,46})for(unsigned old=0;old<(version==43?256u:1u);++old)
    {
        SCOPED_TRACE(version);
        SCOPED_TRACE(old);
        const auto bank=MakeBank({{LegacyEffectPlay(4,version,old)},
            {LegacyEffectPlay(6,version,old)}},false,true,version,true);
        const auto parsed=ParseXsb(bank.bytes);ASSERT_EQ(parsed.sounds.size(),2u);
        ASSERT_EQ(parsed.sounds[0].tracks.size(),2u);
        for(unsigned t=0;t<2;++t)
        {
            const auto& track=parsed.sounds[0].tracks[t];ASSERT_EQ(track.events.size(),1u);
            EXPECT_EQ(track.code,version==43?(t?187u:158u):(t?212u:166u));
            const auto& event=track.events[0];EXPECT_EQ(event.timestamp,4);EXPECT_EQ(event.randomOffset,7);
            const auto& play=std::get<XsbPlayWaveEvent>(event.payload);const auto& wave=play.wave;
            EXPECT_EQ(wave.effectMinPitch,1000);EXPECT_EQ(wave.effectMaxPitch,-1000);
            EXPECT_EQ(wave.effectVariationFlags,version==43?LegacyFlags(old):0x30F3);
            EXPECT_EQ(wave.effectMinFrequency,version==43?0:200);
            EXPECT_EQ(wave.effectMaxFrequency,version==43?0:8000);
            EXPECT_EQ(wave.effectMinQFactor,version==43?0:.5f);
            EXPECT_EQ(wave.effectMaxQFactor,version==43?0:2);
            EXPECT_EQ(play.minVolumeByte,128);EXPECT_EQ(play.maxVolumeByte,255);
            EXPECT_EQ(play.flags,0x81);EXPECT_EQ(play.position,0x2345);EXPECT_EQ(play.angle,0x3456);
            EXPECT_EQ(wave.loopCount,2);
            if(t)
            {
                ASSERT_EQ(wave.trackVariationEntries.size(),1u);
                EXPECT_EQ(wave.trackVariationEntries[0].waveIndex,0x4567);
                EXPECT_EQ(wave.trackVariationEntries[0].wavebankIndex,4);
                EXPECT_EQ(wave.trackVariationEntries[0].weight,17);
            }
            else {EXPECT_EQ(wave.waveIndex,0x1234);EXPECT_EQ(wave.wavebankIndex,3);}
        }
        EXPECT_EQ(bank.bytes.size()-17,version==43?221u:263u);
        ASSERT_EQ(parsed.cues.size(),1u);EXPECT_EQ(parsed.cues[0].soundIndex,1u);
        EXPECT_EQ(parsed.sounds[1].pitchCents,450);
    }
}

TEST(XactLegacyXsb, EveryTruncatedEffectTailOrCandidateTableIsRefused)
{
    // Defensive CNA bounds checks; malformed native buffers are not executed.
    for(const uint16_t version:{43,44,45,46})for(const uint8_t family:{4,6})
    {
        const auto bank=MakeBank({{LegacyEffectPlay(family,version,255)}},false,false,version);
        for(std::size_t size=bank.eventCounts[0]+1;size<bank.bytes.size();++size)
        {
            SCOPED_TRACE(version);
            SCOPED_TRACE(family);
            SCOPED_TRACE(size);
            const Bytes truncated(bank.bytes.begin(),bank.bytes.begin()+size);
            EXPECT_THROW((void)ParseXsb(truncated),std::runtime_error);
        }
    }
}
