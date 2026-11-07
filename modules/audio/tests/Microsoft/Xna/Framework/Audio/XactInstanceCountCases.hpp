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
        else EXPECT_THROW((void)NumInstances(*first),System::InvalidOperationException);
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
        else EXPECT_THROW((void)NumInstances(*prepared),System::InvalidOperationException);
        prepared->Play();
        ASSERT_NE(CueTestAccess::ActiveInstance(*prepared,0),nullptr);
        // Selection sees cached2 before this Play publishes3, so sound214's +300 cents wins.
        EXPECT_FLOAT_EQ(CueTestAccess::ActiveInstance(*prepared,0)->getPitchProperty(),.25f);
        if(access&1)EXPECT_FLOAT_EQ(NumInstances(*prepared),3);
    }
}

TEST(XactInstanceCount, PublicLookupRetainsVariableDomainAndDisposalValidation)
{
    for(const std::uint8_t access:{6u,3u}) // private cue; public global
    {
        ScheduledBanks b({{PlayAt(0,0,255)}},0,false,CountRpcXgs(access));auto cue=b.Play();
        EXPECT_THROW((void)NumInstances(*cue),System::InvalidOperationException);
    }
    ScheduledBanks absent({{PlayAt(0,0,255)}},0,false,CountRpcXgs(7,"OtherVariable"));
    auto cue=absent.Play();EXPECT_THROW((void)NumInstances(*cue),System::InvalidOperationException);
    auto valid=InstanceCountBanks();auto disposed=CountCue(*valid->sound);disposed->Play();disposed->Dispose();
    EXPECT_THROW((void)NumInstances(*disposed),System::ObjectDisposedException);
    valid->sound->Dispose();
}
