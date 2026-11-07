// SPDX-License-Identifier: MS-PL
// Included after scheduler/value cases by the existing two-adapter offline harness.
// Marker PCM includes ValueBaseGain: authored byte180 is slightly above unity.
// Native fallback: FACT_internal.c:177-453, FACT.c:539-569. Refresh managed
// Cue.Play IL_0000-0021 delegates; it does not contain variation algorithms.

#include "XactVariationBankBytes.hpp"

void InjectVariation(Cue& cue, std::vector<uint32_t> rolls,
                     std::vector<uint32_t>& bounds, std::vector<float> units = {})
{
    CueTestAccess::SetVariationRandom(cue,{
        [rolls=std::move(rolls),index=std::size_t{},&bounds](uint32_t bound) mutable {
            bounds.push_back(bound); EXPECT_LT(index,rolls.size());
            auto roll=index<rolls.size() ? rolls[index++] : 0; EXPECT_LT(roll,bound); return roll;
        },
        [units=std::move(units),index=std::size_t{}]() mutable {
            EXPECT_LT(index,units.size()); return index<units.size() ? units[index++] : 0.0f;
        }
    });
}

void ExpectEventVoice(Cue& cue, std::size_t track, std::size_t event, float gain, float ratio)
{
    auto* instance=CueTestAccess::EventVoice(cue,track,event); ASSERT_NE(instance,nullptr);
    const auto receipt=voiceReceipts.find(SoundEffectInstanceTestAccess::MixerHandle(*instance));
    ASSERT_NE(receipt,voiceReceipts.end());
    EXPECT_NEAR(receipt->second.gain,gain,1e-5f); EXPECT_NEAR(receipt->second.ratio,ratio,1e-5f);
}

TEST(XactLoopVariation, OrderedCandidatesReachBothRealMixersForEveryFiniteIteration)
{
    // FACT initial-1 then cyclic+1; loopN means N additional activations.
    for (const uint8_t loops : {uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(254)})
    {
        AuthoredTracks tracks{{VariedAt(0,loops,XsbTrackVariationType::Ordered)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks));
        auto cue=b.Play();
        for (unsigned iteration=0;iteration<=loops;++iteration)
        {
            ExpectEventVoice(*cue,0,0,ValueBaseGain,1);
            ExpectMarker(manualDevice->Render(441),(float(iteration%3+1)*.125f)*ValueBaseGain);
            ExpectSilence(manualDevice->Render(32));
            if (iteration<loops) EXPECT_TRUE(cue->getIsPlayingProperty()); // getter must not erase loop state
            b.Tick((iteration+1)*10);
        }
        EXPECT_TRUE(cue->getIsStoppedProperty()); ExpectSilence(manualDevice->Render(882));
    }
}

TEST(XactLoopVariation, RandomAlgorithmsDeliverLiteralWaveSequencesAndBounds)
{
    // Independent bucket arithmetic from FACT: initial+advance, then one advance per loop.
    for (auto type : {XsbTrackVariationType::OrderedFromRandom,XsbTrackVariationType::Random,
                      XsbTrackVariationType::RandomNoRepeats,XsbTrackVariationType::Shuffle})
    {
        const bool ordered=type==XsbTrackVariationType::OrderedFromRandom;
        const bool random=type==XsbTrackVariationType::Random;
        std::vector<uint32_t> rolls=ordered ? std::vector<uint32_t>{2}
            : random ? std::vector<uint32_t>{5,0,1,3,2} : std::vector<uint32_t>{5,1,3,0,0};
        const std::vector<int> sequence=ordered ? std::vector<int>{2,0,1,2}
            : random ? std::vector<int>{0,1,2,1} : std::vector<int>{1,2,0,1};
        AuthoredTracks tracks{{VariedAt(0,3,type)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks));
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); std::vector<uint32_t> bounds;
        InjectVariation(*cue,rolls,bounds); cue->Play();
        for (int iteration=0;iteration<4;++iteration)
        {
            ExpectMarker(manualDevice->Render(441),(float(sequence[iteration]+1)*.125f)*ValueBaseGain);
            ExpectSilence(manualDevice->Render(32)); b.Tick((iteration+1)*10);
        }
        EXPECT_EQ(bounds,ordered ? (std::vector<uint32_t>{6})
            : random ? (std::vector<uint32_t>{6,6,6,6,6}) : (std::vector<uint32_t>{6,3,4,3,5}));
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactLoopVariation, PitchAndVolumeAddNewAndHoldReachEverySpawnedVoice)
{
    // FACT_internal.c:309-374. Pitch300->600->900 for ADD; NEW stays300.
    // Volume byte90 gives c=-1194.093444090611; total gainB*10^(n*c/2000).
    // B=10^(3*(3969*log10(180/28240)+8715)/2000), defined in value cases.
    for (const uint16_t mode : {uint16_t(0x0005),uint16_t(0x0300),uint16_t(0x0305)})
    {
        AuthoredTracks tracks{{VariedAt(0,2,XsbTrackVariationType::Ordered,0x3000|mode)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks));
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); std::vector<uint32_t> bounds;
        InjectVariation(*cue,{},bounds,std::vector<float>(6,0)); cue->Play();
        for (int iteration=0;iteration<3;++iteration)
        {
            const int n=mode==0x0305 ? iteration+1 : 1;
            const float gain=n==1 ? 0.2535100559f : n==2 ? 0.06411334986f : 0.01621443227f;
            const float ratio=n==1 ? 1.189207115f : n==2 ? 1.414213562f : 1.681792831f;
            ExpectEventVoice(*cue,0,0,gain,ratio);
            if (n <= 2)
                ExpectPitchedValueMarker(manualDevice->Render(32),
                    float(iteration+1)*.125f*gain,0,n*300);
            // Bounded rendering consumes the whole pitched voice plus EOF guard;
            // voice gain/ratio receipts forward to the real adapters, no substitute mixer.
            (void)manualDevice->Render(1024); b.Tick((iteration+1)*10);
        }
        EXPECT_TRUE(bounds.empty()); EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactLoopVariation, FilterNewAndHoldReachFreshVoiceWithoutChangingPitchOrVolume)
{
    // FACT_internal.c:381-424. FirstQ4 ->.25, f4000; nextQ2 ->.5,f2000.
    // Normalized f=2*sin(pi*Hz/44100): .5622222267 and .2839886359.
    for (const uint16_t mode : {uint16_t(0),uint16_t(0x0400),uint16_t(0x0800),uint16_t(0x0c50)})
    {
        AuthoredTracks tracks{{VariedAt(0,1,XsbTrackVariationType::Ordered,0xc000|mode)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks,true));
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); std::vector<uint32_t> bounds;
        InjectVariation(*cue,{},bounds,{.5f,.75f,0,.25f}); cue->Play();
        for (int iteration=0;iteration<2;++iteration)
        {
            auto* instance=CueTestAccess::EventVoice(*cue,0,0); ASSERT_NE(instance,nullptr);
            int kind=-1; float f=-1,q=-1;
            SoundEffectInstanceTestAccess::GetFilterState(*instance,kind,f,q);
            EXPECT_EQ(kind,1); // FilterState::Kind::LowPass
            EXPECT_NEAR(f,iteration && mode ? .2839886359f : .5622222267f,1e-6f);
            EXPECT_NEAR(q,iteration && mode ? .5f : .25f,1e-6f);
            ExpectEventVoice(*cue,0,0,ValueBaseGain,1); // filter draw must not affect other axes
            // FAudio_internal.c:804-806, zero initial state and constant input x:
            // low[0..2]/x =0,f*f,f*f*(3-f*f-q*f), derived independently.
            const std::array<float,3> step = iteration && mode
                ? std::array<float,3>{0,.080649545329f,.223992509643f}
                : std::array<float,3>{0,.316093832154f,.803937441200f};
            const auto pcm=manualDevice->Render(3); ASSERT_EQ(pcm.size(),6u);
            for (std::size_t i=0;i<3;++i)
                for (std::size_t channel=0;channel<2;++channel)
                    EXPECT_NEAR(pcm[2*i+channel],float(iteration+1)*.125f*ValueBaseGain*step[i],1e-6f);
            (void)manualDevice->Render(1024); b.Tick((iteration+1)*10);
        }
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactLoopVariation, InfiniteVariationSurvivesBeyondByteCounterAndStops)
{
    // G02's owner-approved255 sentinel, plus FACT's repeated ordered selection.
    AuthoredTracks tracks{{VariedAt(0,255,XsbTrackVariationType::Ordered)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
    for (int iteration=0;iteration<260;++iteration)
    {
        ExpectMarker(manualDevice->Render(441),(float(iteration%3+1)*.125f)*ValueBaseGain);
        ExpectSilence(manualDevice->Render(32)); b.Tick((iteration+1)*10);
        EXPECT_TRUE(cue->getIsPlayingProperty());
    }
    cue->Stop(AudioStopOptions::Immediate); b.Tick(3000);
    EXPECT_TRUE(cue->getIsStoppedProperty()); ExpectSilence(manualDevice->Render(882));
}

TEST(XactLoopVariation, InfiniteWithoutLoopVariationKeepsTheSelectedVoice)
{
    // FACT_internal.c:271-277:255,!has_variation,!LOOP_MASK delegates to wave.
    AuthoredTracks tracks{{VariedAt(0,255,XsbTrackVariationType::Ordered,0,false)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
    auto* voice=CueTestAccess::EventVoice(*cue,0,0);
    for (int i=0;i<3;++i)
    {
        ExpectMarker(manualDevice->Render(882),(.125f)*ValueBaseGain); b.Tick((i+1)*20);
        EXPECT_EQ(CueTestAccess::EventVoice(*cue,0,0),voice);
    }
}

TEST(XactLoopVariation, InfiniteEffectVariationReappliesNewValuesWithoutTrackVariation)
{
    // FACT_internal.c:271-278 and FACT_internal.h:338-346: NEW effect bits prevent
    // native infinite-loop delegation even without track variation. Each ADD iteration
    // has pitch n*300 cents, ratio 2^(n/4), gain B*10^(n*(-1194.093444090611)/2000).
    AuthoredTracks tracks{{VariedAt(0,255,XsbTrackVariationType::Ordered,0x3305,false)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks));
    std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); std::vector<uint32_t> bounds;
    InjectVariation(*cue,{},bounds,std::vector<float>(6,0)); cue->Play();
    const std::array<float,3> gains{.2535100559f,.06411334986f,.01621443227f};
    const std::array<float,3> ratios{1.189207115f,1.414213562f,1.681792831f};
    for (std::size_t iteration=0;iteration<gains.size();++iteration)
    {
        SCOPED_TRACE(iteration);
        auto* voice=CueTestAccess::EventVoice(*cue,0,0); ASSERT_NE(voice,nullptr);
        EXPECT_FALSE(voice->getIsLoopedProperty());
        EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),255);
        EXPECT_TRUE(cue->getIsPlayingProperty());
        ExpectEventVoice(*cue,0,0,gains[iteration],ratios[iteration]);
        if (iteration+1<gains.size())
        {
            (void)manualDevice->Render(1024);
            b.Tick(static_cast<int64_t>((iteration+1)*10));
        }
    }
    EXPECT_TRUE(bounds.empty());
    cue->Stop(AudioStopOptions::Immediate); b.Tick(30);
    EXPECT_TRUE(cue->getIsStoppedProperty()); ExpectSilence(manualDevice->Render(64));
}

TEST(XactLoopVariation, EachEventAndNewCueStartsFreshAndStoppedCueCannotReplay)
{
    // FACTEventInstance belongs to one event in one sound instance (:724-771).
    AuthoredTracks tracks{{VariedAt(0,1,XsbTrackVariationType::Ordered),
                           VariedAt(40,1,XsbTrackVariationType::Ordered)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
    for (int event=0;event<2;++event)
    {
        if (event) b.Tick(40);
        ExpectMarker(manualDevice->Render(441),(.125f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
        b.Tick(event*40+10);
        ExpectMarker(manualDevice->Render(441),(.25f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
        b.Tick(event*40+20);
    }
    EXPECT_TRUE(cue->getIsStoppedProperty());
    // FACTCue_Play refuses STOPPED; only a newly obtained Cue starts another sequence.
    cue->Play(); EXPECT_TRUE(cue->getIsStoppedProperty()); ExpectSilence(manualDevice->Render(441));
    auto fresh=b.Play(); ExpectMarker(manualDevice->Render(441),(.125f)*ValueBaseGain);
}

TEST(XactLoopVariation, PauseAtCompletionRetainsPendingLoopUntilResume)
{
    // FACT.c:2102-2108,2654-2669 leaves completed waves stopped on Cue.Resume.
    // FACT_internal.c:1725-1733 defers pending promotion while the cue is paused.
    // Ordered advances0->1, so .125 first wave must become .25 only on Update.
    AuthoredTracks tracks{{VariedAt(0,1,XsbTrackVariationType::Ordered)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
    ExpectMarker(manualDevice->Render(441),(.125f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
    auto* completed=CueTestAccess::EventVoice(*cue,0,0); ASSERT_NE(completed,nullptr);
    ASSERT_EQ(completed->getStateProperty(),SoundState::Stopped);
    EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),1);
    cue->Pause(); b.Tick(1000); EXPECT_TRUE(cue->getIsPausedProperty());
    ExpectSilence(manualDevice->Render(441));
    EXPECT_EQ(completed->getStateProperty(),SoundState::Stopped);
    EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),1);
    cue->Resume();
    // Resume itself must not replay the completed .125 voice or consume a loop.
    EXPECT_EQ(completed->getStateProperty(),SoundState::Stopped);
    EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),1);
    ExpectSilence(manualDevice->Render(32));
    b.Tick(1000);
    ExpectMarker(manualDevice->Render(441),(.25f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
    EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),0);
    b.Tick(1010); EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(XactLoopVariation, PausedVoiceFinishesItsRemainingFramesBeforeNextVariation)
{
    // FACT.c:2111-2119 pauses/resumes a live voice without replaying its prefix.
    // FACT_internal.c:1530-1548 promotes after EOF. GAP-01b excludes paused clock time:
    // pause at7ms, resume1007ms ->Update1019 is19ms, Update1020 is20ms.
    for (int played : {400,441})
    {
        SCOPED_TRACE(played);
        AuthoredTracks tracks{{VariedAt(0,1,XsbTrackVariationType::Ordered),StopAt(20,1)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
        ExpectMarker(manualDevice->Render(played),(.125f)*ValueBaseGain);
        b.SetTime(7); cue->Pause(); b.Tick(1007);
        EXPECT_TRUE(cue->getIsPausedProperty());
        ExpectSilence(manualDevice->Render(441));
        EXPECT_EQ(CueTestAccess::LoopsRemaining(*cue,0,0),1);
        cue->Resume();
        //441 authored frames minus400 consumed leaves41; at441 only EOF remains.
        if (played<441) ExpectMarker(manualDevice->Render(441-played),(.125f)*ValueBaseGain);
        ExpectSilence(manualDevice->Render(32));
        b.Tick(1019);
        EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0}})); //Stop at20ms is still future
        ExpectMarker(manualDevice->Render(441),(.25f)*ValueBaseGain);
        ExpectSilence(manualDevice->Render(32));
        b.Tick(1020);
        EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{0,1,20}}));
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactLoopVariation, TrackStopCancelsItsLoopWhileOtherTrackContinues)
{
    // FACT_internal.c:1189-1214 clears only the stopped track's loop budget.
    AuthoredTracks tracks{{VariedAt(0,2,XsbTrackVariationType::Ordered),StopAt(10,1)},
                           {VariedAt(0,1,XsbTrackVariationType::Ordered)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
    ExpectMarker(manualDevice->Render(441),(.25f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
    b.Tick(10); ExpectMarker(manualDevice->Render(441),(.25f)*ValueBaseGain);
    ExpectSilence(manualDevice->Render(32)); b.Tick(20);
    EXPECT_TRUE(cue->getIsStoppedProperty()); ExpectSilence(manualDevice->Render(882));
}

TEST(XactLoopVariation, StopAndDisposeCannotResurrectPendingIterations)
{
    // Refresh Cue.Stop IL_0000-0011; FACT StopTrack cancels pending loops.
    for (int action=0;action<4;++action)
    {
        AuthoredTracks tracks{{VariedAt(0,255,XsbTrackVariationType::Ordered)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
        ExpectMarker(manualDevice->Render(441),(.125f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
        if (action==0) cue->Stop(AudioStopOptions::Immediate);
        else if (action==1) cue->Stop(AudioStopOptions::AsAuthored);
        else if (action==2) cue->Dispose();
        else b.wave->Dispose();
        b.Tick(1000); ExpectSilence(manualDevice->Render(882));
        if (action<2) EXPECT_TRUE(cue->getIsStoppedProperty());
        else EXPECT_TRUE(cue->getIsDisposedProperty());
    }
}

TEST(XactLoopVariation, CategoryAndValueChangesRecombineWithRetainedLoopOffsets)
{
    // FACT_internal.c:1551-1571; category gain multiplies, event pitch adds.
    AuthoredTracks tracks{{VariedAt(0,1,XsbTrackVariationType::Ordered,0x3305),
                           EquationAt(7,5,4,300)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks));
    std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); std::vector<uint32_t> bounds;
    InjectVariation(*cue,{},bounds,std::vector<float>(4,0)); cue->Play(); b.Tick(5);
    auto category=b.engine->GetCategory("Default"); category.SetVolume(.5f);
    ExpectEventVoice(*cue,0,0,.1267550279f,1.414213562f); //300variation+300event
    (void)manualDevice->Render(1024); b.Tick(10);
    ExpectEventVoice(*cue,0,0,.03205667493f,1.681792831f); //600variation+300event
    b.Tick(11); ExpectEventVoice(*cue,0,0,.03205667493f,1.681792831f);
}

TEST(XactLoopVariation, EffectOnlyEventsUseLoopFlagsWithoutAnyCandidateTable)
{
    // FACT simple-wave path:253-257 plus pitch/volume branches:309-374.
    for (uint16_t mode : {uint16_t(0x0005),uint16_t(0x0300),uint16_t(0x0305)})
    {
        auto event=VariedAt(0,1,XsbTrackVariationType::Ordered,0x3000|mode);
        event.type=4;
        event.valuePayload.resize(event.valuePayload.size()-23); //remove8-byte table header+3*5 entries
        event.valuePayload.insert(event.valuePayload.begin()+1,3,0); //simple wave0, bank0
        AuthoredTracks tracks{{event}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks));
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); std::vector<uint32_t> bounds;
        int draws=0;
        CueTestAccess::SetVariationRandom(*cue,{{},[&] { ++draws; return 0.0f; }});
        cue->Play(); ExpectEventVoice(*cue,0,0,.2535100559f,1.189207115f);
        (void)manualDevice->Render(1024); b.Tick(10);
        ExpectEventVoice(*cue,0,0,mode==0x0305 ? .06411334986f : .2535100559f,
                                   mode==0x0305 ? 1.414213562f : 1.189207115f);
        EXPECT_EQ(draws,4); //two enabled axes draw on each finite event activation, also HOLD
    }
}

TEST(XactLoopVariation, FiniteCandidateEventsReselectWithEitherHasVariationValue)
{
    // FACT_internal.c:195-199 is an empty conditional, not a finite-loop gate.
    // Only the infinite delegation check at271-277 uses has_variation.
    for (bool hasVariation : {false,true})
    {
        AuthoredTracks tracks{{VariedAt(0,1,XsbTrackVariationType::Ordered,0,hasVariation)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks)); auto cue=b.Play();
        ExpectMarker(manualDevice->Render(441),(.125f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
        b.Tick(10); ExpectMarker(manualDevice->Render(441),(.25f)*ValueBaseGain);
        ExpectSilence(manualDevice->Render(32)); b.Tick(20);
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactLoopVariation, VariedIterationsUseCompleteWavesWithRegionsForResidentAndStreamedBanks)
{
    // FACT_internal.c:278 passes wave loopCount0 for event-driven repeats.
    // Region metadata must not truncate a one-shot wave or repeat its prefix twice.
    for (bool stream : {false,true})
    {
        auto bytes=ScheduleXwb(); bytes[48]=stream ? 1 : 0;
        for (std::size_t i=0;i<3;++i)
        {
            // XWB metadata24 per entry, loopStart+16=48, loopLength+20=96.
            bytes[144+i*24+16]=48; bytes[144+i*24+20]=96;
        }
        AuthoredTracks tracks{{VariedAt(0,1,XsbTrackVariationType::Ordered)}};
        ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks),bytes,stream);
        auto cue=b.Play();
        ExpectMarker(manualDevice->Render(441),(.125f)*ValueBaseGain); ExpectSilence(manualDevice->Render(32));
        b.Tick(10); ExpectMarker(manualDevice->Render(441),(.25f)*ValueBaseGain);
        ExpectSilence(manualDevice->Render(32)); b.Tick(20);
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}
