// SPDX-License-Identifier: MS-PL
// NULL audio intentionally exposes no mixer. These tests exercise the real Cue/
// AudioEngine scheduler and instance property composition; the isolated offline
// XactValueEvents suite separately qualifies both real mixer adapters.
#include <gtest/gtest.h>
#include "CNA/TestSupport/TestScratch.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "CueTestAccess.hpp"
#include "AudioEngineTestAccess.hpp"
#include "SoundBankTestAccess.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace Microsoft::Xna::Framework::Audio;
using Bytes=std::vector<uint8_t>;
void U8(Bytes& b,uint8_t v) { b.push_back(v); }
void U16(Bytes& b,uint16_t v) { U8(b,v&255); U8(b,v>>8); }
void U32(Bytes& b,uint32_t v) { U16(b,v&65535); U16(b,v>>16); }
void Text(Bytes& b,const char* s,std::size_t width)
{
    const auto start=b.size(); b.resize(start+width,0);
    std::memcpy(b.data()+start,s,std::min(width,std::strlen(s)));
}
#include "XactScheduleBankBytes.hpp"
#include "XactValueEventBytes.hpp"

class ClockBanks
{
    std::filesystem::path directory_;
    std::string Write(const char* name,const Bytes& bytes)
    {
        const auto path=directory_/name; std::ofstream f(path,std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if (!f) throw std::runtime_error("cannot write value clock bank");
        return path.string();
    }
public:
    using Clock=std::chrono::steady_clock;
    Clock::time_point now{std::chrono::milliseconds(1000)};
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    explicit ClockBanks(const AuthoredTracks& tracks,uint16_t fade=0,bool rpc=false)
    {
        static unsigned serial=0;
        directory_=CNA::TestSupport::FixtureRoot()/("gap01c-clock-"+std::to_string(serial++));
        std::filesystem::create_directories(directory_);
        // The XGS uses literal PUBLIC|CUE variable/curves even when unreferenced.
        engine=std::make_unique<AudioEngine>(Write("settings.xgs",ValueRpcXgs()));
        engine->GetCategory("Default").SetVolume(1.0f);
        AudioEngineTestAccess::SetEventClock(*engine,[this] { return now; });
        wave=std::make_unique<WaveBank>(engine.get(),Write("waves.xwb",ScheduleXwb()));
        sound=std::make_unique<SoundBank>(engine.get(),Write("sound.xsb",ScheduleXsb(tracks,fade,rpc)));
    }
    ~ClockBanks()
    {
        sound.reset(); wave.reset(); engine.reset();
        std::error_code ignored; std::filesystem::remove_all(directory_,ignored);
    }
    std::unique_ptr<Cue> Play()
    { std::unique_ptr<Cue> cue(sound->GetCue("Sequence")); cue->Play(); return cue; }
    void SetTime(int64_t ms) { now=Clock::time_point{std::chrono::milliseconds(1000+ms)}; }
    void Tick(int64_t ms) { SetTime(ms); engine->Update(); }
};

// FA FACT_internal.c:66-69,2012,1551-1571. Each authored byte180 gives
// c=3969*log10(180/28240)+8715=.6946086997304519 cB; B=10^(3c/2000).
// Sound*track*category is B. Instance gain clamps at1; pitch is cents/1200.
// 1e-5 covers float/pow rounding, smaller than a one-cent pitch step.
constexpr float B=1.0024019735836718f;
void ExpectValues(Cue& cue,std::size_t index,float gain,float pitch)
{
    auto* instance=CueTestAccess::ActiveInstance(cue,index); ASSERT_NE(instance,nullptr);
    EXPECT_NEAR(instance->getVolumeProperty(),gain,1e-5f);
    EXPECT_NEAR(instance->getPitchProperty(),pitch,1e-5f);
}

TEST(XactValueClock, EquationsReplaceAddAndStayOnOwningTrack)
{
    // FACT_internal.c:1321-1365: ADD accumulates event offsets, not voice bases.
    ClockBanks b({{PlayAt(0),EquationAt(7,20,4,300),EquationAt(8,20,4,-2000),
        EquationAt(7,30,5,300),EquationAt(8,30,5,-2000),
        EquationAt(7,40,4,-600),EquationAt(8,40,4,-1000),StopAt(60000,1)},
        {PlayAt(0,1),StopAt(60000,1)}});
    auto cue=b.Play(); b.Tick(19); ExpectValues(*cue,0,1,0);
    b.Tick(20); ExpectValues(*cue,0,B*.1f,.25f); ExpectValues(*cue,1,1,0);
    b.Tick(30); ExpectValues(*cue,0,B*.01f,.5f);
    b.Tick(40); b.Tick(41); ExpectValues(*cue,0,B*.316227766f,-.5f);
    ExpectValues(*cue,1,1,0);
}

TEST(XactValueClock, RandomDrawsAtActivationAndRepeatNotAtGetters)
{
    // FACT_internal.c:1321-1353,1371-1383; literal bit precedes random bit.
    ClockBanks b({{PlayAt(0),EquationAt(16,10,9,0,1200,1,10),
        EquationAt(8,10,8,-2000,-4000),EquationAt(8,30,12,-2000,999),
        EquationAt(8,40,8,-2000,-2000),StopAt(60000,1)}});
    std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
    const std::array<float,4> samples{.25f,.75f,.5f,1}; std::size_t draws=0;
    CueTestAccess::SetValueRandom(*cue,[&] {
        EXPECT_LT(draws,samples.size()); return draws<samples.size() ? samples[draws++] : 0;
    });
    cue->Play(); b.Tick(9); EXPECT_EQ(draws,0u);
    b.SetTime(10); (void)cue->getIsPlayingProperty(); EXPECT_EQ(draws,0u);
    b.Tick(10); EXPECT_EQ(draws,2u); // 300 cents; -3500 cB, reversed range
    ExpectValues(*cue,0,B*.0177827941f,.25f);
    b.Tick(10); EXPECT_EQ(draws,2u);
    b.Tick(20); EXPECT_EQ(draws,3u); ExpectValues(*cue,0,B*.0177827941f,.75f);
    b.Tick(30); EXPECT_EQ(draws,3u); ExpectValues(*cue,0,B*.1f,.75f);
    b.Tick(40); EXPECT_EQ(draws,4u); // equal bounds still sample
}

TEST(XactValueClock, RepeatCountFrequencyAndLateUpdatesAreBounded)
{
    // FACT_internal.c:788-793,1371-1383: repeats are additional; one per Update.
    for (uint16_t repeats : {0,1,2})
    {
        SCOPED_TRACE(repeats);
        ClockBanks b({{PlayAt(0),EquationAt(16,10,5,100,0,repeats,20),
            EquationAt(17,10,5,-1000,0,repeats,20),StopAt(60000,1)}});
        auto cue=b.Play(); b.Tick(10); b.Tick(29);
        ExpectValues(*cue,0,B*.316227766f,1.0f/12);
        b.Tick(1000); const int count=repeats ? 2 : 1;
        ExpectValues(*cue,0,B*(count==1 ? .316227766f : .1f),count/12.0f);
        b.Tick(1000); b.Tick(1000);
        ExpectValues(*cue,0,B*(repeats==0 ? .316227766f : repeats==1 ? .1f : .0316227766f),
                     (repeats+1)/12.0f);
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u); // only authored Stop remains
    }
    for (uint16_t sentinel : {255,65535})
    {
        ClockBanks b({{EquationAt(17,10,8,-2000,-2000,sentinel,0)}});
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); unsigned draws=0;
        CueTestAccess::SetValueRandom(*cue,[&] { ++draws; return .5f; }); cue->Play();
        const unsigned calls=unsigned(sentinel)+3;
        for (unsigned i=0;i<calls;++i) b.Tick(10);
        EXPECT_EQ(draws,calls); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u);
    }
}

TEST(XactValueClock, AllRampKindsUseDecodedSlopeAndReferenceIgnoresSlopeDelta)
{
    // FACT_internal.c:1300-1317,2445: initialSlope already x100, endpoint has
    // ten slices/sec. Its slopeDelta FIXME means +1/-1 do not change this oracle.
    for (uint8_t type : {7,8,16,17}) for (float delta : {-1.0f,0.0f,1.0f})
    {
        const bool pitch=type==7 || type==16;
        ClockBanks b({{PlayAt(0),RampAt(type,20,pitch ? -600.0f : 0.0f,
            pitch ? 1.5f : -5.0f,400,delta),StopAt(60000,1)}});
        auto cue=b.Play(); b.Tick(19); ExpectValues(*cue,0,1,0);
        b.Tick(20); ExpectValues(*cue,0,1,pitch ? -.5f : 0);
        b.Tick(220); ExpectValues(*cue,0,pitch ? 1 : B*.316227766f,pitch ? -.25f : 0);
        b.Tick(420); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),2u);
        ExpectValues(*cue,0,pitch ? 1 : B*.1f,0);
        b.Tick(421); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u);
        b.Tick(1000); ExpectValues(*cue,0,pitch ? 1 : B*.1f,0);
    }
}

TEST(XactValueClock, ZeroDurationVolumeFollowsReferenceIeeeArithmetic)
{
    // FACT_internal.c:1303-1315 and FAudio_internal.h:218-219. At the start,
    // float(0)/0 is NaN and the comparison clamp retains NaN; 0*NaN stays NaN.
    // On a later tick positive/0 is +inf, clamps to1, leaving initialValue.
    // This is a pinned FAudio fallback; Microsoft native qualification is open.
    // No voice: inspect this exceptional arithmetic before CNA's existing public
    // Volume setter (which rejects NaN). This is not a native mixer-output claim.
    ClockBanks b({{RampAt(8,10,-2000,1,0)}});
    auto cue=b.Play(); b.Tick(10);
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u);
    EXPECT_TRUE(std::isnan(CueTestAccess::TrackVolumeCentibels(*cue,0)));
    b.Tick(11); EXPECT_FLOAT_EQ(CueTestAccess::TrackVolumeCentibels(*cue,0),-2000);
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
}

TEST(XactValueClock, PauseFreezesRampRepeatsAndZeroTimeValuesApplyDuringPlay)
{
    // FACT_internal.c:1471-1476,1727-1736; FACT.c:2622-2689, cue active time.
    for (bool categoryPause : {false,true})
    {
        ClockBanks b({{PlayAt(0),RampAt(8,0,-2000,2.5f,400),
            EquationAt(16,100,5,100,0,2,100),StopAt(60000,1)}});
        auto cue=b.Play(); ExpectValues(*cue,0,B*.1f,0);
        auto category=b.engine->GetCategory("Default"); b.Tick(100);
        if (categoryPause) category.Pause(); else cue->Pause(); b.Tick(1100);
        ExpectValues(*cue,0,B*.133352143f,1.0f/12);
        if (categoryPause) category.Resume(); else cue->Resume(); b.Tick(1200);
        ExpectValues(*cue,0,B*.177827941f,1.0f/6);
        b.Tick(1300); ExpectValues(*cue,0,B*.237137371f,.25f);
        b.Tick(1401); ExpectValues(*cue,0,B*.316227766f,.25f);
    }
}

TEST(XactValueClock, ValuesSurviveDelayedWavesAndPendingRampControlsTermination)
{
    // FACT_internal.c:707-708,1343-1347,1496-1539,1551-1571. Track state
    // survives voice changes; an unfinished ramp is work even without a voice.
    ClockBanks b({{EquationAt(7,10,5,600),EquationAt(8,10,5,-2000),
        PlayAt(20),PlayAt(40,1),StopAt(60000,1)}});
    auto cue=b.Play(); b.Tick(20); ExpectValues(*cue,0,B*.1f,.5f);
    b.Tick(40); ExpectValues(*cue,1,B*.1f,.5f);
    ClockBanks tail({{RampAt(8,20,-2000,2.5f,400)}});
    tail.sound->PlayCue("Sequence"); tail.Tick(220);
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*tail.sound),1u);
    tail.Tick(420); EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*tail.sound),1u);
    tail.Tick(421); EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*tail.sound),0u);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*tail.engine),0u);
}

TEST(XactValueClock, TrackStopAndAllDisposalRoutesCancelValueWork)
{
    // FACT_internal.c:1189-1240,1727-1749; track Stop must not cancel siblings.
    ClockBanks scoped({{PlayAt(0),RampAt(8,0,0,-5,400),StopAt(100,1)},
        {PlayAt(0,1),EquationAt(17,50,5,-1000,0,1,100),StopAt(60000,1)}});
    auto active=scoped.Play(); scoped.Tick(50); scoped.Tick(100); scoped.Tick(150);
    ExpectValues(*active,1,B*.1f,0); EXPECT_EQ(CueTestAccess::PendingEvents(*active),1u);
    for (int owner : {0,1,2,3,4,5})
    {
        ClockBanks b({{PlayAt(0),EquationAt(17,10,8,-2000,-2000,65535,10),StopAt(60000,1)}});
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); unsigned draws=0;
        CueTestAccess::SetValueRandom(*cue,[&] { ++draws; return .25f; });
        cue->Play(); b.Tick(10); b.SetTime(17);
        if (owner==0) cue->Stop(AudioStopOptions::Immediate);
        if (owner==1) cue->Stop(AudioStopOptions::AsAuthored);
        if (owner==2) cue->Dispose();
        if (owner==3) b.sound->Dispose();
        if (owner==4) b.wave->Dispose();
        if (owner==5) b.engine->Dispose();
        b.Tick(100); EXPECT_EQ(draws,1u); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        if (owner<2)
        {
            cue->Play(); b.Tick(200); EXPECT_EQ(draws,1u);
            auto fresh=b.Play(); ExpectValues(*fresh,0,1,0);
            b.Tick(210); ExpectValues(*fresh,0,B*.1f,0);
        }
    }
}

TEST(XactValueClock, RpcCategoryAndReleaseRetainEventsUntilDeadline)
{
    // FACT_internal.c:1442-1471,1551-1571: event+base+RPC before conversion,
    // then category/fade. Combined cents are truncated once, not per component.
    ClockBanks b({{PlayAt(0),EquationAt(7,0,4,600.75f),EquationAt(8,0,4,-2000),
        EquationAt(8,101,4,0),StopAt(60000,1)}},100,true);
    auto cue=b.Play(); ExpectValues(*cue,0,B*.01f,0);
    cue->SetVariable("Volume",1); b.Tick(1); ExpectValues(*cue,0,B*.1f,1);
    auto category=b.engine->GetCategory("Default"); category.SetVolume(.5f);
    ExpectValues(*cue,0,B*.05f,1);
    cue->Stop(AudioStopOptions::AsAuthored); b.Tick(51); ExpectValues(*cue,0,B*.025f,1);
    category.SetVolume(.25f); ExpectValues(*cue,0,B*.0125f,1);
    category.SetVolume(.5f); ExpectValues(*cue,0,B*.025f,1);
    cue->SetVariable("Volume",0); b.Tick(76); ExpectValues(*cue,0,B*.00125f,0);
    unsigned dispatched=0;
    CueTestAccess::ObserveDispatch(*cue,[&](std::size_t,std::size_t,uint64_t) { ++dispatched; });
    b.Tick(101); EXPECT_EQ(dispatched,0u); EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
}
} // namespace
