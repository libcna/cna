// SPDX-License-Identifier: MS-PL
// Included by the isolated offline harness after its value-event helpers.

// Hand oracle: a(b)=10^((3969*log10(b/28240)+8715)/2000).
// Sound, track and explicitly applied category byte255 give G=a(255)^3.
// FACT UpdateSound clamps the final product only in FACTWave_SetVolume, to2^24.
constexpr float PositiveBaseGain=7.972857796926328f;
constexpr float PositiveSecondEvent=.7943282347242815f; // -200cB

Bytes PositiveSettings(bool release=false,float lastY=1000)
{
    auto b=ReleaseRpcXgs(); b.at(85)=255;
    if (!release) std::memcpy(b.data()+161,"Volume",7);
    ValueU32At(b,96,release ? 0x42c80000u : 0x3f800000u); // max100 or1
    ValueU32At(b,121,release ? 0x42c80000u : 0x3f800000u); // volume curve last x
    uint32_t bits;std::memcpy(&bits,&lastY,4);ValueU32At(b,125,bits);
    return b; // neutral pitch curve; original six-byte name indexes stay valid
}
Bytes PositiveWaves()
{
    auto b=ScheduleXwb();
    // Mono S16 markers8/32768,16/32768,24/32768 keep mixed samples below1.
    for (std::size_t wave=0;wave<3;++wave)
    for (std::size_t frame=0;frame<441;++frame)
    {
        const auto offset=216+wave*882+frame*2;
        b.at(offset)=static_cast<uint8_t>(8*(wave+1));b.at(offset+1)=0;
    }
    return b;
}
Bytes PositiveSound(const AuthoredTracks& tracks,uint16_t fade=0,bool rpc=false,bool trackRpc=false)
{
    auto b=ScheduleXsb(tracks,fade,rpc,trackRpc);b.at(205)=255;
    const auto rpcBytes=rpc || trackRpc ? 2+(rpc?9:0)+(trackRpc?tracks.size()*9:0) : 0;
    for (std::size_t i=0;i<tracks.size();++i)b.at(212+rpcBytes+i*9)=255;
    return b;
}
void ExpectPositiveVoice(Cue& cue,std::size_t index,float expected)
{
    auto* instance=CueTestAccess::ActiveInstance(cue,index);ASSERT_NE(instance,nullptr);
    const auto found=voiceReceipts.find(SoundEffectInstanceTestAccess::MixerHandle(*instance));
    ASSERT_NE(found,voiceReceipts.end());
    //2ppm accommodates float centibel/pow rounding even for the2^24 range.
    EXPECT_NEAR(found->second.gain,expected,std::max(1e-5f,std::abs(expected)*2e-6f));
    EXPECT_FLOAT_EQ(found->second.ratio,1);
}
void ExpectPositivePcm(const std::vector<float>& pcm,float expected)
{
    ASSERT_FALSE(pcm.empty());
    //2ppm plus a tiny absolute floor covers float conversion/mixing, including small master gain.
    const float tolerance=std::max(1e-10f,std::abs(expected)*2e-6f);
    for(std::size_t i=0;i<pcm.size();++i)ASSERT_NEAR(pcm[i],expected,tolerance)<<i;
}
void ExpectPositivePair(Cue& cue,float first,float second)
{
    ExpectPositiveVoice(cue,0,first);ExpectPositiveVoice(cue,1,second);
    ExpectPositivePcm(manualDevice->Render(32),(first+2*second)/4096);
}

TEST(XactPositiveGain, StartUpdatesAndCategoryKeepGainAboveUnityWithStrictPublicSetter)
{
    for (bool rpc : {false,true})
    {
        SCOPED_TRACE(rpc);
        const AuthoredTracks tracks{{EquationAt(8,0,4,0),PlayAt(0,0,255)},
            {EquationAt(8,0,4,-200),PlayAt(0,1,255)}};
        ScheduledBanks b(tracks,0,false,PositiveSettings(),false,PositiveSound(tracks,0,rpc),PositiveWaves());
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
        unsigned starts=0;
        CueTestAccess::ObserveDispatch(*cue,[&](std::size_t track,std::size_t event,uint64_t)
        {
            if(event!=1)return;
            ++starts;
            ExpectPositiveVoice(*cue,track,PositiveBaseGain*(track?PositiveSecondEvent:1));
            const float pcm=PositiveBaseGain*(track?1+2*PositiveSecondEvent:1)/4096;
            ExpectPositivePcm(manualDevice->Render(16),pcm); // before reconciliation
        });
        cue->Play();EXPECT_EQ(starts,2u);
        auto* voice=CueTestAccess::ActiveInstance(*cue,0);ASSERT_NE(voice,nullptr);
        // KF-12 remains the public XNA command contract, including on a Cue-owned voice.
        EXPECT_THROW(voice->setVolumeProperty(2.0f),System::ArgumentOutOfRangeException);
        EXPECT_THROW(voice->setVolumeProperty(-.1f),System::ArgumentOutOfRangeException);
        EXPECT_THROW(voice->setVolumeProperty(std::numeric_limits<float>::quiet_NaN()),System::ArgumentOutOfRangeException);
        ExpectPositivePair(*cue,PositiveBaseGain,PositiveBaseGain*PositiveSecondEvent);
        b.Tick(5);ExpectPositivePair(*cue,PositiveBaseGain,PositiveBaseGain*PositiveSecondEvent);
        const float rpcGain=rpc?3.1622776601683793f:1.0f; // +1000cB
        if(rpc)cue->SetVariable("Volume",1);
        b.Tick(10);ExpectPositivePair(*cue,PositiveBaseGain*rpcGain,PositiveBaseGain*rpcGain*PositiveSecondEvent);
        auto category=b.engine->GetCategory("Default");category.SetVolume(.5f);
        ExpectPositivePair(*cue,PositiveBaseGain*rpcGain*.5f,PositiveBaseGain*rpcGain*PositiveSecondEvent*.5f);
        b.Tick(20);ExpectPositivePair(*cue,PositiveBaseGain*rpcGain*.5f,PositiveBaseGain*rpcGain*PositiveSecondEvent*.5f);
        category.SetVolume(1);
        ExpectPositivePair(*cue,PositiveBaseGain*rpcGain,PositiveBaseGain*rpcGain*PositiveSecondEvent);
        if(rpc)cue->SetVariable("Volume",0);
        b.Tick(30);ExpectPositivePair(*cue,PositiveBaseGain,PositiveBaseGain*PositiveSecondEvent);
    }
}

TEST(XactPositiveGain, FadeInAndSettlePreserveUnclampedGainAtDelayedDispatch)
{
    const AuthoredTracks tracks{{EquationAt(8,0,4,0),PlayAt(0,0,255)},
        {EquationAt(8,0,4,-200),PlayAt(40,1,255)}};
    auto sound=PositiveSound(tracks);const auto cueOffset=sound.size()-30;
    sound.at(cueOffset+9)=1;sound.at(cueOffset+10)=100;sound.at(cueOffset+14)=0x10;
    ScheduledBanks b(tracks,0,false,PositiveSettings(),false,sound,PositiveWaves());
    auto victim=b.Play();std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
    unsigned delayed=0;
    CueTestAccess::ObserveDispatch(*cue,[&](std::size_t track,std::size_t event,uint64_t elapsed)
    {
        if(track!=1 || event!=1)return;
        ++delayed;EXPECT_EQ(elapsed,40u);
        ExpectPositivePair(*cue,PositiveBaseGain*.3f,PositiveBaseGain*PositiveSecondEvent*.3f);
    });
    b.SetTime(10);cue->Play();victim->Stop(AudioStopOptions::Immediate);
    ExpectPositiveVoice(*cue,0,0);
    b.Tick(35);ExpectPositiveVoice(*cue,0,PositiveBaseGain*.25f);
    auto category=b.engine->GetCategory("Default");category.SetVolume(.75f);
    ExpectPositiveVoice(*cue,0,PositiveBaseGain*.25f*.75f);
    b.SetTime(50);category.SetVolume(.75f);b.Tick(50);EXPECT_EQ(delayed,1u);
    ExpectPositivePair(*cue,PositiveBaseGain*.3f,PositiveBaseGain*PositiveSecondEvent*.3f);
    b.Tick(109);ExpectPositivePair(*cue,PositiveBaseGain*.99f*.75f,PositiveBaseGain*PositiveSecondEvent*.99f*.75f);
    b.Tick(110);ExpectPositivePair(*cue,PositiveBaseGain*.75f,PositiveBaseGain*PositiveSecondEvent*.75f);
    b.Tick(120);ExpectPositivePair(*cue,PositiveBaseGain*.75f,PositiveBaseGain*PositiveSecondEvent*.75f);
}

TEST(XactPositiveGain, AuthoredFadeAndRpcReleaseRetainTheirPositiveGain)
{
    for (bool authored : {false,true})
    {
        SCOPED_TRACE(authored);
        const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(0,1,255)}};
        ScheduledBanks b(tracks,0,false,PositiveSettings(true),false,
            PositiveSound(tracks,authored?100:0,false,true),PositiveWaves());
        auto cue=b.Play();ExpectPositivePair(*cue,PositiveBaseGain,PositiveBaseGain);
        b.Tick(20);cue->Stop(AudioStopOptions::AsAuthored);
        const auto factor=[&](float age) {return authored?1-age/100:std::pow(10.0f,age/200);};
        b.Tick(45);ExpectPositivePair(*cue,PositiveBaseGain*factor(25),PositiveBaseGain*factor(25));
        b.engine->GetCategory("Default").SetVolume(.5f);
        ExpectPositivePair(*cue,PositiveBaseGain*factor(25)*.5f,PositiveBaseGain*factor(25)*.5f);
        b.Tick(70);ExpectPositivePair(*cue,PositiveBaseGain*factor(50)*.5f,PositiveBaseGain*factor(50)*.5f);
        b.Tick(95);ExpectPositivePair(*cue,PositiveBaseGain*factor(75)*.5f,PositiveBaseGain*factor(75)*.5f);
        b.Tick(120);EXPECT_TRUE(cue->getIsStoppedProperty());ExpectSilence(manualDevice->Render(32));
    }
}

TEST(XactPositiveGain, MaximumIsAppliedAfterFadeIncludingANewlyStartedWave)
{
    constexpr float maximum=16777216.0f;
    constexpr float master=1e-8f; // float output stays below1 even at the native maximum
    const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(110,1,255)}};
    ScheduledBanks b(tracks,0,false,PositiveSettings(false,14000),false,
        PositiveSound(tracks,100,true),PositiveWaves());
    SoundEffect::setMasterVolumeProperty(master);
    std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
    unsigned delayed=0;
    const float finalGain=PositiveBaseGain*10000000.0f*(1.0f-90.0f/100.0f);
    CueTestAccess::ObserveDispatch(*cue,[&](std::size_t track,std::size_t event,uint64_t elapsed)
    {
        if(track!=1 || event!=0)return;
        ++delayed;EXPECT_EQ(elapsed,110u);
        ExpectPositiveVoice(*cue,0,finalGain);ExpectPositiveVoice(*cue,1,finalGain);
        ExpectPositivePcm(manualDevice->Render(32),3*finalGain*master/4096);
    });
    cue->Play();cue->SetVariable("Volume",1);b.Tick(10);
    auto* first=CueTestAccess::ActiveInstance(*cue,0);ASSERT_NE(first,nullptr);
    const auto handle=SoundEffectInstanceTestAccess::MixerHandle(*first);
    EXPECT_FLOAT_EQ(voiceReceipts.at(handle).gain,maximum);
    ExpectPositivePcm(manualDevice->Render(32),maximum*master/4096);
    b.SetTime(20);cue->Stop(AudioStopOptions::AsAuthored);b.Tick(109);
    b.SetTime(110);b.engine->GetCategory("Default").SetVolume(1);b.Tick(110);
    EXPECT_EQ(delayed,1u);
    ExpectPositivePcm(manualDevice->Render(32),3*finalGain*master/4096);
    b.Tick(120);EXPECT_TRUE(cue->getIsStoppedProperty());ExpectSilence(manualDevice->Render(32));
    SoundEffect::setMasterVolumeProperty(1);
}
