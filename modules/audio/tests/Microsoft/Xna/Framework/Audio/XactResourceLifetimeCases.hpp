// SPDX-License-Identifier: MS-PL
// HND-XACT-04 native-v5-lifetimes,74 cells including resident control/shape repeats.
// Exact identities/notification oracles: resource-lifetimes/PROVENANCE.json.
// Native notification registration is not a managed CNA notification API.

std::unique_ptr<WaveBank> LifetimeShapeB(ScheduledBanks& b,const char* profile)
{ return std::string(profile)=="ordered"?PrefetchB(b,profile):PreparationShapeB(b,profile); }

TEST(HndXactResourceLifetime, NeverPlayedStopOrDestructionKeepsOnlyResolvedBankOwners)
{
    // All5 profiles/unplayed-stop,unplayed-destroy: no WAVEPLAY/marker; native
    // prepared wave DESTROY except interactive(no wave). SB owner survives cue.
    for(const char* profile:{"resident","weighted","interactive","streaming","ordered"})for(bool stop:{false,true})
    {
        SCOPED_TRACE(profile);
        SCOPED_TRACE(stop);auto b=CrossBanks(profile);auto other=LifetimeShapeB(*b,profile);
        auto cue=CountCue(*b->sound,"ShapeCue");const bool a=b->wave->getIsInUseProperty(),bb=other->getIsInUseProperty();
        EXPECT_EQ(unsigned(a)+unsigned(bb),std::string(profile)=="interactive"?0u:1u);
        EXPECT_TRUE(cue->getIsPreparedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);b->Tick(300);
        if(stop){cue->Stop(AudioStopOptions::Immediate);b->Tick(400);EXPECT_TRUE(cue->getIsStoppedProperty());
            EXPECT_FLOAT_EQ(NumInstances(*cue),0);EXPECT_TRUE(b->sound->getIsInUseProperty());}
        cue.reset();b->Tick(2400);ExpectSilence(manualDevice->Render(128));
        EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_EQ(b->wave->getIsInUseProperty(),a);EXPECT_EQ(other->getIsInUseProperty(),bb);
        b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}

TEST(HndXactResourceLifetime, DisposingPlayingCueStopsActualAudioButLeavesItsResolvedBanksOwned)
{
    // All5 profiles/destroy-playing at100ms: cue gone, wave stopped/destroyed,
    // resolved bank(s)132 until SoundBank Destroy, then4. Ordered successor was
    // prepared but never played. No precise callback delivery deadline is asserted.
    for(const char* profile:{"resident","weighted","interactive","streaming","ordered"})
    {
        SCOPED_TRACE(profile);auto b=CrossBanks(profile);auto other=LifetimeShapeB(*b,profile);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);cue->Play();b->Tick(400);
        ASSERT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0)->getStateProperty(),SoundState::Playing);
        EXPECT_FLOAT_EQ(NumInstances(*cue),1);(void)manualDevice->Render(128);
        const bool a=b->wave->getIsInUseProperty(),bb=other->getIsInUseProperty();
        EXPECT_EQ(unsigned(a)+unsigned(bb),std::string(profile)=="ordered"?2u:1u);
        cue->Dispose();EXPECT_TRUE(cue->getIsDisposedProperty());EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        EXPECT_FALSE(b->sound->getIsInUseProperty());ExpectSilence(manualDevice->Render(256));b->Tick(4800);
        EXPECT_EQ(b->wave->getIsInUseProperty(),a);EXPECT_EQ(other->getIsInUseProperty(),bb);
        cue.reset();b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}

TEST(HndXactResourceLifetime, WeightedAndStreamingClonesReleaseTheirIndependentOwnersOnlyAtBankDisposal)
{
    // weighted/streaming clone complete native lifecycles: one played wave per
    // cue, two destroyed waves; after first SB destruction only clone's selected
    // bank remains132, after second both4. No fixed weighted RNG sequence.
    for(const char* profile:{"weighted","streaming"})
    {
        SCOPED_TRACE(profile);auto b=CrossBanks(profile);auto other=LifetimeShapeB(*b,profile);
        auto first=CountCue(*b->sound,"ShapeCue");b->Tick(300);first->Play();b->Tick(400);
        auto bank=b->LoadSoundBank("lifetime-clone.xsb",CrossSound(profile));auto second=CountCue(*bank,"ShapeCue");second->Play();b->Tick(500);
        ASSERT_NE(CueTestAccess::ActiveInstance(*first,0),nullptr);ASSERT_NE(CueTestAccess::ActiveInstance(*second,0),nullptr);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*first,0)->getStateProperty(),SoundState::Playing);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*second,0)->getStateProperty(),SoundState::Playing);
        (void)manualDevice->Render(50000);b->Tick(4800);EXPECT_TRUE(first->getIsStoppedProperty());EXPECT_TRUE(second->getIsStoppedProperty());
        EXPECT_FLOAT_EQ(NumInstances(*first),0);EXPECT_FLOAT_EQ(NumInstances(*second),0);
        first.reset();second.reset();EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(bank->getIsInUseProperty());
        b->sound->Dispose();EXPECT_NE(b->wave->getIsInUseProperty(),other->getIsInUseProperty());
        if(std::string(profile)=="streaming"){EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());}
        bank->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}
