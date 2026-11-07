// SPDX-License-Identifier: MS-PL
// Included by both real offline mixers. Primary Microsoft3.6 ownership controls:
// bank-lifetime/PROVENANCE.json; count0 does not imply that either bank is unused.

Bytes LifetimeBank(const char* profile,const char* name)
{
    const auto path=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/"bank-lifetime"/profile/name;
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing SDK lifetime bank: "+path.string());
    return Bytes(std::istreambuf_iterator<char>(input),{});
}

std::unique_ptr<ScheduledBanks> LifetimeBanks(const char* profile="staggered")
{
    return std::make_unique<ScheduledBanks>(AuthoredTracks{},0,false,LifetimeBank(profile,"lifetime.xgs"),
        false,LifetimeBank(profile,"lifetime.xsb"),LifetimeBank(profile,"A.xwb"));
}

std::unique_ptr<Cue> LifetimeCue(SoundBank& bank)
{ return std::unique_ptr<Cue>(bank.GetCue("LifetimeCue")); }

TEST(XactBankLifetime, StoppedCueRetainsSoundBankAndCueDisposalRetainsResolvedWaves)
{
    for(const char* profile:{"simple","staggered"})
    {
        SCOPED_TRACE(profile);auto b=LifetimeBanks(profile);
        auto second=b->LoadWaveBank("B.xwb",LifetimeBank(profile,"B.xwb"));
        EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(b->wave->getIsInUseProperty());
        auto cue=LifetimeCue(*b->sound);cue->Play();cue->Stop(AudioStopOptions::Immediate);
        EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
        EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        EXPECT_EQ(second->getIsInUseProperty(),std::string(profile)=="staggered");
        cue.reset();b->Tick(2000);
        EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        EXPECT_EQ(second->getIsInUseProperty(),std::string(profile)=="staggered");
        b->sound->Dispose();
        EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(second->getIsInUseProperty());
    }
}

TEST(XactBankLifetime, NeverPlayedStopAndDisposalUseTheSameOwnerBoundary)
{
    auto b=LifetimeBanks("simple");auto cue=LifetimeCue(*b->sound);
    cue->Stop(AudioStopOptions::Immediate);EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
    cue->Dispose();EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
    b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
}

TEST(XactBankLifetime, NaturalRetirementKeepsBankUseWithoutAnyPlayingInstance)
{
    auto b=LifetimeBanks("simple");auto cue=LifetimeCue(*b->sound);cue->Play();
    // The native property reports2205 samples at22050Hz; consume its real EOF.
    (void)manualDevice->Render(5000);b->Tick(300);
    EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
    EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
    cue.reset();EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
    b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
}

TEST(XactBankLifetime, PartialPrepareFailureRetainsResolvedBankWithoutACueAndRetrySucceeds)
{
    auto b=LifetimeBanks();
    EXPECT_THROW((void)LifetimeCue(*b->sound),System::InvalidOperationException);
    EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),0u);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b->engine),0u);
    EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
    auto second=b->LoadWaveBank("B.xwb",LifetimeBank("staggered","B.xwb"));
    auto cue=LifetimeCue(*b->sound);EXPECT_TRUE(cue->getIsPreparedProperty());
    EXPECT_TRUE(second->getIsInUseProperty());cue.reset();
    EXPECT_FALSE(b->sound->getIsInUseProperty());
    EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_TRUE(second->getIsInUseProperty());
    b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(second->getIsInUseProperty());
}

TEST(XactBankLifetime, IndependentSoundBanksDetachTheirResolvedResourcesSeparately)
{
    auto b=LifetimeBanks("simple");
    auto other=b->LoadSoundBank("other.xsb",LifetimeBank("simple","lifetime.xsb"));
    auto first=LifetimeCue(*b->sound);auto second=LifetimeCue(*other);first.reset();second.reset();
    EXPECT_TRUE(b->wave->getIsInUseProperty());b->sound->Dispose();
    EXPECT_TRUE(b->wave->getIsInUseProperty());other->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
}

TEST(XactBankLifetime, ForcedWaveBankDisposalDetachesCachedOwnersBeforeObjectDestruction)
{
    // Preserve CNA's existing safe cascade; native in-use destruction is not qualified.
    auto b=LifetimeBanks("simple");auto cue=LifetimeCue(*b->sound);b->wave.reset();
    EXPECT_TRUE(cue->getIsDisposedProperty());EXPECT_FALSE(b->sound->getIsInUseProperty());
    EXPECT_THROW((void)LifetimeCue(*b->sound),System::InvalidOperationException);
    b->wave=b->LoadWaveBank("replacement.xwb",LifetimeBank("simple","A.xwb"));
    auto retry=LifetimeCue(*b->sound);retry.reset();EXPECT_TRUE(b->wave->getIsInUseProperty());
    b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
}

TEST(XactBankLifetime, ManagedAuthoredTailKeepsItsCueOwnerUntilTheTailActuallyEnds)
{
    auto b=InstanceCountBanks("cue_fail");b->sound->PlayCue("ClockCue");
    auto* cue=SoundBankTestAccess::LastFireAndForgetCue(*b->sound);ASSERT_NE(cue,nullptr);
    cue->Stop(AudioStopOptions::AsAuthored);b->Tick(20);
    ASSERT_EQ(SoundBankTestAccess::FireAndForgetCount(*b->sound),1u);
    EXPECT_TRUE(cue->getIsStoppingProperty());EXPECT_TRUE(b->sound->getIsInUseProperty());
    b->Tick(50);EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b->sound),0u);
    EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
    b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
}

TEST(XactBankLifetime, DeferredInteractivePlaybackRetainsOnlyItsResolvedWaveUntilBankDisposal)
{
    // Interactive selection is source-derived; this is not a Microsoft-native shape control.
    ScheduledBanks b({},0,false,CountRpcXgs(),false,CountInteractiveXsb());
    auto cue=CountCue(*b.sound,"Sequence");
    EXPECT_TRUE(b.sound->getIsInUseProperty());EXPECT_FALSE(b.wave->getIsInUseProperty());
    cue->Play();ASSERT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);
    EXPECT_TRUE(b.wave->getIsInUseProperty());cue.reset();
    EXPECT_FALSE(b.sound->getIsInUseProperty());EXPECT_TRUE(b.wave->getIsInUseProperty());
    b.sound->Dispose();EXPECT_FALSE(b.wave->getIsInUseProperty());
}

TEST(XactBankLifetime, CueDisposalCallbackCanDestroyAResolvedWaveBankDuringSoundBankDisposal)
{
    auto b=LifetimeBanks("simple");auto cue=LifetimeCue(*b->sound);bool notified=false;
    cue->Disposing += [&](System::Object*,const System::EventArgs&)
    { b->wave.reset();notified=true; };
    b->sound->Dispose();EXPECT_TRUE(notified);EXPECT_EQ(b->wave,nullptr);
    EXPECT_TRUE(cue->getIsDisposedProperty());EXPECT_FALSE(b->sound->getIsInUseProperty());
}

TEST(XactBankLifetime, LaterVariationCandidateAddsItsOwnPersistentBankOwner)
{
    // Source-derived loop selection, not measured Microsoft variation-prefetch timing.
    auto event=VariedAt(0,2,XsbTrackVariationType::Ordered);
    event.valuePayload[14+5+2]=1; // Second candidate: wave1 in the second bank.
    AuthoredTracks tracks{{event}};
    ScheduledBanks b(tracks,0,false,{},false,
        ScheduleXsb(tracks,0,false,false,0,{107,130},{},{"ScheduleWave","SecondWave"}));
    auto bytes=ScheduleXwb();std::fill(bytes.begin()+56,bytes.begin()+120,0);
    const std::string name="SecondWave";std::copy(name.begin(),name.end(),bytes.begin()+56);
    auto second=b.LoadWaveBank("second.xwb",bytes);
    auto cue=std::unique_ptr<Cue>(b.sound->GetCue("Sequence"));
    EXPECT_TRUE(b.wave->getIsInUseProperty());EXPECT_FALSE(second->getIsInUseProperty());
    cue->Play();ExpectMarker(manualDevice->Render(441),.125f*ValueBaseGain);
    ExpectSilence(manualDevice->Render(32));b.Tick(10);
    ExpectMarker(manualDevice->Render(441),.25f*ValueBaseGain);
    EXPECT_TRUE(second->getIsInUseProperty());cue.reset();
    EXPECT_TRUE(b.wave->getIsInUseProperty());EXPECT_TRUE(second->getIsInUseProperty());
    b.sound->Dispose();EXPECT_FALSE(b.wave->getIsInUseProperty());EXPECT_FALSE(second->getIsInUseProperty());
}
