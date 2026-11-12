// SPDX-License-Identifier: MS-PL
#pragma once
// Included inside the isolated GAP-02 harness's unnamed namespace. Its manual
// device is the only seam; WaveBank, decoder, Cue and both real mixers stay intact.
TEST(Gap05DownmixPlayback, EverySourceChannelContributesInBothMixerAdapters)
{
    EnsureMixer(); ASSERT_EQ(factoryCalls,1); ASSERT_NE(manualDevice,nullptr);
    SoundEffect::setMasterVolumeProperty(1.0f);
    // Independent matrix oracle: FAudio matrix_defaults.inl:159-160.
    // Six source frames isolate FL/FR/FC/LFE/BL/BR, each at8192; each literal
    // is round(8192*coefficient), then native44100Hz float mixing divides by32768.
    constexpr std::array<std::array<int,2>,6> expected{{
        {2413,0},{0,2413},{1705,1705},{745,745},{2063,1266},{1266,2063}
    }};
    std::vector<std::int16_t> native(36,0);
    for (int channel=0; channel<6; ++channel) native[channel*6+channel]=8192;
    const auto pcm=DownmixEncodedWaveBank51(native);
    SoundEffect effect(pcm,44100,AudioChannels::Stereo);
    auto instance=effect.CreateInstance(); instance.Play();
    const auto output=manualDevice->Render(38);
    ASSERT_EQ(output.size(),76u);
    for (int frame=0; frame<38; ++frame) for (int channel=0; channel<2; ++channel)
    {
        const float sample=frame<6 ? expected[frame][channel]/32768.0f : 0.0f;
        ASSERT_NEAR(output[frame*2+channel],sample,1e-6f) << "frame " << frame << " channel " << channel;
    }
    EXPECT_EQ(instance.getStateProperty(),SoundState::Stopped);
}

#if defined(CNA_AUDIO_WAVEBANK_FFMPEG_AVAILABLE)

Bytes Gap05Read(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("Required licensed GAP-05 fixture missing: "+path.string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}

std::uint32_t Gap05U32(const Bytes& b,std::size_t at)
{
    return std::uint32_t(b.at(at))|(std::uint32_t(b.at(at+1))<<8)|
        (std::uint32_t(b.at(at+2))<<16)|(std::uint32_t(b.at(at+3))<<24);
}

void Gap05Put(Bytes& b,std::size_t at,std::uint32_t value)
{
    for (int i=0; i<4; ++i) b.at(at+i)=static_cast<std::uint8_t>(value>>(8*i));
}

class Gap05Banks
{
    std::filesystem::path directory_;
    std::string Write(const char* name,const Bytes& bytes)
    {
        const auto path=directory_/name;
        std::ofstream output(path,std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if (!output) throw std::runtime_error("Could not write GAP-05 fixture");
        return path.string();
    }
public:
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    std::vector<std::int16_t> golden;
    bool subregion=false;
    int Total(int repeats) const { return 8192+repeats*(subregion ? 4096 : 8192); }
    Gap05Banks(const std::string& stem,std::uint8_t count,bool streaming,bool truncated=false,
               bool throughEnd=false,bool nativeFull=false,bool newOnLoop=false)
    {
        const auto root=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT);
        const bool pro=stem=="wmapro";
        auto bytes=Gap05Read(root/(stem+(pro || nativeFull ? ".native.xwb" : ".xwb")));
        auto raw=pro ? Bytes{} : Gap05Read(root/(stem+(nativeFull ? ".native.s16le" : ".s16le")));
        if (!pro && raw.size()!=(nativeFull ? 32768u : 8192u)*2*2)
            throw std::runtime_error("Stereo golden must have the authored native frame count");
        for (std::size_t i=0; i<raw.size(); i+=2)
        {
            const auto word=std::uint16_t(raw[i])|(std::uint16_t(raw[i+1])<<8);
            golden.push_back(static_cast<std::int16_t>(word>=32768 ? int(word)-65536 : int(word)));
        }
        const auto parsed=ParseXwb(bytes);
        if (parsed.entries.size()!=1 || parsed.entries[0].sampleRate!=(pro ? 48000u : 44100u) ||
            parsed.entries[0].channels!=(pro ? 6 : 2) ||
            parsed.entries[0].durationSamples!=(pro ? 37713u : nativeFull ? 32768u : 8192u) ||
            parsed.entries[0].loopStartSample!=(pro || nativeFull ? 0u : 2048u) ||
            parsed.entries[0].loopTotalSamples!=(pro || nativeFull ? 0u : 4096u))
            throw std::runtime_error("GAP-05 fixture does not meet acquisition specification");
        // The copied XSB helper references Gap02Wave. Rename only the bank header;
        // encoded bytes, seek tables and loop metadata remain the independent fixture.
        const std::size_t segments=Gap05U32(bytes,4)>43 ? 12 : 8;
        const std::size_t bank=Gap05U32(bytes,segments);
        if (bank+72>bytes.size()) throw std::runtime_error("Truncated bank name");
        std::fill(bytes.begin()+bank+8,bytes.begin()+bank+72,0);
        std::memcpy(bytes.data()+bank+8,"Gap02Wave",9);
        Gap05Put(bytes,bank,(Gap05U32(bytes,bank)&~1u)|(streaming ? 1u : 0u));
        subregion=!streaming;
        if (!pro && !nativeFull && (streaming || throughEnd))
        {
            // FACT.c:1856-1860 disallows encoded streaming subregions. Use the
            // whole decoded wave for this positive native-compatible control.
            const auto entry=Gap05U32(bytes,segments+8);
            if (Gap05U32(bytes,bank+72)<24) throw std::runtime_error("Full entry metadata required");
            Gap05Put(bytes,entry+16,streaming ? 0 : 2048);
            Gap05Put(bytes,entry+20,throughEnd ? 0 : 8192);
        }
        if (truncated)
        {
            const auto& entry=parsed.entries[0];
            if (entry.dataLength<2) throw std::runtime_error("Fixture has no encoded payload");
            bytes.resize(static_cast<std::size_t>(entry.dataOffset)+entry.dataLength-1);
        }
        directory_=CNA::TestSupport::FixtureRoot()/"gap05-encoded";
        std::filesystem::create_directories(directory_);
        engine=std::make_unique<AudioEngine>(Write("fixture.xgs",Xgs()));
        if (streaming)
        {
            // A complete PCM decoy at byte0 satisfies CheckWaveBankHeader. The real
            // encoded bank starts at 2048; this catches lost bank/entry byte offsets.
            Bytes embedded=Xwb(0,false,false);
            embedded.resize(2048,0); embedded.insert(embedded.end(),bytes.begin(),bytes.end());
            wave=std::make_unique<WaveBank>(engine.get(),Write("fixture.xwb",embedded),2048,2);
        }
        else wave=std::make_unique<WaveBank>(engine.get(),Write("fixture.xwb",bytes));
        sound=std::make_unique<SoundBank>(engine.get(),Write("fixture.xsb",Xsb(count,newOnLoop)));
    }
    ~Gap05Banks()
    {
        sound.reset(); wave.reset(); engine.reset();
        std::error_code ignored; std::filesystem::remove_all(directory_,ignored);
    }
    std::unique_ptr<Cue> Play(bool valid=true)
    {
        std::unique_ptr<Cue> cue(sound->GetCue("Loop")); cue->Play();
        auto* instance=CueTestAccess::ActiveInstance(*cue,0);
        if (valid && !instance) throw std::runtime_error("Encoded entry produced no voice");
        if (!valid && instance) throw std::runtime_error("Truncated entry produced a voice");
        if (instance) instance->setVolumeProperty(1.0f);
        return cue;
    }
    void Update(Cue& cue)
    {
        engine->Update();
        if(auto* instance=CueTestAccess::EventVoice(cue,0,0))instance->setVolumeProperty(1);
    }
};

void Gap05Expect(const std::vector<float>& actual,const std::vector<std::int16_t>& golden,
                 int begin,int repeats,bool subregion)
{
    ASSERT_EQ(actual.size()%2,0u);
    const int total=8192+(repeats==255 ? 0 : repeats*(subregion ? 4096 : 8192));
    for (std::size_t i=0; i<actual.size()/2; ++i)
    {
        const int at=begin+static_cast<int>(i);
        int source=at;
        if (!subregion) source=at%8192;
        else if (repeats!=0 && at>=2048)
        {
            const int loopEnd=2048+(repeats+1)*4096;
            source=repeats==255 || at<loopEnd ? 2048+(at-2048)%4096 : 6144+(at-loopEnd);
        }
        for (int c=0; c<2; ++c)
        {
            // Independent CLI S16 /32768; 2 LSB decoder budget + float mixing noise.
            const float expected=repeats!=255 && at>=total ? 0.0f : golden.at(source*2+c)/32768.0f;
            ASSERT_NEAR(actual[i*2+c],expected,3.0f/32768.0f) << "frame " << at << " channel " << c;
        }
    }
}

class Gap05EncodedPlayback : public ::testing::TestWithParam<std::tuple<std::string,bool>>
{
protected:
    void SetUp() override
    {
        EnsureMixer(); ASSERT_EQ(factoryCalls,1); ASSERT_NE(manualDevice,nullptr);
        SoundEffect::setMasterVolumeProperty(1.0f);
    }
};

TEST_P(Gap05EncodedPlayback, CachedSoundDurationAndLoopMetadataMatchDecodedFrames)
{
    const auto [stem,streaming]=GetParam(); Gap05Banks banks(stem,0,streaming);
    const auto* effect=WaveBankTestAccess::GetSoundEffect(*banks.wave,0);
    ASSERT_NE(effect,nullptr);
    // Independent golden: 8192 native44100Hz frames; no encoded-byte duration estimate.
    EXPECT_NEAR(effect->getDurationProperty().getTotalSecondsProperty(),8192.0/44100.0,1e-6);
    EXPECT_EQ(WaveBankTestAccess::GetSoundEffect(*banks.wave,0),effect);
    auto instance=effect->CreateInstance();
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(instance),streaming ? 0u : 2048u);
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(instance),streaming ? 8192u : 4096u);
}

void Gap05ExpectCompleteWaves(Gap05Banks& banks,Cue& cue,int waves)
{
    for(int pass=0;pass<waves;++pass)
    {
        Gap05Expect(manualDevice->Render(8192),banks.golden,0,0,false);
        ExpectSilence(manualDevice->Render(32));banks.Update(cue);
    }
}

TEST_P(Gap05EncodedPlayback, WholeOutputFiniteLoopRegionAndCompletionMatchReference)
{
    const auto [stem,streaming]=GetParam();
    // HND-XACT-05: raw n plays n+1 passes. A plain PlayWave hands n (and 255) to the
    // decoded wave's buffer region; NEW_ON_LOOP replays complete decoded waves.
    for (const bool newOnLoop : {false,true})
    for (int count : {0,2,255})
    {
        SCOPED_TRACE(newOnLoop);
        SCOPED_TRACE(count);
        Gap05Banks banks(stem,static_cast<std::uint8_t>(count),streaming,false,false,false,newOnLoop);
        auto cue=banks.Play(); ASSERT_TRUE(cue->getIsPlayingProperty());
        if(newOnLoop)Gap05ExpectCompleteWaves(banks,*cue,count==255?3:count+1);
        else if(count==255)Gap05Expect(manualDevice->Render(24576),banks.golden,0,255,banks.subregion);
        else
        {
            Gap05Expect(manualDevice->Render(banks.Total(count)+32),banks.golden,0,count,banks.subregion);
            banks.Update(*cue);
        }
        EXPECT_EQ(cue->getIsStoppedProperty(),count!=255);
        EXPECT_EQ(cue->getIsPlayingProperty(),count==255);
        cue->Stop(AudioStopOptions::Immediate); ExpectSilence(manualDevice->Render(32));
    }
}

TEST_P(Gap05EncodedPlayback, PauseResumeStopsAndFreshPlaybackKeepDecodedPosition)
{
    // NEW_ON_LOOP raw2: three complete decoded waves (HND-XACT-05 n+1).
    const auto [stem,streaming]=GetParam(); Gap05Banks banks(stem,2,streaming,false,false,false,true);
    auto cue=banks.Play();
    Gap05Expect(manualDevice->Render(3001),banks.golden,0,0,false);
    cue->Pause(); EXPECT_TRUE(cue->getIsPausedProperty());
    ExpectSilence(manualDevice->Render(1024)); cue->Resume();
    Gap05Expect(manualDevice->Render(8192-3001),banks.golden,3001,0,false);
    ExpectSilence(manualDevice->Render(32));banks.Update(*cue);
    Gap05ExpectCompleteWaves(banks,*cue,2);
    EXPECT_TRUE(cue->getIsStoppedProperty());
    for (auto stop : {AudioStopOptions::Immediate,AudioStopOptions::AsAuthored})
    {
        auto fresh=banks.Play(); Gap05Expect(manualDevice->Render(3001),banks.golden,0,0,false);
        fresh->Stop(stop); ExpectSilence(manualDevice->Render(1024));
        EXPECT_TRUE(fresh->getIsStoppedProperty());
    }
    auto fresh=banks.Play();
    Gap05ExpectCompleteWaves(banks,*fresh,3);
    EXPECT_TRUE(fresh->getIsStoppedProperty());
}

TEST_P(Gap05EncodedPlayback, BufferRepeatsKeepTheirAdditionalPassesAndDecodedRegionContract)
{
    const auto [stem,streaming]=GetParam();
    for(const bool throughEnd:{false,true})
    {
        Gap05Banks banks(stem,0,streaming,false,throughEnd);
        const auto* effect=WaveBankTestAccess::GetSoundEffect(*banks.wave,0);ASSERT_NE(effect,nullptr);
        auto instance=effect->CreateInstance();
        SoundEffectInstanceTestAccess::SetXactLoopCount(instance,2);instance.Play();
        if(!throughEnd)
            Gap05Expect(manualDevice->Render(banks.Total(2)+32),banks.golden,0,2,banks.subregion);
        else
        {
            const int start=streaming?0:2048,total=8192+2*(8192-start);
            const auto pcm=manualDevice->Render(total+32);
            for(int frame=0;frame<total+32;++frame)for(int channel=0;channel<2;++channel)
            {
                const int source=frame<8192?frame:start+(frame-8192)%(8192-start);
                const float expected=frame>=total?0:banks.golden[source*2+channel]/32768.0f;
                ASSERT_NEAR(pcm[frame*2+channel],expected,3.0f/32768.0f)<<frame<<":"<<channel;
            }
        }
        EXPECT_EQ(instance.getStateProperty(),SoundState::Stopped);
    }
}

TEST_P(Gap05EncodedPlayback, BankDisposalStopsOutputOnceAndDoesNotRetainDecoder)
{
    const auto [stem,streaming]=GetParam();
    // Refresh WaveBank.Dispose(bool) IL_0011-001a guards repeat disposal;
    // FACTWaveBank_Destroy stops bank-dependent voices before freeing buffers.
    for (int i=0; i<8; ++i)
    {
        Gap05Banks banks(stem,255,streaming); auto cue=banks.Play();
        Gap05Expect(manualDevice->Render(3001),banks.golden,0,255,banks.subregion);
        int events=0;
        banks.wave->Disposing += [&](System::Object*,const System::EventArgs&) { ++events; };
        banks.wave->Dispose(); banks.wave->Dispose();
        EXPECT_EQ(events,1); EXPECT_TRUE(banks.wave->getIsDisposedProperty());
        // CNA's wave-bank teardown disposes the cues still using it (as XactLoopVariationCases
        // asserts too), and a disposed cue reports no state (Cue.get_IsStopped IL_0000-0014).
        EXPECT_TRUE(cue->getIsDisposedProperty()); ExpectSilence(manualDevice->Render(1024));
    }
}

TEST_P(Gap05EncodedPlayback, TruncatedEntryCannotCreateAPlayingVoice)
{
    const auto [stem,streaming]=GetParam(); Gap05Banks banks(stem,0,streaming,true);
    auto cue=banks.Play(false);
    // ReconcileState deliberately retains unresolved cues (Cue.cpp). Refusal
    // means no active voice and silence; Stop ends that unresolved cue lifetime.
    EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
    ExpectSilence(manualDevice->Render(8192+32));
    cue->Stop(AudioStopOptions::Immediate);
    EXPECT_TRUE(banks.wave->getIsInUseProperty()); EXPECT_TRUE(cue->getIsStoppedProperty());
    banks.sound->Dispose();EXPECT_FALSE(banks.wave->getIsInUseProperty());
}

TEST_P(Gap05EncodedPlayback, FiniteEventsWithThroughEofRegionReplayCompleteDecodedWaves)
{
    const auto [stem,streaming]=GetParam(); Gap05Banks banks(stem,2,streaming,false,true,false,true);
    auto cue=banks.Play();
    auto* instance=CueTestAccess::ActiveInstance(*cue,0); ASSERT_NE(instance,nullptr);
    const int start=streaming ? 0 : 2048;
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopStart(*instance),static_cast<unsigned>(start));
    EXPECT_EQ(SoundEffectInstanceTestAccess::LoopLength(*instance),0u);
    // All three NEW_ON_LOOP activations replay all8192 frames, irrespective of loop start.
    Gap05ExpectCompleteWaves(banks,*cue,3);
    EXPECT_TRUE(cue->getIsStoppedProperty());
}

TEST(Gap05EncodedRefusal, GenuineWmaProNeverCreatesAPlayingVoiceInEitherStorageMode)
{
    EnsureMixer(); ASSERT_NE(manualDevice,nullptr);
    // Phase 3 fallback: the genuine SDK Pro bank has37713 native frames but
    // FFmpeg yields35665. Even a prefix that decodes must not qualify playback.
    for (bool streaming : {false,true})
    {
        SCOPED_TRACE(streaming);
        Gap05Banks banks("wmapro",0,streaming);
        testing::internal::CaptureStderr();
        const auto* effect=WaveBankTestAccess::GetSoundEffect(*banks.wave,0);
        const auto log=testing::internal::GetCapturedStderr();
        EXPECT_EQ(effect,nullptr);
        EXPECT_NE(log.find("WMA Pro decoding is unqualified"),std::string::npos) << log;
        auto cue=banks.Play(false);
        EXPECT_EQ(CueTestAccess::ActiveInstance(*cue,0),nullptr);
        ExpectSilence(manualDevice->Render(1024));
        cue->Stop(AudioStopOptions::Immediate);
        EXPECT_TRUE(cue->getIsStoppedProperty()); EXPECT_TRUE(banks.wave->getIsInUseProperty());
        banks.wave->Dispose(); banks.wave->Dispose();
        EXPECT_TRUE(banks.wave->getIsDisposedProperty());
    }
}

TEST(Gap05NativePlayback, GenuineWmav2ReportsNativeDurationAndPlaysTheCompleteIndependentTail)
{
    EnsureMixer(); ASSERT_NE(manualDevice,nullptr);
    SoundEffect::setMasterVolumeProperty(1.0f);
    for (bool streaming : {false,true})
    {
        SCOPED_TRACE(streaming);
        Gap05Banks banks("wmav2",0,streaming,false,false,true);
        const auto* effect=WaveBankTestAccess::GetSoundEffect(*banks.wave,0);
        ASSERT_NE(effect,nullptr);
        // Unmodified native Duration=32768, native rate44100, independent CLI PCM.
        EXPECT_NEAR(effect->getDurationProperty().getTotalSecondsProperty(),32768.0/44100.0,1e-6);
        auto cue=banks.Play();
        const auto actual=manualDevice->Render(32768+32);
        ASSERT_EQ(actual.size(),(32768u+32u)*2);
        for (std::size_t i=0; i<actual.size(); ++i)
        {
            const float expected=i<32768u*2 ? banks.golden.at(i)/32768.0f : 0.0f;
            ASSERT_NEAR(actual[i],expected,3.0f/32768.0f) << "native sample " << i;
        }
        EXPECT_TRUE(cue->getIsStoppedProperty());
    }
}

// Pro is an explicit refusal until native duration/priming is measured. The
// independent six-channel matrix remains covered separately above and in pure tests.
INSTANTIATE_TEST_SUITE_P(Wmav2ResidentEmbeddedStream,Gap05EncodedPlayback,
    ::testing::Combine(::testing::Values(std::string("wmav2")),
                       ::testing::Bool()));
#endif
