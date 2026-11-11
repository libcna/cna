// SPDX-License-Identifier: MS-PL
// HND-XACT-06: every case replays an SDK-authored loop-extensions/<shape>/raw<n>/shapes.xsb, one
// authored field away from an HND-XACT-05 bank, whose genuine XACT3.6 x86/x64 lifecycle is
// recorded in loop-extensions/ORACLE.json; check-xact-loop-extensions-oracle.py checks the native
// cells independently. The waves are finite-cardinality's one-second 22050 Hz PCM16 constants
// (A0 1024, A1 3072 /32768); region/A.xwb loops samples [5512, 16538) of both A waves.

Bytes LoopExtensionFile(const std::string& relative)
{
    const auto path=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/
        "loop-extensions"/relative;
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Missing native loop-extension input: "+path.string());
    return Bytes(std::istreambuf_iterator<char>(in),{});
}

std::unique_ptr<ScheduledBanks> LoopExtensionBanks(const std::string& variant,bool region=false)
{
    // The variant's own SDK sound bank with base/raw3's global settings and wave bank (or the
    // region wave bank); the authored copies differ from them only in build timestamps.
    return std::make_unique<ScheduledBanks>(AuthoredTracks{},0,false,CardinalityFile("shared/shapes.xgs"),
        false,LoopExtensionFile(variant+"/shapes.xsb"),
        region?LoopExtensionFile("region/A.xwb"):CardinalityFile("shared/A.xwb"),false,false);
}

class HndXactLoopExtension : public NativeRateXact {};

constexpr unsigned RegionPrefixFrames=5512,RegionFrames=11026,
    RegionSuffixFrames=CardinalityWaveFrames-RegionPrefixFrames-RegionFrames;

// The SDK Pitch Variation is -100..100 cents; a unit draw of .5 is int16(.5*200)-100 = 0 cents
// (FACT_internal.c:309-334), so every pass is exactly one wave of frames. Ordered selection makes
// no integer draw; other selections take the scripted rolls.
void ScriptLoopExtensionDraws(AudioEngine& engine,std::vector<uint32_t> rolls,unsigned& units)
{
    AudioEngineTestAccess::SetPreparationVariationRandom(engine,{
        [rolls=std::move(rolls),index=std::size_t{}](uint32_t bound) mutable {
            EXPECT_LT(index,rolls.size())<<"unexpected candidate draw";
            const auto roll=index<rolls.size()?rolls[index++]:0u;EXPECT_LT(roll,bound);return roll;},
        [&units] {++units;return .5f;}});
}

// One native WAVEPLAY..WAVESTOP per entry of `order`, each a complete wave of its candidate.
void ExpectSeparateWaves(ScheduledBanks& b,Cue& cue,const std::vector<int>& order)
{
    for(std::size_t i=0;i<order.size();++i)
    {
        SCOPED_TRACE(i);
        ExpectCardinalityActivation(cue,order[i]?CardinalityA1:CardinalityA0,CardinalityWaveFrames);
        if(::testing::Test::HasFailure())return;
        if(i+1<order.size()){b.Tick(1000+1000*static_cast<int64_t>(i));EXPECT_TRUE(cue.getIsPlayingProperty());}
    }
    ExpectCardinalityEnd(b,cue,1000*static_cast<int64_t>(order.size()));
}

std::vector<int> OrderedWaves(unsigned raw)
{
    std::vector<int> order;for(unsigned i=0;i<=raw;++i)order.push_back(static_cast<int>(i%2));return order;
}

TEST_F(HndXactLoopExtension, EffectFamiliesWithoutAnyNewOnLoopHoldOneWave)
{
    // type4 (PlayWave + Pitch Variation, no NEW_ON_LOOP) raw0..4: native one PREPARED/PLAY/
    // STOP/DESTROY of A0 held n+1 passes. FAudio instead plays max(n,1) separate waves.
    for(unsigned raw=0;raw<=4;++raw)
    {
        SCOPED_TRACE(raw);
        auto b=LoopExtensionBanks("type4/raw"+std::to_string(raw));
        unsigned units=0;ScriptLoopExtensionDraws(*b->engine,{},units);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectCardinalityActivation(*cue,CardinalityA0,CardinalityWaveFrames*(raw+1));
        if(::testing::Test::HasFailure())return;
        ExpectCardinalityEnd(*b,*cue,1000);
        EXPECT_EQ(units,1u); // one wave, one pitch draw
    }
}

TEST_F(HndXactLoopExtension, EffectNewOnLoopAlonePlaysNPlusOnePassesOfTheFirstCandidate)
{
    // type4nol (pitch NEW_ON_LOOP) and type6nol0 (pitch NEW_ON_LOOP, track variation without
    // it) raw0..4: native one A0 wave held n+1 passes whose pitch varies over the passes
    // (HND-XACT-06 pitch cells). CNA renders each pass as a complete A0 wave with a new pitch
    // draw: the measured count and candidate, not the native single lifecycle.
    for(const char* shape:{"type4nol","type6nol0"})for(unsigned raw=0;raw<=4;++raw)
    {
        SCOPED_TRACE(shape);
        SCOPED_TRACE(raw);
        auto b=LoopExtensionBanks(std::string(shape)+"/raw"+std::to_string(raw));
        unsigned units=0;ScriptLoopExtensionDraws(*b->engine,{},units);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectSeparateWaves(*b,*cue,std::vector<int>(raw+1,0));
        if(::testing::Test::HasFailure())return;
        EXPECT_EQ(units,raw+1);
    }
}

TEST_F(HndXactLoopExtension, TrackNewOnLoopEffectVariationPlaysNPlusOneCompleteWaves)
{
    // type6 (ordered track NEW_ON_LOOP plus a Pitch Variation) raw0..4: native n+1 waves
    // A0,A1,A0,..., each one pass, exactly as the type3 base shape.
    for(unsigned raw=0;raw<=4;++raw)
    {
        SCOPED_TRACE(raw);
        auto b=LoopExtensionBanks("type6/raw"+std::to_string(raw));
        unsigned units=0;ScriptLoopExtensionDraws(*b->engine,{},units);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectSeparateWaves(*b,*cue,OrderedWaves(raw));
        if(::testing::Test::HasFailure())return;
        EXPECT_EQ(units,raw+1); // each wave draws its pitch
    }
}

TEST_F(HndXactLoopExtension, HeldWaveRepeatsItsLoopRegionWhileNewOnLoopWavesPlayItOnce)
{
    // type1region raw n: native one wave held for prefix + (n+1) x region + suffix (1 + n/2
    // passes; XACT_TRACK_PROPERTIES duration 1000 + 500n ms). baseregion raw n: n+1 complete
    // one-pass waves whose region is not repeated.
    for(unsigned raw=0;raw<=4;++raw)
    {
        SCOPED_TRACE(raw);
        auto b=LoopExtensionBanks("type1region/raw"+std::to_string(raw),true);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectCardinalityActivation(*cue,CardinalityA0,RegionPrefixFrames+RegionFrames*(raw+1)+RegionSuffixFrames);
        if(::testing::Test::HasFailure())return;
        ExpectCardinalityEnd(*b,*cue,1000);
    }
    for(unsigned raw=0;raw<=4;++raw)
    {
        SCOPED_TRACE(raw);
        auto b=LoopExtensionBanks("baseregion/raw"+std::to_string(raw),true);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectSeparateWaves(*b,*cue,OrderedWaves(raw));
        if(::testing::Test::HasFailure())return;
    }
}

TEST_F(HndXactLoopExtension, RandomAndShuffleFollowNewOnLoopLikeOrderedVariations)
{
    // Two weight-255 candidates (total 510; a roll >= 255 picks A1, PickXactCandidate). Native
    // with NEW_ON_LOOP: n+1 one-pass waves, random repeating candidates (x86 random/raw3 0,1,1,0),
    // shuffle alternating; without it one wave of the drawn candidate held n+1 passes (x86
    // randomnol0/raw1 held A1). The rolls reproduce those draws; they claim no native RNG.
    struct Case { const char* variant; std::vector<uint32_t> rolls; std::vector<int> order; };
    for(const Case& c:{Case{"random/raw1",{0,0,0},{0,0}},Case{"random/raw3",{0,0,300,300,0},{0,1,1,0}},
                       Case{"shuffle/raw1",{300,0,0},{0,1}},Case{"shuffle/raw3",{300,0,0,0,0},{0,1,0,1}}})
    {
        SCOPED_TRACE(c.variant);
        auto b=LoopExtensionBanks(c.variant);unsigned units=0;ScriptLoopExtensionDraws(*b->engine,c.rolls,units);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectSeparateWaves(*b,*cue,c.order);
        if(::testing::Test::HasFailure())return;
    }
    for(const char* shape:{"randomnol0","shufflenol0"})for(unsigned raw:{1u,3u})
    {
        SCOPED_TRACE(shape);
        SCOPED_TRACE(raw);
        const bool random=shape[0]=='r';
        auto b=LoopExtensionBanks(std::string(shape)+"/raw"+std::to_string(raw));unsigned units=0;
        // Initial draw then the first advance; shuffle excludes the initial candidate.
        ScriptLoopExtensionDraws(*b->engine,random?std::vector<uint32_t>{0,300}:std::vector<uint32_t>{300,0},units);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectCardinalityActivation(*cue,random?CardinalityA1:CardinalityA0,CardinalityWaveFrames*(raw+1));
        if(::testing::Test::HasFailure())return;
        ExpectCardinalityEnd(*b,*cue,1000);
    }
}

TEST_F(HndXactLoopExtension, LongFiniteCountsPlayNPlusOnePasses)
{
    // base and nol0 raw5, raw100 and raw254: native n+1 one-pass waves (6, 101, 255) with
    // NEW_ON_LOOP, otherwise one A0 wave held 6, 101 and 255 passes.
    for(unsigned raw:{5u,100u,254u})
    {
        SCOPED_TRACE(raw);
        {
            auto b=LoopExtensionBanks("base/raw"+std::to_string(raw));
            auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
            ExpectSeparateWaves(*b,*cue,OrderedWaves(raw));
            if(::testing::Test::HasFailure())return;
        }
        auto b=LoopExtensionBanks("nol0/raw"+std::to_string(raw));
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();
        ExpectCardinalityActivation(*cue,CardinalityA0,CardinalityWaveFrames*(raw+1));
        if(::testing::Test::HasFailure())return;
        ExpectCardinalityEnd(*b,*cue,1000);
    }
}
