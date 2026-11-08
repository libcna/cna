// SPDX-License-Identifier: MS-PL
// Native oracle:22 cells native-v4-cross-bank-{x86,x64}-<profile>-<mode>-250-io0-0.
// Exact probe/DLL/input identities: cross-bank/PROVENANCE.json and native matrix.

std::unique_ptr<ScheduledBanks> CrossBanks(const char* profile)
{
    if(std::string(profile)=="ordered")return PrefetchBanks(profile);
    if(std::string(profile)!="admission")return PreparationShapeBanks(profile);
    const auto root=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/"cross-bank/admission";
    auto read=[&](const char* n){std::ifstream f(root/n,std::ios::binary);if(!f)throw std::runtime_error("Missing admission fixture");return Bytes(std::istreambuf_iterator<char>(f),{});};
    return std::make_unique<ScheduledBanks>(AuthoredTracks{},0,false,read("shapes.xgs"),false,read("shapes.xsb"),read("A.xwb"),false,false);
}
Bytes CrossSound(const char* profile)
{
    if(std::string(profile)=="ordered")return PrefetchFile(profile,"shapes.xsb");
    if(std::string(profile)!="admission")return PreparationShapeFile(profile,"shapes.xsb");
    const auto p=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/"cross-bank/admission/shapes.xsb";
    std::ifstream f(p,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(f),{});
}

TEST(HndXactCrossBank, DuplicateNamesSelectFirstRegisteredBankForPreparationAndActualPlayback)
{
    // resident/ordered/interactive duplicateA: bank0 used132, duplicate slot2 stays4.
    for(const char* profile:{"resident","ordered","interactive"})
    {
        SCOPED_TRACE(profile);auto b=CrossBanks(profile);
        const auto a=std::string(profile)=="ordered"?PrefetchFile(profile,"A.xwb"):PreparationShapeFile(profile,"A.xwb");
        auto duplicate=b->LoadWaveBank("duplicate-A.xwb",a);
        auto other=std::string(profile)=="ordered"?PrefetchB(*b,profile):PreparationShapeB(*b,profile);
        auto cue=CountCue(*b->sound,"ShapeCue");
        EXPECT_EQ(b->wave->getIsInUseProperty(),std::string(profile)!="interactive");
        EXPECT_FALSE(duplicate->getIsInUseProperty());b->Tick(300);cue->Play();b->Tick(500);
        PrefetchCurrentWave(*cue,1024.f/32768);EXPECT_TRUE(b->wave->getIsInUseProperty());
        EXPECT_FALSE(duplicate->getIsInUseProperty());
        EXPECT_EQ(other->getIsInUseProperty(),std::string(profile)=="ordered");
        cue.reset();EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_FALSE(duplicate->getIsInUseProperty());
        b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}

TEST(HndXactCrossBank, ClonedSoundBanksRetainIndependentOwnersOfTheSameResolvedWaveBanks)
{
    // resident/ordered/interactive clone: two actual cues; after destroying first
    // soundbank owners remain132 until destroyed_both_soundbanks ->4.
    for(const char* profile:{"resident","ordered","interactive"})
    {
        SCOPED_TRACE(profile);auto b=CrossBanks(profile);
        auto other=std::string(profile)=="ordered"?PrefetchB(*b,profile):PreparationShapeB(*b,profile);
        auto first=CountCue(*b->sound,"ShapeCue");b->Tick(300);first->Play();b->Tick(400);
        auto bank=b->LoadSoundBank("clone.xsb",CrossSound(profile));auto second=CountCue(*bank,"ShapeCue");second->Play();b->Tick(500);
        EXPECT_TRUE(first->getIsPlayingProperty());EXPECT_TRUE(second->getIsPlayingProperty());
        EXPECT_FLOAT_EQ(NumInstances(*first),1);EXPECT_FLOAT_EQ(NumInstances(*second),1);
        ASSERT_NE(CueTestAccess::ActiveInstance(*first,0),nullptr);ASSERT_NE(CueTestAccess::ActiveInstance(*second,0),nullptr);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*first,0)->getStateProperty(),SoundState::Playing);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*second,0)->getStateProperty(),SoundState::Playing);
        first.reset();second.reset();EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(bank->getIsInUseProperty());
        EXPECT_TRUE(b->wave->getIsInUseProperty());EXPECT_EQ(other->getIsInUseProperty(),std::string(profile)=="ordered");
        b->sound->Dispose();EXPECT_TRUE(b->wave->getIsInUseProperty());
        EXPECT_EQ(other->getIsInUseProperty(),std::string(profile)=="ordered");bank->Dispose();
        EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}

TEST(HndXactCrossBank, CategoryOldestReplacesTheSolePlayingVictimAcrossSoundBanks)
{
    // admission normal positive, same-soundbank and clone: at400ms victim32,
    // incoming8; both dispatch one real wave; clone counts0/1 then0/0 atEOF.
    for(bool clone:{false,true})
    {
        SCOPED_TRACE(clone);auto b=CrossBanks("admission");auto first=CountCue(*b->sound,"ShapeCue");
        b->Tick(300);first->Play();PrefetchCurrentWave(*first,1024.f/32768);b->Tick(400);
        auto bank=clone?b->LoadSoundBank("clone.xsb",CrossSound("admission")):std::unique_ptr<SoundBank>{};
        auto second=CountCue(clone?*bank:*b->sound,"ShapeCue");second->Play();b->Tick(700);
        EXPECT_TRUE(first->getIsStoppedProperty());EXPECT_TRUE(second->getIsPlayingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*first,0),nullptr);PrefetchCurrentWave(*second,1024.f/32768);
        EXPECT_FLOAT_EQ(NumInstances(*first),clone?0.f:1.f);EXPECT_FLOAT_EQ(NumInstances(*second),1);
        (void)manualDevice->Render(50000);b->Tick(1900);EXPECT_TRUE(second->getIsStoppedProperty());
        EXPECT_FLOAT_EQ(NumInstances(*first),0);EXPECT_FLOAT_EQ(NumInstances(*second),0);
        first.reset();second.reset();EXPECT_TRUE(b->wave->getIsInUseProperty());b->sound->Dispose();
        EXPECT_EQ(b->wave->getIsInUseProperty(),clone);if(bank)bank->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
    }
}
