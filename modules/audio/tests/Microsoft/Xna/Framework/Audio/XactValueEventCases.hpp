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
    ExpectVoice(*cue,0,ValueBaseGain,1); ExpectVoice(*cue,1,ValueBaseGain,1);
    b.Tick(20); ExpectVoice(*cue,0,ValueBaseGain*0.1f,1.189207115f);
    ExpectVoice(*cue,1,ValueBaseGain,1);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.1f,0.25f*ValueBaseGain,300);
    ExpectPitchedValueMarker(manualDevice->Render(32),0.125f*ValueBaseGain*0.1f,0.25f*ValueBaseGain,300,1);
    b.Tick(30); ExpectVoice(*cue,0,ValueBaseGain*0.01f,1.414213562f);
    b.Tick(40); ExpectVoice(*cue,0,ValueBaseGain*0.316227766f,0.707106781f);
    ExpectVoice(*cue,1,ValueBaseGain,1);
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
        auto cue=b.Play(); b.Tick(9); ExpectVoice(*cue,0,ValueBaseGain,1);
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
        auto cue=b.Play(); b.Tick(19); ExpectVoice(*cue,0,ValueBaseGain,1);
        b.Tick(20); ExpectVoice(*cue,0,ValueBaseGain,pitch ? 0.707106781f : 1);
        b.Tick(220); ExpectVoice(*cue,0,pitch ? ValueBaseGain : ValueBaseGain*0.316227766f,
                                pitch ? 0.840896415f : 1);
        b.Tick(420); ExpectVoice(*cue,0,pitch ? ValueBaseGain : ValueBaseGain*0.1f,1);
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u); // FACT uses <= duration
        b.Tick(421); EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        b.Tick(1000); ExpectVoice(*cue,0,pitch ? ValueBaseGain : ValueBaseGain*0.1f,1);
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
    // Count2 supplies two complete441-frame waves, while the envelope remains pending.
    for (uint8_t loops : {0,2})
    {
        ScheduledBanks b({{PlayAt(0,0,loops),RampAt(8,20,-2000,2.5f,400)}},0,false);
        b.sound->PlayCue("Sequence");
        auto* cue=SoundBankTestAccess::LastFireAndForgetCue(*b.sound); ASSERT_NE(cue,nullptr);
        for(unsigned pass=0;pass<std::max<unsigned>(loops,1);++pass)
        {
            ExpectMarker(manualDevice->Render(441),0.125f*ValueBaseGain);
            ExpectSilence(manualDevice->Render(32));b.Tick(pass+1);
        }
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
            auto fresh=b.Play(); ExpectVoice(*fresh,0,ValueBaseGain,1);
            b.Tick(209); ExpectVoice(*fresh,0,ValueBaseGain,1);
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

TEST(XactValueEvents, RpcOnlyReleaseRejectsAnEventAndRandomDrawAtItsExactDeadline)
{
    // FACT_internal.c:802-827 derives release duration from the last volume-RPC x;
    // its RELEASE_RPC branch at1454-1462 returns before scanning events at1496+.
    // Stop at20ms plus the literal150ms release admits169, excludes170 exactly.
    // FACT derives release from track RPCs; the neutral sound-level binding also
    // enters CNA's existing release path without changing either voice property.
    const AuthoredTracks tracks{{PlayAt(0,0,255),EquationAt(7,169,4,300),
                                 EquationAt(7,170,8,0,1200)}};
    ScheduledBanks b(tracks,0,false,ReleaseRpcXgs(),true,ScheduleXsb(tracks,0,true,true));
    std::size_t draws=0;
    std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
    b.Observe(*cue);
    CueTestAccess::SetValueRandom(*cue,[&] { ++draws; return .25f; });
    cue->Play(); b.SetTime(20); cue->Stop(AudioStopOptions::AsAuthored);
    ASSERT_TRUE(cue->getIsStoppingProperty());
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue),2u);
    b.Tick(169);
    EXPECT_TRUE(cue->getIsStoppingProperty());
    ExpectVoice(*cue,0,ValueBaseGain,1.189207115f); // admitted300 cents:2^(300/1200)
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{0,1,169}}));
    EXPECT_EQ(draws,0u);
    b.Tick(170); b.Tick(171);
    EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{0,1,169}}));
    EXPECT_EQ(draws,0u);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    ExpectSilence(manualDevice->Render(64));
}

TEST(XactValueEvents, TrackScopedReleaseUsesCueAccessibilityWithoutRequiringPublic)
{
    // FACT_internal.c:802-827 uses track lists and requires CUE, not PUBLIC.
    for (uint8_t accessibility : {uint8_t(4),uint8_t(5),uint8_t(7)})
    {
        SCOPED_TRACE(accessibility);
        auto settings=ReleaseRpcXgs(); settings.at(87)=accessibility;
        const AuthoredTracks tracks{{PlayAt(0,0,255),EquationAt(7,169,4,300)}};
        ScheduledBanks b(tracks,0,false,settings,false,ScheduleXsb(tracks,0,false,true));
        auto cue=b.Play(); b.SetTime(20); cue->Stop(AudioStopOptions::AsAuthored);
        ASSERT_TRUE(cue->getIsStoppingProperty());
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),1u);
        b.Tick(169); EXPECT_TRUE(cue->getIsStoppingProperty());
        EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{0,1,169}}));
        b.Tick(170); EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        ExpectSilence(manualDevice->Render(64));
    }
}

TEST(XactValueEvents, ReleaseDurationIsTheMaximumAcrossAllTrackBindingsInEitherOrder)
{
    // FACT_internal.c:806-824 scans all tracks and takes max(last x).
    // Stop20 + max(75,150) =170, independently of track order.
    for (bool longFirst : {false,true})
    {
        SCOPED_TRACE(longFirst);
        auto settings=ReleaseRpcXgs();
        ValueU32At(settings,121,0x42960000u); // first curve's last x=75.0f
        settings.at(133)=0; // second curve targets VOLUME as well
        const AuthoredTracks tracks{{PlayAt(0,0,255)},
            {PlayAt(0,1,255),EquationAt(7,169,4,300),EquationAt(7,170,4,600)}};
        auto sound=ScheduleXsb(tracks,0,false,true);
        // Sound202 + fixed header10 + RPC length2: two9-byte lists at214/223.
        for (std::size_t offset : {std::size_t(215),std::size_t(219)})
            ValueU32At(sound,offset,longFirst ? 130 : 107);
        for (std::size_t offset : {std::size_t(224),std::size_t(228)})
            ValueU32At(sound,offset,longFirst ? 107 : 130);
        ScheduledBanks b(tracks,0,false,settings,false,sound);
        auto cue=b.Play(); b.SetTime(20); cue->Stop(AudioStopOptions::AsAuthored);
        ASSERT_TRUE(cue->getIsStoppingProperty());
        b.Tick(95); EXPECT_TRUE(cue->getIsStoppingProperty());
        b.Tick(169); EXPECT_TRUE(cue->getIsStoppingProperty());
        EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{1,0,0},{1,1,169}}));
        b.Tick(170); EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0},{1,0,0},{1,1,169}}));
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
    }
}

TEST(XactValueEvents, ReleaseIgnoresSoundOnlyGlobalWrongVariableAndNonVolumeBindings)
{
    // FACT_internal.c:806-819 accepts only track-scoped CUE ReleaseTime VOLUME.
    for (int kind=0;kind<4;++kind)
    {
        SCOPED_TRACE(kind);
        auto settings=kind==2 ? ValueRpcXgs() : ReleaseRpcXgs();
        if (kind==1) settings.at(87)=1; // PUBLIC engine-global, not CUE
        const AuthoredTracks tracks{{PlayAt(0,0,255),EquationAt(7,170,4,300)}};
        auto sound=ScheduleXsb(tracks,0,kind==0,kind!=0);
        if (kind==3)
        {
            ValueU32At(sound,215,130); ValueU32At(sound,219,130); // PITCH only
        }
        ScheduledBanks b(tracks,0,false,settings,false,sound);
        auto cue=b.Play(); b.SetTime(20); cue->Stop(AudioStopOptions::AsAuthored);
        EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_FALSE(cue->getIsStoppingProperty());
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        b.Tick(170); EXPECT_EQ(b.trace,(std::vector<Dispatch>{{0,0,0}}));
        ExpectSilence(manualDevice->Render(64));
    }
}

TEST(XactValueEvents, TrackRpcValuesAccumulateWithSoundAndRemainOnTheirOwnVoice)
{
    // FACT_internal.c:1474-1492,1551-1571 sums sound/track volume and pitch.
    // Each two-code list repeats one axis: sound volume twice, track0 pitch
    // twice, track1 volume twice. At v=.25: -3000cB sound, -600 cents track0,
    // extra -3000cB track1. At v=.75: -1000cB sound, +600 cents, extra -1000cB.
    auto settings=ValueRpcXgs(); ValueU32At(settings,88,0x3e800000u); // initial .25f
    const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(0,1,255)}};
    auto sound=ScheduleXsb(tracks,0,true,true);
    ValueU32At(sound,219,107); // sound list107/107
    ValueU32At(sound,224,130); // track0 list130/130
    ValueU32At(sound,237,107); // track1 list107/107
    ScheduledBanks b(tracks,0,false,settings,true,sound); auto cue=b.Play();
    ExpectVoice(*cue,0,ValueBaseGain*.0316227766f,.7071067812f);
    ExpectVoice(*cue,1,ValueBaseGain*.001f,1);
    cue->SetVariable("Volume",.75f); b.Tick(1);
    ExpectVoice(*cue,0,ValueBaseGain*.316227766f,1.414213562f);
    ExpectVoice(*cue,1,ValueBaseGain*.1f,1);
    b.engine->GetCategory("Default").SetVolume(.5f); // immediate per-voice reapplication
    ExpectVoice(*cue,0,ValueBaseGain*.158113883f,1.414213562f);
    ExpectVoice(*cue,1,ValueBaseGain*.05f,1);
}

TEST(XactValueEvents, PrivateTrackRpcVariablesUseTheirAuthoredInitialValue)
{
    // FACT variables feed RPCs independently of PUBLIC. Private globals and
    // private cue locals both start at authored .5, giving -1000cB and0 cents.
    for (uint8_t accessibility : {uint8_t(0),uint8_t(4)})
    {
        SCOPED_TRACE(accessibility);
        auto settings=ValueRpcXgs(); settings.at(87)=accessibility;
        ValueU32At(settings,88,0x3f000000u); // initial .5f
        const AuthoredTracks tracks{{PlayAt(0,0,255)}};
        ScheduledBanks b(tracks,0,false,settings,false,ScheduleXsb(tracks,0,false,true));
        auto cue=b.Play();
        ExpectVoice(*cue,0,ValueBaseGain*.316227766f,1);
        b.Tick(1); ExpectVoice(*cue,0,ValueBaseGain*.316227766f,1);
    }
}

TEST(XactValueEvents, AttackTimeUsesTrackOriginAndExcludesCueAndCategoryPauses)
{
    // FACT_internal.c:1046-1052,1471-1492 supplies accessibility/origins.
    // Pause assertions pin CNA's existing scheduler policy, not native FACT
    // resume accounting; the measured discrepancy is recorded in handoff §5.1.
    // Global names never invoke the intrinsic clock. Curve -4000..-2000cB
    // over0..100ms gives .01*10^(elapsed/100) amplitude.
    for (bool soundScope : {false,true})
    for (bool cueLocal : {false,true})
    for (bool categoryPause : {false,true})
    {
        SCOPED_TRACE(::testing::PrintToString(std::make_tuple(soundScope,cueLocal,categoryPause)));
        auto settings=ReleaseRpcXgs();
        std::fill(settings.begin()+161,settings.begin()+173,0);
        const std::string name="AttackTime";
        std::copy(name.begin(),name.end(),settings.begin()+161);
        settings.at(87)=cueLocal ? 4 : 1;
        ValueU32At(settings,88,0x42480000u); // authored initial50, also global RPC input
        ValueU32At(settings,116,0xc57a0000u); // -4000
        ValueU32At(settings,121,0x42c80000u); // 100
        ValueU32At(settings,125,0xc4fa0000u); // -2000
        const AuthoredTracks tracks{{PlayAt(20,0,255)},{PlayAt(40,1,255)}};
        ScheduledBanks b(tracks,0,false,settings,false,
            ScheduleXsb(tracks,0,soundScope,!soundScope));
        auto cue=b.Play(); b.Tick(20); b.Tick(40); b.Tick(60);
        const float first=cueLocal ? .02511886432f : .03162277660f;
        const float second=cueLocal && !soundScope ? .01584893192f : first;
        ExpectVoice(*cue,0,ValueBaseGain*first,1);
        ExpectVoice(*cue,1,ValueBaseGain*second,1);
        auto category=b.engine->GetCategory("Default");
        if (categoryPause) category.Pause(); else cue->Pause();
        b.Tick(1060);
        ExpectVoice(*cue,0,ValueBaseGain*first,1);
        ExpectVoice(*cue,1,ValueBaseGain*second,1);
        if (categoryPause) category.Resume(); else cue->Resume();
        b.Tick(1080); // active cue elapsed80; track ages60/40
        const float resumedFirst=cueLocal ? .03981071706f : .03162277660f;
        const float resumedSecond=cueLocal && !soundScope ? .02511886432f : resumedFirst;
        ExpectVoice(*cue,0,ValueBaseGain*resumedFirst,1);
        ExpectVoice(*cue,1,ValueBaseGain*resumedSecond,1);
    }
}

TEST(XactValueEvents, TrackFiltersOverrideSoundPerAxisAndNeverEnableAnUnauthoredFilter)
{
    // FACT_internal.c:1485-1492 inherits sound filter axes before applying
    // track curves;1557-1598 gates the result on an authored track filter.
    const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(0,1,255)},{PlayAt(0,2,255)}};
    auto sound=ScheduleXsb(tracks,0,true,true);
    ValueU32At(sound,224,153); ValueU32At(sound,228,176); // track0 overrides both axes
    ValueU32At(sound,237,107); // track1 repeats frequency, inherits sound Q
    // Track records start250, each9 bytes: volume1, offset4, filter2, Hz2.
    for (std::size_t offset : {std::size_t(255),std::size_t(264)})
    {
        sound.at(offset)=1; sound.at(offset+1)=64; // low-pass enabled
        sound.at(offset+2)=0xf4; sound.at(offset+3)=1; // authored500Hz
    }
    ScheduledBanks b(tracks,0,false,TrackFilterRpcXgs(),false,sound); auto cue=b.Play();
    const auto expectFilter=[&](std::size_t voice, float hz, float q)
    {
        auto* instance=CueTestAccess::ActiveInstance(*cue,voice); ASSERT_NE(instance,nullptr);
        int kind=-1; float frequency=-1,oneOverQ=-1;
        SoundEffectInstanceTestAccess::GetFilterState(*instance,kind,frequency,oneOverQ);
        EXPECT_EQ(kind,1); // low-pass
        // FACT_INTERNAL_CalculateFilterFrequency also caps the result at1.
        const float cutoff=std::min(1.0f,2.0f*std::sin(3.14159265358979323846f*hz/44100.0f));
        EXPECT_NEAR(frequency,cutoff,1e-6f); EXPECT_NEAR(oneOverQ,1.0f/q,1e-6f);
    };
    expectFilter(0,2000,5); expectFilter(1,4500,2.5f);
    cue->SetVariable("Volume",.75f); b.Tick(1);
    expectFilter(0,4000,7); expectFilter(1,9500,3.5f);
    auto* unfiltered=CueTestAccess::ActiveInstance(*cue,2); ASSERT_NE(unfiltered,nullptr);
    int kind=-1; float frequency=-1,oneOverQ=-1;
    SoundEffectInstanceTestAccess::GetFilterState(*unfiltered,kind,frequency,oneOverQ);
    EXPECT_EQ(kind,0);
}

TEST(XactValueEvents, TrackReleaseCurvesUseStopRelativeTimeAndAuthoredFadeKeepsTheirInputZero)
{
    // FACT_internal.c:1058-1074,1424-1462,1551-1562. Track0 sums to
    // -100-20t cB; track1 to -1000+10t cB. RPC-only release uses t since
    // Stop and no linear fade. An authored fade keeps ReleaseTime input0.
    for (bool authoredFade : {false,true})
    {
        SCOPED_TRACE(authoredFade);
        auto settings=ReleaseRpcXgs(); settings.at(133)=0; // both curves VOLUME
        ValueU32At(settings,116,0xc2480000u); // -50
        ValueU32At(settings,121,0x42c80000u); //100
        ValueU32At(settings,125,0xc4834000u); // -1050
        ValueU32At(settings,139,0xc3fa0000u); // -500
        ValueU32At(settings,144,0x42c80000u); //100; second final y remains0
        const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(0,1,255)}};
        auto sound=ScheduleXsb(tracks,authoredFade ? 100 : 0,false,true);
        ValueU32At(sound,219,107); ValueU32At(sound,224,130); //107/107 and130/130
        ScheduledBanks b(tracks,0,false,settings,false,sound); auto cue=b.Play();
        const auto check=[&](float first,float second)
        {
            ExpectVoice(*cue,0,ValueBaseGain*first,1);
            ExpectVoice(*cue,1,ValueBaseGain*second,1);
            ExpectMarker(manualDevice->Render(32),ValueBaseGain*(.125f*first+.25f*second));
        };
        b.Tick(10); check(.8912509381f,.3162277660f); // input0 while Playing
        b.SetTime(20); cue->Stop(AudioStopOptions::AsAuthored);
        ASSERT_TRUE(cue->getIsStoppingProperty());
        b.Tick(20); check(.8912509381f,.3162277660f);
        b.Tick(35); // release age15; authored fade multiplier .85
        const float first=authoredFade ? .8912509381f*.85f : .6309573445f;
        const float second=authoredFade ? .3162277660f*.85f : .3758374043f;
        check(first,second);
        auto category=b.engine->GetCategory("Default"); category.SetVolume(.5f);
        check(first*.5f,second*.5f); // immediate; RPC release has no extra .85
        b.Tick(35); check(first*.5f,second*.5f);
        b.Tick(119); // release age99; authored fade multiplier .01
        const float lateFirst=authoredFade ? .8912509381f*.01f : .0912010839f;
        const float lateSecond=authoredFade ? .3162277660f*.01f : .9885530947f;
        check(lateFirst*.5f,lateSecond*.5f);
        category.SetVolume(.25f); check(lateFirst*.25f,lateSecond*.25f);
        b.Tick(120); EXPECT_TRUE(cue->getIsStoppedProperty());
        EXPECT_EQ(CueTestAccess::PendingEvents(*cue),0u);
        ExpectSilence(manualDevice->Render(32));
    }
}

TEST(XactValueEvents, FadeInKeepsTrackRpcEventAndCategoryGainsAtDispatchAndDuringUpdates)
{
    // FACT_internal.c:843-869 starts a fade only after an instance limit
    // permits replacement.1424-1440,1551-1562 apply its fraction after the
    // independent per-track event/RPC sum. All gains stay below the clamp.
    for (int trigger : {0,1,2,3}) // unlimited, cue replace, category replace, cue FAIL
    {
        SCOPED_TRACE(trigger);
        auto settings=ValueRpcXgs(); settings.at(133)=0;
        ValueU32At(settings,88,0x3e800000u); // Volume=.25: RPC totals -3000/-600cB
        const AuthoredTracks tracks{{EquationAt(8,0,4,-1000),PlayAt(0,0,255)},
            {EquationAt(8,20,4,-400),PlayAt(40,1,255)}};
        auto sound=ScheduleXsb(tracks,0,false,true);
        ValueU32At(sound,219,107); ValueU32At(sound,224,130);
        // Two39-byte event blocks after track table250 give cue record328.
        ASSERT_EQ(sound.size(),358u);
        sound.at(337)=trigger==1 || trigger==3 ? 1 : 255;
        sound.at(338)=100; sound.at(339)=0; // authored fade-in100 even for unlimited control
        sound.at(342)=trigger==3 ? 0 : 0x10; // FAIL or REPLACE_OLDEST
        if (trigger==2)
        {
            settings.at(77)=1; settings.at(78)=100; settings.at(79)=0;
            settings.at(82)=0x10; // category limit1, fade-in100, REPLACE_OLDEST
        }
        ScheduledBanks b(tracks,0,false,settings,false,sound);
        auto victim=b.Play(); ExpectVoice(*victim,0,ValueBaseGain*.01f,1); // first Play is unfaded
        std::unique_ptr<Cue> cue(b.sound->GetCue("Sequence"));
        unsigned delayedDispatches=0;
        CueTestAccess::ObserveDispatch(*cue,[&](std::size_t t,std::size_t e,uint64_t elapsed)
        {
            if (t==1 && e==1)
            {
                ++delayedDispatches; EXPECT_EQ(elapsed,40u);
                // Observe configured voice before this tick's reconciliation
                // could repair a wrong starting gain: B*.316227766*.5*.4.
                ExpectVoice(*cue,1,ValueBaseGain*.0632455532f,1);
            }
        });
        b.SetTime(10); cue->Play();
        if (trigger==0)
        {
            ExpectVoice(*cue,0,ValueBaseGain*.01f,1);
            EXPECT_TRUE(victim->getIsPlayingProperty());
            continue;
        }
        if (trigger==3)
        {
            EXPECT_TRUE(cue->getIsStoppedProperty());
            EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
            ExpectVoice(*victim,0,ValueBaseGain*.01f,1);
            continue;
        }
        ExpectVoice(*cue,0,0,1);
        b.Tick(35); ExpectVoice(*cue,0,ValueBaseGain*.0025f,1); // fade age25
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,1),nullptr);
        auto category=b.engine->GetCategory("Default"); category.SetVolume(.5f);
        ExpectVoice(*cue,0,ValueBaseGain*.00125f,1);
        b.Tick(50); EXPECT_EQ(delayedDispatches,1u); // fade age40, delayed track starts
        ExpectVoice(*cue,0,ValueBaseGain*.002f,1);
        ExpectVoice(*cue,1,ValueBaseGain*.0632455532f,1);
        ExpectMarker(manualDevice->Render(32),ValueBaseGain*(.125f*.002f+.25f*.0632455532f));
        b.Tick(80); ExpectVoice(*cue,0,ValueBaseGain*.0035f,1);
        ExpectVoice(*cue,1,ValueBaseGain*.1106797181f,1);
        category.SetVolume(.25f);
        ExpectVoice(*cue,0,ValueBaseGain*.00175f,1);
        ExpectVoice(*cue,1,ValueBaseGain*.0553398591f,1);
        b.Tick(109); ExpectVoice(*cue,0,ValueBaseGain*.002475f,1);
        ExpectVoice(*cue,1,ValueBaseGain*.0782663725f,1);
        b.Tick(110); ExpectVoice(*cue,0,ValueBaseGain*.0025f,1);
        ExpectVoice(*cue,1,ValueBaseGain*.0790569415f,1);
        category.SetVolume(.5f); ExpectVoice(*cue,0,ValueBaseGain*.005f,1);
        ExpectVoice(*cue,1,ValueBaseGain*.1581138830f,1);
        EXPECT_EQ(delayedDispatches,1u);
    }
}

TEST(XactValueEvents, DelayedWaveStartsInCurrentPhaseWhenReleaseInterruptsFadeIn)
{
    // FACT BeginReleaseRPC replaces FADE_IN with RELEASE_RPC; UpdateSound uses
    // fadeVolume1 there. FADE_OUT instead uses its own remaining fraction.
    // CNA configures new voices before playback. The observer renders before reconciliation
    // can repair a bad first gain; the native probe qualifies phase math, not playback latency.
    constexpr float gain=.5095793393682869f; // sound180*track180*category128
    for (int phase : {0,1,2}) // ongoing fade-in, authored fade-out, RPC-only release
    {
        SCOPED_TRACE(phase);
        auto settings=ReleaseRpcXgs(); settings.at(85)=128;
        const AuthoredTracks tracks{{PlayAt(0,0,255)},{PlayAt(40,1,255)}};
        auto sound=ScheduleXsb(tracks,phase==1 ? 100 : 0,false,true);
        const auto cueOffset=sound.size()-30; // complex cue15 + name index6 + name9
        sound.at(cueOffset+9)=1;
        sound.at(cueOffset+10)=100; sound.at(cueOffset+11)=0;
        sound.at(cueOffset+14)=0x10; // REPLACE_OLDEST triggers fade-in100
        ScheduledBanks b(tracks,0,false,settings,false,sound);
        auto victim=b.Play();
        std::unique_ptr<Cue> incoming(b.sound->GetCue("Sequence"));
        unsigned delayed=0;
        const float delayedFraction=phase==0 ? .4f : phase==1 ? .8f : 1.0f;
        CueTestAccess::ObserveDispatch(*incoming,[&](std::size_t track,std::size_t event,uint64_t elapsed)
        {
            if (track!=1 || event!=0) return;
            ++delayed; EXPECT_EQ(elapsed,40u);
            ExpectVoice(*incoming,1,gain*.5f*delayedFraction,1);
            ExpectMarker(manualDevice->Render(32),gain*.5f*delayedFraction*(.125f+.25f));
        });
        b.SetTime(10); incoming->Play(); victim->Stop(AudioStopOptions::Immediate);
        ExpectVoice(*incoming,0,0,1);
        b.Tick(30); ExpectVoice(*incoming,0,gain*.2f,1);
        if (phase!=0) incoming->Stop(AudioStopOptions::AsAuthored);
        b.Tick(35);
        const float firstFraction=phase==0 ? .25f : phase==1 ? .95f : 1.0f;
        ExpectVoice(*incoming,0,gain*firstFraction,1);
        b.engine->GetCategory("Default").SetVolume(.5f);
        ExpectVoice(*incoming,0,gain*.5f*firstFraction,1);
        b.Tick(49); EXPECT_EQ(delayed,0u);
        b.SetTime(50);
        // Establish the existing voice's exact current gain before the delayed start.
        b.engine->GetCategory("Default").SetVolume(.5f);
        ExpectVoice(*incoming,0,gain*.5f*delayedFraction,1);
        b.Tick(50); EXPECT_EQ(delayed,1u);
        ExpectVoice(*incoming,0,gain*.5f*delayedFraction,1);
        ExpectVoice(*incoming,1,gain*.5f*delayedFraction,1);
        ExpectMarker(manualDevice->Render(32),gain*.5f*delayedFraction*(.125f+.25f));
        if (phase!=0)
        {
            b.Tick(phase==1 ? 130 : 180);
            EXPECT_TRUE(incoming->getIsStoppedProperty()); ExpectSilence(manualDevice->Render(32));
        }
    }
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
