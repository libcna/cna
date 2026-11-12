// SPDX-License-Identifier: MS-PL
// Included only by XactFiniteLoopTests.cpp's isolated offline harness, inside its
// anonymous namespace. Reuses the real wrapped ManualDevice and literal XGS writer.
// Native scheduling oracle: FACT_internal.c:1189-1290,1496-1552; managed wrapper
// oracle: Refresh Cue.Pause/Resume IL_0000-0011, state getters IL_0000-0014.
// Pause clock: measured retail XACT3.6 notifications, docs/xact-pause-clock.md.

#include "XactScheduleBankBytes.hpp"

// Render guard frames after the final PCM frame before querying completion:
// mixer EOF is observed on the next read. Those guard frames must be exact zero.
void ExpectMarker(const std::vector<float>& output, float expected)
{
    ASSERT_FALSE(output.empty());
    // Exact dyadic PCM at native rate, unity test gain, identical L/R. 1e-6
    // accommodates mixer float rounding; it is <1/100000 of the 1/8 marker gap.
    for (std::size_t i = 0; i < output.size(); ++i) ASSERT_NEAR(output[i], expected, 1e-6f) << i;
}

class ScheduledBanks
{
    std::filesystem::path directory_;
    std::string Write(const char* name, const Bytes& bytes)
    {
        const auto path = directory_ / name;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) throw std::runtime_error("cannot write authored scheduler bank");
        return path.string();
    }
public:
    using Clock = std::chrono::steady_clock;
    Clock::time_point now{std::chrono::milliseconds(1000)};
    AuthoredTracks authored;
    bool normalizeWaveGain = true;
    std::vector<Dispatch> trace;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    /**
     * @brief Creates literal banks with an independent clock and offline audio device.
     *
     * @param tracks Authored events to serialize unless soundBytes supplies the bank.
     * @param fadeOut Cue fade-out duration in milliseconds.
     * @param normalize Whether dispatch resets wave gain to isolate event scheduling.
     * @param settings Optional literal XGS data; empty selects the default settings.
     * @param rpc Whether the default sound bank binds the two value RPC curves.
     * @param soundBytes Optional literal sound-bank data.
     * @param waveBytes Optional literal wave-bank data.
     * @param streamWave Whether to load the wave bank through its streaming constructor.
     * @param applyAuthoredCategoryGain Whether to explicitly apply the authored category gain.
     */
    explicit ScheduledBanks(AuthoredTracks tracks, uint16_t fadeOut = 0,
                            bool normalize = true, Bytes settings = {}, bool rpc = false,
                            Bytes soundBytes = {}, Bytes waveBytes = {}, bool streamWave = false,
                            bool applyAuthoredCategoryGain = true)
        : authored(std::move(tracks)), normalizeWaveGain(normalize)
    {
        voiceReceipts.clear(); // do not reuse observations from a destroyed fixture's track
        EnsureMixer();
        if (factoryCalls != 1 || !manualDevice) throw std::runtime_error("offline device seam was bypassed");
        SoundEffect::setMasterVolumeProperty(1.0f);
        static unsigned nextDirectory = 0;
        directory_ = CNA::TestSupport::FixtureRoot() / ("gap01b-schedule-"+std::to_string(nextDirectory++));
        std::filesystem::create_directories(directory_);
        engine = std::make_unique<AudioEngine>(Write("settings.xgs", settings.empty() ? Xgs() : settings));
        // FACT starts currentVolume at1. Explicit SetVolume applies authored
        // category gain, so marker arithmetic does not assume constructor timing.
        if (applyAuthoredCategoryGain) engine->GetCategory("Default").SetVolume(1.0f);
        AudioEngineTestAccess::SetEventClock(*engine, [this] { return now; });
        const auto wavePath = Write("waves.xwb", waveBytes.empty() ? ScheduleXwb() : waveBytes);
        wave = streamWave ? std::make_unique<WaveBank>(engine.get(), wavePath, 0, 0)
                          : std::make_unique<WaveBank>(engine.get(), wavePath);
        sound = std::make_unique<SoundBank>(engine.get(), Write("sound.xsb",
            soundBytes.empty() ? ScheduleXsb(authored, fadeOut, rpc) : soundBytes));
    }
    ~ScheduledBanks()
    {
        sound.reset(); wave.reset(); engine.reset();
        std::error_code ignored; std::filesystem::remove_all(directory_, ignored);
    }
    /**
     * @brief Loads another sound bank sharing this fixture's engine and wave bank.
     *
     * @param fileName Fixture-local name for the authored bank file.
     * @param bytes Literal sound-bank data.
     * @return An owned bank that must be destroyed before this fixture.
     */
    std::unique_ptr<SoundBank> LoadSoundBank(const char* fileName,const Bytes& bytes)
    {
        return std::make_unique<SoundBank>(engine.get(),Write(fileName,bytes));
    }
    /**
     * @brief Loads an additional wave bank on the fixture's engine.
     * @param fileName Fixture-local file name.
     * @param bytes Authored wave-bank bytes.
     * @return The owned wave bank.
     */
    std::unique_ptr<WaveBank> LoadWaveBank(const char* fileName,const Bytes& bytes)
    { return std::make_unique<WaveBank>(engine.get(),Write(fileName,bytes)); }
    void Observe(Cue& cue)
    {
        CueTestAccess::ObserveDispatch(cue, [this, ptr=&cue](std::size_t t, std::size_t e, uint64_t elapsed)
        {
            trace.emplace_back(t,e,elapsed);
            const auto type=authored[t][e].type;
            if (!normalizeWaveGain || (type != 1 && type != 3 && type != 4 && type != 6)) return;
            // Isolate dispatch/output from the unrelated XACT logarithmic byte-volume
            // conversion. No device samples are consumed between Play and this hook.
            for (std::size_t i=0; auto* instance = CueTestAccess::ActiveInstance(*ptr,i); ++i)
                instance->setVolumeProperty(1.0f);
        });
    }
    std::unique_ptr<Cue> Play()
    {
        std::unique_ptr<Cue> cue(sound->GetCue("Sequence"));
        Observe(*cue); cue->Play(); return cue;
    }
    void SetTime(int64_t absoluteMS) { now = Clock::time_point{std::chrono::milliseconds(1000+absoluteMS)}; }
    void Tick(int64_t absoluteMS) { SetTime(absoluteMS); engine->Update(); }
};

TEST(XactScheduler, BeforeAtAfterTimestampDispatchesExactlyOnce)
{
    ScheduledBanks b({{PlayAt(20)}}); auto cue = b.Play();
    EXPECT_TRUE(cue->getIsPlayingProperty()); EXPECT_FALSE(cue->getIsStoppedProperty());
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue), 1u); EXPECT_TRUE(b.trace.empty());
    ExpectSilence(manualDevice->Render(32));
    b.Tick(19); EXPECT_TRUE(b.trace.empty()); ExpectSilence(manualDevice->Render(32));
    b.Tick(20); EXPECT_EQ(b.trace, (std::vector<Dispatch>{{0,0,20}}));
    ExpectMarker(manualDevice->Render(32), 0.125f);
    b.Tick(21); b.Tick(21); EXPECT_EQ(b.trace.size(), 1u);
    ExpectMarker(manualDevice->Render(32), 0.125f);
}

TEST(XactScheduler, SecondPlaySurvivesSilentGapAndFinishesOnlyAfterLastWave)
{
    ScheduledBanks b({{PlayAt(0), PlayAt(40,1)}}); auto cue = b.Play();
    ExpectMarker(manualDevice->Render(441), 0.125f); ExpectSilence(manualDevice->Render(32));
    b.Tick(39); EXPECT_TRUE(cue->getIsPlayingProperty()); EXPECT_FALSE(cue->getIsStoppedProperty());
    ExpectSilence(manualDevice->Render(32));
    b.Tick(40); EXPECT_EQ(b.trace, (std::vector<Dispatch>{{0,0,0},{0,1,40}}));
    ExpectMarker(manualDevice->Render(441), 0.25f); ExpectSilence(manualDevice->Render(32));
    b.Tick(41); EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_FALSE(cue->getIsPlayingProperty());
    EXPECT_TRUE(b.sound->getIsInUseProperty()); EXPECT_TRUE(b.wave->getIsInUseProperty());
    cue.reset();EXPECT_FALSE(b.sound->getIsInUseProperty());EXPECT_TRUE(b.wave->getIsInUseProperty());
    b.sound->Dispose();EXPECT_FALSE(b.wave->getIsInUseProperty());
}

TEST(XactScheduler, EqualTimePlayThenStopAndStopThenPlayKeepFileOrder)
{
    for (bool stopFirst : {false,true})
    {
        AuthoredTracks tracks = stopFirst ? AuthoredTracks{{StopAt(20,1),PlayAt(20),PlayAt(30,1)}}
                                         : AuthoredTracks{{PlayAt(20),StopAt(20,1),PlayAt(30,1)}};
        ScheduledBanks b(tracks); auto cue=b.Play(); b.Tick(20);
        const std::vector<Dispatch> expected = stopFirst ? std::vector<Dispatch>{{0,0,20}}
                                                       : std::vector<Dispatch>{{0,0,20},{0,1,20}};
        EXPECT_EQ(b.trace,expected); ExpectSilence(manualDevice->Render(32));
        b.Tick(100); EXPECT_EQ(b.trace,expected); EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactScheduler, TrackStopCancelsOnlyItsOwnFutureEvents)
{
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,1),PlayAt(30,2)},
                      {PlayAt(0,1,255),StopAt(40,1)}}); auto cue=b.Play();
    ExpectMarker(manualDevice->Render(32),0.375f); // 1/8 + 1/4
    b.Tick(20); ExpectMarker(manualDevice->Render(32),0.25f);
    EXPECT_TRUE(cue->getIsPlayingProperty());
    b.Tick(30); EXPECT_EQ(b.trace.size(),3u); // cancelled track-0 Play never fires
    b.Tick(40); ExpectSilence(manualDevice->Render(32));
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{1,0,0},{0,1,20},{1,1,40}}));
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(XactScheduler, CueStopFlagCancelsEveryTrackWithoutInvalidatingEngineIteration)
{
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,3),PlayAt(30,2)},
                      {PlayAt(0,1,255),PlayAt(40,2)}});
    auto first=b.Play(); auto second=b.Play(); auto third=b.Play();
    b.Tick(20); ExpectSilence(manualDevice->Render(32));
    EXPECT_TRUE(first->getIsStoppedProperty()); EXPECT_TRUE(second->getIsStoppedProperty());
    EXPECT_TRUE(third->getIsStoppedProperty());
    EXPECT_EQ(b.trace.size(),9u); b.Tick(100); EXPECT_EQ(b.trace.size(),9u);
}

TEST(XactScheduler, TrackAuthoredStopExitsLoopAndLetsCurrentWaveFinish)
{
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,0),PlayAt(30,2)}}); auto cue=b.Play();
    ExpectMarker(manualDevice->Render(64),0.125f); b.Tick(20);
    EXPECT_TRUE(cue->getIsPlayingProperty());
    ExpectMarker(manualDevice->Render(377),0.125f); // 441-64 frames remain in this pass
    ExpectSilence(manualDevice->Render(32)); b.Tick(100);
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{0,1,20}}));
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(XactScheduler, ScheduledCueAuthoredStopRunsEventsDuringFadeAndCancelsAtDeadline)
{
    // FACT BeginFadeOut:921-941 only starts a release; UpdateSound:1442-1522
    // keeps scanning events until the fade deadline. An event exactly at 120 is too late.
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,2)}, {PlayAt(30,2),PlayAt(120,1)}},100);
    auto cue=b.Play(); b.Tick(20);
    EXPECT_TRUE(cue->getIsStoppingProperty()); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),2u);
    b.Tick(70); const auto half=manualDevice->Render(32);
    // Only test nonzero release here: logarithmic bank volume is a separate contract.
    for (float sample:half) EXPECT_GT(sample,0.0f);
    b.Tick(120); ExpectSilence(manualDevice->Render(32)); EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{0,1,20},{1,0,70}}));
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
}

TEST(XactScheduler, ASecondAuthoredCueStopRestartsTheReleaseUnlikeAPublicStop)
{
    // An authored cue-wide Stop event calls BeginFadeOut directly (FAudio FACT_internal.c:
    // 1224-1252), which restarts the release (:936-940); only the public FACTCue_Stop
    // (FACT.c:2393-2403) leaves a release under way alone.
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,2)},{StopAt(70,2)}},100);
    auto cue=b.Play(); b.Tick(20); EXPECT_TRUE(cue->getIsStoppingProperty());
    b.Tick(70); b.Tick(130);
    EXPECT_TRUE(cue->getIsStoppingProperty()) << "the second authored stop did not restart the release";
    b.Tick(170); EXPECT_TRUE(cue->getIsStoppedProperty());

    ScheduledBanks p({{PlayAt(0,0,255)}},100);
    auto stopped=p.Play(); p.Tick(10); stopped->Stop(AudioStopOptions::AsAuthored);
    p.Tick(60); stopped->Stop(AudioStopOptions::AsAuthored);
    p.Tick(115); EXPECT_TRUE(stopped->getIsStoppedProperty()) << "a public authored stop restarted the release";
}

TEST(XactScheduler, PauseBeforeFirstEventExcludesPausedTimeAndIsIdempotent)
{
    ScheduledBanks b({{PlayAt(20)}}); auto cue=b.Play();
    b.SetTime(7); cue->Pause(); cue->Pause(); b.Tick(1007);
    EXPECT_TRUE(cue->getIsPausedProperty()); EXPECT_TRUE(cue->getIsPlayingProperty());
    EXPECT_TRUE(b.trace.empty()); ExpectSilence(manualDevice->Render(32));
    cue->Resume(); cue->Resume(); b.Tick(1019); EXPECT_TRUE(b.trace.empty());
    b.Tick(1020); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,20}}));
    ExpectMarker(manualDevice->Render(32),0.125f);
}

Bytes SdkPauseClockBank(const char* extension)
{
    const auto path=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/
        "pause-clock"/(std::string("clock.")+extension);
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Missing SDK pause-clock bank: "+path.string());
    return Bytes(std::istreambuf_iterator<char>(file),{});
}

std::unique_ptr<Cue> PlaySdkPauseClock(ScheduledBanks& banks)
{
    std::unique_ptr<Cue> cue(banks.sound->GetCue("ClockCue"));
    CueTestAccess::ObserveDispatch(*cue,[&banks](std::size_t track,std::size_t event,uint64_t elapsed)
    { banks.trace.emplace_back(track,event,elapsed); });
    cue->Play();return cue;
}

TEST(XactScheduler, SdkMarkersExcludePausedTimeAndTwoPausesNeedOnlyOneResume)
{
    // TQ-XACT-24: real Microsoft XACT3.6 marker notifications on these untouched
    // SDK banks shift500/1000/2000/3000 by the1200ms pause, including T,T,F.
    // This observer measures CNA event dispatch, not a public marker API or PCM.
    for(const int mode:{0,1,2}) // control, one Pause, duplicate Pause + one Resume
    {
        SCOPED_TRACE(mode);
        ScheduledBanks banks({},0,false,SdkPauseClockBank("xgs"),false,
                             SdkPauseClockBank("xsb"),SdkPauseClockBank("xwb"));
        auto cue=PlaySdkPauseClock(banks);
        ASSERT_EQ(banks.trace,(std::vector<Dispatch>{{0,0,0}}));
        std::uint64_t shift=0;
        if(mode)
        {
            banks.SetTime(200);cue->Pause();
            if(mode==2){banks.SetTime(600);cue->Pause();}
            banks.Tick(1399);banks.Tick(1400);
            EXPECT_TRUE(cue->getIsPausedProperty());EXPECT_TRUE(cue->getIsPlayingProperty());
            EXPECT_EQ(banks.trace.size(),1u);EXPECT_EQ(CueTestAccess::PendingEvents(*cue),4u);
            cue->Resume();EXPECT_FALSE(cue->getIsPausedProperty());
            EXPECT_TRUE(cue->getIsPlayingProperty());shift=1200;
        }
        std::vector<Dispatch> expected{{0,0,0}};
        std::size_t event=0;
        for(const std::uint64_t markerTime:{500u,1000u,2000u,3000u})
        {
            banks.Tick(shift+markerTime-1);EXPECT_EQ(banks.trace,expected);
            expected.emplace_back(0,++event,markerTime);
            banks.Tick(shift+markerTime);banks.Tick(shift+markerTime);
            EXPECT_EQ(banks.trace,expected);
        }
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    }
}

TEST(XactScheduler, SdkMarkersKeepAnEarlierMarkerAndAccumulateTwoSeparatePauses)
{
    for(const bool twice:{false,true})
    {
        SCOPED_TRACE(twice);
        ScheduledBanks banks({},0,false,SdkPauseClockBank("xgs"),false,
                             SdkPauseClockBank("xsb"),SdkPauseClockBank("xwb"));
        auto cue=PlaySdkPauseClock(banks);std::uint64_t shift=0;
        if(twice)
        {
            banks.SetTime(200);cue->Pause();banks.Tick(1400);cue->Resume();shift=1200;
        }
        banks.Tick(shift+499);ASSERT_EQ(banks.trace,(std::vector<Dispatch>{{0,0,0}}));
        banks.Tick(shift+500);
        std::vector<Dispatch> expected{{0,0,0},{0,1,500}};
        EXPECT_EQ(banks.trace,expected);
        banks.SetTime(shift+700);cue->Pause();
        const std::uint64_t hold=twice?600:1200;
        banks.Tick(shift+700+hold);EXPECT_EQ(banks.trace,expected);
        cue->Resume();shift+=hold;
        std::size_t event=1;
        for(const std::uint64_t markerTime:{1000u,2000u,3000u})
        {
            banks.Tick(shift+markerTime-1);EXPECT_EQ(banks.trace,expected);
            expected.emplace_back(0,++event,markerTime);
            banks.Tick(shift+markerTime);banks.Tick(shift+markerTime);
            EXPECT_EQ(banks.trace,expected);
        }
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    }
}

TEST(XactScheduler, PauseBetweenEventsFreezesMixingAndCueClock)
{
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,1)}}); auto cue=b.Play();
    ExpectMarker(manualDevice->Render(64),0.125f);
    b.SetTime(7); cue->Pause(); b.Tick(1007); ExpectSilence(manualDevice->Render(512));
    cue->Resume(); b.Tick(1019); ExpectMarker(manualDevice->Render(32),0.125f);
    b.Tick(1020); ExpectSilence(manualDevice->Render(32)); EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{0,1,20}}));
}

TEST(XactScheduler, CategoryPauseAndResumeUseTheSameClock)
{
    ScheduledBanks b({{PlayAt(20)}}); auto cue=b.Play(); auto category=b.engine->GetCategory("Default");
    b.SetTime(7); category.Pause(); b.Tick(1007); EXPECT_TRUE(b.trace.empty());
    category.Resume(); b.Tick(1019); EXPECT_TRUE(b.trace.empty());
    b.Tick(1020); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,20}}));
    ExpectMarker(manualDevice->Render(32),0.125f);
}

TEST(XactScheduler, ExplicitStopsCancelPendingEventsAndSameCueCannotReplay)
{
    for (auto stop:{AudioStopOptions::Immediate,AudioStopOptions::AsAuthored})
    {
        ScheduledBanks b({{PlayAt(20)}}); auto cue=b.Play(); b.SetTime(7); cue->Stop(stop);
        b.Tick(100); cue->Play(); b.Tick(200);
        EXPECT_TRUE(b.trace.empty()); ExpectSilence(manualDevice->Render(32));
        EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        auto fresh=b.Play(); b.Tick(219); EXPECT_TRUE(b.trace.empty()); b.Tick(220);
        EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,20}})); ExpectMarker(manualDevice->Render(32),0.125f);
    }
}

TEST(XactScheduler, DisposingCueBankOrEngineCancelsEventsBeforeFirstVoice)
{
    for (int owner:{0,1,2,3})
    {
        ScheduledBanks b({{PlayAt(20)}}); auto cue=b.Play();
        if (owner==0) cue->Dispose();
        if (owner==1) b.sound->Dispose();
        if (owner==2) b.wave->Dispose();
        if (owner==3) b.engine->Dispose();
        b.Tick(100); EXPECT_TRUE(cue->getIsDisposedProperty()); EXPECT_TRUE(b.trace.empty());
        ExpectSilence(manualDevice->Render(32));
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    }
}

TEST(XactScheduler, FireAndForgetSurvivesSilentGapThenUpdateSweepsFinalCompletion)
{
    ScheduledBanks b({{PlayAt(20),PlayAt(60,1)}}); b.sound->PlayCue("Sequence");
    auto* cue=SoundBankTestAccess::LastFireAndForgetCue(*b.sound);
    ASSERT_NE(cue,nullptr); b.Observe(*cue); b.Tick(19);
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b.sound),1u);
    b.Tick(20); ExpectMarker(manualDevice->Render(441),0.125f); b.Tick(59);
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b.sound),1u);
    b.Tick(60); ExpectMarker(manualDevice->Render(441),0.25f); ExpectSilence(manualDevice->Render(32)); b.Tick(61);
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,20},{0,1,60}}));
    EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b.sound),0u);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b.engine),0u);
}

TEST(XactScheduler, DecreasingTimesAreIndependentlyDueAndLateTicksKeepFileOrder)
{
    ScheduledBanks b({{PlayAt(30,0),PlayAt(10,1)}}); auto cue=b.Play();
    b.Tick(10); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,1,10}}));
    ExpectMarker(manualDevice->Render(441),0.25f); b.Tick(30);
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,1,10},{0,0,30}}));
    ExpectMarker(manualDevice->Render(441),0.125f);
    // A late tick scans file order among all due events; it does not sort them.
    ScheduledBanks late({{PlayAt(30,0)},{PlayAt(10,1)}}); auto lateCue=late.Play(); late.Tick(40);
    EXPECT_EQ(late.trace,(std::vector<Dispatch>{{0,0,40},{1,0,40}}));
    // Distinct tracks avoid the unresolved native same-track overlap question.
}



TEST(XactScheduler, IncompleteTrackKeepsImmediateFirstWaveWhileCompleteTrackSchedules)
{
    AuthoredEvent unknown; unknown.type=23;
    ScheduledBanks b({{PlayAt(20),unknown},{PlayAt(30,1)}}); auto cue=b.Play();
    auto* legacy=CueTestAccess::ActiveInstance(*cue,0); ASSERT_NE(legacy,nullptr);
    legacy->setVolumeProperty(1.0f); // the fallback has no event observer
    ExpectMarker(manualDevice->Render(441),0.125f); EXPECT_TRUE(b.trace.empty());
    b.Tick(29); EXPECT_TRUE(b.trace.empty()); EXPECT_TRUE(cue->getIsPlayingProperty());
    b.Tick(30); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{1,0,30}}));
    ExpectMarker(manualDevice->Render(441),0.25f);
}

TEST(XactScheduler, MissingWaveDoesNotBlockLaterValidPlayOrLeaveCueStuck)
{
    ScheduledBanks b({{PlayAt(10,99),PlayAt(30,1)}}); auto cue=b.Play();
    b.Tick(10); ExpectSilence(manualDevice->Render(32)); EXPECT_TRUE(cue->getIsPlayingProperty());
    b.Tick(30); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,10},{0,1,30}}));
    ExpectMarker(manualDevice->Render(441),0.25f); ExpectSilence(manualDevice->Render(32)); b.Tick(31); EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(XactScheduler, DelayedWavePreservesFiniteLoopBudget)
{
    // HND-XACT-05: a plain PlayWave hands raw2 to its one wave, three passes.
    ScheduledBanks b({{PlayAt(20,0,2)}}); auto cue=b.Play(); b.Tick(20);
    ExpectMarker(manualDevice->Render(3*441),0.125f);
    ExpectSilence(manualDevice->Render(32));b.Tick(21);
    EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_FALSE(cue->getIsPlayingProperty());
}

TEST(XactScheduler, EveryPlayWavePayloadFamilyUsesTheAuthoredDeadline)
{
    for (uint8_t type:{1,3,4,6})
    {
        SCOPED_TRACE(type); auto event=PlayAt(20,1); event.type=type;
        ScheduledBanks b({{event}}); auto cue=b.Play(); b.Tick(19);
        EXPECT_TRUE(b.trace.empty()); ExpectSilence(manualDevice->Render(32));
        b.Tick(20); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,20}}));
        ExpectMarker(manualDevice->Render(441),0.25f); ExpectSilence(manualDevice->Render(32)); b.Tick(21);
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactScheduler, FractionalTicksAccumulateBeforeConvertingToMilliseconds)
{
    ScheduledBanks b({{PlayAt(5)}}); auto cue=b.Play();
    for (int i=1;i<=19;++i)
    {
        b.now+=std::chrono::microseconds(250); b.engine->Update();
        EXPECT_TRUE(b.trace.empty()) << i;
    }
    b.now+=std::chrono::microseconds(250); b.engine->Update();
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,5}})); // 20*250us=5000us=5ms
    ExpectMarker(manualDevice->Render(32),0.125f);
}


TEST(XactScheduler, CueAuthoredStopWithoutTailCancelsAllTracksImmediately)
{
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,2),PlayAt(30,2)},
                      {PlayAt(0,1,255),PlayAt(40,2)}}); auto cue=b.Play();
    b.Tick(20); ExpectSilence(manualDevice->Render(32));
    EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    b.Tick(100); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{1,0,0},{0,1,20}}));
}

TEST(XactScheduler, ExplicitAuthoredStopWhilePausedCannotEnterRelease)
{
    ScheduledBanks b({{PlayAt(0,0,255),PlayAt(30,1)}},100); auto cue=b.Play();
    ExpectMarker(manualDevice->Render(32),0.125f); b.SetTime(7); cue->Pause();
    cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_FALSE(cue->getIsStoppingProperty());
    ExpectSilence(manualDevice->Render(32)); b.Tick(200); EXPECT_EQ(b.trace.size(),1u);
}

TEST(XactScheduler, MissingBankRejectsCuePreparationWhileRetainingEarlierResolvedUse)
{
    auto missing=PlayAt(10);missing.bank=99;
    for(const auto& events:{std::vector<AuthoredEvent>{missing,PlayAt(30,1)},
                            std::vector<AuthoredEvent>{PlayAt(0,0),missing}})
    {
        ScheduledBanks b({events});
        EXPECT_THROW((void)b.Play(),System::InvalidOperationException);
        EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b.sound),0u);
        EXPECT_FALSE(b.sound->getIsInUseProperty());
        EXPECT_EQ(b.wave->getIsInUseProperty(),events.front().bank!=99);
        b.sound->Dispose();EXPECT_FALSE(b.wave->getIsInUseProperty());
        EXPECT_TRUE(b.trace.empty());ExpectSilence(manualDevice->Render(64));
        b.Tick(100);EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b.engine),0u);
    }
}

TEST(XactScheduler, EmptyOrStopOnlyCompleteTrackCanTerminateWithoutAVoice)
{
    for (const auto& tracks : {AuthoredTracks{{}},AuthoredTracks{{StopAt(20,1)}}})
    {
        ScheduledBanks b(tracks); auto cue=b.Play();
        b.Tick(20); EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsPlayingProperty()); ExpectSilence(manualDevice->Render(32));
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    }
}

TEST(XactScheduler, AuthoredCueStopBeforeFirstVoiceStillHasItsReleaseDeadline)
{
    ScheduledBanks b({{PlayAt(20),PlayAt(107,1)}},100); auto cue=b.Play();
    b.SetTime(7); cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppingProperty()); // parsed sound exists, even with no active voice
    b.Tick(20); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,20}}));
    for (float sample:manualDevice->Render(32)) EXPECT_GT(sample,0.0f);
    b.Tick(107); EXPECT_TRUE(cue->getIsStoppedProperty());
    b.Tick(200); EXPECT_EQ(b.trace.size(),1u); ExpectSilence(manualDevice->Render(32));
}

TEST(XactScheduler, SeparateEnginesKeepIndependentEventClocks)
{
    ScheduledBanks a({{PlayAt(20,0,255)}}), b({{PlayAt(20,1,255)}});
    auto first=a.Play(); auto second=b.Play(); a.Tick(20);
    EXPECT_EQ(a.trace,(std::vector<Dispatch>{{0,0,20}})); EXPECT_TRUE(b.trace.empty());
    ExpectMarker(manualDevice->Render(32),0.125f);
    b.Tick(19); EXPECT_TRUE(b.trace.empty()); b.Tick(20);
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,20}}));
    ExpectMarker(manualDevice->Render(32),0.375f); // independent 1/8 + 1/4 voices
}

TEST(XactScheduler, NonzeroRandomOffsetPreservesAuthoredTimestamp)
{
    // Oracle: FACT_internal.c:728-729 initializes event time from timestamp;
    // randomOffset is retained by the parser but not applied by FAudio.
    for (uint16_t time : {uint16_t(20),uint16_t(65535)})
    {
        ScheduledBanks b({{PlayAt(time,0,0,65535)}}); auto cue=b.Play();
        b.Tick(time-1); EXPECT_TRUE(b.trace.empty()); ExpectSilence(manualDevice->Render(32));
        b.Tick(time); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,time}}));
        ExpectMarker(manualDevice->Render(441),0.125f); ExpectSilence(manualDevice->Render(32)); b.Tick(time+1);
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}
