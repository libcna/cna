// SPDX-License-Identifier: MS-PL
// GetNextWave stores basePitch as int16 before any RPC/total pitch bound.
// Native helper measurements qualify the integer narrowing on Linux x86_64;
// they force helper calls and do not establish full native repeat scheduling.

AuthoredEvent WrapEffectAt(uint8_t loops,int16_t pitch,uint16_t flags)
{
    auto event=PlayAt(0,0,loops);event.type=4;
    auto& bytes=event.valuePayload;
    U8(bytes,0);U16(bytes,0);U8(bytes,0);U8(bytes,loops);U16(bytes,0);U16(bytes,0);
    U16(bytes,static_cast<uint16_t>(pitch));U16(bytes,static_cast<uint16_t>(pitch));
    U8(bytes,180);U8(bytes,180);for(int i=0;i<4;++i)U32(bytes,0);U16(bytes,flags);
    return event;
}
int WrapPitchExpected(int64_t value)
{
    // Hand congruence, independent of a C++ narrowing cast or the CNA implementation.
    auto result=(value%65536+65536)%65536;return static_cast<int>(result>=32768?result-65536:result);
}
float WrapPitchRatio(int cents)
{
    return static_cast<float>(std::exp2(std::clamp(cents,-2400,2400)/1200.0));
}

TEST(XactEffectPitchWrap, FiniteAddWrapsTheCombinedBaseAndKeepsNewHoldControls)
{
    for(int sign:{-1,1})for(uint16_t flags:{uint16_t(0x1104),uint16_t(0x1100),uint16_t(0x1004)})
    {
        SCOPED_TRACE(sign);
        SCOPED_TRACE(flags);
        const unsigned waves=flags==0x1104?255:4;
        const AuthoredTracks tracks{{WrapEffectAt(static_cast<uint8_t>(waves-1),sign*300,flags)}};
        ScheduledBanks banks(tracks,0,false,{},false,PitchSound(tracks,sign*900));
        unsigned draws=0;std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));
        CueTestAccess::SetVariationRandom(*cue,{
            [](uint32_t) {ADD_FAILURE()<<"simple effect has no candidate draw";return 0u;},
            [&] {++draws;return 0.0f;}});
        cue->Play();
        for(unsigned step=1;step<=waves;++step)
        {
            SCOPED_TRACE(step);
            const int base=WrapPitchExpected(sign*(900+300*static_cast<int>(flags==0x1104?step:1)));
            ExpectEventVoice(*cue,0,0,ValueBaseGain,WrapPitchRatio(base));
            const auto pcm=manualDevice->Render(4096);
            if(flags==0x1104 && (step==106 || step==107))
            {
                unsigned activeFrames=0;
                for(std::size_t i=0;i<pcm.size();i+=2)activeFrames+=std::abs(pcm[i])>1e-4f;
                // 441 source frames: ratio4 gives about111 outputs, ratio.25 about1764.
                // These bounds allow resampler edge/history frames but reject the16x error.
                const bool slow=(sign>0)==(step==107);
                if(slow) {EXPECT_GT(activeFrames,1600u);EXPECT_LT(activeFrames,1900u);}
                else EXPECT_LT(activeFrames,200u);
            }
            ExpectSilence(manualDevice->Render(32));banks.Tick(step*10);
        }
        EXPECT_EQ(draws,waves);EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

TEST(XactEffectPitchWrap, LongEffectStateKeepsIntegerPrecisionWhenARealNextVoiceStarts)
{
    // The event state is advanced directly, then the real completion controller starts
    // one next voice. This qualifies arithmetic after many activations, not their timing.
    // wrap16(900+55742*301)=2026; an unbounded float delta instead loses2 cents.
    constexpr unsigned activations=55742;
    for(int sign:{-1,1})
    {
        SCOPED_TRACE(sign);
        const AuthoredTracks tracks{{WrapEffectAt(1,sign*301,0x1104)}};
        ScheduledBanks banks(tracks,0,false,{},false,PitchSound(tracks,sign*900));
        unsigned draws=0;std::unique_ptr<Cue> cue(banks.sound->GetCue("Sequence"));
        CueTestAccess::SetVariationRandom(*cue,{
            [](uint32_t) {ADD_FAILURE()<<"simple effect has no candidate draw";return 0u;},
            [&] {++draws;return 0.0f;}});
        cue->Play();ASSERT_EQ(draws,1u);
        CueTestAccess::AdvanceEffectState(*cue,0,0,activations-2);
        EXPECT_EQ(draws,activations-1);
        (void)manualDevice->Render(4096);ExpectSilence(manualDevice->Render(32));banks.Tick(10);
        EXPECT_EQ(draws,activations);
        ExpectEventVoice(*cue,0,0,ValueBaseGain,sign>0?3.22284171272692f:.310285173501083f);
        (void)manualDevice->Render(4096);banks.Tick(20);EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}
