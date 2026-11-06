// SPDX-License-Identifier: MS-PL
// Included by the isolated offline harness after its scheduler/value fixtures.

TEST(XactInstanceLimits, CategoryCountsAndVolumeSpanBanksButReplacementStaysInTheIncomingBank)
{
    // FACT_internal.c:547-558,850-869: engine-global count, bank-local victim
    // search. A native play_sound probe independently checks both branches.
    // read_volbyte:180 gives .6946086997304519cB,128 gives -586.9655945352697cB.
    // Two180 bytes plus category128 give gain10^(-585.5763771358088/2000).
    constexpr float gain=.5095793393682869f;
    for (bool sameBank : {true,false})
    {
        SCOPED_TRACE(sameBank);
        auto settings=ValueRpcXgs();
        settings.at(77)=1; // shared category limit1
        settings.at(78)=100; settings.at(79)=0;
        settings.at(80)=100; settings.at(81)=0;
        settings.at(82)=0x10; settings.at(85)=128; // REPLACE_OLDEST, unclamped gain
        const AuthoredTracks tracks{{PlayAt(0,0,255)}};
        ScheduledBanks b(tracks,0,false,settings);
        std::unique_ptr<SoundBank> otherBank;
        if (!sameBank)
        {
            auto bytes=ScheduleXsb({{PlayAt(0,1,255)}});
            std::fill(bytes.begin()+74,bytes.begin()+138,0);
            const std::string name="SecondSound";
            std::copy(name.begin(),name.end(),bytes.begin()+74);
            otherBank=b.LoadSoundBank("second.xsb",bytes);
        }
        auto older=b.Play(); ExpectVoice(*older,0,gain,1);
        ExpectMarker(manualDevice->Render(32),.125f*gain);
        auto* incomingBank=sameBank ? b.sound.get() : otherBank.get();
        std::unique_ptr<Cue> incoming(incomingBank->GetCue("Sequence"));
        b.SetTime(10); incoming->Play();
        EXPECT_EQ(older->getIsStoppingProperty(),sameBank);
        if (!sameBank) EXPECT_TRUE(older->getIsPlayingProperty());
        ExpectVoice(*incoming,0,0,1); // count from the other bank still triggers fade-in
        b.Tick(60); //50ms of both authored fades
        const float outgoingFraction=sameBank ? .5f : 1.0f;
        const float incomingMarker=sameBank ? .125f : .25f;
        ExpectVoice(*older,0,gain*outgoingFraction,1);
        ExpectVoice(*incoming,0,gain*.5f,1);
        ExpectMarker(manualDevice->Render(32),gain*(.125f*outgoingFraction+incomingMarker*.5f));
        auto category=b.engine->GetCategory("Default"); category.SetVolume(.5f);
        ExpectVoice(*older,0,gain*outgoingFraction*.5f,1);
        ExpectVoice(*incoming,0,gain*.25f,1);
        ExpectMarker(manualDevice->Render(32),gain*(.0625f*outgoingFraction+incomingMarker*.25f));
        b.Tick(110);
        EXPECT_EQ(older->getIsStoppedProperty(),sameBank);
        if (sameBank) EXPECT_EQ(CueTestAccess::ActiveInstance(*older,0),nullptr);
        else ExpectVoice(*older,0,gain*.5f,1);
        ExpectVoice(*incoming,0,gain*.5f,1);
        const float retainedMarker=sameBank ? 0.0f : .125f;
        ExpectMarker(manualDevice->Render(32),gain*.5f*(retainedMarker+incomingMarker));
    }
}
