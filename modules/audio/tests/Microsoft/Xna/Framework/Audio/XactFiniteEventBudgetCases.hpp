// SPDX-License-Identifier: MS-PL
// Microsoft XACT3.6 (HND-XACT-05 finite-cardinality/ and HND-XACT-06 loop-extensions/
// ORACLE.json): every play-wave family plays raw n as n+1 passes -- separate complete waves with
// track NEW_ON_LOOP, otherwise one wave looping n times. An effect NEW_ON_LOOP alone natively
// holds one wave whose effect varies over its passes; CNA renders those passes as complete waves
// of the first candidate. FAudio's budget is max(n,1) waves for every family.
// Wave completion and PCM here come from both real mixers under explicit engine updates.
// Track/effect-family controls and ordered selection follow FACT_internal.c:201-206,739-783.

TEST(XactFiniteEventBudget, MeasuredFamiliesPlayNPlusOnePasses)
{
    for(const int family:{1,3})for(const uint8_t raw:{0,1,2,80,254})
    {
        SCOPED_TRACE(family);
        SCOPED_TRACE(raw);
        const auto event=family==1?PlayAt(0,0,raw):VariedAt(0,raw,XsbTrackVariationType::Ordered,0,true,0);
        const AuthoredTracks tracks{{event}};
        ScheduledBanks banks(tracks,0,false,{},false,VariationSound(tracks));
        AudioEngineTestAccess::SetPreparationVariationRandom(*banks.engine,{
            [](uint32_t) {ADD_FAILURE()<<"ordered selection requires no integer draw";return 0u;},
            [] {ADD_FAILURE()<<"no effect axis is authored";return 0.0f;}});
        std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));cue->Play();
        const unsigned passes=raw+1u,waves=family==3?passes:1;
        ASSERT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),waves-1);
        banks.Tick(1);ASSERT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),waves-1);
        for(unsigned activation=0;activation<waves;++activation)
        {
            SCOPED_TRACE(activation);
            ExpectEventVoice(*cue,0,0,ValueBaseGain,1);
            // A plain PlayWave's one wave holds all passes of candidate0.
            ExpectMarker(manualDevice->Render(441*(family==3?1:passes)),(activation%3+1)*.125f*ValueBaseGain);
            ExpectSilence(manualDevice->Render(32));
            if(::testing::Test::HasFailure())return;
            banks.Tick((activation+1)*10);
            EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),activation+2<waves?waves-activation-2:0);
            EXPECT_EQ(cue->getIsStoppedProperty(),activation+1==waves);
        }
        banks.Tick(10000);EXPECT_TRUE(cue->getIsStoppedProperty());
        ExpectSilence(manualDevice->Render(441));
    }
}

TEST(XactFiniteEventBudget, EffectFamiliesPlayNPlusOnePassesSeparatedOnlyByNewOnLoop)
{
    // HND-XACT-06 type4/type4nol/type6/type6nol0: one held wave without any NEW_ON_LOOP; n+1
    // waves of the next candidate with track NEW_ON_LOOP; n+1 waves of the first candidate for
    // an effect NEW_ON_LOOP alone (natively one held wave whose pitch varies per pass).
    struct Case { int family; uint16_t flags; bool trackNewOnLoop; };
    for(const Case c:{Case{4,0x1000,false},Case{4,0x1100,false},Case{6,0x1000,true},Case{6,0x1100,false}})
        for(const uint8_t raw:{0,1,2,80,254})
    {
        SCOPED_TRACE(c.family);
        SCOPED_TRACE(c.flags);
        SCOPED_TRACE(raw);
        const auto event=c.family==4?WrapEffectAt(raw,0,c.flags)
            :VariedAt(0,raw,XsbTrackVariationType::Ordered,c.flags,c.trackNewOnLoop,0);
        const AuthoredTracks tracks{{event}};
        ScheduledBanks banks(tracks,0,false,{},false,VariationSound(tracks));
        unsigned draws=0;
        AudioEngineTestAccess::SetPreparationVariationRandom(*banks.engine,{
            [](uint32_t) {ADD_FAILURE()<<"ordered selection requires no integer draw";return 0u;},
            [&] {++draws;return 0.0f;}});
        std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));cue->Play();
        const unsigned passes=raw+1u,waves=c.trackNewOnLoop||(c.flags&0x0100)?passes:1;
        ASSERT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),waves-1);
        banks.Tick(1);ASSERT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),waves-1);
        for(unsigned activation=0;activation<waves;++activation)
        {
            SCOPED_TRACE(activation);
            const unsigned candidate=c.trackNewOnLoop?activation%3:0;
            ExpectEventVoice(*cue,0,0,ValueBaseGain,1);
            ExpectMarker(manualDevice->Render(441*(waves==1?passes:1)),(candidate+1)*.125f*ValueBaseGain);
            ExpectSilence(manualDevice->Render(32));
            if(::testing::Test::HasFailure())return;
            banks.Tick((activation+1)*10);
            EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),activation+2<waves?waves-activation-2:0);
            EXPECT_EQ(cue->getIsStoppedProperty(),activation+1==waves);
        }
        EXPECT_EQ(draws,waves); // one pitch draw per wave
        banks.Tick(10000);EXPECT_TRUE(cue->getIsStoppedProperty());
        ExpectSilence(manualDevice->Render(441));
    }
}

TEST(XactFiniteEventBudget, NewOnLoopFiniteWavesReplayPrefixRegionAndSuffixForEveryCodecAndStorage)
{
    // NEW_ON_LOOP raw2: three complete activations, each with wave loop0 (HND-XACT-05).
    EnsureMixer();SoundEffect::setMasterVolumeProperty(1);
    for(const int codec:{0,1,2})for(const bool streamed:{false,true})
        for(const bool region:{false,true})for(const bool throughEnd:{false,true})
        {
            SCOPED_TRACE(codec);
            SCOPED_TRACE(streamed);
            SCOPED_TRACE(region);
            SCOPED_TRACE(throughEnd);
            Banks banks(2,codec,region,streamed,throughEnd,true);auto cue=banks.Play();
            for(unsigned activation=0;activation<3;++activation)
            {
                const auto pcm=manualDevice->Render(192);ASSERT_EQ(pcm.size(),384u);
                for(unsigned frame=0;frame<192;++frame)
                    for(unsigned channel=0;channel<2;++channel)
                        ASSERT_NEAR(pcm[frame*2+channel],Marker(frame),1e-6f)<<activation<<":"<<frame;
                ExpectSilence(manualDevice->Render(32));if(::testing::Test::HasFailure())return;
                banks.engine->Update();
                if(activation+1<3)
                {
                    auto* voice=CueTestAccess::EventVoice(*cue,0,0);ASSERT_NE(voice,nullptr);
                    voice->setVolumeProperty(1);
                }
            }
            EXPECT_TRUE(cue->getIsStoppedProperty());ExpectSilence(manualDevice->Render(192));
        }
}
