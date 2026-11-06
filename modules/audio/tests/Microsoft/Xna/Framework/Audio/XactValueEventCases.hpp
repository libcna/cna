// SPDX-License-Identifier: MS-PL
// Included after XactSchedulerCases.hpp by the isolated offline harness only.
// Native event oracle: FAudio FACT_internal.c:707-708,788-793,1295-1383,
// 1496-1571,2441-2467. Managed Refresh AudioEngine.Update IL_0000-000b
// delegates to native code and does not establish the envelope arithmetic.

#include "XactValueEventBytes.hpp"

// Hand derivation, not a CNA snapshot: each authored byte180 gives
// c=3969*log10(180/28240)+8715 = 0.6946086997304519 centibels.
// Sound, track and category contribute 3c, so B=10^(3c/2000).
// FACT_internal.c:66-69,2012,1551-1562. 1e-5 gain/ratio tolerates float
// conversion/pow rounding; PCM tolerates 1e-6, far below the event deltas.
constexpr float ValueBaseGain = 1.0024019735836718f;
void ExpectVoice(Cue& cue, std::size_t index, float gain, float ratio)
{
    auto* instance=CueTestAccess::ActiveInstance(cue,index);
    ASSERT_NE(instance,nullptr);
    const auto found=voiceReceipts.find(SoundEffectInstanceTestAccess::MixerHandle(*instance));
    ASSERT_NE(found,voiceReceipts.end());
    EXPECT_NEAR(found->second.gain,gain,1e-5f);
    EXPECT_NEAR(found->second.ratio,ratio,1e-5f);
}

// PCM startup oracle for the two real adapters, independent of CNA output.
// SDL resampler:33-49,402-518,585-601,674-706; audio queue:630-650;
// stream conversion:402-413 (all under third_party/SDL/src/audio).
// Kaiser beta=.1102*(80-8.7), sinc, four-point Lagrange interpolation;
// sourceRate=int(float(44100)*ratio), step=ceil(sourceRate*2^32/44100).
// Sum the kernel only over nonnegative source indices (zero left history).
// Fresh-stream callback requests floor(31*step)+1 inputs without right padding.
// The unflushed queue retains N+1 lookahead frames: produced=ceil((inputs-N-1)/step).
// At32 outputs the SIMD profile gives26 frames at300 cents,27 at600 cents;
// mixer.c:565,581-598 zero-fills the rest. The next pull has retained lookahead.
// Stream/queue oracle: audio conversion:1360-1400,1134-1152; queue:630-650.
// The scalar profile has5 zero crossings; default SSE/NEON profiles have6.
// The next pull starts at (produced*step-consumedInputs*2^32) and has nonzero
// history throughout. Its DC ripple is the sum of all finite-kernel weights.
// derive_phase3_resampler.py in the GAP-01c lane derives both32-frame blocks.
// CnaMixer.cpp:446-451 linearly interpolates identical adjacent marker samples,
// so its unit response is1 throughout. No boundary frames are omitted.
[[maybe_unused]] constexpr std::array<float,64> ValueStep5_300{
    1.0000000000f, 0.9699225051f, 1.0111700136f, 0.9988125336f,
    0.9999500707f, 1.0000169590f, 1.0000055190f, 0.9999014489f,
    0.9998471636f, 0.9999172198f, 1.0000134419f, 1.0000174146f,
    0.9999333457f, 0.9998499860f, 0.9998872546f, 0.9999947263f,
    1.0000114601f, 0.9999659968f, 0.9998637488f, 0.9998639706f,
    0.9999663662f, 1.0000112632f, 0.9999944381f, 0.9998869352f,
    0.9998500867f, 0.9999337344f, 1.0000174585f, 0.0000000000f,
    0.0000000000f, 0.0000000000f, 0.0000000000f, 0.0000000000f,
    1.0000132993f, 0.9999168426f, 0.9998471306f, 0.9999018033f,
    1.0000057420f, 1.0000170265f, 0.9999496845f, 0.9998554991f,
    0.9998747257f, 0.9999815806f, 1.0000003385f, 0.9999809070f,
    0.9998741739f, 0.9998558257f, 0.9999504567f, 1.0000168886f,
    1.0000052944f, 0.9999010951f, 0.9998471982f, 0.9999175974f,
    1.0000135824f, 1.0000173682f, 0.9999329572f, 0.9998498869f,
    0.9998875750f, 0.9999950132f, 1.0000116539f, 0.9999656268f,
    0.9998635284f, 0.9998641936f, 0.9999667350f, 1.0000110630f,
};
[[maybe_unused]] constexpr std::array<float,64> ValueStep5_600{
    1.0000000000f, 0.9597179017f, 1.0030392398f, 0.9999504292f,
    0.9998913017f, 1.0000178157f, 0.9998472291f, 1.0000149275f,
    0.9999076143f, 0.9999322638f, 1.0000030599f, 0.9998529448f,
    1.0000121640f, 0.9998718460f, 0.9999746832f, 0.9999680734f,
    0.9998768897f, 1.0000150113f, 0.9998506755f, 1.0000072089f,
    0.9999251691f, 0.9999144010f, 1.0000124219f, 0.9998481752f,
    1.0000174402f, 0.9998853751f, 0.9999574682f, 0.0000000000f,
    0.0000000000f, 0.0000000000f, 0.0000000000f, 0.0000000000f,
    0.9999843526f, 0.9998648707f, 1.0000056564f, 0.9998574166f,
    0.9999956032f, 0.9999433076f, 0.9998975217f, 1.0000173122f,
    0.9998468289f, 1.0000166866f, 0.9999010193f, 0.9999394086f,
    0.9999983681f, 0.9998557084f, 1.0000082530f, 0.9998672127f,
    0.9999810276f, 0.9999612529f, 0.9998822844f, 1.0000168471f,
    0.9998489150f, 1.0000107614f, 0.9999181645f, 0.9999213396f,
    1.0000092217f, 0.9998496524f, 1.0000161335f, 0.9998797820f,
    0.9999643806f, 0.9999781757f, 0.9998692690f, 1.0000101687f,
};
[[maybe_unused]] constexpr std::array<float,64> ValueStep6_300{
    1.0000000000f, 0.9647038206f, 1.0189423730f, 0.9953496908f,
    1.0002801497f, 0.9999834564f, 0.9999823970f, 1.0000347226f,
    1.0000651664f, 1.0000260926f, 0.9999800577f, 0.9999802789f,
    1.0000174070f, 1.0000635597f, 1.0000425845f, 0.9999867620f,
    0.9999898048f, 1.0000004267f, 1.0000557642f, 1.0000556391f,
    1.0000002410f, 0.9999899980f, 0.9999868877f, 1.0000427624f,
    1.0000635025f, 1.0000171996f, 0.0000000000f, 0.0000000000f,
    0.0000000000f, 0.0000000000f, 0.0000000000f, 0.0000000000f,
    0.9999803175f, 0.9999800842f, 1.0000262975f, 1.0000651851f,
    1.0000345273f, 0.9999823165f, 0.9999833458f, 1.0000087859f,
    1.0000604295f, 1.0000495864f, 0.9999927819f, 0.9999997176f,
    0.9999931030f, 1.0000498962f, 1.0000602443f, 1.0000083839f,
    0.9999835689f, 0.9999824786f, 1.0000349175f, 1.0000651467f,
    1.0000258875f, 0.9999800326f, 0.9999802420f, 1.0000176144f,
    1.0000636162f, 1.0000424062f, 0.9999866373f, 0.9999896136f,
    1.0000006128f, 1.0000558886f, 1.0000555132f, 1.0000000559f,
};
[[maybe_unused]] constexpr std::array<float,64> ValueStep6_600{
    1.0000000000f, 0.9504652682f, 1.0063444955f, 1.0010943716f,
    1.0000403348f, 0.9999811062f, 1.0000651291f, 0.9999798557f,
    1.0000313345f, 1.0000179848f, 0.9999833178f, 1.0000618784f,
    0.9999891049f, 1.0000512040f, 0.9999961139f, 0.9999993854f,
    1.0000483728f, 0.9999860468f, 1.0000631676f, 0.9999818017f,
    1.0000217916f, 1.0000276259f, 0.9999802675f, 1.0000645905f,
    0.9999825805f, 1.0000436314f, 1.0000047607f, 0.0000000000f,
    0.0000000000f, 0.0000000000f, 0.0000000000f, 0.0000000000f,
    0.9999914714f, 1.0000551313f, 0.9999951661f, 1.0000593432f,
    0.9999863819f, 1.0000121259f, 1.0000368895f, 0.9999802017f,
    1.0000653560f, 0.9999799382f, 1.0000349592f, 1.0000141843f,
    0.9999852074f, 1.0000603108f, 0.9999928379f, 1.0000538113f,
    0.9999930455f, 1.0000028266f, 1.0000453557f, 0.9999836340f,
    1.0000641692f, 0.9999806893f, 1.0000255796f, 1.0000238591f,
    0.9999811441f, 1.0000637495f, 0.9999846543f, 1.0000467541f,
    1.0000012411f, 0.9999944150f, 1.0000526534f, 0.9999910529f,
};

void ExpectPitchedValueMarker(const std::vector<float>& output, float pitched,
                             float unpitched, int cents, std::size_t block = 0)
{
    ASSERT_EQ(output.size(),64u); // all32 frames, both channels
    ASSERT_TRUE(cents==300 || cents==600);
    ASSERT_LT(block,2u);
#if defined(CNA_GAP01C_CNA_MIXER) || defined(CNA_AUDIO_PLATFORM_ALSA)
    for (std::size_t i=0;i<output.size();++i)
        ASSERT_NEAR(output[i],unpitched+pitched,1e-6f) << i;
#else
#if defined(__SSE__) || defined(__ARM_NEON) || defined(__aarch64__)
    const auto& response=cents==300 ? ValueStep6_300 : ValueStep6_600;
#else
    const auto& response=cents==300 ? ValueStep5_300 : ValueStep5_600;
#endif
    for (std::size_t i=0;i<output.size();++i)
        ASSERT_NEAR(output[i],unpitched+pitched*response[block*32+i/2],1e-6f) << i;
#endif
}

TEST(XactValueEvents, EquationsReplaceAndAddOnlyOnTheirOwningTrack)
{
    // FACT:1295-1365; ratios are 2^(cents/1200). Volume is centibels.
    ScheduledBanks b({{PlayAt(0,0,255),EquationAt(7,20,4,300),EquationAt(8,20,4,-2000),
        EquationAt(7,30,5,300),EquationAt(8,30,5,-2000),
        EquationAt(7,40,4,-600),EquationAt(8,40,4,-1000)}, {PlayAt(0,1,255)}},0,false);
    auto cue=b.Play(); b.Tick(19);
    ExpectVoice(*cue,0,1,1); ExpectVoice(*cue,1,1,1);
    b.Tick(20); ExpectVoice(*cue,0,ValueBaseGain*0.1f,1.189207115f);
    ExpectVoice(*cue,1,1,1);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.1f,0.25f,300);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.1f,0.25f,300,1);
    b.Tick(30); ExpectVoice(*cue,0,ValueBaseGain*0.01f,1.414213562f);
    b.Tick(40); ExpectVoice(*cue,0,ValueBaseGain*0.316227766f,0.707106781f);
    ExpectVoice(*cue,1,1,1);
    b.Tick(41); ExpectVoice(*cue,0,ValueBaseGain*0.316227766f,0.707106781f);
}

TEST(XactValueEvents, RandomEquationsDrawAtActivationAndEachRepeatWithLiteralPrecedence)
{
    // FACT:1321-1335,1371-1383; stb.h:356-359. Descending bounds are
    // interpolated without sorting. The float-cast RNG can round its top draw to 1.
    ScheduledBanks b({{PlayAt(0,0,255),EquationAt(7,10,8,-600,600),
        EquationAt(8,10,8,-4000,0),EquationAt(16,20,9,0,1200,1,10),
        EquationAt(8,20,8,-2000,-4000),EquationAt(8,30,12,-2000,999),
        EquationAt(8,40,8,-2000,-2000),EquationAt(7,50,8,0,1200)}},0,false);
    std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence")); b.Observe(*cue);
    const std::array<float,7> samples{0,0.25f,0.75f,0.75f,0.25f,1,1};
    std::size_t draws=0;
    CueTestAccess::SetValueRandom(*cue,[&] {
        EXPECT_LT(draws,samples.size());
        return draws<samples.size() ? samples[draws++] : 0.0f;
    });
    cue->Play(); b.Tick(9); EXPECT_EQ(draws,0u);
    b.Tick(10); EXPECT_EQ(draws,2u); // -600 cents and -3000 cB
    ExpectVoice(*cue,0,ValueBaseGain*0.0316227766f,0.707106781f);
    b.Tick(19); (void)cue->getIsPlayingProperty(); (void)cue->getIsStoppedProperty();
    EXPECT_EQ(draws,2u);
    b.SetTime(20); (void)cue->getIsPlayingProperty(); (void)cue->getIsStoppedProperty();
    EXPECT_EQ(draws,2u); // a getter at a due timestamp does not dispatch
    b.Tick(20); EXPECT_EQ(draws,4u); // -600+900=300, -2000+.75*(-2000)=-3500
    ExpectVoice(*cue,0,ValueBaseGain*0.0177827941f,1.189207115f);
    b.Tick(20); EXPECT_EQ(draws,4u);
    b.Tick(30); EXPECT_EQ(draws,5u); // 300+300=600; literal wins over random bit
    ExpectVoice(*cue,0,ValueBaseGain*0.1f,1.414213562f);
    b.Tick(40); EXPECT_EQ(draws,6u); // even an equal-bounds random equation draws
    b.Tick(50); EXPECT_EQ(draws,7u); ExpectVoice(*cue,0,ValueBaseGain*0.1f,2);
}

TEST(XactValueEvents, FiniteRepeatsUseAdditionalCountFrequencyAndOneActivationPerLateUpdate)
{
    // FACT:788-793,1371-1383,1496-1522. A late Update activates each event
    // once; another Update at the same time may activate the next overdue repeat.
    for (uint16_t repeats : {0,1,2})
    {
        SCOPED_TRACE(repeats);
        ScheduledBanks b({{PlayAt(0,0,255),EquationAt(16,10,5,100,0,repeats,20),
            EquationAt(17,10,5,-1000,0,repeats,20)}},0,false);
        auto cue=b.Play(); b.Tick(9); ExpectVoice(*cue,0,1,1);
        b.Tick(10); ExpectVoice(*cue,0,ValueBaseGain*0.316227766f,1.059463094f);
        b.Tick(29); ExpectVoice(*cue,0,ValueBaseGain*0.316227766f,1.059463094f);
        b.Tick(1000);
        const int count=repeats ? 2 : 1;
        // 100 or 200 cents; -1000 or -2000 cB, independently derived.
        ExpectVoice(*cue,0,ValueBaseGain*(count==1 ? 0.316227766f : 0.1f),
                    count==1 ? 1.059463094f : 1.122462048f);
        b.Tick(1000); b.Tick(1000);
        ExpectVoice(*cue,0,ValueBaseGain*(repeats==0 ? 0.316227766f : repeats==1 ? 0.1f : 0.0316227766f),
                    repeats==0 ? 1.059463094f : repeats==1 ? 1.122462048f : 1.189207115f);
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    }
}

TEST(XactValueEvents, BothInfiniteSentinelsAndZeroFrequencyStayBoundedPerUpdate)
{
    // FACT:1371-1383 treats 255 AND 65535 as infinite. Zero interval must
    // not spin inside one Update. More activations than either finite count
    // prove that neither sentinel was silently decremented to completion.
    for (uint16_t sentinel : {255,65535})
    {
        SCOPED_TRACE(sentinel);
        ScheduledBanks b({{PlayAt(0,0,255),EquationAt(17,10,8,-2000,-2000,sentinel,0)}},0,false);
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
        uint32_t draws=0; CueTestAccess::SetValueRandom(*cue,[&] { ++draws; return 0.5f; });
        cue->Play(); EXPECT_EQ(draws,0u);
        const uint32_t calls=uint32_t(sentinel)+3;
        for (uint32_t i=0;i<calls;++i) b.Tick(10);
        EXPECT_EQ(draws,calls); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u);
        ExpectVoice(*cue,0,ValueBaseGain*0.1f,1);
        cue->Stop(AudioStopOptions::Immediate); b.Tick(100000);
        EXPECT_EQ(draws,calls); ExpectSilence(manualDevice->Render(32));
    }
}

TEST(XactValueEvents, AllFourRampKindsUseDecodedSlopeAndHoldTheEndpoint)
{
    // FACT:1300-1317,2444-2447. Pitch: decoded slope=1.5*100=150;
    // 150*400/1000*10=600 cents. Volume: -5*100=-500 -> -2000 cB.
    // Native slopeDelta is unresolved; these literal banks author delta=0.
    for (uint8_t type : {7,8,16,17})
    {
        SCOPED_TRACE(int(type)); const bool pitch=type==7 || type==16;
        ScheduledBanks b({{PlayAt(0,0,255),RampAt(type,20,pitch ? -600.0f : 0.0f,
            pitch ? 1.5f : -5.0f,400)}},0,false);
        auto cue=b.Play(); b.Tick(19); ExpectVoice(*cue,0,1,1);
        b.Tick(20); ExpectVoice(*cue,0,1,pitch ? 0.707106781f : 1);
        b.Tick(220); ExpectVoice(*cue,0,pitch ? 1 : ValueBaseGain*0.316227766f,
                                pitch ? 0.840896415f : 1);
        b.Tick(420); ExpectVoice(*cue,0,pitch ? 1 : ValueBaseGain*0.1f,1);
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u); // FACT uses <= duration
        b.Tick(421); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        b.Tick(1000); ExpectVoice(*cue,0,pitch ? 1 : ValueBaseGain*0.1f,1);
    }
}

TEST(XactValueEvents, LateRampActivationClampsToItsAuthoredEnd)
{
    ScheduledBanks b({{PlayAt(0,0,255),RampAt(8,20,0,-5,400)}},0,false);
    auto cue=b.Play(); b.Tick(1000);
    // A late update evaluates from authored t=20, not from arrival t=1000.
    ExpectVoice(*cue,0,ValueBaseGain*0.1f,1);
    ExpectMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.1f);
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
}

TEST(XactValueEvents, CueAndCategoryPauseFreezeRampAndRepeatClocks)
{
    // FACT:1471-1476,1727-1736 and FACT.c:2622-2689; Refresh Cue.Pause/
    // Resume IL_0000-0011 delegates the state. A 1000 ms pause contributes 0.
    for (bool categoryPause : {false,true})
    {
        ScheduledBanks b({{PlayAt(0,0,255),RampAt(8,0,-2000,2.5f,400),
            EquationAt(16,100,5,100,0,2,100)}},0,false);
        auto cue=b.Play(); auto category=b.engine->GetCategory("Default");
        ExpectVoice(*cue,0,ValueBaseGain*0.1f,1); // zero-time ramp applies during Play
        b.Tick(100);
        ExpectVoice(*cue,0,ValueBaseGain*0.133352143f,1.059463094f); // -1750 cB
        if (categoryPause) category.Pause(); else cue->Pause();
        b.Tick(1100); ExpectSilence(manualDevice->Render(32));
        ExpectVoice(*cue,0,ValueBaseGain*0.133352143f,1.059463094f);
        if (categoryPause) category.Resume(); else cue->Resume();
        b.Tick(1200); // active elapsed200: -1500 cB and 200 cents
        ExpectVoice(*cue,0,ValueBaseGain*0.177827941f,1.122462048f);
        b.Tick(1300); // active elapsed300: -1250 cB and 300 cents
        ExpectVoice(*cue,0,ValueBaseGain*0.237137371f,1.189207115f);
        b.Tick(1401); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        ExpectVoice(*cue,0,ValueBaseGain*0.316227766f,1.189207115f);
    }
}

TEST(XactValueEvents, ValuesBeforeDelayedWavesPersistWithoutReapplyingAdditions)
{
    // FACT:707-708,1343-1347,1551-1571: event state belongs to the track,
    // not its current voice. No same-track overlapping waves in this fixture.
    ScheduledBanks b({{EquationAt(7,10,5,600),EquationAt(8,10,5,-2000),
        PlayAt(20),PlayAt(40,1)}},0,false);
    auto cue=b.Play(); b.Tick(10); EXPECT_TRUE(cue->getIsPlayingProperty());
    ExpectSilence(manualDevice->Render(32)); b.Tick(20);
    ExpectVoice(*cue,0,ValueBaseGain*0.1f,1.414213562f);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.1f,0,600);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.1f,0,600,1);
    (void)manualDevice->Render(1024); b.Tick(39);
    EXPECT_TRUE(cue->getIsPlayingProperty()); b.Tick(40);
    ExpectVoice(*cue,1,ValueBaseGain*0.1f,1.414213562f);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.25f*ValueBaseGain*0.1f,0,600);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.25f*ValueBaseGain*0.1f,0,600,1);
    (void)manualDevice->Render(1024); b.Tick(41);
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(XactValueEvents, PendingEnvelopeOutlivesFiniteWaveThenFireAndForgetTerminates)
{
    // FACT:1496-1532 keeps unfinished envelopes alive after a voice ends.
    // Also guards G02: count2 is exactly 441*(1+2)=1323 PCM frames, not infinite.
    for (uint8_t loops : {0,2})
    {
        ScheduledBanks b({{PlayAt(0,0,loops),RampAt(8,20,-2000,2.5f,400)}},0,false);
        b.sound->PlayCue("Sequence");
        auto* cue=SoundBankTestAccess::LastFireAndForgetCue(*b.sound); ASSERT_NE(cue,nullptr);
        ExpectMarker(manualDevice->Render(441*(loops+1)),0.125f);
        ExpectSilence(manualDevice->Render(32)); b.Tick(19);
        EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b.sound),1u);
        b.Tick(420); EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b.sound),1u);
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u);
        b.Tick(421); // last envelope evaluation finishes; Update sweeps the cue
        EXPECT_EQ(SoundBankTestAccess::FireAndForgetCount(*b.sound),0u);
        EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*b.engine),0u);
        EXPECT_FALSE(b.wave->getIsInUseProperty()); ExpectSilence(manualDevice->Render(32));
    }
}

TEST(XactValueEvents, TrackStopCancelsItsRampAndRepeatsWhileSiblingContinues)
{
    // FACT_INTERNAL_StopTrack:1189-1214 cancels future work only on its track.
    ScheduledBanks b({{PlayAt(0,0,255),RampAt(8,0,0,-5,400),
        EquationAt(16,50,5,100,0,65535,50),StopAt(100,1),EquationAt(8,200,4,-4000)},
        {PlayAt(0,1,255),EquationAt(17,50,5,-1000,0,1,100)}},0,false);
    auto cue=b.Play(); b.Tick(50); b.Tick(100);
    ExpectVoice(*cue,1,ValueBaseGain*0.316227766f,1);
    ExpectMarker(manualDevice->Render(32),0.25f*ValueBaseGain*0.316227766f);
    b.Tick(150); ExpectVoice(*cue,1,ValueBaseGain*0.1f,1);
    const auto dispatches=b.trace.size(); b.Tick(1000);
    EXPECT_EQ(b.trace.size(),dispatches); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    ExpectMarker(manualDevice->Render(32),0.25f*ValueBaseGain*0.1f);
}

TEST(XactValueEvents, StopFreshCueAndDisposalCancelValueWorkAndResetTrackState)
{
    for (int owner : {0,1,2,3,4,5})
    {
        ScheduledBanks b({{PlayAt(0,0,255),EquationAt(17,10,8,-2000,-2000,65535,10)}},0,false);
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
        unsigned draws=0; CueTestAccess::SetValueRandom(*cue,[&] { ++draws; return 0.25f; });
        cue->Play(); b.Tick(10); EXPECT_EQ(draws,1u);
        ExpectVoice(*cue,0,ValueBaseGain*0.1f,1); b.SetTime(17);
        if (owner==0) cue->Stop(AudioStopOptions::Immediate);
        if (owner==1) cue->Stop(AudioStopOptions::AsAuthored); // zero authored release
        if (owner==2) cue->Dispose();
        if (owner==3) b.sound->Dispose();
        if (owner==4) b.wave->Dispose();
        if (owner==5) b.engine->Dispose();
        b.Tick(100); EXPECT_EQ(draws,1u); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        ExpectSilence(manualDevice->Render(32));
        if (owner<2)
        {
            cue->Play(); b.Tick(200); EXPECT_EQ(draws,1u); // Refresh played latch
            auto fresh=b.Play(); ExpectVoice(*fresh,0,1,1);
            b.Tick(209); ExpectVoice(*fresh,0,1,1);
            b.Tick(210); ExpectVoice(*fresh,0,ValueBaseGain*0.1f,1);
        }
    }
}

TEST(XactValueEvents, AuthoredReleaseAppliesValuesBeforeButNeverAtItsDeadline)
{
    // FACT:1442-1471,1496-1522: fade-out keeps events running before expiry.
    ScheduledBanks b({{PlayAt(0,0,255),StopAt(20,2),EquationAt(7,40,4,600),
        EquationAt(17,70,4,-2000,0,65535,10),EquationAt(8,120,4,0)}},100,false);
    auto cue=b.Play(); b.Tick(20); EXPECT_TRUE(cue->getIsStoppingProperty());
    b.Tick(70); // release50/100: 0.5 amplitude, event multiplier0.1
    ExpectVoice(*cue,0,ValueBaseGain*0.05f,1.414213562f);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.05f,0,600);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.05f,0,600,1);
    b.Tick(119); ExpectVoice(*cue,0,ValueBaseGain*0.001f,1.414213562f);
    const auto dispatches=b.trace.size(); b.Tick(120);
    EXPECT_EQ(b.trace.size(),dispatches); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    EXPECT_TRUE(cue->getIsStoppedProperty()); ExpectSilence(manualDevice->Render(32));
}

TEST(XactValueEvents, EventValuesComposeWithLiveRpcCategoryAndReleaseWithoutDrift)
{
    // FACT:1551-1571: sum event+RPC+base cents before int16 truncation;
    // multiply category and fade only after logarithmic volume conversion.
    ScheduledBanks b({{PlayAt(0,0,255),EquationAt(7,0,4,600.75f),
        EquationAt(8,0,4,-2000)}},100,false,ValueRpcXgs(),true);
    auto cue=b.Play(); b.Tick(0);
    ExpectVoice(*cue,0,ValueBaseGain*0.01f,1); // trunc(-600+600.75)=0
    cue->SetVariable("Volume",1); b.Tick(1);
    ExpectVoice(*cue,0,ValueBaseGain*0.1f,2); // trunc(600+600.75)=1200
    auto category=b.engine->GetCategory("Default"); category.SetVolume(0.5f);
    ExpectVoice(*cue,0,ValueBaseGain*0.05f,2); // immediately, no Update
    b.Tick(1); ExpectVoice(*cue,0,ValueBaseGain*0.05f,2);
    cue->Stop(AudioStopOptions::AsAuthored); b.Tick(51);
    ExpectVoice(*cue,0,ValueBaseGain*0.025f,2);
    category.SetVolume(0.25f); ExpectVoice(*cue,0,ValueBaseGain*0.0125f,2);
    category.SetVolume(0.5f); ExpectVoice(*cue,0,ValueBaseGain*0.025f,2);
    cue->SetVariable("Volume",0); b.Tick(76);
    ExpectVoice(*cue,0,ValueBaseGain*0.00125f,1);
    b.Tick(101); EXPECT_TRUE(cue->getIsStoppedProperty());
    ExpectSilence(manualDevice->Render(32));
}
