// SPDX-License-Identifier: MS-PL
// Included after the scheduler, value and variation helpers by the offline harness.
// FACT_internal.c:1563-1571 sums cents before int16 conversion; FACT.c:2139-2156
// bounds the total to +/-2400 and applies 2^(cents/1200), independently of XNA Pitch.

void PitchF32At(Bytes& bytes,std::size_t offset,float value)
{
    uint32_t bits;std::memcpy(&bits,&value,4);ValueU32At(bytes,offset,bits);
}
Bytes PitchSettings(float first,float last,bool release=false)
{
    auto bytes=release?ReleaseRpcXgs():ValueRpcXgs();
    PitchF32At(bytes,116,0);PitchF32At(bytes,125,0); // neutral volume RPC
    PitchF32At(bytes,139,first);PitchF32At(bytes,148,last);
    return bytes;
}
Bytes PitchSound(const AuthoredTracks& tracks,int16_t base,uint16_t fade=0,
                 bool soundRpc=false,bool trackRpc=false)
{
    auto bytes=ScheduleXsb(tracks,fade,soundRpc,trackRpc);
    const auto bits=static_cast<uint16_t>(base);
    bytes.at(206)=static_cast<uint8_t>(bits);bytes.at(207)=static_cast<uint8_t>(bits>>8);
    return bytes;
}
Bytes PitchWaves()
{
    // Literal mono441Hz sine, S16 at44100Hz. Four seconds keep every capture
    // inside the source even at ratio4; no EOF or repeated-wave timing oracle.
    constexpr uint32_t frames=176400,bytesPerWave=frames*2;
    auto bytes=ScheduleXwb();bytes.resize(216);
    ValueU32At(bytes,44,3*bytesPerWave);
    for(uint32_t wave=0;wave<3;++wave)
    {
        const auto entry=144+24*wave;
        ValueU32At(bytes,entry,frames<<4);ValueU32At(bytes,entry+8,wave*bytesPerWave);
        ValueU32At(bytes,entry+12,bytesPerWave);
    }
    for(unsigned wave=0;wave<3;++wave)
        for(uint32_t i=0;i<frames;++i)
            U16(bytes,static_cast<uint16_t>(static_cast<int16_t>(std::lround(
                4096*std::sin(2*std::acos(-1.0)*static_cast<double>(i%100)/100)))));
    return bytes;
}
void ExpectPitchFrequency(float ratio)
{
    const auto pcm=manualDevice->Render(8192);ASSERT_EQ(pcm.size(),16384u);
    unsigned crossings=0;double energy=0;bool negativeHalf=false;
    for(std::size_t i=0;i<pcm.size();i+=2)
    {
        EXPECT_NEAR(pcm[i],pcm[i+1],1e-6f);
        energy+=pcm[i]*pcm[i];
        if(pcm[i]<-.02f)negativeHalf=true;
        if(negativeHalf && pcm[i]>.02f) {++crossings;negativeHalf=false;}
    }
    // One cycle/100 source frames. Hysteresis rejects small resampler startup
    // ripples at zero; every sample contributes to the period and energy checks.
    // Finite-window phase can move the count by at most1.5 cycles;
    // the old +/-1200 cap differs by at least6 cycles.
    EXPECT_NEAR(static_cast<double>(crossings),8192*static_cast<double>(ratio)/100,1.5);
    EXPECT_GT(std::sqrt(energy/8192),.05);EXPECT_LT(std::sqrt(energy/8192),.12);
}

TEST(XactPitchRange, AuthoredAndEventTotalsUseTwoOctavesAtDispatchAndUpdates)
{
    const auto waves=PitchWaves();
    for(int sign:{-1,1})for(int event:{600,1500,2100})
    {
        SCOPED_TRACE(sign);
        SCOPED_TRACE(event);
        // +/-900 base plus +/-600,1500,2100 event: +/-1500,2400,3000.
        const float ratio=event==600?(sign>0?2.37841423000544f:.420448207626857f)
                                    :(sign>0?4.0f:.25f);
        AuthoredTracks tracks{{EquationAt(7,0,4,static_cast<float>(sign*event)),PlayAt(0,0,255)}};
        ScheduledBanks banks(tracks,0,false,{},false,PitchSound(tracks,sign*900),waves);
        std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));unsigned starts=0;
        CueTestAccess::ObserveDispatch(*cue,[&](std::size_t,std::size_t eventIndex,uint64_t)
        {
            if(eventIndex!=1)return;
            ++starts;ExpectVoice(*cue,0,ValueBaseGain,ratio); // before reconciliation
            ExpectPitchFrequency(ratio);
        });
        cue->Play();EXPECT_EQ(starts,1u);banks.Tick(1);
        ExpectVoice(*cue,0,ValueBaseGain,ratio);ExpectPitchFrequency(ratio);
        auto* instance=CueTestAccess::ActiveInstance(*cue,0);ASSERT_NE(instance,nullptr);
        EXPECT_THROW(instance->setPitchProperty(1.5f),System::ArgumentOutOfRangeException);
        EXPECT_THROW(instance->setPitchProperty(-1.5f),System::ArgumentOutOfRangeException);
        EXPECT_THROW(instance->setPitchProperty(std::numeric_limits<float>::quiet_NaN()),System::ArgumentOutOfRangeException);
        ExpectVoice(*cue,0,ValueBaseGain,ratio);
    }
}

TEST(XactPitchRange, FractionalRpcWithoutValueEventsTruncatesTheCompleteSum)
{
    for(int sign:{-1,1})
    {
        SCOPED_TRACE(sign);
        AuthoredTracks tracks{{PlayAt(0,0,255)}};
        ScheduledBanks banks(tracks,0,false,PitchSettings(sign*.75f,sign*600.75f),false,
            PitchSound(tracks,sign*300,0,true));
        auto cue=banks.Play();
        // trunc(+/-300.75) is +/-300, not floor(-300.75)=-301.
        const float initial=sign>0?1.18920711500272f:.840896415253715f;
        ExpectVoice(*cue,0,ValueBaseGain,initial);banks.Tick(1);
        ExpectVoice(*cue,0,ValueBaseGain,initial);
        cue->SetVariable("Volume",1);banks.Tick(2);
        ExpectVoice(*cue,0,ValueBaseGain,sign>0?1.68179283050743f:.594603557501361f);
    }
}

TEST(XactPitchRange, SoundTrackEffectAndEventContributionsPrecedeOneIntegerConversion)
{
    for(int sign:{-1,1})
    {
        SCOPED_TRACE(sign);
        AuthoredTracks tracks{{EquationAt(7,0,4,sign*300.75f),
            VariedAt(0,0,XsbTrackVariationType::Ordered,0x1000,true,static_cast<uint16_t>(sign*300)),
            EquationAt(7,10,4,sign*900.75f)}};
        ScheduledBanks banks(tracks,0,false,PitchSettings(sign*300.75f,sign*600.75f),false,
            PitchSound(tracks,sign*300,0,true,true));
        std::vector<uint32_t> bounds;InjectVariation(*banks.engine,{},bounds,{0});
        std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));cue->Play();
        // +/-[300+300(effect)+300.75(event)+2*300.75(RPC)] = +/-1502.25.
        ExpectVoice(*cue,0,ValueBaseGain,sign>0?2.38116346958562f:.419962767266047f);
        cue->SetVariable("Volume",1);banks.Tick(1);
        // Complete sum +/-2102.25 truncates to +/-2102, without intermediate clamps.
        ExpectVoice(*cue,0,ValueBaseGain,sign>0?3.36747367291536f:.29695852057969f);
        banks.Tick(10);ExpectVoice(*cue,0,ValueBaseGain,sign>0?4.0f:.25f);
    }
}

TEST(XactPitchRange, FadePhasesAndDelayedStartsKeepTheTotalPitch)
{
    for(int phase:{0,1,2})for(int sign:{-1,1})
    {
        SCOPED_TRACE(phase);
        SCOPED_TRACE(sign);
        AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(40,1,255)}};
        auto sound=PitchSound(tracks,sign*900,phase==1?100:0,phase!=2,phase==2);
        const auto cueOffset=sound.size()-30;
        sound.at(cueOffset+9)=1;sound.at(cueOffset+10)=100;sound.at(cueOffset+14)=0x10;
        ScheduledBanks banks(tracks,0,false,PitchSettings(sign*600,sign*(phase==2?1200:900),phase==2),false,sound);
        auto victim=banks.Play();std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));
        const float ratio=sign>0?2.37841423000544f:.420448207626857f;
        const float changed=sign>0?2.82842712474619f:.353553390593274f; // +/-1800cents
        const float settled=sign>0?2.59367910930202f:.385552706351985f; // +/-1650cents
        const float releaseFive=sign>0?2.40605007216423f:.415618948071394f; // +/-1520cents
        const float releaseTwenty=sign>0?2.49089924471765f:.401461440931688f; // +/-1580cents
        unsigned starts=0;
        CueTestAccess::ObserveDispatch(*cue,[&](std::size_t track,std::size_t,uint64_t)
        {
            ++starts;
            auto* instance=CueTestAccess::ActiveInstance(*cue,track);ASSERT_NE(instance,nullptr);
            const auto handle=SoundEffectInstanceTestAccess::MixerHandle(*instance);
            EXPECT_NEAR(voiceReceipts.at(handle).ratio,track==1?(phase==2?releaseTwenty:changed):ratio,1e-5f);
        });
        banks.SetTime(10);cue->Play();victim->Stop(AudioStopOptions::Immediate);
        banks.Tick(30);
        if(phase)cue->Stop(AudioStopOptions::AsAuthored);
        if(phase!=2)cue->SetVariable("Volume",1);
        banks.Tick(35);
        if(phase==2)ExpectVoice(*cue,0,ValueBaseGain,releaseFive);
        else ExpectVoice(*cue,0,ValueBaseGain*(phase==0?.25f:.95f),changed);
        banks.Tick(50);EXPECT_EQ(starts,2u);
        const float fraction=phase==0?.4f:phase==1?.8f:1.0f;
        const float currentRatio=phase==2?releaseTwenty:changed;
        ExpectVoice(*cue,0,ValueBaseGain*fraction,currentRatio);ExpectVoice(*cue,1,ValueBaseGain*fraction,currentRatio);
        if(!phase)
        {
            banks.Tick(109);ExpectVoice(*cue,0,ValueBaseGain*.99f,changed);
            cue->SetVariable("Volume",.5f);
            banks.Tick(110);ExpectVoice(*cue,0,ValueBaseGain,settled);
        }
        else {banks.Tick(phase==1?130:180);EXPECT_TRUE(cue->getIsStoppedProperty());}
    }
}
