// SPDX-License-Identifier: MS-PL
// DynamicSoundEffectInstance cases that need submitted buffers to be consumed (or provably not
// consumed). Included in the isolated manual-device harness, for both real mixers: the test renders
// exactly the frames that consume the buffers, instead of waiting for a real device thread against
// a deadline (independent review rv-prv2 of RV-DSI-STOP). The harness links only the audio module,
// so the dynamic instance's own Update stands in for FrameworkDispatcher.Update: both deliver the
// instance's reserved batch through the same DynamicSoundEffectDispatch path.
namespace DsiOffline
{
using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;
using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::SoundState;

class DynamicSoundEffectOffline : public ::testing::Test
{
protected:
    void SetUp() override
    {
        EnsureMixer();
        ASSERT_EQ(factoryCalls, 1) << "real selected device must never be opened";
        ASSERT_NE(manualDevice, nullptr);
    }

    void TearDown() override
    {
        // Drain whatever a failed case left playing, so the next case starts silent.
        (void)manualDevice->Render(32768);
    }

    // Renders until the mixer has taken every queued byte of d's stream. Each Render pulls a
    // fixed number of frames, so this is a bounded, clock-free loop.
    static void ConsumeEverything(const DynamicSoundEffectInstance& d)
    {
        for (int i = 0; i < 64 && DynamicSoundEffectInstanceTestAccess::StreamQueuedBytes(d) != 0; ++i)
            (void)manualDevice->Render(1024);
        ASSERT_EQ(DynamicSoundEffectInstanceTestAccess::StreamQueuedBytes(d), 0);
    }
};

// RV-DSI-STOP: Refresh kernel Stop IL_0065-0072 only exits a loop; a dynamic packet has none.
// Neither the Playing/Paused state, the buffer count nor the posted messages change. Nothing is
// rendered between Play and the checks, so the buffer cannot finish behind the test's back.
TEST_F(DynamicSoundEffectOffline, StopFalsePreservesPlayingAndPausedDynamicVoices)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.SubmitBuffer(std::vector<unsigned char>(4 * 4096, 0));
    d.Play();
    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++calls; };
    EXPECT_NO_THROW(d.Stop(false));
    EXPECT_EQ(d.getStateProperty(), SoundState::Playing);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
    d.Update();
    EXPECT_EQ(calls, 1) << "the start notification survives";
    d.Pause();
    ASSERT_EQ(d.getStateProperty(), SoundState::Paused);
    EXPECT_NO_THROW(d.Stop(false));
    EXPECT_EQ(d.getStateProperty(), SoundState::Paused);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
    d.Update();
    EXPECT_EQ(calls, 1) << "ExitLoop posts nothing";
    d.Stop(true);
}

// Review rv-fix8 #2: XNA's dispatcher clears its copied batch only after every callback returned
// (FrameworkDispatcher.Update IL_0031-0037, IL_00de-00e5, IL_0112-0117), so a handler that throws
// leaves the whole batch for the next update: three completions, a first callback that throws,
// then a clean update -- four callbacks in all, even if the failing handler Stops the voice.
TEST_F(DynamicSoundEffectOffline, AThrowingHandlerLeavesTheWholeBatchForTheNextUpdate)
{
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    const std::vector<unsigned char> pcm(4 * 64, 0); // 8 ms each
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) {
        if (calls++ == 0)
        {
            d.Stop(true); // Stop must not invalidate this reserved batch or its restoration.
            throw std::runtime_error("first handler call fails");
        }
    };
    d.Play();
    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);
    ConsumeEverything(d);
    ASSERT_EQ(d.getPendingBufferCountProperty(), 0);
    EXPECT_THROW(d.Update(), std::runtime_error);
    EXPECT_EQ(calls, 1);
    d.Update();
    EXPECT_EQ(calls, 4) << "the failed batch of three is delivered again in full";
    d.Update();
    EXPECT_EQ(calls, 4);
    d.Stop(true);
}

// Owner decision 2026-11-12: three buffers pending at Play post nothing (more than two), and each
// completion that leaves at most two posts one (OnBufferEnd IL_004b-006a) -- three in all. The
// count drops as the buffers finish, before any update (get_PendingBufferCount IL_002f-0035 reads
// the live native count); the notifications wait for FrameworkDispatcher.Update.
TEST_F(DynamicSoundEffectOffline, EachCompletedBufferPostsOneNotificationAndTheCountIsLive)
{
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++calls; };
    const std::vector<unsigned char> pcm(4 * 64, 0); // 64 stereo S16 frames: 8 ms each
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    d.Play();
    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 3) << "nothing rendered, nothing finished";
    ConsumeEverything(d);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0) << "played buffers stop being pending without an update";
    EXPECT_EQ(calls, 0) << "completions post; only the dispatcher delivers";
    d.Stop(true); // RV-DSI-STOP: already posted completions survive without a flush to generate more.
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    d.Update();
    EXPECT_EQ(calls, 3);
    d.Update();
    EXPECT_EQ(calls, 3) << "a drained stream asks for nothing more";
    d.Stop(true);
}

// Natural ends post even at zero pending (OnBufferEnd IL_004b-006a); only the final Stopping
// flush end suppresses that post (IL_0039-0049). Stop must retire the buffers the mixer already
// consumed -- CNA notices a completion lazily -- before it counts what it flushes.
TEST_F(DynamicSoundEffectOffline, StopRetiresUnpolledCompletionsBeforeFlushing)
{
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    const std::vector<unsigned char> pcm(4 * 64, 0);
    for (int k = 0; k < 3; ++k) d.SubmitBuffer(pcm);
    d.Play();
    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++calls; };
    ConsumeEverything(d);
    ASSERT_EQ(DynamicSoundEffectInstanceTestAccess::PendingBuffers(d), 3u) << "no CNA retirement yet";
    d.Stop();
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    d.Update();
    EXPECT_EQ(calls, 3) << "three natural ends, rather than two flush posts";
    d.Update();
    EXPECT_EQ(calls, 3);
}
} // namespace DsiOffline
