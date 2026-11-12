// SPDX-License-Identifier: MS-PL
// HND-XACT-01: native3.6 x86/x64, matrix-preparation.json, 104 cells.
// Cell IDs, exact probe/DLL identities and scope: preparation-shapes/PROVENANCE.json.
// No timing/RNG/PCM expectation is derived from CNA output.

Bytes PreparationShapeFile(const char* profile,const char* file)
{
    const auto path=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/
        "preparation-shapes"/profile/file;
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Missing native preparation input: "+path.string());
    return Bytes(std::istreambuf_iterator<char>(in),{});
}

std::unique_ptr<ScheduledBanks> PreparationShapeBanks(const char* profile,bool loadA=true)
{
    auto b=std::make_unique<ScheduledBanks>(AuthoredTracks{},0,false,
        PreparationShapeFile(profile,"shapes.xgs"),false,PreparationShapeFile(profile,"shapes.xsb"),
        PreparationShapeFile(profile,"A.xwb"),std::string(profile)=="streaming",false);
    if(!loadA)b->wave.reset();
    return b;
}

std::unique_ptr<WaveBank> PreparationShapeB(ScheduledBanks& b,const char* profile)
{
    const auto path=CNA::TestSupport::FixtureRoot()/"hnd-xact-preparation-B.xwb";
    const auto bytes=PreparationShapeFile(profile,"B.xwb");
    {std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
     if(!out)throw std::runtime_error("Cannot materialize preparation B");}
    // Streaming constructor uses the same native streaming bank, not resident decoding.
    return std::string(profile)=="streaming" ? std::make_unique<WaveBank>(b.engine.get(),path.string(),0,0)
        :std::make_unique<WaveBank>(b.engine.get(),path.string());
}

TEST(HndXactPreparation, OrdinaryResidentAndStreamingOwnOnlyUsedBankBeforePlaybackAndAfterCueDestruction)
{
    // x86/x64 resident/normal and streaming/{normal,no-props,missingB}, io0/1.
    for(const char* profile:{"resident","streaming"})
    {
        SCOPED_TRACE(profile);auto b=PreparationShapeBanks(profile);auto unused=PreparationShapeB(*b,profile);
        auto cue=CountCue(*b->sound,"ShapeCue");
        EXPECT_TRUE(cue->getIsPreparedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
        EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        EXPECT_FALSE(unused->getIsInUseProperty());EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        b->Tick(300);EXPECT_TRUE(cue->getIsPreparedProperty());ExpectSilence(manualDevice->Render(64));
        cue->Play();ASSERT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0)->getStateProperty(),SoundState::Playing);
        EXPECT_FLOAT_EQ(NumInstances(*cue),1);
        // Real offline EOF, not a sleep or a native700ms output-duration claim.
        (void)manualDevice->Render(50000);b->Tick(4800);
        EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
        EXPECT_TRUE(b->sound->getIsInUseProperty());cue.reset();b->Tick(6800);
        EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        EXPECT_FALSE(unused->getIsInUseProperty());b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
    }
}

TEST(HndXactPreparation, WeightedPreparationCachesOneSelectedBankAndDoesNotRerollAtPlay)
{
    // x86/x64 weighted/{normal,no-props}, repeats0..7: both alternatives observed,
    // each retains exactly its selected bank before Play, then plays that same bank.
    bool sawA=false,sawB=false;
    for(unsigned seed=0;seed<8;++seed)
    {
        auto b=PreparationShapeBanks("weighted");auto other=PreparationShapeB(*b,"weighted");
        CueTestAccess::SeedRng(seed);auto cue=CountCue(*b->sound,"ShapeCue");
        const bool a=b->wave->getIsInUseProperty(),bb=other->getIsInUseProperty();
        ASSERT_NE(a,bb);sawA|=a;sawB|=bb;
        EXPECT_TRUE(cue->getIsPreparedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);b->Tick(300);
        ExpectSilence(manualDevice->Render(64));CueTestAccess::SeedRng(seed+99);cue->Play();
        ASSERT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0)->getStateProperty(),SoundState::Playing);
        EXPECT_EQ(b->wave->getIsInUseProperty(),a);EXPECT_EQ(other->getIsInUseProperty(),bb);
        cue->Stop(AudioStopOptions::Immediate);EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_TRUE(b->sound->getIsInUseProperty());cue.reset();b->Tick(2300);
        EXPECT_FALSE(b->sound->getIsInUseProperty());
        EXPECT_EQ(b->wave->getIsInUseProperty(),a);EXPECT_EQ(other->getIsInUseProperty(),bb);
        b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
    // Coverage of both authored alternatives, no Microsoft RNG sequence/probability claim.
    EXPECT_TRUE(sawA);EXPECT_TRUE(sawB);
}

TEST(HndXactPreparation, WeightedMissingSelectedBankFailsPrepareWhileUnusedMissingBankIsAllowed)
{
    // weighted/missingA and missingB native cells include both NoWaveBank8ac70013
    // and successful playback of the loaded alternative. Selection must not fall back.
    for(bool loadA:{false,true})
    {
        bool success=false,refused=false;
        for(unsigned seed=0;seed<8;++seed)
        {
            auto b=PreparationShapeBanks("weighted",loadA);
            auto other=loadA?std::unique_ptr<WaveBank>{}:PreparationShapeB(*b,"weighted");
            CueTestAccess::SeedRng(seed);std::unique_ptr<Cue> cue;
            try {cue=CountCue(*b->sound,"ShapeCue");}
            catch(const System::InvalidOperationException& error)
            {
                refused=true;EXPECT_EQ(error.getMessageProperty(),"No wavebank exists for the requested operation.");
                EXPECT_FALSE(b->sound->getIsInUseProperty());
                EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),0u);
                EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b->engine),0u);
                EXPECT_FALSE(loadA?b->wave->getIsInUseProperty():other->getIsInUseProperty());continue;
            }
            success=true;ASSERT_NE(cue,nullptr);EXPECT_TRUE(cue->getIsPreparedProperty());cue->Play();
            ASSERT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);
            EXPECT_TRUE(loadA?b->wave->getIsInUseProperty():other->getIsInUseProperty());
        }
        EXPECT_TRUE(success);EXPECT_TRUE(refused);
    }
}

TEST(HndXactPreparation, StreamingMissingRequiredBankFailsPrepareWithoutLeavingACue)
{
    // x86/x64 streaming/missingA rc4/8ac70013; unused B stays4.
    auto b=PreparationShapeBanks("streaming",false);auto other=PreparationShapeB(*b,"streaming");
    EXPECT_THROW((void)CountCue(*b->sound,"ShapeCue"),System::InvalidOperationException);
    EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*b->sound),0u);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b->engine),0u);
}

TEST(HndXactPreparation, InteractiveDefersResourcesAndUsesTheVariableValueBeforePlay)
{
    // x86/x64 interactive/{normal,no-props,AtoB,BtoA}: Prepare4/SB128/WBs4/4;
    // Play-selected bank132, other4. Wave STOP/DESTROY precede cue Playing at4500ms.
    for(bool chooseB:{false,true})
    {
        auto b=PreparationShapeBanks("interactive");auto other=PreparationShapeB(*b,"interactive");
        b->engine->SetGlobalVariable("Branch",chooseB?0.f:1.f);auto cue=CountCue(*b->sound,"ShapeCue");
        EXPECT_TRUE(cue->getIsPreparedProperty());EXPECT_TRUE(b->sound->getIsInUseProperty());
        EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
        b->Tick(300);ExpectSilence(manualDevice->Render(64));
        b->engine->SetGlobalVariable("Branch",chooseB?1.f:0.f);cue->Play();b->Tick(400);
        ASSERT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0)->getStateProperty(),SoundState::Playing);
        EXPECT_EQ(b->wave->getIsInUseProperty(),!chooseB);EXPECT_EQ(other->getIsInUseProperty(),chooseB);
        (void)manualDevice->Render(50000);b->Tick(4800);
        EXPECT_TRUE(cue->getIsPlayingProperty());EXPECT_FALSE(cue->getIsStoppedProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);EXPECT_FLOAT_EQ(NumInstances(*cue),1);
        cue->Stop(AudioStopOptions::Immediate);b->Tick(4900);EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FLOAT_EQ(NumInstances(*cue),0);EXPECT_TRUE(b->sound->getIsInUseProperty());
        cue.reset();b->Tick(6900);EXPECT_FALSE(b->sound->getIsInUseProperty());
        EXPECT_EQ(b->wave->getIsInUseProperty(),!chooseB);EXPECT_EQ(other->getIsInUseProperty(),chooseB);
        b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}

TEST(HndXactPreparation, InteractiveMissingBankPlaySucceedsSilentlyWithoutRetryingAfterLateLoad)
{
    // x86/x64 interactive/missingA,missingB,lateB-playing: native Play0/cue8/count1,
    // no WAVEPLAY or markers through4500ms, even when B loads100ms after Play.
    for(bool missingB:{false,true})
    {
        auto b=PreparationShapeBanks("interactive",missingB);
        auto other=missingB?std::unique_ptr<WaveBank>{}:PreparationShapeB(*b,"interactive");
        b->engine->SetGlobalVariable("Branch",missingB?1.f:0.f);auto cue=CountCue(*b->sound,"ShapeCue");
        unsigned dispatches=0;CueTestAccess::ObserveDispatch(*cue,[&](auto,auto,auto){++dispatches;});
        EXPECT_TRUE(cue->getIsPreparedProperty());b->Tick(300);EXPECT_NO_THROW(cue->Play());
        EXPECT_TRUE(cue->getIsPlayingProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),1);
        // Refresh Cue.Play IL_000c-001c sets played after native success; Apply3D's
        // existing played guard must therefore take its error path for this successful Play.
        EXPECT_THROW(cue->Apply3D(AudioListener{},AudioEmitter{}),System::InvalidOperationException);
        b->Tick(400);
        if(missingB)other=PreparationShapeB(*b,"interactive");
        b->Tick(700);b->Tick(4800);EXPECT_TRUE(cue->getIsPlayingProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);EXPECT_EQ(dispatches,0u);
        ExpectSilence(manualDevice->Render(128));EXPECT_FALSE(other->getIsInUseProperty());
        if(b->wave)EXPECT_FALSE(b->wave->getIsInUseProperty());
        EXPECT_FLOAT_EQ(NumInstances(*cue),1);cue->Stop(AudioStopOptions::Immediate);b->Tick(4900);
        EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_TRUE(b->sound->getIsInUseProperty());
        cue.reset();EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(other->getIsInUseProperty());
    }
}

TEST(HndXactPreparation, InteractiveBankLoadedAfterPrepareBeforePlayCanBeSelected)
{
    // x86/x64 interactive/lateB: Prepare accepts absent B; loading it before Play
    // permits real B-wave playback and sound-bank-owned B retention.
    auto b=PreparationShapeBanks("interactive");b->engine->SetGlobalVariable("Branch",1);
    auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);auto other=PreparationShapeB(*b,"interactive");
    EXPECT_FALSE(other->getIsInUseProperty());cue->Play();b->Tick(400);
    ASSERT_NE(CueTestAccess::ActiveInstance(*cue,0),nullptr);EXPECT_TRUE(other->getIsInUseProperty());
    EXPECT_FALSE(b->wave->getIsInUseProperty());cue.reset();EXPECT_TRUE(other->getIsInUseProperty());
    b->sound->Dispose();EXPECT_FALSE(other->getIsInUseProperty());
}

TEST(HndXactPreparation, InteractiveOtherPreparationErrorsStillPropagateWithoutSettingPlayed)
{
    // A source-derived unsupported DSP control distinguishes the measured missing-
    // bank success from swallowing arbitrary errors. No native DSP claim is made.
    auto b=PreparationShapeBanks("interactive",false);
    SoundBankTestAccess::SetUnsupportedDspChain(*b->sound);
    auto cue=CountCue(*b->sound,"ShapeCue");
    EXPECT_THROW(cue->Play(),System::InvalidOperationException);
    EXPECT_TRUE(cue->getIsPreparedProperty());EXPECT_FLOAT_EQ(NumInstances(*cue),0);
    EXPECT_NO_THROW(cue->Apply3D(AudioListener{},AudioEmitter{}));
    EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
}

TEST(HndXactPreparation, NeverPlayedInteractiveAndStreamingHandlesRetainDifferentResources)
{
    // x86/x64 {interactive,streaming}/{unplayed-stop,unplayed-destroy}.
    // Immediate Stop has asynchronous16->32; only the settled100ms observation is used.
    for(const char* profile:{"interactive","streaming"})for(bool stop:{false,true})
    {
        auto b=PreparationShapeBanks(profile);auto other=PreparationShapeB(*b,profile);
        auto cue=CountCue(*b->sound,"ShapeCue");b->Tick(300);
        const bool retains=std::string(profile)=="streaming";
        EXPECT_EQ(b->wave->getIsInUseProperty(),retains);EXPECT_FALSE(other->getIsInUseProperty());
        if(stop){cue->Stop(AudioStopOptions::Immediate);b->Tick(400);EXPECT_TRUE(cue->getIsStoppedProperty());
            EXPECT_TRUE(b->sound->getIsInUseProperty());}
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);ExpectSilence(manualDevice->Render(64));
        cue.reset();b->Tick(2400);EXPECT_FALSE(b->sound->getIsInUseProperty());
        EXPECT_EQ(b->wave->getIsInUseProperty(),retains);b->sound->Dispose();EXPECT_FALSE(b->wave->getIsInUseProperty());
    }
}

TEST(HndXactPreparation, AuthoredBuiltInCueVariablesKeepTheirInitialValueRangeAndReadOnlyFlag)
{
    // The XACT tool authors DopplerPitchScalar (initial 1, range [0,4]) and a read-only AttackTime
    // into every project's .xgs, this genuine one included. The cue API reads and writes that
    // authored variable (FAudio FACT.c:2559-2616, the policy CNA already applies to every other
    // authored cue variable); a hand-made project that lacks the name keeps the built-in 0.
    auto b=PreparationShapeBanks("resident");
    auto cue=CountCue(*b->sound,"ShapeCue");
    EXPECT_FLOAT_EQ(cue->GetVariable("DopplerPitchScalar"),1.0f);
    cue->SetVariable("DopplerPitchScalar",99.0f);
    EXPECT_FLOAT_EQ(cue->GetVariable("DopplerPitchScalar"),4.0f);
    cue->SetVariable("AttackTime",5.0f);
    EXPECT_FLOAT_EQ(cue->GetVariable("AttackTime"),0.0f);
}
