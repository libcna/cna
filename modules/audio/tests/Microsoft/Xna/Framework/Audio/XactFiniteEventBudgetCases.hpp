// SPDX-License-Identifier: MS-PL
// Native Prepare/Play/DoWork probe: finite raw0,1,2,80,254 activates1,1,2,80,254 waves.
// Wave completion and PCM here come from both real mixers under explicit engine updates.
// Track/effect-family controls and ordered selection follow FACT_internal.c:201-206,739-783.

TEST(XactFiniteEventBudget, EveryPlayWaveFamilyConsumesTheFirstFiniteActivation)
{
    for(const int family:{1,3,4,6})for(const uint8_t raw:{0,1,2,80,254})
    {
        SCOPED_TRACE(family);
        SCOPED_TRACE(raw);
        const auto event=family==1?PlayAt(0,0,raw):family==4?WrapEffectAt(raw,0,0x1000)
            :VariedAt(0,raw,XsbTrackVariationType::Ordered,family==6?0x1000:0,true,0);
        const AuthoredTracks tracks{{event}};
        ScheduledBanks banks(tracks,0,false,{},false,VariationSound(tracks));
        std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));unsigned draws=0;
        CueTestAccess::SetVariationRandom(*cue,{
            [](uint32_t) {ADD_FAILURE()<<"ordered selection requires no integer draw";return 0u;},
            [&] {++draws;return 0.0f;}});
        cue->Play();const unsigned waves=std::max<unsigned>(raw,1);
        ASSERT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),waves-1);
        banks.Tick(1);ASSERT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),waves-1);
        for(unsigned activation=0;activation<waves;++activation)
        {
            SCOPED_TRACE(activation);
            const unsigned candidate=(family==3 || family==6)?activation%3:0;
            ExpectEventVoice(*cue,0,0,ValueBaseGain,1);
            ExpectMarker(manualDevice->Render(441),(candidate+1)*.125f*ValueBaseGain);
            ExpectSilence(manualDevice->Render(32));
            if(::testing::Test::HasFailure())return;
            banks.Tick((activation+1)*10);
            EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),activation+2<waves?waves-activation-2:0);
            EXPECT_EQ(cue->getIsStoppedProperty(),activation+1==waves);
        }
        EXPECT_EQ(draws,(family==4 || family==6)?waves:0);
        banks.Tick(10000);EXPECT_TRUE(cue->getIsStoppedProperty());
        ExpectSilence(manualDevice->Render(441));
    }
}

TEST(XactFiniteEventBudget, PlainFiniteWavesReplayPrefixRegionAndSuffixForEveryCodecAndStorage)
{
    EnsureMixer();SoundEffect::setMasterVolumeProperty(1);
    for(const int codec:{0,1,2})for(const bool streamed:{false,true})
        for(const bool region:{false,true})for(const bool throughEnd:{false,true})
        {
            SCOPED_TRACE(codec);
            SCOPED_TRACE(streamed);
            SCOPED_TRACE(region);
            SCOPED_TRACE(throughEnd);
            Banks banks(2,codec,region,streamed,throughEnd);auto cue=banks.Play();
            for(unsigned activation=0;activation<2;++activation)
            {
                const auto pcm=manualDevice->Render(192);ASSERT_EQ(pcm.size(),384u);
                for(unsigned frame=0;frame<192;++frame)
                    for(unsigned channel=0;channel<2;++channel)
                        ASSERT_NEAR(pcm[frame*2+channel],Marker(frame),1e-6f)<<activation<<":"<<frame;
                ExpectSilence(manualDevice->Render(32));if(::testing::Test::HasFailure())return;
                banks.engine->Update();
                if(activation==0)
                {
                    auto* voice=CueTestAccess::EventVoice(*cue,0,0);ASSERT_NE(voice,nullptr);
                    voice->setVolumeProperty(1);
                }
            }
            EXPECT_TRUE(cue->getIsStoppedProperty());ExpectSilence(manualDevice->Render(192));
        }
}
