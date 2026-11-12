// SPDX-License-Identifier: MS-PL
// Included by the isolated offline harness after the scheduler fixtures.
// Oracle: genuine Microsoft XACT3.6 public count/cache probes, TQ-XACT-25.

Bytes InstanceCountBank(const char* profile,const char* extension)
{
    const auto path=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/
        "instance-count"/profile/(std::string("clock.")+extension);
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Missing SDK instance-count bank: "+path.string());
    return Bytes(std::istreambuf_iterator<char>(file),{});
}

std::unique_ptr<ScheduledBanks> InstanceCountBanks(const char* profile="unlimited")
{
    return std::make_unique<ScheduledBanks>(AuthoredTracks{},0,false,
        InstanceCountBank(profile,"xgs"),false,InstanceCountBank(profile,"xsb"),
        InstanceCountBank(profile,"xwb"));
}

std::unique_ptr<Cue> CountCue(SoundBank& bank,const char* name="ClockCue")
{ return std::unique_ptr<Cue>(bank.GetCue(name)); }

float NumInstances(const Cue& cue) { return cue.GetVariable("NumCueInstances"); }

TEST(XactInstanceCount, FreshPreparedCacheIsZeroUntilItsDefinitionCountChanges)
{
    auto b=InstanceCountBanks();auto observer=CountCue(*b->sound);
    EXPECT_FLOAT_EQ(NumInstances(*observer),0);observer->Play();
    EXPECT_FLOAT_EQ(NumInstances(*observer),1);
    auto later=CountCue(*b->sound);EXPECT_FLOAT_EQ(NumInstances(*later),0);
    b->Tick(150);EXPECT_FLOAT_EQ(NumInstances(*later),0); // native still0 after150ms DoWork
    auto disposable=CountCue(*b->sound);disposable->Dispose();
    EXPECT_FLOAT_EQ(NumInstances(*later),0); // unchanged count must not publish1
    auto foreign=CountCue(*b->sound,"OtherCue");foreign->Play();
    EXPECT_FLOAT_EQ(NumInstances(*foreign),1);EXPECT_FLOAT_EQ(NumInstances(*later),0);
    auto secondBank=b->LoadSoundBank("same-definition-second-bank.xsb",InstanceCountBank("unlimited","xsb"));
    auto second=CountCue(*secondBank);second->Play();
    EXPECT_FLOAT_EQ(NumInstances(*second),1);EXPECT_FLOAT_EQ(NumInstances(*observer),1);
    EXPECT_FLOAT_EQ(NumInstances(*later),0);
    observer->Pause();auto incoming=CountCue(*b->sound);incoming->Play();
    EXPECT_TRUE(observer->getIsPausedProperty());
    EXPECT_FLOAT_EQ(NumInstances(*observer),2);EXPECT_FLOAT_EQ(NumInstances(*later),2);
    EXPECT_FLOAT_EQ(NumInstances(*incoming),2);EXPECT_FLOAT_EQ(NumInstances(*foreign),1);
    EXPECT_FLOAT_EQ(NumInstances(*second),1);
    incoming->Stop(AudioStopOptions::Immediate);
    EXPECT_FLOAT_EQ(NumInstances(*incoming),1);EXPECT_FLOAT_EQ(NumInstances(*later),1);
    EXPECT_FLOAT_EQ(NumInstances(*observer),1);incoming->Dispose();
    observer->Resume();observer->Stop(AudioStopOptions::Immediate);
    EXPECT_FLOAT_EQ(NumInstances(*observer),0);EXPECT_FLOAT_EQ(NumInstances(*later),0);
    auto replacement=CountCue(*b->sound);replacement->Play();
    EXPECT_FLOAT_EQ(NumInstances(*observer),1); // stopped handles still receive publication
    EXPECT_FLOAT_EQ(NumInstances(*later),1);EXPECT_FLOAT_EQ(NumInstances(*foreign),1);
}

TEST(XactInstanceCount, StoppingTailStopsCountingBeforeItsAudioIsRetired)
{
    auto b=InstanceCountBanks("cue_two");auto peer=CountCue(*b->sound);auto live=CountCue(*b->sound);
    live->Play();EXPECT_FLOAT_EQ(NumInstances(*peer),1);
    live->Stop(AudioStopOptions::AsAuthored);b->Tick(20);
    ASSERT_TRUE(live->getIsStoppingProperty());
    EXPECT_FLOAT_EQ(NumInstances(*peer),0);EXPECT_FLOAT_EQ(NumInstances(*live),0);
    auto replacement=CountCue(*b->sound);replacement->Play();
    EXPECT_FLOAT_EQ(NumInstances(*peer),1);EXPECT_FLOAT_EQ(NumInstances(*live),1);
    auto fresh=CountCue(*b->sound);EXPECT_FLOAT_EQ(NumInstances(*fresh),0);
    b->Tick(51);EXPECT_TRUE(live->getIsStoppedProperty());
    EXPECT_FLOAT_EQ(NumInstances(*fresh),0); // end of an already-uncounted tail changes nothing
    EXPECT_FLOAT_EQ(NumInstances(*replacement),1);
}

TEST(XactInstanceCount, DirectDisposalAndNaturalCompletionPublishTheReducedCount)
{
    {
        auto b=InstanceCountBanks();auto peer=CountCue(*b->sound);auto live=CountCue(*b->sound);
        live->Play();EXPECT_FLOAT_EQ(NumInstances(*peer),1);live->Dispose();
        EXPECT_FLOAT_EQ(NumInstances(*peer),0);
    }
    {
        auto b=InstanceCountBanks("short");auto peer=CountCue(*b->sound);auto live=CountCue(*b->sound);
        live->Play();EXPECT_FLOAT_EQ(NumInstances(*peer),1);
        // SDK source2205 frames at22050Hz =100ms; offline device44100Hz.
        // Render past its4410 output frames and service the real completion controller.
        (void)manualDevice->Render(5000);b->Tick(500);
        EXPECT_FLOAT_EQ(NumInstances(*peer),0); // Update must publish before any live-state query
        EXPECT_TRUE(live->getIsStoppedProperty());
        EXPECT_FLOAT_EQ(NumInstances(*live),0);
    }
}

Bytes CountRpcXgs(std::uint8_t access=7,const char* name="NumCueInstances")
{
    // Full77-byte header, category77, variable87, two23-byte RPCs107/130.
    // Linear pitch curve x0->-600,x2->600 means count1->0,count2->600 cents.
    const auto nameEnd=161u+static_cast<std::uint32_t>(std::strlen(name))+1u;
    Bytes b;Text(b,"XGSF",4);U16(b,46);U16(b,42);U16(b,0);b.resize(18,0);U8(b,3);
    for(std::uint16_t n:{1,1,0,0,2,0,0})U16(b,n);
    for(std::uint32_t n:{77u,87u,0u,nameEnd,0u,nameEnd+6,153u,161u,107u,0u,0u})U32(b,n);
    U8(b,255);U16(b,0);U16(b,0);U8(b,0);U16(b,65535);U8(b,180);U8(b,0);
    U8(b,access);ValueF32(b,0);ValueF32(b,0);ValueF32(b,16);b.resize(107,0);
    for(std::uint16_t parameter:{0,1})
    {
        U16(b,0);U8(b,2);U16(b,parameter);
        ValueF32(b,0);ValueF32(b,parameter?-600.0f:0.0f);U8(b,0);
        ValueF32(b,2);ValueF32(b,parameter?600.0f:0.0f);U8(b,0);
    }
    Text(b,"Default",8);Text(b,name,std::strlen(name)+1);
    U32(b,153);U16(b,255);U32(b,161);U16(b,255);return b;
}

Bytes CountInteractiveXsb()
{
    // FAudio's interactive table: two12-byte simple sounds202/214,
    // 40-byte table226,15-byte complex cue266,name index281,string287.
    Bytes b;Text(b,"SDBK",4);U16(b,46);U16(b,43);U16(b,0);b.resize(18,0);U8(b,0);
    U16(b,0);U16(b,1);U16(b,0);U16(b,0);U8(b,1);U16(b,2);U16(b,0);U16(b,0);
    for(std::uint32_t n:{0xffffffffu,266u,0xffffffffu,0u,226u,0u,138u,0u,281u,202u})U32(b,n);
    Text(b,"CountInteractive",64);Text(b,"ScheduleWave",64);
    for(const std::int16_t pitch:{-300,300})
    { U8(b,0);U16(b,0);U8(b,180);U16(b,static_cast<std::uint16_t>(pitch));U8(b,0);U16(b,0);U16(b,0);U8(b,0); }
    U32(b,2u|(3u<<19));U16(b,0);U16(b,0); // Num variable0
    U32(b,202);ValueF32(b,0);ValueF32(b,1);U32(b,0);
    U32(b,214);ValueF32(b,2);ValueF32(b,16);U32(b,0);
    U8(b,0);U32(b,226);U32(b,0);U8(b,255);U16(b,0);U16(b,0);U8(b,0);
    U32(b,287);U16(b,0);Text(b,"Sequence",9);return b;
}

TEST(XactInstanceCount, RpcAndInteractiveReadersUseThePublishedCache)
{
    // SDK XACT Variables, Visibility: private variables remain available to XACT content.
    // This literal-fixture consumer check is not a native Microsoft private-variable run.
    for(const std::uint8_t access:{7u,6u})
    {
        const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(0,1,255)}};
        ScheduledBanks b(tracks,0,false,CountRpcXgs(access),true);
        auto first=b.Play();
        ASSERT_NE(CueTestAccess::ActiveInstance(*first,0),nullptr);
        if(access&1)EXPECT_FLOAT_EQ(NumInstances(*first),1);
        else EXPECT_THROW((void)NumInstances(*first),System::IndexOutOfRangeException);
        EXPECT_FLOAT_EQ(CueTestAccess::ActiveInstance(*first,0)->getPitchProperty(),0);
        auto second=b.Play();b.Tick(1);
        if(access&1)EXPECT_FLOAT_EQ(NumInstances(*first),2);
        for(std::size_t i=0;i<2;++i)
        {
            ASSERT_NE(CueTestAccess::ActiveInstance(*first,i),nullptr);
            EXPECT_FLOAT_EQ(CueTestAccess::ActiveInstance(*first,i)->getPitchProperty(),.5f);
        }
        second->Stop(AudioStopOptions::Immediate);b.Tick(2);
        EXPECT_FLOAT_EQ(CueTestAccess::ActiveInstance(*first,0)->getPitchProperty(),0);
    }
    for(const std::uint8_t access:{7u,6u})
    {
        ScheduledBanks b({},0,false,CountRpcXgs(access),false,CountInteractiveXsb());
        auto prepared=CountCue(*b.sound,"Sequence");auto first=CountCue(*b.sound,"Sequence");
        first->Play();auto second=CountCue(*b.sound,"Sequence");second->Play();
        if(access&1)EXPECT_FLOAT_EQ(NumInstances(*prepared),2);
        else EXPECT_THROW((void)NumInstances(*prepared),System::IndexOutOfRangeException);
        prepared->Play();
        ASSERT_NE(CueTestAccess::ActiveInstance(*prepared,0),nullptr);
        // Selection sees cached2 before this Play publishes3, so sound214's +300 cents wins.
        EXPECT_FLOAT_EQ(CueTestAccess::ActiveInstance(*prepared,0)->getPitchProperty(),.25f);
        if(access&1)EXPECT_FLOAT_EQ(NumInstances(*prepared),3);
    }
}

TEST(XactInstanceCount, PublicLookupRetainsVariableDomainAndDisposalValidation)
{
    // An inaccessible name is an invalid index: Framework Helpers.GetExceptionFromResult
    // IL_0189-0193 (XACTENGINE_E_INVALIDVARIABLEINDEX) gives IndexOutOfRangeException.
    for(const std::uint8_t access:{6u,3u}) // private cue; public global
    {
        ScheduledBanks b({{PlayAt(0,0,255)}},0,false,CountRpcXgs(access));auto cue=b.Play();
        EXPECT_THROW((void)NumInstances(*cue),System::IndexOutOfRangeException);
    }
    // A built-in name the project authors as a private cue variable is refused too; only a
    // project that does not author it at all falls back to the built-in default.
    {
        ScheduledBanks b({{PlayAt(0,0,255)}},0,false,CountRpcXgs(6,"Distance"));
        // XACT3DApply writes Distance through the same SetVariable and checks it
        // (Xact IL_0060-0063): Apply3D refuses too, before it positions the cue.
        std::unique_ptr<Cue> prepared(b.sound->GetCue("Sequence"));
        EXPECT_THROW(prepared->Apply3D(AudioListener{},AudioEmitter{}),System::IndexOutOfRangeException);
        prepared->Play();
        EXPECT_THROW(prepared->Apply3D(AudioListener{},AudioEmitter{}),System::InvalidOperationException)
            << "the refused Apply3D positioned the cue";
        auto cue=b.Play();
        EXPECT_THROW((void)cue->GetVariable("Distance"),System::IndexOutOfRangeException);
        EXPECT_THROW(cue->SetVariable("Distance",9.0f),System::IndexOutOfRangeException);
    }
    {
        // XACT3DApply writes Distance (IL_005b) before DopplerPitchScalar fails its check
        // (IL_008c-008f): the Distance write stays, and the cue is not positioned.
        ScheduledBanks b({{PlayAt(0,0,255)}},0,false,CountRpcXgs(6,"DopplerPitchScalar"));
        std::unique_ptr<Cue> prepared(b.sound->GetCue("Sequence"));
        AudioEmitter emitter;
        emitter.setPositionProperty(Microsoft::Xna::Framework::Vector3(5.0f,0.0f,0.0f));
        EXPECT_THROW(prepared->Apply3D(AudioListener{},emitter),System::IndexOutOfRangeException);
        EXPECT_FLOAT_EQ(prepared->GetVariable("Distance"),5.0f) << "the earlier Distance write was undone";
        prepared->Play();
        EXPECT_THROW(prepared->Apply3D(AudioListener{},emitter),System::InvalidOperationException);
    }
    ScheduledBanks absent({{PlayAt(0,0,255)}},0,false,CountRpcXgs(7,"OtherVariable"));
    auto cue=absent.Play();EXPECT_THROW((void)NumInstances(*cue),System::IndexOutOfRangeException);
    auto valid=InstanceCountBanks();auto disposed=CountCue(*valid->sound);disposed->Play();disposed->Dispose();
    EXPECT_THROW((void)NumInstances(*disposed),System::ObjectDisposedException);
    valid->sound->Dispose();
}

TEST(XactInstanceCount, InstanceLimitFadeRetainsPlayingPausedStateAndCountUntilRetirement)
{
    // Genuine Microsoft3.6 x86/x64 OLDEST controls: a victim remains8/72
    // (Playing/Playing|Paused), count2 at20ms, then32/count1 after its fade.
    // Linear fade factors and inclusive deadlines also follow FACT_INTERNAL_UpdateSound.
    for(const char* profile:{"cue_oldest","cat_oldest"})
    for(bool paused:{false,true})
    {
        SCOPED_TRACE(profile);
        SCOPED_TRACE(paused);
        const unsigned fade=std::strcmp(profile,"cue_oldest")==0?50:70;
        auto b=InstanceCountBanks(profile);auto live=CountCue(*b->sound);live->Play();b->Tick(100);
        auto* voice=CueTestAccess::ActiveInstance(*live,0);ASSERT_NE(voice,nullptr);
        const float originalGain=voice->getVolumeProperty();ASSERT_GT(originalGain,0);
        if(paused)live->Pause();
        auto incoming=CountCue(*b->sound);incoming->Play();b->Tick(120);
        EXPECT_TRUE(live->getIsPlayingProperty());EXPECT_FALSE(live->getIsStoppingProperty());
        EXPECT_EQ(live->getIsPausedProperty(),paused);
        EXPECT_FLOAT_EQ(NumInstances(*live),2);EXPECT_FLOAT_EQ(NumInstances(*incoming),2);
        EXPECT_NEAR(voice->getVolumeProperty(),originalGain*(1.0f-20.0f/fade),1e-6f);
        b->engine->GetCategory("Default").SetVolume(.5f);
        EXPECT_NEAR(voice->getVolumeProperty(),originalGain*.5f*(1.0f-20.0f/fade),1e-6f);
        b->Tick(100+fade);
        EXPECT_TRUE(live->getIsStoppedProperty());EXPECT_FALSE(live->getIsPausedProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*live,0),nullptr);
        EXPECT_FLOAT_EQ(NumInstances(*live),1);EXPECT_FLOAT_EQ(NumInstances(*incoming),1);
    }
}

TEST(XactInstanceCount, InstanceLimitFadeCancelsSdkEventsAtItsDeadline)
{
    // FACT_INTERNAL_UpdateSound handles an expired fade before dispatching events.
    // The untouched SDK's first marker is at500ms; arrange the eviction deadline there.
    for(const char* profile:{"cue_oldest","cat_oldest"})
    {
        SCOPED_TRACE(profile);const unsigned fade=std::strcmp(profile,"cue_oldest")==0?50:70;
        auto b=InstanceCountBanks(profile);auto live=CountCue(*b->sound);
        CueTestAccess::ObserveDispatch(*live,[&](std::size_t t,std::size_t e,uint64_t elapsed)
        { b->trace.emplace_back(t,e,elapsed); });
        live->Play();ASSERT_EQ(b->trace,(std::vector<Dispatch>{{0,0,0}}));
        b->Tick(500-fade);auto incoming=CountCue(*b->sound);incoming->Play();
        b->Tick(499);EXPECT_TRUE(live->getIsPlayingProperty());
        b->Tick(500);EXPECT_TRUE(live->getIsStoppedProperty());
        EXPECT_EQ(b->trace,(std::vector<Dispatch>{{0,0,0}}));
        EXPECT_EQ(CueTestAccess::PendingEvents(*live),0u);
        EXPECT_FLOAT_EQ(NumInstances(*incoming),1);
    }
}

TEST(XactInstanceCount, ConsecutiveEvictionsDoNotRestartAnEarlierVictimsFade)
{
    // FACT handle_instance_limit excludes a victim once BeginFadeOut sets its
    // STOPPING bit. CNA retains Microsoft's public Playing state, so the private
    // fade marker must retain that exclusion. No native overlap timing is claimed.
    for(const char* profile:{"cue_oldest","cat_oldest"})
    {
        SCOPED_TRACE(profile);const unsigned fade=std::strcmp(profile,"cue_oldest")==0?50:70;
        auto b=InstanceCountBanks(profile);auto first=CountCue(*b->sound);first->Play();
        b->Tick(100);auto second=CountCue(*b->sound);second->Play();
        b->Tick(110);auto third=CountCue(*b->sound);third->Play();
        EXPECT_FLOAT_EQ(NumInstances(*third),3);
        b->Tick(100+fade);
        EXPECT_TRUE(first->getIsStoppedProperty());EXPECT_TRUE(second->getIsPlayingProperty());
        EXPECT_FALSE(second->getIsStoppingProperty());EXPECT_FLOAT_EQ(NumInstances(*third),2);
        b->Tick(110+fade);
        EXPECT_TRUE(second->getIsStoppedProperty());EXPECT_TRUE(third->getIsPlayingProperty());
        EXPECT_FLOAT_EQ(NumInstances(*third),1);
    }
}

TEST(XactInstanceCount, WaveStartingDuringInstanceLimitFadeUsesTheSameRemainingGain)
{
    // FACT UpdateSound applies the current fadeVolume to every wave, including
    // one dispatched during the fade. Both authored waves have identical gain.
    const AuthoredTracks tracks{{PlayAt(0,0,255),PlayAt(40,1,255)}};
    auto sound=ScheduleXsb(tracks,50);
    // The literal writer ends with a15-byte cue,6-byte name index and9-byte name.
    const auto cueOffset=sound.size()-30;
    sound.at(cueOffset+9)=1;sound.at(cueOffset+14)=2u<<3; // maximum1, OLDEST
    ScheduledBanks b(tracks,50,false,CountRpcXgs(),false,sound);
    auto first=b.Play();
    auto* initial=CueTestAccess::ActiveInstance(*first,0);ASSERT_NE(initial,nullptr);
    const float initialGain=initial->getVolumeProperty();ASSERT_GT(initialGain,0);
    bool sawLateWave=false;
    CueTestAccess::ObserveDispatch(*first,[&](std::size_t,std::size_t event,uint64_t)
    {
        if(event!=1)return;
        auto* voice=CueTestAccess::ActiveInstance(*first,1);ASSERT_NE(voice,nullptr);
        // Observe before ReconcileState can repair a transient unfaded start:
        // at40ms the50ms fade started at10ms has20ms left, so gain is0.4 of baseline.
        EXPECT_NEAR(voice->getVolumeProperty(),initialGain*.4f,1e-6f);
        sawLateWave=true;
    });
    b.Tick(10);auto incoming=b.Play();b.Tick(40);EXPECT_TRUE(sawLateWave);
    ASSERT_TRUE(first->getIsPlayingProperty());
    auto* earlier=CueTestAccess::ActiveInstance(*first,0);
    auto* later=CueTestAccess::ActiveInstance(*first,1);
    ASSERT_NE(earlier,nullptr);ASSERT_NE(later,nullptr);
    EXPECT_GT(earlier->getVolumeProperty(),0);
    EXPECT_FLOAT_EQ(later->getVolumeProperty(),earlier->getVolumeProperty());
    b.Tick(60);EXPECT_TRUE(first->getIsStoppedProperty());
}

TEST(XactInstanceCount, AuthoredStoppingTailReleasesCueAndCategoryAdmissionCapacity)
{
    // Microsoft3.6 cue/category FAIL maximum1 controls: after a settled authored
    // stop, count0 permits I.Prepare/Play even in snapshots where L remains16.
    for(const char* profile:{"cue_fail","cat_fail_tail"})
    {
        SCOPED_TRACE(profile);auto b=InstanceCountBanks(profile);
        auto prepared=CountCue(*b->sound,"OtherCue");auto live=CountCue(*b->sound);
        live->Play();live->Stop(AudioStopOptions::AsAuthored);b->Tick(20);
        ASSERT_TRUE(live->getIsStoppingProperty());EXPECT_FLOAT_EQ(NumInstances(*live),0);
        auto incoming=CountCue(*b->sound);incoming->Play();
        EXPECT_TRUE(incoming->getIsPlayingProperty());
        EXPECT_NE(CueTestAccess::ActiveInstance(*incoming,0),nullptr);
        EXPECT_FLOAT_EQ(NumInstances(*incoming),1);EXPECT_FLOAT_EQ(NumInstances(*live),1);
        EXPECT_FLOAT_EQ(NumInstances(*prepared),0);
        b->Tick(50);EXPECT_TRUE(live->getIsStoppedProperty());
        EXPECT_TRUE(incoming->getIsPlayingProperty());EXPECT_FLOAT_EQ(NumInstances(*incoming),1);
        EXPECT_FLOAT_EQ(NumInstances(*prepared),0);
    }
}

TEST(XactInstanceCount, AuthoredTailKeepsBankOwnershipBeyondRetirement)
{
    // Microsoft3.6 x86/x64 bank snapshots: cue16/count0 still owns states128/132.
    // Keep this cue alone, so another prepared/playing handle cannot mask the defect.
    for(const char* profile:{"cue_fail","cat_fail_tail"})
    {
        SCOPED_TRACE(profile);auto b=InstanceCountBanks(profile);
        EXPECT_FALSE(b->sound->getIsInUseProperty());EXPECT_FALSE(b->wave->getIsInUseProperty());
        auto live=CountCue(*b->sound);live->Play();
        ASSERT_TRUE(b->sound->getIsInUseProperty());ASSERT_TRUE(b->wave->getIsInUseProperty());
        live->Stop(AudioStopOptions::AsAuthored);b->Tick(20);
        ASSERT_TRUE(live->getIsStoppingProperty());EXPECT_FLOAT_EQ(NumInstances(*live),0);
        auto* voice=CueTestAccess::ActiveInstance(*live,0);ASSERT_NE(voice,nullptr);
        EXPECT_EQ(voice->getStateProperty(),SoundState::Playing);EXPECT_GT(voice->getVolumeProperty(),0);
        EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        b->SetTime(49); // Ownership is independent of the cue state reconciliation.
        EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        b->SetTime(50);
        EXPECT_TRUE(b->sound->getIsInUseProperty());EXPECT_TRUE(b->wave->getIsInUseProperty());
        EXPECT_TRUE(live->getIsStoppedProperty());EXPECT_FLOAT_EQ(NumInstances(*live),0);
    }
}
