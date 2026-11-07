// SPDX-License-Identifier: MS-PL
// Included by the isolated offline harness after the scheduler/value helpers.

Bytes CategoryGainXgs()
{
    // Native v46/tool42 full77-byte header. Two10-byte categories at77/87,
    // names at97/105 and six-byte name-index records at111/117; no variables/RPCs.
    Bytes b; Text(b,"XGSF",4); U16(b,46); U16(b,42); U16(b,0); b.resize(18,0); U8(b,3);
    for (uint16_t count : {2,0,0,0,0,0,0}) U16(b,count);
    for (uint32_t offset : {77,97,0,111,0,123,97,111,123,123,123}) U32(b,offset);
    for (uint16_t parent : {65535,0})
    {
        U8(b,255); U16(b,0); U16(b,0); U8(b,0); U16(b,parent); U8(b,128); U8(b,0);
    }
    Text(b,"Default",8); Text(b,"Child",6);
    U32(b,97); U16(b,255); U32(b,105); U16(b,255);
    return b;
}

// FACT_internal.c's parser seeds currentVolume=1, separately from authored volume;
// FACT.c SetVolume assigns authored*requested, then recurses with each child's
// previous currentVolume. FNA's constructor/GetCategory do not issue SetVolume.
// Hand arithmetic: a(b)=10^((3969*log10(b/28240)+8715)/2000).
// Category128 gives A, sound128*track180 gives B; PCM is B*category/8.
// These gains stay below1, independently of CNA's separate positive-gain clamp.
constexpr float CategoryAuthoredGain = 0.5087649718175143f;
constexpr float CategoryWaveGain = 0.5091719927809207f;

TEST(XactCategoryGain, FreshEngineAndLookupsKeepUnityUntilExplicitSetVolume)
{
    const AuthoredTracks tracks{{PlayAt(0,0,255)}};
    auto sound=ScheduleXsb(tracks); sound.at(205)=128;
    ScheduledBanks b(tracks,0,false,CategoryGainXgs(),false,sound,{},false,false);
    EXPECT_FLOAT_EQ(AudioEngineTestAccess::GetCategoryVolume(*b.engine,0),1.0f);
    auto category=b.engine->GetCategory("Default");
    auto cue=b.Play();
    const auto expect=[&](float categoryGain)
    {
        b.engine->Update(); // Native applies category state to live waves during UpdateSound.
        EXPECT_NEAR(AudioEngineTestAccess::GetCategoryVolume(*b.engine,0),categoryGain,1e-6f);
        ExpectVoice(*cue,0,CategoryWaveGain*categoryGain,1.0f);
        ExpectMarker(manualDevice->Render(32),CategoryWaveGain*categoryGain/8);
    };
    expect(1.0f);
    auto secondLookup=b.engine->GetCategory("Default");
    b.Tick(20); expect(1.0f);
    category.SetVolume(1.0f); expect(CategoryAuthoredGain);
    secondLookup.SetVolume(.5f); expect(CategoryAuthoredGain*.5f);
    b.Tick(40); expect(CategoryAuthoredGain*.5f);
    category.SetVolume(1.0f); expect(CategoryAuthoredGain);
    category.SetVolume(1.0f); expect(CategoryAuthoredGain);
}

TEST(XactCategoryGain, FreshChildCascadeUsesPreviousUnityRatherThanParentMultiplier)
{
    const AuthoredTracks tracks{{PlayAt(0,0,255)}};
    auto sound=ScheduleXsb(tracks); sound.at(203)=1; sound.at(205)=128;
    ScheduledBanks b(tracks,0,false,CategoryGainXgs(),false,sound,{},false,false);
    auto parent=b.engine->GetCategory("Default");
    auto child=b.engine->GetCategory("Child");
    EXPECT_FLOAT_EQ(AudioEngineTestAccess::GetCategoryVolume(*b.engine,0),1.0f);
    EXPECT_FLOAT_EQ(AudioEngineTestAccess::GetCategoryVolume(*b.engine,1),1.0f);
    auto cue=b.Play();
    const auto expect=[&](float parentGain,float childGain)
    {
        b.engine->Update();
        EXPECT_NEAR(AudioEngineTestAccess::GetCategoryVolume(*b.engine,0),parentGain,1e-6f);
        EXPECT_NEAR(AudioEngineTestAccess::GetCategoryVolume(*b.engine,1),childGain,1e-6f);
        ExpectVoice(*cue,0,CategoryWaveGain*childGain,1.0f);
        ExpectMarker(manualDevice->Render(32),CategoryWaveGain*childGain/8);
    };
    expect(1.0f,1.0f);
    parent.SetVolume(.25f); expect(CategoryAuthoredGain*.25f,CategoryAuthoredGain);
    b.Tick(20); expect(CategoryAuthoredGain*.25f,CategoryAuthoredGain);
    parent.SetVolume(0); expect(0,CategoryAuthoredGain*CategoryAuthoredGain);
    child.SetVolume(.5f); expect(0,CategoryAuthoredGain*.5f);
    parent.SetVolume(1); expect(CategoryAuthoredGain,CategoryAuthoredGain*CategoryAuthoredGain*.5f);
}
