// SPDX-License-Identifier: MS-PL
// Included by the isolated offline harness after its scheduler/value fixtures.

TEST(XactInstanceLimits, CategoryCountsVolumeAndSoleVictimReplacementSpanBanks)
{
    // HND-XACT-03 genuine3.6 admission/clone settles sole foreign OLDEST victim
    // selection over the prior FAudio bank-local fallback. Fade100ms/gain below
    // retain their source arithmetic, not a new native gain/ramp timing claim.
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
        EXPECT_TRUE(older->getIsPlayingProperty());
        EXPECT_FALSE(older->getIsStoppingProperty());
        EXPECT_TRUE(older->getIsPlayingProperty());
        ExpectVoice(*incoming,0,0,1); // count from the other bank still triggers fade-in
        b.Tick(60); //50ms of both authored fades
        const float outgoingFraction=.5f;
        const float incomingMarker=sameBank ? .125f : .25f;
        ExpectVoice(*older,0,gain*outgoingFraction,1);
        ExpectVoice(*incoming,0,gain*.5f,1);
        ExpectMarker(manualDevice->Render(32),gain*(.125f*outgoingFraction+incomingMarker*.5f));
        auto category=b.engine->GetCategory("Default"); category.SetVolume(.5f);
        ExpectVoice(*older,0,gain*outgoingFraction*.5f,1);
        ExpectVoice(*incoming,0,gain*.25f,1);
        ExpectMarker(manualDevice->Render(32),gain*(.0625f*outgoingFraction+incomingMarker*.25f));
        b.Tick(110);
        EXPECT_TRUE(older->getIsStoppedProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*older,0),nullptr);
        ExpectVoice(*incoming,0,gain*.5f,1);
        const float retainedMarker=0.0f;
        ExpectMarker(manualDevice->Render(32),gain*.5f*(retainedMarker+incomingMarker));
    }
}

TEST(XactInstanceLimits, PlayingVictimsFollowPreparationOrderModeAndPrioritySentinel)
{
    // FACT.c:1164-1177 appends prepared cues; FACT_internal.c:547-593 chooses
    // the first for QUEUE/OLDEST, last for the current QUIETEST stub, and
    // first strictly lower priority than255 for LOWEST_PRIORITY. Native
    // play_sound probes cover both limits and both play orders independently.
    struct Scenario
    {
        const char* name;
        uint8_t behavior;
        bool reverse;
        uint8_t priority;
        int selected;
    };
    const std::array<Scenario,9> scenarios{{
        {"queue-forward",1,false,0,0},{"queue-reverse",1,true,0,0},
        {"oldest-forward",2,false,0,0},{"oldest-reverse",2,true,0,0},
        {"quietest-forward",3,false,0,1},{"quietest-reverse",3,true,0,1},
        {"priority-tie-reverse",4,true,0,0},{"priority255-no-victim",4,false,255,-1},
        {"reserved-mode-no-victim",5,false,0,-1}}};
    constexpr float gain=.5095793393682869f; // same hand-derived byte volumes as bank-scope test
    for (bool categoryLimit : {false,true})
    for (const auto& scenario : scenarios)
    {
        SCOPED_TRACE(categoryLimit);
        SCOPED_TRACE(scenario.name);
        auto settings=ValueRpcXgs(); settings.at(85)=128;
        auto sound=ScheduleXsb({{PlayAt(0,0,255)}});
        ASSERT_EQ(sound.size(),268u);
        sound.at(208)=scenario.priority; // sound202 + flags1,category2,volume1,pitch2
        if (categoryLimit)
        {
            settings.at(77)=2; settings.at(78)=100; settings.at(80)=100;
            settings.at(82)=scenario.behavior<<3;
        }
        else
        {
            // One17-byte event block follows the track table221: cue238.
            sound.at(247)=2; sound.at(248)=100; sound.at(250)=100;
            sound.at(252)=scenario.behavior<<3;
        }
        ScheduledBanks b({{PlayAt(0,0,255)}},0,false,settings,false,sound);
        std::unique_ptr<Cue> first(b.sound->GetCue("Sequence"));
        std::unique_ptr<Cue> second(b.sound->GetCue("Sequence"));
        std::unique_ptr<Cue> incoming(b.sound->GetCue("Sequence"));
        auto* earlierPlayed=scenario.reverse ? second.get() : first.get();
        auto* laterPlayed=scenario.reverse ? first.get() : second.get();
        earlierPlayed->Play(); b.SetTime(5); laterPlayed->Play();
        ExpectVoice(*first,0,gain,1); ExpectVoice(*second,0,gain,1);
        b.SetTime(10); incoming->Play();
        // BeginFadeOut retains Playing; the selected victim is distinguished by its gain/end.
        EXPECT_TRUE(first->getIsPlayingProperty());EXPECT_TRUE(second->getIsPlayingProperty());
        EXPECT_FALSE(first->getIsStoppingProperty());EXPECT_FALSE(second->getIsStoppingProperty());
        ExpectVoice(*incoming,0,0,1); // the limit still triggers when no victim qualifies
        b.Tick(60);
        const float firstFraction=scenario.selected==0 ? .5f : 1.0f;
        const float secondFraction=scenario.selected==1 ? .5f : 1.0f;
        ExpectVoice(*first,0,gain*firstFraction,1);
        ExpectVoice(*second,0,gain*secondFraction,1);
        ExpectVoice(*incoming,0,gain*.5f,1);
        ExpectMarker(manualDevice->Render(32),gain*.125f*(firstFraction+secondFraction+.5f));
        b.Tick(110);
        EXPECT_EQ(first->getIsStoppedProperty(),scenario.selected==0);
        EXPECT_EQ(second->getIsStoppedProperty(),scenario.selected==1);
        if (scenario.selected!=0) ExpectVoice(*first,0,gain,1);
        if (scenario.selected!=1) ExpectVoice(*second,0,gain,1);
        ExpectVoice(*incoming,0,gain,1);
        const float remaining=scenario.selected<0 ? 3.0f : 2.0f;
        ExpectMarker(manualDevice->Render(32),gain*.125f*remaining);
    }
}

TEST(XactInstanceLimits, NativeQuietestStubSelectsLastPreparedEvenWhenThatCueIsLouder)
{
    // FACT_internal.c:581-586 overwrites the candidate without comparing
    // volume. Distinct RPC gains keep a true minimum-volume implementation
    // from passing merely because all eligible cues have equal loudness.
    constexpr float gain=.5095793393682869f;
    for (bool categoryLimit : {false,true})
    for (bool reverse : {false,true})
    {
        SCOPED_TRACE(categoryLimit);
        SCOPED_TRACE(reverse);
        auto settings=ValueRpcXgs(); settings.at(85)=128;
        auto sound=ScheduleXsb({{PlayAt(0,0,255)}},0,true);
        ValueU32At(sound,219,107); // sound VOLUME curve107 twice; pitch remains0
        ASSERT_EQ(sound.size(),279u);
        if (categoryLimit)
        {
            settings.at(77)=2; settings.at(78)=100; settings.at(80)=100;
            settings.at(82)=0x18; // QUIETEST
        }
        else
        {
            // Sound RPC block11 moves cue249: limit+9, fadeIn+10, fadeOut+12, behavior+14.
            sound.at(258)=2; sound.at(259)=100; sound.at(261)=100; sound.at(263)=0x18;
        }
        ScheduledBanks b({{PlayAt(0,0,255)}},0,false,settings,false,sound);
        std::unique_ptr<Cue> quiet(b.sound->GetCue("Sequence"));
        std::unique_ptr<Cue> loud(b.sound->GetCue("Sequence"));
        std::unique_ptr<Cue> incoming(b.sound->GetCue("Sequence"));
        quiet->SetVariable("Volume",0); loud->SetVariable("Volume",1);
        incoming->SetVariable("Volume",.5f); // -2000cB total; target gain*.1
        auto* earlier=reverse ? loud.get() : quiet.get();
        auto* later=reverse ? quiet.get() : loud.get();
        earlier->Play(); b.SetTime(5); later->Play();
        ExpectVoice(*quiet,0,gain*.01f,1); ExpectVoice(*loud,0,gain,1);
        b.SetTime(10); incoming->Play();
        EXPECT_FALSE(quiet->getIsStoppingProperty());
        EXPECT_TRUE(loud->getIsPlayingProperty());
        EXPECT_FALSE(loud->getIsStoppingProperty());
        b.Tick(60);
        ExpectVoice(*quiet,0,gain*.01f,1); ExpectVoice(*loud,0,gain*.5f,1);
        ExpectVoice(*incoming,0,gain*.05f,1);
        ExpectMarker(manualDevice->Render(32),gain*.125f*(.01f+.5f+.05f));
        b.Tick(110);
        EXPECT_TRUE(loud->getIsStoppedProperty());
        EXPECT_EQ(CueTestAccess::ActiveInstance(*loud,0),nullptr);
        ExpectVoice(*quiet,0,gain*.01f,1); ExpectVoice(*incoming,0,gain*.1f,1);
        ExpectMarker(manualDevice->Render(32),gain*.125f*.11f);
    }
}
