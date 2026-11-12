// SPDX-License-Identifier: MS-PL
// RV-SFX-DISPOSE: included in the isolated manual-device harness, for both real mixers.
// XNA Refresh SoundEffect.Play IL_0089-0095; Dispose(bool) IL_0079-00a2.
namespace SfxDispose
{
Bytes Tone(int frames = 16384)
{
    Bytes pcm;
    for (int i = 0; i < frames * 2; ++i) U16(pcm, 8192);
    return pcm;
}

class SoundEffectDisposeOffline : public ::testing::Test
{
    bool updateWasCalled_ = false;
protected:
    void SetUp() override
    {
        using namespace CNA::Internal::FrameworkUpdates;
        updateWasCalled_ = UpdateCalledAtLeastOnce();
        SetUpdateCalledAtLeastOnceForTests(true); // isolate the Play prerequisite, IL_0000-0021.
        EnsureMixer();
        SetMixerMasterGain(1.0f);
    }

    void TearDown() override
    {
        // A red run on HEAD leaves these finite voices alive. Finish them so each test
        // still starts silent; no sleeping or device output is involved.
        (void)manualDevice->Render(32768);
        CNA::Internal::FrameworkUpdates::SetUpdateCalledAtLeastOnceForTests(updateWasCalled_);
    }

    static void ExpectAudible(const std::vector<float>& samples)
    {
        EXPECT_TRUE(std::any_of(samples.begin(), samples.end(),
                                [](float value) { return std::abs(value) > 0.01f; }));
    }
};

TEST_F(SoundEffectDisposeOffline, DisposesEveryFireAndForgetAndExplicitChildOnlyForItsEffect)
{
    SoundEffect effect(Tone(), 44100, AudioChannels::Stereo);
    SoundEffect other(Tone(), 44100, AudioChannels::Stereo);
    auto child = effect.CreateInstance();
    child.setIsLoopedProperty(true);
    child.Play();
    ASSERT_TRUE(effect.Play());
    ASSERT_TRUE(effect.Play(0.5f, 0.0f, -1.0f));
    ASSERT_TRUE(effect.Play(0.5f, 0.0f, 1.0f));
    ASSERT_TRUE(other.Play());
    ExpectAudible(manualDevice->Render(64));
    effect.Dispose();
    EXPECT_TRUE(child.getIsDisposedProperty());
    EXPECT_THROW((void)child.getStateProperty(), System::ObjectDisposedException);
    EXPECT_NO_THROW(effect.Dispose());
    // Only the other effect remains: do not stop the entire mixer's tracks.
    for (float sample : manualDevice->Render(64))
        EXPECT_NEAR(sample, 0.25f, 1e-6f); // HEAD: all three disposed-effect tracks also play.
    effect.Dispose();
    ExpectAudible(manualDevice->Render(64));
    other.Dispose();
    ExpectSilence(manualDevice->Render(64));
}

TEST_F(SoundEffectDisposeOffline, HandlesNaturalEofAndReclaimedNodesBeforeDisposingLivePlayback)
{
    {
        SoundEffect empty(std::string{});
        EXPECT_FALSE(empty.Play());
        empty.Dispose();
    }
    {
        SoundEffect temporary(Tone(32), 44100, AudioChannels::Stereo);
        ASSERT_TRUE(temporary.Play(1.0f, 0.0f, 1.0f));
    } // wrapper destruction does not invalidate callback-owned registry storage.
    ExpectAudible(manualDevice->Render(4096));
    ExpectSilence(manualDevice->Render(64));
    SoundEffect shortEffect(Tone(32), 44100, AudioChannels::Stereo);
    SoundEffect longEffect(Tone(), 44100, AudioChannels::Stereo);
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(shortEffect.Play(1.0f, 0.0f, i % 2 ? -1.0f : 1.0f));
        ExpectAudible(manualDevice->Render(4096)); // final cooked callback may follow stopped.
        ExpectSilence(manualDevice->Render(64));
    }
    // The next Play drains deferred native tracks and pan nodes; shortEffect must retain none.
    ASSERT_TRUE(longEffect.Play(1.0f, 0.0f, -1.0f));
    shortEffect.Dispose();
    ExpectAudible(manualDevice->Render(64));
    longEffect.Dispose();
    ExpectSilence(manualDevice->Render(64)); // HEAD: longEffect's track is not stopped.
}

TEST_F(SoundEffectDisposeOffline, RegistrationFollowsMoveAndContentRepresentative)
{
    SoundEffect source(Tone(), 44100, AudioChannels::Stereo);
    ASSERT_TRUE(source.Play(1.0f, 0.0f, 1.0f));
    SoundEffect moved(std::move(source));
    auto record = CNA::Internal::ContentOwnershipAccess::Record(moved);
    ASSERT_TRUE(record);
    ExpectAudible(manualDevice->Render(64));
    record->Dispose();
    EXPECT_TRUE(moved.getIsDisposedProperty());
    ExpectSilence(manualDevice->Render(64)); // HEAD: content disposal also misses this track.
    EXPECT_NO_THROW(moved.Dispose());
}

TEST_F(SoundEffectDisposeOffline, EngineDisposalDoesNotPreventLaterSoundDisposal)
{
    Banks banks(0, 0, false, false);
    SoundEffect effect(Tone(), 44100, AudioChannels::Stereo);
    ASSERT_TRUE(effect.Play(1.0f, 0.0f, -1.0f));
    banks.engine->Dispose();
    ExpectAudible(manualDevice->Render(64)); // AudioEngine does not own the shared mixer.
    effect.Dispose();
    ExpectSilence(manualDevice->Render(64)); // HEAD: standalone SoundEffect track survives.
}

TEST_F(SoundEffectDisposeOffline, PlayAndDisposeRaceLeavesNoVoiceOrLateRegistration)
{
    for (int i = 0; i < 32; ++i)
    {
        SoundEffect effect(Tone(), 44100, AudioChannels::Stereo);
        ASSERT_TRUE(effect.Play(1.0f, 0.0f, -1.0f)); // guarantees a red check on HEAD.
        std::barrier start(3);
        bool accepted = false, refused = false;
        std::exception_ptr error;
        std::thread player([&]
        {
            start.arrive_and_wait();
            try { accepted = effect.Play(1.0f, 0.0f, 1.0f); }
            catch (const System::ObjectDisposedException&) { refused = true; }
            catch (...) { error = std::current_exception(); }
        });
        std::thread disposer([&] { start.arrive_and_wait(); effect.Dispose(); });
        start.arrive_and_wait();
        // Exercise real mixing concurrently with the two control operations as well.
        (void)manualDevice->Render(64);
        player.join();
        disposer.join();
        EXPECT_FALSE(error);
        EXPECT_TRUE(accepted || refused);
        EXPECT_TRUE(effect.getIsDisposedProperty());
        EXPECT_THROW((void)effect.Play(), System::ObjectDisposedException);
        effect.Dispose();
        ExpectSilence(manualDevice->Render(64)); // HEAD: at least the first Play survives.
    }
}

// A deadlock must fail the suite, not hang it: run fn on a worker and end the process when it has
// not returned in time (ctest reports the nonzero exit).
template<class F>
void FinishesWithin(std::chrono::seconds limit, F fn, const char* what)
{
    auto done = std::async(std::launch::async, std::move(fn));
    if (done.wait_for(limit) != std::future_status::ready)
    {
        std::fprintf(stderr, "%s did not finish within %lld s: deadlock\n", what,
                     static_cast<long long>(limit.count()));
        std::fflush(stderr);
        std::_Exit(3);
    }
    done.get();
}

// Review rv-prv1 S1: a derived instance may dispose its effect from its own disposal hook. XNA's
// Monitor is reentrant and Dispose(bool) publishes disposal (IL_001e-0020) before the children run
// (IL_009d-009e), so the nested Dispose returns at once.
class EffectDisposingChild final : public SoundEffectInstance
{
public:
    EffectDisposingChild(SoundEffectInstance&& base, SoundEffect& effect)
        : SoundEffectInstance(std::move(base)), effect_(effect) {}
    using SoundEffectInstance::Dispose;
protected:
    void Dispose(bool disposing) override
    {
        SoundEffectInstance::Dispose(disposing);
        effect_.Dispose();
    }
private:
    SoundEffect& effect_;
};

TEST_F(SoundEffectDisposeOffline, AChildWhoseDisposalDisposesItsEffectDoesNotDeadlock)
{
    SoundEffect effect(Tone(), 44100, AudioChannels::Stereo);
    EffectDisposingChild child(effect.CreateInstance(), effect);
    child.setIsLoopedProperty(true);
    child.Play();
    ASSERT_TRUE(effect.Play());
    ExpectAudible(manualDevice->Render(64));
    FinishesWithin(std::chrono::seconds(20), [&] { effect.Dispose(); },
                   "SoundEffect.Dispose re-entered from a child's Dispose(bool)");
    EXPECT_TRUE(child.getIsDisposedProperty());
    EXPECT_TRUE(effect.getIsDisposedProperty());
    ExpectSilence(manualDevice->Render(64));
}

// A silent raw track whose cooked callback stands in for code running inside a mix iteration.
struct CallbackCarrier
{
    CNA::Internal::Audio::MixerAudioPtr audio;
    CNA::Internal::Audio::MixerTrack* track = nullptr;

    CallbackCarrier(CNA::Internal::Audio::MixerTrackMixCallback callback, void* userdata)
    {
        const std::vector<std::byte> silence(16384 * 4);
        audio = CNA::Internal::Audio::LoadMixerRawAudio(
            silence, CNA::Internal::Audio::MixerFormat{44100, 2,
                CNA::Internal::Audio::MixerSampleFormat::Signed16});
        track = CNA::Internal::Audio::CreateMixerTrack();
        if (!audio || !track || !CNA::Internal::Audio::SetMixerTrackAudio(track, audio.get())) return;
        CNA::Internal::Audio::SetMixerTrackMixCallback(track, callback, userdata);
    }
    ~CallbackCarrier()
    {
        if (!track) return;
        CNA::Internal::Audio::SetMixerTrackMixCallback(track, nullptr, nullptr);
        CNA::Internal::Audio::StopMixerTrack(track);
        CNA::Internal::Audio::DestroyMixerTrack(track);
    }
};

struct CallbackDisposal
{
    SoundEffect* effect = nullptr;
    std::atomic<int> calls{0};
    std::atomic<bool> entered{false};
    std::atomic<bool> playStarted{false};
};

void DisposeFromAMixCallback(void* userdata, CNA::Internal::Audio::MixerTrack*, int, float*, int)
{
    auto* request = static_cast<CallbackDisposal*>(userdata);
    if (request->calls++ == 0) request->effect->Dispose();
}

// Review rv-prv1 S3: SDL saves the next track before it calls a track's cooked callback and visits
// it afterwards. The effect's fire-and-forget track was started first, so it is that next track; a
// Dispose from the callback must stop it without freeing it under the iteration.
TEST_F(SoundEffectDisposeOffline, DisposalFromAnotherTracksMixCallbackKeepsTheIterationValid)
{
    SoundEffect effect(Tone(), 44100, AudioChannels::Stereo);
    ASSERT_TRUE(effect.Play());
    CallbackDisposal request;
    request.effect = &effect;
    CallbackCarrier carrier(DisposeFromAMixCallback, &request);
    ASSERT_NE(carrier.track, nullptr);
    ASSERT_TRUE(CNA::Internal::Audio::PlayMixerTrack(carrier.track));
    (void)manualDevice->Render(64);
    EXPECT_GE(request.calls.load(), 1);
    EXPECT_TRUE(effect.getIsDisposedProperty());
    ExpectSilence(manualDevice->Render(64));
}

void DisposeWhilePlayWaitsForTheMixer(void* userdata, CNA::Internal::Audio::MixerTrack*, int, float*, int)
{
    auto* request = static_cast<CallbackDisposal*>(userdata);
    if (request->calls++ != 0) return;
    request->entered.store(true);
    for (int i = 0; i < 5000 && !request->playStarted.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Give Play time to reach the mixer barrier this render holds; correctness does not depend
    // on it, only whether the old lock cycle could form.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    request->effect->Dispose();
}

// Review rv-prv1 S2: a mix iteration holds the mixer lock while a callback disposes the effect,
// as another thread's Play waits for that lock. Play must not hold the effect's lock while it
// waits, or the two block each other; afterwards Play sees the disposal and refuses.
TEST_F(SoundEffectDisposeOffline, DisposalFromAMixCallbackWhilePlayWaitsDoesNotDeadlock)
{
    SoundEffect effect(Tone(), 44100, AudioChannels::Stereo);
    CallbackDisposal request;
    request.effect = &effect;
    CallbackCarrier carrier(DisposeWhilePlayWaitsForTheMixer, &request);
    ASSERT_NE(carrier.track, nullptr);
    ASSERT_TRUE(CNA::Internal::Audio::PlayMixerTrack(carrier.track));
    bool refused = false;
    bool accepted = false;
    FinishesWithin(std::chrono::seconds(20), [&] {
        std::thread renderer([] { (void)manualDevice->Render(64); });
        for (int i = 0; i < 10000 && !request.entered.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        request.playStarted.store(true);
        try { accepted = effect.Play(); }
        catch (const System::ObjectDisposedException&) { refused = true; }
        renderer.join();
    }, "SoundEffect.Play racing a Dispose from a mix callback");
    EXPECT_TRUE(request.entered.load());
    EXPECT_TRUE(refused) << "the effect was disposed while Play waited for the mixer";
    EXPECT_FALSE(accepted);
    EXPECT_TRUE(effect.getIsDisposedProperty());
    ExpectSilence(manualDevice->Render(64));
}

#if !defined(CNA_GAP01C_CNA_MIXER) && defined(CNA_AUDIO_PLATFORM_SDL3)
TEST_F(SoundEffectDisposeOffline, DisposesAfterWholeMixerTeardownWithoutReopeningIt)
{
    // Keep this harness's one-device invariant intact for the other suites.
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT({
        SoundEffect retired(Tone(), 44100, AudioChannels::Stereo);
        auto child = retired.CreateInstance();
        child.Play();
        ASSERT_TRUE(retired.Play(1.0f, 0.0f, 1.0f));
        DestroyMixer(); // externally synchronized, as AudioMixer.hpp requires.
        const int afterShutdown = factoryCalls;
        retired.Dispose();
        EXPECT_TRUE(child.getIsDisposedProperty());
        retired.Dispose();
        EXPECT_EQ(factoryCalls, afterShutdown); // no device needed to dispose an empty registry.
        // Recreated generation: old queued nodes cannot unlink/stop a new effect's live track.
        SoundEffect current(Tone(), 44100, AudioChannels::Stereo);
        ASSERT_TRUE(current.Play(1.0f, 0.0f, -1.0f));
        retired.Dispose();
        ExpectAudible(manualDevice->Render(64));
        current.Dispose();
        ExpectSilence(manualDevice->Render(64)); // HEAD: the recreated track still plays.
        std::_Exit(::testing::Test::HasFailure() ? 1 : 0);
    }, ::testing::ExitedWithCode(0), "");
}
#endif
} // namespace SfxDispose
