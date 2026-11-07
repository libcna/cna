// SPDX-License-Identifier: MS-PL
// Included inside the existing isolated offline harness namespace after ValueEventCases.
// Proposed routing policy in DESIGN.md must be approved before these output tests land.
namespace Gap04
{
void F(Bytes &b, float v)
{
    uint32_t u;
    std::memcpy(&u, &v, 4);
    U32(b, u);
}
void P16(Bytes &b, std::size_t p, uint16_t v)
{
    b.at(p) = v & 255;
    b.at(p + 1) = v >> 8;
}
void P32(Bytes &b, std::size_t p, uint32_t v)
{
    P16(b, p, v & 65535);
    P16(b, p + 2, v >> 16);
}
struct SettingsBytes
{
    Bytes data;
    uint32_t globalSend, localSend, preset[2];
};
SettingsBytes Settings(bool mixRpc = false, float firstMix = 100.0f)
{
    // Full XGS header: 19+7*2+11*4=77. Literal format oracle: FACT_internal.c:2073-2090.
    SettingsBytes s;
    auto &b = s.data;
    b.resize(77, 0);
    b[0] = 'X';
    b[1] = 'G';
    b[2] = 'S';
    b[3] = 'F';
    P16(b, 4, 46);
    P16(b, 6, 42);
    b[18] = 3;
    P16(b, 19, 1);
    P16(b, 21, 3);
    P16(b, 27, 3);
    P16(b, 29, 2);
    P16(b, 31, 44);
    P32(b, 33, static_cast<uint32_t>(b.size()));
    U8(b, 255);
    U16(b, 0);
    U16(b, 0);
    U8(b, 0);
    U16(b, 65535);
    U8(b, 128);
    U8(b, 0);
    P32(b, 37, static_cast<uint32_t>(b.size()));
    for (int i = 0; i < 3; ++i)
    {
        U8(b, i == 2 ? 5 : 1);
        F(b, i == 1 ? 1.0f : 0.0f);
        F(b, 0);
        F(b, 1);
    }
    P32(b, 57, static_cast<uint32_t>(b.size()));
    Text(b, "Default", 8);
    P32(b, 61, static_cast<uint32_t>(b.size()));
    Text(b, "GlobalSend", 11);
    Text(b, "Mix", 4);
    Text(b, "LocalSend", 10);
    P32(b, 65, static_cast<uint32_t>(b.size()));
    for (int i = 0; i < 3; ++i)
    {
        if (i == 0)
            s.globalSend = static_cast<uint32_t>(b.size());
        if (i == 2)
            s.localSend = static_cast<uint32_t>(b.size());
        U16(b, i);
        U8(b, 2);
        U16(b, i == 1 ? (mixRpc ? 26 : 0) : 2);
        // Linear send curves sum in centibels: x=0 -> 0cb, x=1 -> -2000cb.
        F(b, 0);
        F(b, i == 1 && mixRpc ? -100.0f : 0.0f);
        U8(b, 0);
        F(b, 1);
        F(b, i == 1 && mixRpc ? 200.0f : i == 1 ? 100.0f : -2000.0f);
        U8(b, 0);
    }
    P32(b, 69, static_cast<uint32_t>(b.size()));
    for (int i = 0; i < 2; ++i)
    {
        s.preset[i] = static_cast<uint32_t>(b.size());
        U8(b, 1);
        U16(b, 22);
        U16(b, 0);
    }
    P32(b, 73, static_cast<uint32_t>(b.size()));
    // FACT_internal.c:1154-1175 index map; FX_reverb.c:428-450 valid parameter units.
    const std::array<float, 22> values{10, 11, 6,    6, 27, 27, 8, 8, 8,   4,   8,
                                       4,  5,  5000, 0, 0,  0,  0, 1, 100, 100, 100};
    for (int preset = 0; preset < 2; ++preset)
        for (int i = 0; i < 22; ++i)
        {
            U8(b, 0);
            F(b, i == 21 ? (preset == 0 ? firstMix : 0.0f) : values[i]);
            F(b, i == 21 ? 0.0f : -10000.0f);
            F(b, i == 21 ? 100.0f : 10000.0f);
            U16(b, 0);
        }
    return s;
}
Bytes Sound(const SettingsBytes &s, bool wet, int preset, bool loop, uint32_t code = 0,
            bool trackLocal = false, bool repeatEvent = false, uint16_t fadeOut = 0)
{
    // Insert sound RPC(11 bytes) + DSP(7 bytes) before original track metadata.
    // Oracle: FACT_internal.c:2670-2714; all absolute following offsets move by 18.
    Bytes b = Xsb(loop ? 255 : 0);
    if (!wet)
        return b;
    Bytes block;
    U16(block, trackLocal ? 12 : 11);
    U8(block, trackLocal ? 1 : 2);
    U32(block, s.globalSend);
    if (trackLocal)
        U8(block, 1);
    U32(block, s.localSend);
    U16(block, 7);
    U8(block, 1);
    U32(block, code ? code : s.preset[preset]);
    const auto shift = static_cast<uint32_t>(block.size());
    b.insert(b.begin() + 212, block.begin(), block.end());
    b[202] = trackLocal ? 0x17 : 0x13;
    P32(b, 34, 238 + shift);
    P32(b, 66, 243 + shift);
    P32(b, 213 + shift, 221 + shift);
    P32(b, 243 + shift, 249 + shift);
    if (repeatEvent)
    {
        Bytes second(b.begin() + 222 + shift, b.begin() + 238 + shift);
        P32(second, 0, 1u | (20u << 5));
        b.insert(b.begin() + 238 + shift, second.begin(), second.end());
        b[221 + shift] = 2;
        P32(b, 34, 254 + shift);
        P32(b, 66, 259 + shift);
        P32(b, 259 + shift, 265 + shift);
    }
    if (fadeOut)
    {
        // XSB complex-cue instance metadata: flags4 + soundCode4 + transition4 +
        // limit1 + fadeIn2 + fadeOut2 + behavior1; FACT_internal.c complex cue parser.
        const uint32_t cue = 238 + shift + (repeatEvent ? 16 : 0);
        Bytes extra;
        U32(extra, 0);
        U8(extra, 255);
        U16(extra, 0);
        U16(extra, fadeOut);
        U8(extra, 0);
        b.insert(b.begin() + cue + 5, extra.begin(), extra.end());
        b[cue] = 4;
        P16(b, 19, 0);
        P16(b, 21, 1);
        P32(b, 34, 0xFFFFFFFFu);
        P32(b, 38, cue);
        P32(b, 66, cue + 15);
        P32(b, cue + 15, cue + 21);
    }
    return b;
}
class Banks
{
    std::filesystem::path directory_;
    std::string Write(const char *name, const Bytes &bytes)
    {
        auto p = directory_ / name;
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!f)
            throw std::runtime_error("cannot write GAP04 literal bank");
        return p.string();
    }

  public:
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    Banks(bool wet = true, int preset = 0, bool impulse = true, bool mixRpc = false,
          float firstMix = 100.0f, uint32_t code = 0, bool trackLocal = false, uint16_t fadeOut = 0,
          bool repeatEvent = false)
    {
        EnsureMixer();
        if (!manualDevice || factoryCalls != 1)
            throw std::runtime_error("manual device bypassed");
        SoundEffect::setMasterVolumeProperty(1.0f);
        static unsigned serial = 0;
        directory_ = CNA::TestSupport::FixtureRoot() / ("gap04-" + std::to_string(serial++));
        std::filesystem::create_directories(directory_);
        auto settings = Settings(mixRpc, firstMix);
        P16(settings.data, 80, fadeOut);
        engine = std::make_unique<AudioEngine>(Write("reverb.xgs", settings.data));
        engine->GetCategory("Default").SetVolume(1.0f);
        auto w = Xwb(0, false, false);
        // Native-rate mono PCM16, amplitude 4096/32768=1/8. No CNA generated samples.
        for (int i = 0; i < 192; ++i)
            P16(w, 168 + i * 2, (!impulse || i == 0) ? 4096 : 0);
        wave = std::make_unique<WaveBank>(engine.get(), Write("reverb.xwb", w));
        sound = std::make_unique<SoundBank>(
            engine.get(), Write("reverb.xsb", Sound(settings, wet, preset, !impulse, code, trackLocal,
                                                    repeatEvent, fadeOut)));
    }
    ~Banks()
    {
        sound.reset();
        wave.reset();
        engine.reset();
        std::error_code e;
        std::filesystem::remove_all(directory_, e);
    }
    std::unique_ptr<Cue> Play()
    {
        std::unique_ptr<Cue> c(sound->GetCue("Loop"));
        c->Play();
        return c;
    }
};
// Oracle: sound/track byte180 each give .6946086997304519cb; category byte128
// gives -586.9655945352697cb (FACT read_volbyte). P=(1/8)*10^((2*c180+c128)/2000).
// Explicit SetVolume(1) applies category128 and keeps voice gain below1.
constexpr float P = 0.063697417421035865f;
void Impulse(const std::vector<float> &pcm, float amplitude)
{
    ASSERT_GE(pcm.size(), 2u);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        ASSERT_NEAR(pcm[i], i < 2 ? amplitude : 0.0f, 2e-6f) << i;
}
void Constant(const std::vector<float> &pcm, float amplitude)
{
    ASSERT_FALSE(pcm.empty());
    for (float v : pcm)
        ASSERT_NEAR(v, amplitude, 2e-6f);
}
void Tail(const std::vector<float> &pcm)
{
    double energy = 0;
    for (float v : pcm)
    {
        ASSERT_TRUE(std::isfinite(v));
        energy += double(v) * v;
    }
    // A nonzero tail is a necessary effect witness, not native timbre/decay parity.
    EXPECT_GT(energy, 1e-12);
}
TEST(XactReverb, DryImpulseHasNoTail)
{
    Banks b(false);
    auto c = b.Play();
    Impulse(manualDevice->Render(192 + 44100), P);
}
TEST(XactReverb, AuthoredPresetProducesTailAfterSourceEof)
{
    Banks b;
    auto c = b.Play();
    const auto first = manualDevice->Render(192);
    // 10ms reflection pre-delay >192/44100s: first source packet is dry only.
    Impulse(first, P);
    Tail(manualDevice->Render(44100));
}
TEST(XactReverb, EarlyAllPassAndFirstLateCombMatchHandDerivedImpulse)
{
    Banks b;
    auto c = b.Play();
    const auto pcm = manualDevice->Render(2043);
    // FX_reverb.c:49-52,359-368,452-480,704-715,732-758,890-940.
    // k=.6-(8/15)*.2=37/75. Pre-delay floor(44100*10/1000)=441.
    // Early APF delay floor(44100*13.28/1000)=585; y[0]=-k,
    // y[585]=1-k^2, y[1170]=k*(1-k^2). Room/position/EQ gains are unity.
    // First late comb: 441+floor(44100*11/1000)+floor(44100*25.31/1000)
    // =441+485+1116=2042, with 1/8 averaging and four APF feedthroughs (-k)^4.
    constexpr float k = 37.0f / 75.0f;
    for (int frame = 0; frame < 2043; ++frame)
    {
        float expected = frame == 0      ? P
                         : frame == 441  ? -k * P
                         : frame == 1026 ? (1 - k * k) * P
                         : frame == 1611 ? k * (1 - k * k) * P
                         : frame == 2042 ? -(k * k * k * k * k) * P / 8
                                         : 0.0f;
        ASSERT_NEAR(pcm[frame * 2], expected, 2e-6f) << frame;
        ASSERT_NEAR(pcm[frame * 2 + 1], expected, 2e-6f) << frame;
    }
}
TEST(XactReverb, SecondAbsolutePresetCodeSelectsItsOwnWetDryMix)
{
    Banks b(true, 1);
    auto c = b.Play();
    // FX_reverb.c:834-835,956: WetDryMix=0 passes the send unchanged.
    // Proposed stereo bus m=(L+R)/2: m=P, dry+send=P+P=2P.
    Impulse(manualDevice->Render(192 + 44100), 2 * P);
}
TEST(XactReverb, GlobalAndCueSendCurvesSumInCentibelsAndUpdateLive)
{
    Banks b(true, 1, false);
    auto c = b.Play();
    Constant(manualDevice->Render(64), 2 * P);
    b.engine->SetGlobalVariable("GlobalSend", 1);
    b.engine->Update();
    // g=10^(-2000/2000)=.1; dry P + send .1P.
    Constant(manualDevice->Render(64), 1.1f * P);
    c->SetVariable("LocalSend", 1);
    b.engine->Update();
    // Two -2000cb curves => g=10^(-4000/2000)=.01, not .2 or .1.
    Constant(manualDevice->Render(64), 1.01f * P);
}
TEST(XactReverb, TrackSendCombinesWithSoundSendWithoutLosingItsBinding)
{
    Banks b(true, 1, false, false, 100.0f, 0, true);
    auto c = b.Play();
    Constant(manualDevice->Render(64), 2 * P);
    b.engine->SetGlobalVariable("GlobalSend", 1);
    c->SetVariable("LocalSend", 1);
    b.engine->Update();
    // Sound -2000cb plus track -2000cb => .01 send, not .1 when the track list is discarded.
    Constant(manualDevice->Render(64), 1.01f * P);
}
TEST(XactReverb, GlobalPresetRpcClampsAndChangesAudibleMixWithoutRestart)
{
    Banks b(true, 0, true, true);
    b.engine->Update();
    auto first = b.Play();
    Impulse(manualDevice->Render(192), P);
    Tail(manualDevice->Render(1024));
    b.engine->SetGlobalVariable("Mix", 0);
    b.engine->Update();
    auto next = b.Play();
    b.engine->Update();
    // parameter26 targets index21. x=1 -> 200 clamps to100; x=0 -> -100
    // clamps to0, so the old wet history is inaudible.
    Impulse(manualDevice->Render(192 + 1024), 2 * P);
}
TEST(XactReverb, LargePullsKeepSendOffsetsAndSumSiblingRoutes)
{
    Banks b(true, 1, false);
    auto a = b.Play();
    auto c = b.Play();
    // Two sources, each dry P + send P, including CnaMixer's 1024-frame block boundaries.
    Constant(manualDevice->Render(5000), 4 * P);
}
TEST(XactReverb, EnginePresetStateCannotLeakToAnotherEngine)
{
    Banks a(true, 1, false), b(true, 1, false);
    auto ca = a.Play();
    auto cb = b.Play();
    a.engine->SetGlobalVariable("GlobalSend", 1);
    a.engine->Update();
    // A=1.1P, B=2P, independent engine state even with identical code offsets.
    Constant(manualDevice->Render(64), 3.1f * P);
    a.engine->Dispose();
    Constant(manualDevice->Render(64), 2 * P);
    b.engine->Dispose();
    ExpectSilence(manualDevice->Render(64));
}
TEST(XactReverb, CueSendAndPauseStopDisposeDoNotAffectSibling)
{
    Banks b(true, 1, false);
    auto a = b.Play();
    auto sibling = b.Play();
    a->SetVariable("LocalSend", 1);
    b.engine->Update();
    Constant(manualDevice->Render(64), 3.1f * P);
    a->Pause();
    Constant(manualDevice->Render(64), 2 * P);
    a->Resume();
    Constant(manualDevice->Render(64), 3.1f * P);
    a->Stop(AudioStopOptions::Immediate);
    Constant(manualDevice->Render(64), 2 * P);
    a->Dispose();
    Constant(manualDevice->Render(64), 2 * P);
    sibling->Stop(AudioStopOptions::AsAuthored); // no authored fade or release: stop source now
    ExpectSilence(manualDevice->Render(64));
}
TEST(XactReverb, PauseAndCueDisposalKeepSharedWetHistoryButEngineDisposalClearsIt)
{
    Banks b;
    auto c = b.Play();
    manualDevice->Render(192);
    c->Pause();
    Tail(manualDevice->Render(512));
    c->Dispose();
    Tail(manualDevice->Render(1024)); // shared engine bus remains alive
    b.engine->Dispose();
    ExpectSilence(manualDevice->Render(4096));
    Banks fresh(false);
    auto n = fresh.Play();
    Impulse(manualDevice->Render(4096), P);
}
TEST(XactReverb, AuthoredFadeAttenuatesDryAndSendTogether)
{
    Banks b(true, 1, false, false, 100.0f, 0, false, 100);
    auto now = std::chrono::steady_clock::time_point{std::chrono::milliseconds(1000)};
    AudioEngineTestAccess::SetEventClock(*b.engine, [&] { return now; });
    auto c = b.Play();
    Constant(manualDevice->Render(64), 2 * P);
    c->Stop(AudioStopOptions::AsAuthored);
    now += std::chrono::milliseconds(50);
    b.engine->Update();
    // FACT's linear authored fade: half of both dry P and bypassed-send P = P.
    Constant(manualDevice->Render(64), P);
    now += std::chrono::milliseconds(50);
    b.engine->Update();
    ExpectSilence(manualDevice->Render(64));
}
TEST(XactReverb, ScheduledLaterVoiceReceivesTheSameAuthoredRoute)
{
    Banks b(true, 1, true, false, 100.0f, 0, false, 0, true);
    auto now = std::chrono::steady_clock::time_point{std::chrono::milliseconds(1000)};
    AudioEngineTestAccess::SetEventClock(*b.engine, [&] { return now; });
    auto c = b.Play();
    Impulse(manualDevice->Render(224), 2 * P);
    now += std::chrono::milliseconds(20);
    b.engine->Update();
    Impulse(manualDevice->Render(4096), 2 * P); // second authored PlayWave, not Cue.Play replay
}
TEST(XactReverb, ReturnUsesMasterGainExactlyOnce)
{
    Banks b(true, 1);
    auto c = b.Play();
    SoundEffect::setMasterVolumeProperty(.5f);
    Impulse(manualDevice->Render(192 + 1024), P); // .5*(P+P), not .5P+.25P
    SoundEffect::setMasterVolumeProperty(1.0f);
}
TEST(XactReverb, PublicPostMixObserverStillReceivesFinalWetOutput)
{
    Banks b(true, 1);
    auto c = b.Play();
    struct Capture
    {
        float first = -99;
        int samples = 0;
    } capture;
    const auto callback = +[](void *p, int channels, float *pcm, int count)
    {
        auto &x = *static_cast<Capture *>(p);
        if (x.samples == 0 && count > 0)
            x.first = pcm[0];
        x.samples += count;
        (void)channels;
    };
    struct Remove
    {
        ~Remove() { (void)SetMixerPostMixCallback(nullptr, nullptr); }
    } remove;
    ASSERT_TRUE(SetMixerPostMixCallback(callback, &capture));
    Impulse(manualDevice->Render(192), 2 * P);
    EXPECT_NEAR(capture.first, 2 * P, 2e-6f);
    EXPECT_GE(capture.samples, 384);
}
TEST(XactReverb, UnresolvedPresetCodeCannotAliasToFirstPreset)
{
    Banks b(true, 0, true, false, 100.0f, 0xDEADBEEFu);
    std::unique_ptr<Cue> c(b.sound->GetCue("Loop"));
    // Proposed CNA refusal policy; native malformed-bank exception remains unqualified.
    EXPECT_THROW(c->Play(), System::InvalidOperationException);
    ExpectSilence(manualDevice->Render(4096));
}
TEST(XactReverb, ControlUpdatesAndEngineDisposalSynchronizeWithRendering)
{
    Banks b(true, 1, false);
    auto cue = b.Play();
    // All allocation below is on the test render thread, outside FillBuffer's production callback.
    // Main thread exclusively owns public XACT calls; device thread exclusively renders.
    std::atomic<bool> disposing = false;
    std::atomic<unsigned> blocks = 0, bad = 0;
    // Fixed cohorts prevent a synthetic infinite render loop starving the control thread.
    // Both threads enter each cohort together; the real callback barrier chooses old/new state.
    std::barrier begin(2), end(2);
    std::jthread renderer(
        [&]
        {
            for (int i = 0; i < 1000; ++i)
            {
                begin.arrive_and_wait();
                const auto pcm = manualDevice->Render(64);
                ++blocks;
                const float first = pcm.front();
                bool valid = std::abs(first - 2 * P) < 2e-6f || std::abs(first - 1.1f * P) < 2e-6f ||
                             first == 0 ||
                             (disposing.load(std::memory_order_acquire) && std::abs(first - P) < 2e-6f);
                for (float v : pcm)
                    valid = valid && std::isfinite(v) && std::abs(v - first) < 2e-6f;
                if (!valid)
                    ++bad;
                end.arrive_and_wait();
            }
        });
    for (int i = 0; i < 1000; ++i)
    {
        b.engine->SetGlobalVariable("GlobalSend", float(i & 1));
        if (i == 999)
            disposing.store(true, std::memory_order_release);
        begin.arrive_and_wait();
        if (i == 999)
            b.engine->Dispose();
        else
            b.engine->Update();
        end.arrive_and_wait();
    }
    renderer.join();
    EXPECT_GT(blocks.load(), 0u);
    EXPECT_EQ(bad.load(), 0u);
    ExpectSilence(manualDevice->Render(4096));
}
} // namespace Gap04
