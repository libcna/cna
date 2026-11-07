// SPDX-License-Identifier: MS-PL
// FACT_internal.c:1563-1571: integer base/effect, sound RPC, track RPC, then event.
// f32 -1199.9 is -9829581/8192; .9 is15099494/16777216.
// Native (1200-1199.9f)+.9f =16776806/16777216 <1, truncates0.
// Merging RPCs first rounds their sum to-1199 exactly, giving1 cent instead.

Bytes OrderSettings(int sign,bool exact=false,bool release=false)
{
    auto bytes=TrackFilterRpcXgs();
    for(uint32_t code:{107u,130u,153u,176u})
    {
        bytes.at(code+3)=1;bytes.at(code+4)=0; // pitch, with neutral trailing codes
        PitchF32At(bytes,code+9,0);PitchF32At(bytes,code+18,0);
    }
    PitchF32At(bytes,116,sign*(exact?-1199.5f:-1199.9f));
    PitchF32At(bytes,125,sign*(release?-1199.9f:-1199.5f));
    PitchF32At(bytes,139,sign*(exact?.5f:.9f));
    PitchF32At(bytes,148,sign*(release?.9f:.5f));
    if(release)
    {
        // Extend Volume's7-byte slot to ReleaseTime's12, preserving name pointers
        // and moving only its following six-byte index records214/220 ->219/225.
        bytes.insert(bytes.begin()+214,5,0);std::memcpy(bytes.data()+207,"ReleaseTime",12);
        ValueU32At(bytes,45,219);ValueU32At(bytes,53,225);
        PitchF32At(bytes,96,150);
        bytes.at(179)=0; // code176 is neutral volume, supplying track release150
        PitchF32At(bytes,190,150);
    }
    return bytes;
}
Bytes OrderSound(const AuthoredTracks& tracks,int16_t base,uint16_t fade=0,bool trackRpc=true)
{
    // Keep scope lists separate: sound107+153(neutral), track130+176(neutral).
    auto bytes=ScheduleXsb(tracks,fade,true,trackRpc,0,{107,153},{130,176});
    const auto bits=static_cast<uint16_t>(base);
    bytes.at(206)=static_cast<uint8_t>(bits);bytes.at(207)=static_cast<uint8_t>(bits>>8);
    return bytes;
}

TEST(XactPitchOrder, NativeScopeOrderSurvivesDispatchAndLiveBoundaryChanges)
{
    // Boundary, effect fold, sound-only, exact binary control, and event-last control.
    for(int sign:{-1,1})for(int kind=0;kind<5;++kind)
    {
        SCOPED_TRACE(sign);
        SCOPED_TRACE(kind);
        AuthoredTracks tracks{{}};
        if(kind==4)tracks[0].push_back(EquationAt(7,0,4,sign*.9f));
        tracks[0].push_back(kind==1
            ?VariedAt(0,0,XsbTrackVariationType::Ordered,0x1000,true,static_cast<uint16_t>(sign*600))
            :PlayAt(0,0,255));
        auto settings=OrderSettings(sign,kind==3);
        if(kind==4) {PitchF32At(settings,139,0);PitchF32At(settings,148,0);}
        ScheduledBanks banks(tracks,0,false,settings,false,OrderSound(tracks,sign*(kind==1?600:1200),0,kind!=2));
        std::vector<uint32_t> bounds;if(kind==1)InjectVariation(*banks.engine,{},bounds,{0});
        std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));cue->SetVariable("Volume",0);
        const float oneCent=sign>0?1.00057778950655f:.999422544141381f;
        unsigned starts=0;
        CueTestAccess::ObserveDispatch(*cue,[&](std::size_t,std::size_t eventIndex,uint64_t)
        {
            if(eventIndex!=static_cast<std::size_t>(kind==4))return;
            ++starts;ExpectVoice(*cue,0,ValueBaseGain,kind==3?oneCent:1);
        });
        cue->Play();EXPECT_EQ(starts,1u);
        banks.Tick(1);ExpectVoice(*cue,0,ValueBaseGain,kind==3?oneCent:1);
        cue->SetVariable("Volume",1);banks.Tick(2);
        ExpectVoice(*cue,0,ValueBaseGain,kind==2?1:oneCent);
        cue->SetVariable("Volume",0);banks.Tick(3);
        ExpectVoice(*cue,0,ValueBaseGain,kind==3?oneCent:1);
    }
}

TEST(XactPitchOrder, EveryFadePhaseAndDelayedDispatchRetainsTheNativeScopeOrder)
{
    for(int phase:{0,1,2})for(int sign:{-1,1})
    {
        SCOPED_TRACE(phase);
        SCOPED_TRACE(sign);
        const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(40,1,255)}};
        auto sound=OrderSound(tracks,sign*1200,phase==1?100:0);
        const auto cueOffset=sound.size()-30;
        sound.at(cueOffset+9)=1;sound.at(cueOffset+10)=100;sound.at(cueOffset+14)=0x10;
        ScheduledBanks banks(tracks,0,false,OrderSettings(sign,false,phase==2),false,sound);
        auto victim=banks.Play();std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));
        if(phase!=2)cue->SetVariable("Volume",0);
        unsigned starts=0;
        CueTestAccess::ObserveDispatch(*cue,[&](std::size_t track,std::size_t,uint64_t)
        {
            ++starts;auto* instance=CueTestAccess::ActiveInstance(*cue,track);ASSERT_NE(instance,nullptr);
            EXPECT_NEAR(voiceReceipts.at(SoundEffectInstanceTestAccess::MixerHandle(*instance)).ratio,1,1e-5f);
        });
        banks.SetTime(10);cue->Play();victim->Stop(AudioStopOptions::Immediate);
        if(phase!=2)cue->SetVariable("Volume",1);
        banks.Tick(20);
        if(phase!=2)ExpectVoice(*cue,0,ValueBaseGain*.1f,sign>0?1.00057778950655f:.999422544141381f);
        banks.SetTime(30);if(phase)cue->Stop(AudioStopOptions::AsAuthored);
        if(phase!=2)cue->SetVariable("Volume",0);
        banks.Tick(35);
        ExpectVoice(*cue,0,ValueBaseGain*(phase==0?.25f:phase==1?.95f:1),1);
        banks.Tick(50);EXPECT_EQ(starts,2u);
        const float fraction=phase==0?.4f:phase==1?.8f:1;
        ExpectVoice(*cue,0,ValueBaseGain*fraction,1);ExpectVoice(*cue,1,ValueBaseGain*fraction,1);
        if(!phase)
        {
            banks.Tick(109);ExpectVoice(*cue,0,ValueBaseGain*.99f,1);
            banks.Tick(110);ExpectVoice(*cue,0,ValueBaseGain,1);
        }
        else {banks.Tick(phase==1?130:180);EXPECT_TRUE(cue->getIsStoppedProperty());}
    }
}
