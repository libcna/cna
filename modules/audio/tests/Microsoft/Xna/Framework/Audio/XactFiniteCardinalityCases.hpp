// SPDX-License-Identifier: MS-PL
// HND-XACT-05: native-v6-{cardinality,effect}-{x86,x64}-<shape>-raw<n>-<repeat>-cell<k>.
// Each case replays the SDK-authored finite-cardinality/<shape>/raw<n>/shapes.xsb whose
// genuine XACT3.6 lifecycle is recorded in finite-cardinality/ORACLE.json; the native
// cells are checked independently by check-xact-finite-cardinality-oracle.py. Waves are
// the shared one-second 22050 Hz PCM16 constants (A0 1024, A1 3072 /32768).

Bytes CardinalityFile(const std::string& relative)
{
    const auto path=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/
        "finite-cardinality"/relative;
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Missing native cardinality input: "+path.string());
    return Bytes(std::istreambuf_iterator<char>(in),{});
}

std::unique_ptr<ScheduledBanks> CardinalityBanks(const std::string& shape,unsigned raw)
{
    // The variant's own SDK sound bank with the shared global settings and wave bank;
    // the authored copies differ from these only in their build timestamps (PROVENANCE).
    return std::make_unique<ScheduledBanks>(AuthoredTracks{},0,false,CardinalityFile("shared/shapes.xgs"),
        false,CardinalityFile(shape+"/raw"+std::to_string(raw)+"/shapes.xsb"),
        CardinalityFile("shared/A.xwb"),false,false);
}

class HndXactCardinality : public NativeRateXact {};

constexpr unsigned CardinalityWaveFrames=22050;
constexpr float CardinalityA0=1024.f/32768,CardinalityA1=3072.f/32768;

// One native WAVEPLAY..WAVESTOP: a single voice renders `frames` frames of `sample` and then
// nothing; a successor needs the engine update that observes this completion.
void ExpectCardinalityActivation(Cue& cue,float sample,std::size_t frames)
{
    auto* voice=CueTestAccess::ActiveInstance(cue,0);ASSERT_NE(voice,nullptr);
    EXPECT_EQ(CueTestAccess::ActiveInstance(cue,1),nullptr); // never two overlapping waves
    EXPECT_EQ(voice->getStateProperty(),SoundState::Playing);
    // Gain normalized to1 to identify the candidate; not a native gain measurement.
    voice->setVolumeProperty(1.f);ExpectMarker(manualDevice->Render(frames),sample);
    ExpectSilence(manualDevice->Render(64));
}

void ExpectCardinalityEnd(ScheduledBanks& b,Cue& cue,int64_t at)
{
    b.Tick(at);
    EXPECT_EQ(CueTestAccess::ActiveInstance(cue,0),nullptr);
    EXPECT_TRUE(cue.getIsStoppedProperty());EXPECT_FALSE(cue.getIsPlayingProperty());
    EXPECT_FLOAT_EQ(NumInstances(cue),0);
    b.Tick(at+5000);ExpectSilence(manualDevice->Render(CardinalityWaveFrames));
}

TEST_F(HndXactCardinality, NewOnLoopTrackVariationPlaysNPlusOneCompleteWavesForEitherFlagsByte)
{
    // base (flags0x0c) and flags0 (0x00), ordered type3 NEW_ON_LOOP raw0..4: native
    // PREPARED/PLAY/STOP/DESTROY each n+1, indices0,1,0,1,0 in A; every wave one pass.
    for(const char* shape:{"base","flags0"})for(unsigned raw=0;raw<=4;++raw)
    {
        SCOPED_TRACE(shape);
        SCOPED_TRACE(raw);
        auto b=CardinalityBanks(shape,raw);auto cue=CountCue(*b->sound,"ShapeCue");
        b->Tick(300);cue->Play();
        for(unsigned i=0;i<=raw;++i)
        {
            SCOPED_TRACE(i);
            ExpectCardinalityActivation(*cue,i%2?CardinalityA1:CardinalityA0,CardinalityWaveFrames);
            if(::testing::Test::HasFailure())return;
            if(i<raw){b->Tick(1000+1000*i);EXPECT_TRUE(cue->getIsPlayingProperty());}
        }
        ExpectCardinalityEnd(*b,*cue,1000+1000*raw);
    }
}

TEST_F(HndXactCardinality, WithoutNewOnLoopOneWaveLoopsTheWholeWaveNTimes)
{
    // nol0 (ordered type3, NEW_ON_LOOP off) and type1 (PlayWave A0) raw0..4: native one
    // PREPARED/PLAY/STOP/DESTROY of A0 held n+1 wave lengths; no second candidate.
    for(const char* shape:{"nol0","type1"})for(unsigned raw=0;raw<=4;++raw)
    {
        SCOPED_TRACE(shape);
        SCOPED_TRACE(raw);
        auto b=CardinalityBanks(shape,raw);auto cue=CountCue(*b->sound,"ShapeCue");
        b->Tick(300);cue->Play();
        ExpectCardinalityActivation(*cue,CardinalityA0,CardinalityWaveFrames*(raw+1));
        if(::testing::Test::HasFailure())return;
        ExpectCardinalityEnd(*b,*cue,1000);
    }
}
