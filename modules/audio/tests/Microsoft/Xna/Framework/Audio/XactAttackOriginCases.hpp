// SPDX-License-Identifier: MS-PL
// Included by the isolated offline harness after the value-event helpers.

TEST(XactValueEvents, SoundAttackUsesPreDispatchMutableOriginAndTrackAttackStaysAuthored)
{
    // FACT_internal.c:1481 reads the mutable first event-instance timestamp for
    // sound RPCs; :1492 reads authored track timestamps. Both precede event dispatch.
    // An unmodified native UpdateSound probe executes this repeat progression without
    // waves/devices. The PCM below is hand-derived through CNA's two real adapters.
    const AuthoredTracks tracks{{EquationAt(17,0,4,0,0,2,10),PlayAt(0,0,255)},
                               {EquationAt(8,0,4,-1000),PlayAt(0,1,255)}};
    auto settings=ReleaseRpcXgs(); settings.at(85)=128;
    std::memcpy(settings.data()+161,"AttackTime",11); // existing12-byte name slot, indexed at173
    ValueU32At(settings,121,0x41a00000u); // first curve last x20
    ValueU32At(settings,125,0xc57a0000u); // last y-4000; first y0, neutral pitch curve
    // a(b)=10^((3969*log10(b/28240)+8715)/2000); G=a(180)^2*a(128).
    constexpr float gain=0.5095793393682869f;
    struct Row { int time; float soundCb,trackCb; unsigned dispatches; };
    const std::array<Row,7> rows{{{0,0,0,4},{5,-4000,-1000,4},{10,0,-2000,5},
        {10,-4000,-2000,5},{11,-4000,-2200,5},{20,0,-4000,6},{21,-200,-4000,6}}};
    for (unsigned scope : {1u,2u,3u})
    {
        SCOPED_TRACE(scope);
        auto sound=ScheduleXsb(tracks,0,(scope&1)!=0,(scope&2)!=0);
        ScheduledBanks b(tracks,0,false,settings,false,sound); auto cue=b.Play();
        bool first=true;
        for (const auto& row:rows)
        {
            SCOPED_TRACE(row.time);
            if (!first) b.Tick(row.time);
            first=false;
            const float cb=((scope&1)?row.soundCb:0)+((scope&2)?row.trackCb:0);
            const float firstGain=gain*std::pow(10.0f,cb/2000);
            const float secondGain=gain*std::pow(10.0f,(cb-1000)/2000);
            ExpectVoice(*cue,0,firstGain,1);
            ExpectVoice(*cue,1,secondGain,1);
            ExpectMarker(manualDevice->Render(32),firstGain/8+secondGain/4);
            EXPECT_EQ(b.trace.size(),row.dispatches);
            // Getters must not see a different origin after dispatch in the same pass.
            EXPECT_TRUE(cue->getIsPlayingProperty()); EXPECT_TRUE(cue->getIsPlayingProperty());
            ExpectVoice(*cue,0,firstGain,1); ExpectVoice(*cue,1,secondGain,1);
        }
    }
}
