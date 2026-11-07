// SPDX-License-Identifier: MS-PL
// NULL completion-state tests: actual Cue/AudioEngine/voice property paths,
// with completed-voice loop budgets explicitly armed by the friend accessor.
// Real mixer EOF and PCM are separately covered by XactLoopVariation cases.
#if !defined(SOUND_ENABLED)
#include <gtest/gtest.h>
#include "CNA/TestSupport/TestScratch.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CueTestAccess.hpp"
#include "AudioEngineTestAccess.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace Microsoft::Xna::Framework::Audio;
using namespace CNA::Internal::Audio;
using Bytes=std::vector<uint8_t>;
void U8(Bytes& b,uint8_t v) { b.push_back(v); }
void U16(Bytes& b,uint16_t v) { U8(b,v&255); U8(b,v>>8); }
void U32(Bytes& b,uint32_t v) { U16(b,v&65535); U16(b,v>>16); }
void Text(Bytes& b,const char* s,std::size_t width)
{
    auto offset=b.size(); b.resize(offset+width,0);
    std::memcpy(b.data()+offset,s,std::min(width,std::strlen(s)));
}
#include "XactScheduleBankBytes.hpp"
#include "XactValueEventBytes.hpp"
#include "XactVariationBankBytes.hpp"

class VariationClockBank
{
    std::filesystem::path directory_;
    std::string Write(const char* name,const Bytes& bytes)
    {
        const auto path=directory_/name; std::ofstream f(path,std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if (!f) throw std::runtime_error("cannot write loop clock bank");
        return path.string();
    }
public:
    using Clock=std::chrono::steady_clock;
    Clock::time_point now{std::chrono::milliseconds(1000)};
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    explicit VariationClockBank(const AuthoredTracks& tracks)
    {
        static unsigned serial=0;
        directory_=CNA::TestSupport::FixtureRoot()/("gap03-clock-"+std::to_string(serial++));
        std::filesystem::create_directories(directory_);
        engine=std::make_unique<AudioEngine>(Write("settings.xgs",ValueRpcXgs()));
        engine->GetCategory("Default").SetVolume(1.0f);
        AudioEngineTestAccess::SetEventClock(*engine,[this]{ return now; });
        auto bytes=ScheduleXwb();
        // Literal per-candidate loopStart metadata1,2,3 proves which source wave
        // the newly created instance actually receives, without an audio device.
        for (std::size_t i=0;i<3;++i) bytes[144+24*i+16]=uint8_t(i+1);
        wave=std::make_unique<WaveBank>(engine.get(),Write("waves.xwb",bytes));
        sound=std::make_unique<SoundBank>(engine.get(),Write("sound.xsb",VariationSound(tracks)));
    }
    ~VariationClockBank()
    {
        sound.reset(); wave.reset(); engine.reset();
        std::error_code ignored; std::filesystem::remove_all(directory_,ignored);
    }
    void Tick(int ms)
    { now=Clock::time_point{std::chrono::milliseconds(1000+ms)}; engine->Update(); }
    std::unique_ptr<Cue> CueWithInput(std::vector<uint32_t> rolls,std::vector<uint32_t>& bounds,
                                    std::vector<float> units={})
    {
        std::unique_ptr<Cue> cue(sound->GetCue("Sequence"));
        CueTestAccess::SetVariationRandom(*cue,{
            [rolls=std::move(rolls),index=std::size_t{},&bounds](uint32_t bound) mutable {
                bounds.push_back(bound); EXPECT_LT(index,rolls.size());
                auto value=index<rolls.size() ? rolls[index++] : 0u;
                EXPECT_LT(value,bound); return value;
            },
            [units=std::move(units),index=std::size_t{}]() mutable {
                EXPECT_LT(index,units.size()); return index<units.size() ? units[index++] : 0.0f;
            }});
        cue->Play(); return cue;
    }
};
void ExpectWaveAndValues(Cue& cue,std::size_t track,std::size_t event,
                         uint32_t wave,float volume=1,float pitch=0)
{
    auto* voice=CueTestAccess::EventVoice(cue,track,event); ASSERT_NE(voice,nullptr);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(*voice),wave+1);
    // FACT_internal.c:66-69,309-379,1551-1571: raw variation cB and cents.
    EXPECT_NEAR(voice->getVolumeProperty(),volume,1e-5f);
    EXPECT_NEAR(voice->getPitchProperty(),pitch,1e-5f);
}

TEST(XactLoopClock, ActualEventControllerAdvancesOrderedAndFiniteBudget)
{
    // FACT_internal.c:201-206,443-452; count2 ->initial0,loop1,loop2; wrap covered at254.
    for (uint8_t count : {uint8_t(1),uint8_t(2),uint8_t(254)})
    {
        VariationClockBank bank({{VariedAt(0,count,XsbTrackVariationType::Ordered),StopAt(60000,1)}});
        std::vector<uint32_t> bounds; auto cue=bank.CueWithInput({},bounds);
        CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0);
        ExpectWaveAndValues(*cue,0,0,0);
        for (unsigned i=1;i<=count;++i)
        {
            bank.Tick(i); ExpectWaveAndValues(*cue,0,0,i%3);
            EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),count-i);
        }
        bank.Tick(1000); ExpectWaveAndValues(*cue,0,0,count%3); //no extra activation
        EXPECT_TRUE(bounds.empty()); bank.Tick(60000); EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactLoopClock, RandomInputsPersistThroughRealVoiceCreation)
{
    // FACT_internal.c:734-771,199-247; weights[1,2,3], intervals[0,1),[1,3),[3,6).
    for (auto type : {XsbTrackVariationType::OrderedFromRandom,XsbTrackVariationType::Random,
                      XsbTrackVariationType::RandomNoRepeats,XsbTrackVariationType::Shuffle})
    {
        const bool ordered=type==XsbTrackVariationType::OrderedFromRandom;
        const bool random=type==XsbTrackVariationType::Random;
        const std::vector<uint32_t> rolls=ordered ? std::vector<uint32_t>{2}
            : random ? std::vector<uint32_t>{5,0,1,3,2} : std::vector<uint32_t>{5,1,3,0,0};
        const std::vector<uint32_t> expected=ordered ? std::vector<uint32_t>{2,0,1,2}
            : random ? std::vector<uint32_t>{0,1,2,1} : std::vector<uint32_t>{1,2,0,1};
        VariationClockBank bank({{VariedAt(0,3,type),StopAt(60000,1)}});
        std::vector<uint32_t> bounds; auto cue=bank.CueWithInput(rolls,bounds);
        CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0); ExpectWaveAndValues(*cue,0,0,expected[0]);
        for (int i=1;i<4;++i) { bank.Tick(i); ExpectWaveAndValues(*cue,0,0,expected[i]); }
        EXPECT_EQ(bounds,ordered ? (std::vector<uint32_t>{6}) : random
            ? (std::vector<uint32_t>{6,6,6,6,6}) : (std::vector<uint32_t>{6,3,4,3,5}));
    }
}

TEST(XactLoopClock, LoopEffectsRecombineAddNewAndHoldOnRealInstances)
{
    // FACT_internal.c:309-379; byte90c=-1194.0934440906112, B=1.0024019735836718.
    // GainB*10^(n*c/2000); pitchn*300/1200. Literal values from hand arithmetic.
    for (uint16_t mode : {uint16_t(0x0005),uint16_t(0x0300),uint16_t(0x0305)})
    {
        VariationClockBank bank({{VariedAt(0,2,XsbTrackVariationType::Ordered,0x3000|mode),StopAt(60000,1)}});
        std::vector<uint32_t> bounds; auto cue=bank.CueWithInput({},bounds,std::vector<float>(6,0));
        CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0);
        for (int i=0;i<3;++i)
        {
            if (i) bank.Tick(i);
            const int n=mode==0x0305 ? i+1 : 1;
            ExpectWaveAndValues(*cue,0,0,i,n==1 ? .2535100559f : n==2 ? .06411334986f : .01621443227f,n*.25f);
        }
    }
}

TEST(XactLoopClock, GetterKeepsPendingCompletedLoopAliveAndPauseDefersPromotion)
{
    // Refresh Pause/Resume IL_0000-0011, FACT paused update guard; pending EOF isn't final.
    // No future Stop/event may mask a lost pending-loop guard in ReconcileState.
    VariationClockBank bank({{VariedAt(0,2,XsbTrackVariationType::Ordered)}});
    std::vector<uint32_t> bounds; auto cue=bank.CueWithInput({},bounds);
    CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0);
    EXPECT_TRUE(cue->getIsPlayingProperty()); cue->Pause(); bank.Tick(1000);
    EXPECT_TRUE(cue->getIsPausedProperty()); ExpectWaveAndValues(*cue,0,0,0);
    cue->Resume(); bank.Tick(1000); ExpectWaveAndValues(*cue,0,0,1);
}

TEST(XactLoopClock, TrackStopCancelsOnlyItsOwnLoopBeforePromotion)
{
    // FACT_internal.c:1189-1214; due Stop precedes this Update's EOF promotion.
    VariationClockBank bank({{VariedAt(0,2,XsbTrackVariationType::Ordered),StopAt(10,1)},
                            {VariedAt(0,2,XsbTrackVariationType::Ordered),StopAt(60000,1)}});
    std::vector<uint32_t> bounds; auto cue=bank.CueWithInput({},bounds);
    CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0); CueTestAccess::ArmCompletedVoiceLoop(*cue,1,0);
    bank.Tick(10); EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),0);
    EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,1,0),1); ExpectWaveAndValues(*cue,1,0,1);
}

TEST(XactLoopClock, InfiniteBudgetSurvives260CompletedVoicesAndThenStop)
{
    // Owner's G02/brief255 infinite contract; FAudio's nondelegated decrement is unqualified.
    VariationClockBank bank({{VariedAt(0,255,XsbTrackVariationType::Ordered),StopAt(60000,1)}});
    std::vector<uint32_t> bounds; auto cue=bank.CueWithInput({},bounds);
    CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0);
    for (int i=1;i<=260;++i)
    { bank.Tick(i); ExpectWaveAndValues(*cue,0,0,i%3); EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),255); }
    cue->Stop(AudioStopOptions::Immediate); bank.Tick(1000);
    EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),0);
}

TEST(XactLoopClock, NewEventsAndNewCuesStartFreshWhileReplayIsIgnored)
{
    // FACTEventInstance owns valuei, not track/cue/global shared state (:724-771).
    VariationClockBank bank({{VariedAt(0,1,XsbTrackVariationType::Ordered),
                             VariedAt(40,1,XsbTrackVariationType::Ordered),StopAt(60000,1)}});
    std::vector<uint32_t> bounds; auto cue=bank.CueWithInput({},bounds);
    CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0); bank.Tick(10); ExpectWaveAndValues(*cue,0,0,1);
    bank.Tick(40); ExpectWaveAndValues(*cue,0,1,0);
    CueTestAccess::ArmCompletedVoiceLoop(*cue,0,1); bank.Tick(41); ExpectWaveAndValues(*cue,0,1,1);
    cue->Stop(AudioStopOptions::Immediate); cue->Play();
    // FACTCue_Play refuses STOPPED; Cue is a single playthrough, not a restartable voice.
    EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_EQ(CueTestAccess::EventVoice(*cue,0,0),nullptr);
    auto fresh=bank.CueWithInput({},bounds); ExpectWaveAndValues(*fresh,0,0,0);
}

TEST(XactLoopClock, CategoryAndEquationOffsetsDoNotAccumulateIntoVariationState)
{
    // FACT_internal.c:1551-1571; category multiplies, independent track pitch adds once.
    VariationClockBank bank({{VariedAt(0,1,XsbTrackVariationType::Ordered,0x3305),
                             EquationAt(7,5,4,300),StopAt(60000,1)}});
    std::vector<uint32_t> bounds; auto cue=bank.CueWithInput({},bounds,std::vector<float>(4,0));
    bank.Tick(5); auto category=bank.engine->GetCategory("Default"); category.SetVolume(.5f);
    ExpectWaveAndValues(*cue,0,0,0,.12675502794f,.5f);
    CueTestAccess::ArmCompletedVoiceLoop(*cue,0,0); bank.Tick(10);
    ExpectWaveAndValues(*cue,0,0,1,.03205667493f,.75f);
    bank.Tick(11); ExpectWaveAndValues(*cue,0,0,1,.03205667493f,.75f);
}
} // namespace
#endif
