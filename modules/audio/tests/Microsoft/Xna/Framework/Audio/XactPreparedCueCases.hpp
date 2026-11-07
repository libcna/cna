// SPDX-License-Identifier: MS-PL
// Included after the SDK instance-count fixtures by both real offline adapters.
// Microsoft3.6 preparation controls: PREPARED_CUE_PROVENANCE.json and TQ-XACT-26.

TEST(XactPreparedCue, PreparationOwnsBanksWithoutDispatchAndPlayStartsItsOwnClock)
{
    auto b=InstanceCountBanks();auto cue=CountCue(*b->sound);
    EXPECT_TRUE(cue->getIsPreparedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
    EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
    EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
    std::vector<Dispatch> trace;
    CueTestAccess::ObserveDispatch(*cue,[&](auto t,auto e,auto ms){trace.emplace_back(t,e,ms);});
    b->Tick(900);EXPECT_TRUE(trace.empty());ExpectSilence(manualDevice->Render(64));
    cue->Play();EXPECT_EQ(trace,(std::vector<Dispatch>{{0,0,0}}));
    EXPECT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);EXPECT_FLOAT_EQ(NumInstances(*cue),1);
    b->Tick(1399);EXPECT_EQ(trace.size(),1u);
    b->Tick(1400);EXPECT_EQ(trace,(std::vector<Dispatch>{{0,0,0},{0,1,500}}));
    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(b->wave->getIsInUseProperty());
}

TEST(XactPreparedCue, UnplayedStopReleasesPreparationWithoutAnAuthoredFade)
{
    auto b=InstanceCountBanks("cue_oldest");auto cue=CountCue(*b->sound);
    ASSERT_TRUE(b->sound->getIsInUseProperty());ASSERT_TRUE(b->wave->getIsInUseProperty());
    cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_FALSE(cue->getIsStoppingProperty());
    EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(b->wave->getIsInUseProperty());
    EXPECT_FLOAT_EQ(NumInstances(*cue),0);cue->Play();EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(XactPreparedCue, InitialCandidateAndEffectsArePreparedOnceBeforeTheFirstDispatch)
{
    // FACT Prepare/create_sound calls GetNextWave before Play; the initial waveform
    // and effects are reused, and the second complete-wave iteration advances them.
    AuthoredTracks tracks{{VariedAt(40,2,XsbTrackVariationType::OrderedFromRandom,0x3000)}};
    ScheduledBanks b(tracks,0,false,{},false,VariationSound(tracks));
    std::vector<uint32_t> bounds;unsigned draws=0;
    AudioEngineTestAccess::SetPreparationVariationRandom(*b.engine,{
        [&](uint32_t bound){bounds.push_back(bound);return 2u;},
        [&]{++draws;return 0.0f;}});
    auto cue=std::unique_ptr<Cue>(b.sound->GetCue("Sequence"));
    EXPECT_EQ(bounds,(std::vector<uint32_t>{6}));EXPECT_EQ(draws,2u);
    EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);ExpectSilence(manualDevice->Render(32));
    b.Tick(700);cue->Play();EXPECT_EQ(draws,2u);b.Tick(739);EXPECT_EQ(draws,2u);
    b.Tick(740);EXPECT_EQ(bounds,(std::vector<uint32_t>{6}));EXPECT_EQ(draws,2u);
    ExpectEventVoice(*cue,0,0,.2535100559f,1.189207115f);
    (void)manualDevice->Render(1024);b.Tick(750);
    EXPECT_EQ(bounds,(std::vector<uint32_t>{6}));EXPECT_EQ(draws,4u);
    ExpectEventVoice(*cue,0,0,.2535100559f,1.189207115f);
}

TEST(XactPreparedCue, SelectedPreparedCandidateKeepsOwnershipAndIsConsumedOnlyOnce)
{
    for(const char* profile:{"cue_oldest","cat_oldest"})
    {
        SCOPED_TRACE(profile);auto b=InstanceCountBanks(profile);auto p=CountCue(*b->sound);
        auto l=CountCue(*b->sound);l->Play();b->Tick(100);
        auto i=CountCue(*b->sound);i->Play();b->Tick(300);
        EXPECT_TRUE(p->getIsPreparedProperty());EXPECT_EQ(CueTestAccess::ActiveInstance(*p,0),nullptr);
        EXPECT_TRUE(l->getIsPlayingProperty());EXPECT_TRUE(i->getIsPlayingProperty());
        EXPECT_FLOAT_EQ(NumInstances(*p),2);
        auto x=CountCue(*b->sound);x->Play();b->Tick(500);
        EXPECT_TRUE(l->getIsStoppedProperty());EXPECT_TRUE(i->getIsPlayingProperty());
        EXPECT_TRUE(x->getIsPlayingProperty());EXPECT_TRUE(p->getIsPreparedProperty());
        EXPECT_FLOAT_EQ(NumInstances(*p),2);
        l.reset();i.reset();x.reset();b->Tick(700);
        EXPECT_FLOAT_EQ(NumInstances(*p),0);
        EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        p->Play();EXPECT_TRUE(p->getIsPlayingProperty());EXPECT_FLOAT_EQ(NumInstances(*p),1);
        EXPECT_NE(CueTestAccess::ActiveInstance(*p,0),nullptr);
    }
}

TEST(XactPreparedCue, LaterPlayOfSelectedPreparedCueKeepsItsOriginalReplacementPosition)
{
    for(const char* profile:{"cue_oldest","cat_oldest"})
    {
        SCOPED_TRACE(profile);auto b=InstanceCountBanks(profile);auto p=CountCue(*b->sound);
        auto l=CountCue(*b->sound);l->Play();b->Tick(100);
        auto i=CountCue(*b->sound);i->Play();b->Tick(300);
        p->Play();b->Tick(500);
        EXPECT_TRUE(l->getIsStoppedProperty());EXPECT_TRUE(p->getIsPlayingProperty());
        auto j=CountCue(*b->sound);j->Play();b->Tick(700);
        EXPECT_TRUE(p->getIsStoppedProperty());EXPECT_TRUE(i->getIsPlayingProperty());
        EXPECT_TRUE(j->getIsPlayingProperty());EXPECT_FLOAT_EQ(NumInstances(*j),2);
    }
}

TEST(XactPreparedCue, PreparationOrderWinsWhenPlayOrderIsReversedAtMaximumTwo)
{
    for(const char* profile:{"cue_oldest_two","cat_oldest_two"})
    {
        SCOPED_TRACE(profile);auto b=InstanceCountBanks(profile);auto p=CountCue(*b->sound);
        auto l=CountCue(*b->sound);l->Play();b->Tick(100);p->Play();b->Tick(200);
        auto i=CountCue(*b->sound);i->Play();b->Tick(400);
        EXPECT_TRUE(p->getIsStoppedProperty());EXPECT_TRUE(l->getIsPlayingProperty());
        EXPECT_TRUE(i->getIsPlayingProperty());EXPECT_FLOAT_EQ(NumInstances(*i),2);
    }
}

TEST(XactPreparedCue, CategoryFailRejectsPrepareWithoutRegisteringButCueFailWaitsForPlay)
{
    {
        auto b=InstanceCountBanks("cat_fail_tail");auto earlier=CountCue(*b->sound);
        auto l=CountCue(*b->sound);l->Play();b->Tick(100);
        const auto retained=SoundBankTestAccess::RetainedCueCount(*b->sound);
        EXPECT_THROW((void)CountCue(*b->sound),InstancePlayLimitException);
        EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),retained);
        b->engine->Update();earlier->Play();EXPECT_TRUE(earlier->getIsStoppedProperty());
        EXPECT_TRUE(l->getIsPlayingProperty());EXPECT_FLOAT_EQ(NumInstances(*l),1);
    }
    {
        auto b=InstanceCountBanks("cue_fail");auto l=CountCue(*b->sound);l->Play();
        auto later=CountCue(*b->sound);EXPECT_TRUE(later->getIsPreparedProperty());later->Play();
        EXPECT_TRUE(later->getIsStoppedProperty());EXPECT_TRUE(l->getIsPlayingProperty());
    }
}

TEST(XactPreparedCue, MissingWaveBankFailsBeforeReturningACueAndRetryHasNoDanglingRegistration)
{
    auto b=InstanceCountBanks();b->wave.reset();
    EXPECT_THROW((void)CountCue(*b->sound),System::InvalidOperationException);
    EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),0u);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b->engine),0u);
    b->engine->Update();EXPECT_FALSE(b->sound->getIsInUseProperty());
    b->wave=b->LoadWaveBank("retry.xwb",InstanceCountBank("unlimited","xwb"));
    auto cue=CountCue(*b->sound);EXPECT_TRUE(cue->getIsPreparedProperty());
    EXPECT_TRUE(b->wave->getIsInUseProperty());cue->Play();
    EXPECT_TRUE(cue->getIsPlayingProperty());EXPECT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);
}

TEST(XactPreparedCue, PlayCueSwallowsPreparationLimitRefusalsAndKeepsOtherErrorMapping)
{
    for(const char* profile:{"cue_fail","cat_fail_tail"})
    {
        SCOPED_TRACE(profile);auto b=InstanceCountBanks(profile);auto live=CountCue(*b->sound);live->Play();
        const auto retained=SoundBankTestAccess::RetainedCueCount(*b->sound);
        EXPECT_NO_THROW(b->sound->PlayCue("ClockCue"));
        EXPECT_NO_THROW(b->sound->PlayCue("ClockCue",AudioListener{},AudioEmitter{}));
        EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),retained);
        EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b->sound),0u);
        EXPECT_TRUE(live->getIsPlayingProperty());EXPECT_FLOAT_EQ(NumInstances(*live),1);
    }
    auto b=InstanceCountBanks();b->wave.reset();
    for(bool spatial:{false,true})
    {
        try
        {
            if(spatial)b->sound->PlayCue("ClockCue",AudioListener{},AudioEmitter{});
            else b->sound->PlayCue("ClockCue");
            FAIL();
        }
        catch(const System::InvalidOperationException& error)
        {
            EXPECT_EQ(error.getMessageProperty(),
                "An error occurred trying to play the cue named \"ClockCue\". Is the cue name correct?");
        }
    }
}

TEST(XactPreparedCue, PreparedDisposalAndWaveBankCascadeReleaseAllOwnership)
{
    auto b=InstanceCountBanks("cue_oldest");
    {
        auto cue=CountCue(*b->sound);
        ASSERT_TRUE(b->sound->getIsInUseProperty());ASSERT_TRUE(b->wave->getIsInUseProperty());
        cue->Dispose();EXPECT_TRUE(cue->getIsDisposedProperty());
        EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(b->wave->getIsInUseProperty());
        EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),0u);
    }
    auto cue=CountCue(*b->sound);b->wave->Dispose();
    EXPECT_TRUE(cue->getIsDisposedProperty());EXPECT_FALSE(b->sound->getIsInUseProperty());
    EXPECT_FALSE(b->wave->getIsInUseProperty());
    EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),0u);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b->engine),0u);
}

TEST(XactPreparedCue, SameNamedBanksKeepPreparationOrderAndSurviveIndependentDisposal)
{
    // FACT keeps every bank in its insertion-ordered wbList and resolves the first match.
    // This is a source-derived duplicate-name policy, not a measured Microsoft policy.
    auto b=InstanceCountBanks();auto duplicate=b->LoadWaveBank("duplicate.xwb",InstanceCountBank("unlimited","xwb"));
    auto cue=CountCue(*b->sound);
    EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_FALSE(duplicate->getIsInUseProperty());
    duplicate->Dispose();EXPECT_TRUE(cue->getIsPreparedProperty());
    auto later=CountCue(*b->sound);later->Play();EXPECT_TRUE(later->getIsPlayingProperty());
    cue.reset();later.reset();
    auto remaining=b->LoadWaveBank("remaining.xwb",InstanceCountBank("unlimited","xwb"));
    b->wave->Dispose();auto replacement=CountCue(*b->sound);
    EXPECT_TRUE(remaining->getIsInUseProperty());replacement.reset();
    auto both=InstanceCountBanks();auto second=both->LoadWaveBank("second.xwb",InstanceCountBank("unlimited","xwb"));
    both->engine->Dispose();EXPECT_TRUE(both->wave->getIsDisposedProperty());EXPECT_TRUE(second->getIsDisposedProperty());
}
