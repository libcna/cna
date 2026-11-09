// SPDX-License-Identifier: MS-PL
// HND-XACT-02: native-v3-prefetch-{x86,x64}-<profile>-<mode>-<ahead>-io<0|1>-<repeat>.
// Exact probe/DLL/fixture identities: variation-prefetch/PROVENANCE.json. All88
// native cells are checked independently by check-xact-prefetch-oracle.py.

Bytes PrefetchFile(const char* profile,const char* file)
{
    const auto path=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/
        "variation-prefetch"/profile/file;
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Missing native prefetch input: "+path.string());
    return Bytes(std::istreambuf_iterator<char>(in),{});
}

std::unique_ptr<ScheduledBanks> PrefetchBanks(const char* profile)
{
    return std::make_unique<ScheduledBanks>(AuthoredTracks{},0,false,PrefetchFile(profile,"shapes.xgs"),
        false,PrefetchFile(profile,"shapes.xsb"),PrefetchFile(profile,"A.xwb"),
        std::string(profile)=="ordered-streaming",false);
}

std::unique_ptr<WaveBank> PrefetchB(ScheduledBanks& b,const char* profile)
{
    const auto path=CNA::TestSupport::FixtureRoot()/"hnd-xact-prefetch-B.xwb";
    const auto bytes=PrefetchFile(profile,"B.xwb");
    {std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
     if(!out)throw std::runtime_error("Cannot materialize prefetch B");}
    return std::string(profile)=="ordered-streaming"?std::make_unique<WaveBank>(b.engine.get(),path.string(),0,0)
        :std::make_unique<WaveBank>(b.engine.get(),path.string());
}

// The captured banks hold 22050 Hz mono PCM. At 44100 Hz SDL3_mixer resamples them with a
// windowed sinc whose output is not DC-exact (0.0350 for the 1/32 marker at frame 1), while
// CnaMixer's linear interpolation reproduces a constant. Rendering at the captures' own rate
// keeps ExpectMarker's values source samples in every mixer. The harness opens one device per
// process, so the rate is chosen before that open: ctest runs these suites as their own process
// (CnaXactFiniteLoop*NativeRateTests, cmake/UnitTests.cmake).
class NativeRateXact : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (factoryCalls == 0) manualDeviceRate = 22050;
        ASSERT_EQ(manualDeviceRate, 22050u)
            << "the device is already open at another rate; run HndXactPrefetch.* and "
               "HndXactCrossBank.* in their own process";
    }
};
class HndXactPrefetch : public NativeRateXact {};

void PrefetchCurrentWave(Cue& cue,float sample)
{
    auto* voice=CueTestAccess::ActiveInstance(cue,0);ASSERT_NE(voice,nullptr);
    EXPECT_EQ(voice->getStateProperty(),SoundState::Playing);
    EXPECT_EQ(CueTestAccess::ActiveInstance(cue,1),nullptr); // prefetch is not overlapping playback
    // Original own PCM16 constants /32768, with gain normalized to1 to identify
    // candidates. This is source arithmetic, not a native gain/PCM measurement.
    voice->setVolumeProperty(1.f);ExpectMarker(manualDevice->Render(128),sample);
}

TEST_F(HndXactPrefetch, SuccessorOwnsItsBankBeforeCurrentWaveEndsWithoutStartingAnotherVoice)
{
    // ordered/ordered-streaming normal50/250/500 repeats0..2, no-props, io1:
    // prepared_300 A132/B4; play_200 A132/B132; next PREPARED before first STOP.
    for(const char* profile:{"ordered","ordered-streaming"})
    {
        SCOPED_TRACE(profile);auto b=PrefetchBanks(profile);auto other=PrefetchB(*b,profile);
        auto cue=CountCue(*b->sound,"ShapeCue");
        EXPECT_TRUE(cue->getIsPreparedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
        EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);b->Tick(300);
        EXPECT_FALSE(other->getIsInUseProperty());ExpectSilence(manualDevice->Render(64));
        cue->Play();b->Tick(320);EXPECT_TRUE(other->getIsInUseProperty());
        EXPECT_FLOAT_EQ(NumInstances(*cue),1);PrefetchCurrentWave(*cue,1024.f/32768);
        // Both native owners persist after cue destruction, even though only A played.
        cue.reset();EXPECT_FALSE(b->sound->getIsInUseProperty());
        EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_TRUE(other->getIsInUseProperty());
        ExpectSilence(manualDevice->Render(128));b->sound->Dispose();
        EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}

TEST_F(HndXactPrefetch, PromotionConsumesThePreparedCandidateAndPreservesTheMeasuredPrefix)
{
    // samebank/ordered/ordered-streaming normal and lateB: native actual PLAY
    // indices0,1,0,1 in A, or bankA,B,A,B/index0. Test prefix0..2 only: raw3's
    // native4 versus previous FAudio3 budget needs the controls in OWNER_QUESTIONS.
    for(const char* profile:{"samebank","ordered","ordered-streaming"})
    {
        SCOPED_TRACE(profile);auto b=PrefetchBanks(profile);auto other=PrefetchB(*b,profile);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        const bool same=std::string(profile)=="samebank";
        for(unsigned i=0;i<3;++i)
        {
            PrefetchCurrentWave(*cue,(i%2?(same?3072.f:2048.f):1024.f)/32768);
            if(i<2){(void)manualDevice->Render(50000);b->Tick(1500+i*1200);}
        }
        EXPECT_EQ(other->getIsInUseProperty(),!same);cue->Stop(AudioStopOptions::Immediate);
        EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
    }
}

TEST_F(HndXactPrefetch, MissingSuccessorDoesNotRefusePlayAndIsNotRetriedAfterItsBankLoads)
{
    // ordered/ordered-streaming missingB and lateB-playing: Prepare0/Play0,
    // exactly one A0 PREPARE/PLAY/STOP/DESTROY; B loaded100ms after Play stays4.
    for(const char* profile:{"ordered","ordered-streaming"})for(bool late:{false,true})
    {
        SCOPED_TRACE(profile);
        SCOPED_TRACE(late);auto b=PrefetchBanks(profile);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);EXPECT_NO_THROW(cue->Play());b->Tick(400);
        auto other=late?PrefetchB(*b,profile):std::unique_ptr<WaveBank>{};
        if(other)EXPECT_FALSE(other->getIsInUseProperty());
        PrefetchCurrentWave(*cue,1024.f/32768);(void)manualDevice->Render(50000);b->Tick(1500);
        EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_FALSE(cue->getIsPlayingProperty());
        EXPECT_FLOAT_EQ(NumInstances(*cue),0);EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        b->Tick(4800);ExpectSilence(manualDevice->Render(256));
        if(other)EXPECT_FALSE(other->getIsInUseProperty());
        EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_TRUE(b->sound->getIsInUseProperty());
        cue.reset();EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
    }
}

TEST_F(HndXactPrefetch, BankLoadedAfterPrepareBeforePlayIsAvailableToTheSuccessor)
{
    // ordered/ordered-streaming lateB: preparation does not require B, loading it
    // before Play permits successor preparation and actual B playback.
    for(const char* profile:{"ordered","ordered-streaming"})
    {
        SCOPED_TRACE(profile);auto b=PrefetchBanks(profile);auto cue=CountCue(*b->sound,"ShapeCue");
        b->Tick(300);auto other=PrefetchB(*b,profile);EXPECT_FALSE(other->getIsInUseProperty());
        cue->Play();b->Tick(320);EXPECT_TRUE(other->getIsInUseProperty());
        PrefetchCurrentWave(*cue,1024.f/32768);(void)manualDevice->Render(50000);b->Tick(1500);
        PrefetchCurrentWave(*cue,2048.f/32768);
    }
}

TEST_F(HndXactPrefetch, SameBankDoesNotRequireAnUnselectedBankEvenWhenLoadedAfterPlay)
{
    // samebank missingB/lateB-playing: A0,A1 still play; irrelevant B stays unused4.
    for(bool late:{false,true})
    {
        auto b=PrefetchBanks("samebank");auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        b->Tick(400);auto other=late?PrefetchB(*b,"samebank"):std::unique_ptr<WaveBank>{};
        PrefetchCurrentWave(*cue,1024.f/32768);(void)manualDevice->Render(50000);b->Tick(1500);
        PrefetchCurrentWave(*cue,3072.f/32768);if(other)EXPECT_FALSE(other->getIsInUseProperty());
    }
}
