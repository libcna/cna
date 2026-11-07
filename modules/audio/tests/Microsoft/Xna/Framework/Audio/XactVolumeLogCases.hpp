// SPDX-License-Identifier: MS-PL
// Textual fragment included once after the scheduler and effect helpers.
// Native GetNextWave/UpdateSound oracle: byte90, 80 ADD draws with sound/track180
// gives base-95526.1015625cB. RPC76000 and category100080000 yield gain below.
// The native probe forces arithmetic calls; the fade/track-RPC controls are source-derived.
// The2ppm checks intentionally preserve native float accumulation and addition order.
constexpr float VolumeLogGain=.01727016642689705f;

Bytes VolumeLogSettings()
{
    // Full77-byte XGS header; category77, two13-byte variables87/100,
    // three23-byte curves128/151/174, strings197/205/212, indexes224/230.
    Bytes bytes;Text(bytes,"XGSF",4);U16(bytes,46);U16(bytes,42);U16(bytes,0);
    bytes.resize(18,0);U8(bytes,3);
    for(uint16_t count:{1,2,0,0,3,0,0})U16(bytes,count);
    for(uint32_t offset:{77,87,0,224,0,230,197,205,128,0,0})U32(bytes,offset);
    U8(bytes,255);U16(bytes,0);U16(bytes,0);U8(bytes,0);U16(bytes,65535);U8(bytes,180);U8(bytes,0);
    U8(bytes,5);ValueF32(bytes,0);ValueF32(bytes,0);ValueF32(bytes,1); // Volume
    U8(bytes,4);ValueF32(bytes,0);ValueF32(bytes,0);ValueF32(bytes,150); // private ReleaseTime
    bytes.resize(128,0);
    for(unsigned curve=0;curve<3;++curve)
    {
        U16(bytes,curve==2?1:0);U8(bytes,2);U16(bytes,curve==1?1:0);
        ValueF32(bytes,0);ValueF32(bytes,0);U8(bytes,0);
        ValueF32(bytes,curve==2?150:1);ValueF32(bytes,curve==0?76000:0);U8(bytes,0);
    }
    Text(bytes,"Default",8);Text(bytes,"Volume",7);Text(bytes,"ReleaseTime",12);
    U32(bytes,197);U16(bytes,255);U32(bytes,205);U16(bytes,255);U32(bytes,212);U16(bytes,255);
    return bytes;
}
AuthoredEvent VolumeLogEffect(uint16_t time,uint8_t loops)
{
    auto event=WrapEffectAt(loops,0,0x2201);event.time=time;
    event.valuePayload.at(13)=90;event.valuePayload.at(14)=90;
    return event;
}
Bytes VolumeLogSound(const AuthoredTracks& tracks,uint16_t fade=0)
{
    return ScheduleXsb(tracks,fade,true,true,0,{128,151},{151,174});
}
void ExpectVolumeLogPcm(float gain)
{
    const auto pcm=manualDevice->Render(32);ASSERT_EQ(pcm.size(),64u);
    const float expected=gain/8,tolerance=std::max(1e-9f,std::abs(expected)*2e-6f);
    for(std::size_t i=0;i<pcm.size();++i)ASSERT_NEAR(pcm[i],expected,tolerance)<<i;
}
void InjectVolumeLogRandom(AudioEngine& engine,unsigned& draws)
{
    AudioEngineTestAccess::SetPreparationVariationRandom(engine,{
        [](uint32_t) {ADD_FAILURE()<<"simple volume effect has no candidate draw";return 0u;},
        [&draws] {++draws;return 0.0f;}});
}

TEST(XactVolumeLog, LiveRpcAndCategoryRecoverAnUnderflowedEffectAfterCompleteWaves)
{
    const AuthoredTracks tracks{{VolumeLogEffect(0,80)}};
    ScheduledBanks banks(tracks,0,false,VolumeLogSettings(),false,VolumeLogSound(tracks));
    unsigned draws=0;InjectVolumeLogRandom(*banks.engine,draws);
    std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));cue->Play();
    for(unsigned wave=1;wave<80;++wave)
    {
        (void)manualDevice->Render(1024);ExpectSilence(manualDevice->Render(32));banks.Tick(wave*10);
    }
    ASSERT_EQ(draws,80u);ExpectEventVoice(*cue,0,0,0,1);
    cue->SetVariable("Volume",1);auto category=banks.engine->GetCategory("Default");category.SetVolume(1e8f);
    ExpectEventVoice(*cue,0,0,VolumeLogGain,1);ExpectVolumeLogPcm(VolumeLogGain);
    banks.Tick(790);ExpectEventVoice(*cue,0,0,VolumeLogGain,1);
    category.SetVolume(5e7f);ExpectEventVoice(*cue,0,0,VolumeLogGain*.5f,1);ExpectVolumeLogPcm(VolumeLogGain*.5f);
    cue->Stop(AudioStopOptions::Immediate);
}

TEST(XactVolumeLog, PrimedEffectStartsWithTheCombinedGainBeforeReconciliation)
{
    const AuthoredTracks tracks{{VolumeLogEffect(40,0)}};
    ScheduledBanks banks(tracks,0,false,VolumeLogSettings(),false,VolumeLogSound(tracks));
    unsigned draws=0,starts=0;InjectVolumeLogRandom(*banks.engine,draws);
    std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));
    CueTestAccess::ObserveDispatch(*cue,[&](std::size_t,std::size_t,uint64_t elapsed)
    {
        ++starts;EXPECT_EQ(elapsed,40u);
        ExpectEventVoice(*cue,0,0,VolumeLogGain,1);ExpectVolumeLogPcm(VolumeLogGain);
    });
    cue->Play();CueTestAccess::AdvanceEffectState(*cue,0,0,79);
    cue->SetVariable("Volume",1);banks.engine->GetCategory("Default").SetVolume(1e8f);
    banks.Tick(39);EXPECT_EQ(starts,0u);banks.Tick(40);EXPECT_EQ(starts,1u);EXPECT_EQ(draws,80u);
}

TEST(XactVolumeLog, CombinedGainSurvivesEveryFadeBranchAndImmediateCategoryWrites)
{
    for(int phase:{0,1,2})
    {
        SCOPED_TRACE(phase);
        const AuthoredTracks tracks{{PlayAt(0,0,255)},{VolumeLogEffect(40,0)}};
        auto sound=VolumeLogSound(tracks,phase==1?100:0);
        sound.at(212+2+9+18)=0; // first track is authored byte0: true silence, with a live voice
        const auto cueOffset=sound.size()-30;
        sound.at(cueOffset+9)=1;sound.at(cueOffset+10)=100;sound.at(cueOffset+14)=0x10;
        ScheduledBanks banks(tracks,0,false,VolumeLogSettings(),false,sound);
        auto victim=banks.Play();unsigned draws=0,delayed=0;
        InjectVolumeLogRandom(*banks.engine,draws);
        std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));
        CueTestAccess::ObserveDispatch(*cue,[&](std::size_t track,std::size_t,uint64_t elapsed)
        {
            if(track!=1)return;
            ++delayed;EXPECT_EQ(elapsed,40u);
            const float fraction=phase==0?.4f:phase==1?.8f:1;
            ExpectEventVoice(*cue,0,0,0,1);ExpectEventVoice(*cue,1,0,VolumeLogGain*fraction,1);
            ExpectVolumeLogPcm(VolumeLogGain*fraction);
        });
        banks.SetTime(10);cue->Play();victim->Stop(AudioStopOptions::Immediate);
        CueTestAccess::AdvanceEffectState(*cue,1,0,79);
        cue->SetVariable("Volume",1);auto category=banks.engine->GetCategory("Default");category.SetVolume(1e8f);
        banks.Tick(30);if(phase)cue->Stop(AudioStopOptions::AsAuthored);
        banks.Tick(50);EXPECT_EQ(delayed,1u);EXPECT_EQ(draws,80u);
        banks.Tick(60);const float fraction=phase==0?.5f:phase==1?.7f:1;
        ExpectEventVoice(*cue,1,0,VolumeLogGain*fraction,1);
        category.SetVolume(5e7f);ExpectEventVoice(*cue,1,0,VolumeLogGain*fraction*.5f,1);
        ExpectVolumeLogPcm(VolumeLogGain*fraction*.5f);
        if(!phase)
        {
            banks.Tick(109);ExpectEventVoice(*cue,1,0,VolumeLogGain*.99f*.5f,1);
            banks.Tick(110);ExpectEventVoice(*cue,1,0,VolumeLogGain*.5f,1);
            banks.Tick(120);ExpectEventVoice(*cue,1,0,VolumeLogGain*.5f,1);
        }
        else {banks.Tick(phase==1?130:180);EXPECT_TRUE(cue->getIsStoppedProperty());}
    }
}
