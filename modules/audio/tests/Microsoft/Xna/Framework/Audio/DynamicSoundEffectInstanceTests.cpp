// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include <typeinfo>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Environment.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/EventArgs.hpp"
#include "System/TimeSpan.hpp"
#include "System/Environment.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "CNA/Internal/Audio/AudioMixer.hpp"

#include <SDL3_mixer/SDL_mixer.h>
#include "System/ArgumentException.hpp"

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::SoundState;

namespace Microsoft::Xna::Framework::Audio
{
    struct DynamicSoundEffectInstanceTestAccess
    {
        static auto QueuedBuffers(const DynamicSoundEffectInstance& instance)
        {
            std::lock_guard<std::mutex> lock(instance.queueMutex_);
            return instance.queuedBuffers_;
        }

        static bool IsFloat(const DynamicSoundEffectInstance& instance)
        {
            return instance.isFloat_;
        }

        static std::size_t PendingBuffers(const DynamicSoundEffectInstance& instance)
        {
            std::lock_guard<std::mutex> lock(instance.queueMutex_);
            return instance.queuedBuffers_.size() + instance.submittedChunkSizes_.size();
        }
    };
}

namespace
{
    void UseDummyAudioDriver()
    {
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    }

    // Tries to start playback under the SDL "dummy" audio driver so device-dependent
    // behaviour can be exercised headlessly. Returns true if the instance reached a
    // non-Stopped state; otherwise the caller should skip the assertion.
    // `frames` sizes the one stereo S16 buffer submitted before Play(). The 256-frame default
    // (~6 ms at 44.1 kHz) is right for tests that only need a Playing instance; a test that
    // asserts on PendingBufferCount after Play() must pass a buffer the device thread cannot
    // finish consuming before the test's next statement runs (see the starved-count test).
    bool tryStartHeadless(DynamicSoundEffectInstance& d, std::size_t frames = 256)
    {
        UseDummyAudioDriver();
        std::vector<unsigned char> pcm(4 * frames, 0); // `frames` stereo S16 frames of silence
        d.SubmitBuffer(pcm);
        try
        {
            d.Play();
        }
        catch (...)
        {
            return false;
        }
        return d.getStateProperty() != SoundState::Stopped;
    }

    // AUD-07-001/002/A-03: mirror of tryStartHeadless() above, but submits a float buffer
    // first so playback starts in float mode instead of int16 mode.
    bool tryStartHeadlessFloat(DynamicSoundEffectInstance& d)
    {
        UseDummyAudioDriver();
        std::vector<float> buf(4 * 256, 0.0f); // 256 stereo float frames of silence
        d.SubmitFloatBufferEXT(buf);
        try
        {
            d.Play();
        }
        catch (...)
        {
            return false;
        }
        return d.getStateProperty() != SoundState::Stopped;
    }
}

// ---------------------------------------------------------------------------
// Headless-safe tests (no audio device required)
// ---------------------------------------------------------------------------

TEST(DynamicSoundEffectInstanceTest, ConstructionDefaultState)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    EXPECT_FALSE(d.getIsDisposedProperty());
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
    EXPECT_FALSE(d.getIsLoopedProperty());
}

// cna-killer KF-12: XNA's constructor refuses a sample rate outside 8000..48000 Hz and a channel
// count other than mono or stereo with ArgumentOutOfRangeException. FNA validated neither, and
// P10-DYN-001..003 had followed FNA; since 2026-09-04 XNA decides.
TEST(DynamicSoundEffectInstanceTest, ConstructorRefusesWhatXnaRefuses)
{
    EXPECT_THROW(DynamicSoundEffectInstance(7999, AudioChannels::Mono), System::ArgumentOutOfRangeException);
    EXPECT_THROW(DynamicSoundEffectInstance(48001, AudioChannels::Stereo), System::ArgumentOutOfRangeException);
    EXPECT_THROW(DynamicSoundEffectInstance(0, AudioChannels::Mono), System::ArgumentOutOfRangeException);
    EXPECT_THROW(DynamicSoundEffectInstance(-1, AudioChannels::Mono), System::ArgumentOutOfRangeException);
    EXPECT_THROW(DynamicSoundEffectInstance(22050, static_cast<AudioChannels>(3)),
                 System::ArgumentOutOfRangeException);
    EXPECT_NO_THROW(DynamicSoundEffectInstance(8000, AudioChannels::Mono));
    EXPECT_NO_THROW(DynamicSoundEffectInstance(48000, AudioChannels::Stereo));
}

// ISSUE-038: genuine XNA's conversion helpers check disposal before validating inputs;
// the previous FNA-derived no-throw expectation was incorrect.
TEST(DynamicSoundEffectInstanceTest, GetSampleDurationAfterDisposeThrowsObjectDisposed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_THROW((void)d.GetSampleDuration(4000), System::ObjectDisposedException);
}

TEST(DynamicSoundEffectInstanceTest, GetSampleSizeInBytesAfterDisposeThrowsObjectDisposed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_THROW((void)d.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0)),
                 System::ObjectDisposedException);
}

// KF-12: a streamed voice cannot loop. XNA refuses IsLooped = true with InvalidOperationException
// and accepts false; FNA ignored both.
TEST(DynamicSoundEffectInstanceTest, IsLoopedTrueIsRefusedFalseIsAccepted)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    EXPECT_THROW(d.setIsLoopedProperty(true), System::InvalidOperationException);
    EXPECT_NO_THROW(d.setIsLoopedProperty(false));
    EXPECT_FALSE(d.getIsLoopedProperty());
}

// Regression for the override bug (T-2A): going through a SoundEffectInstance& must dispatch to
// the dynamic override, whose rule is XNA's: true is refused, false accepted.
TEST(DynamicSoundEffectInstanceTest, IsLoopedViaBaseRefFollowsTheDynamicRuleWhenStopped)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    SoundEffectInstance& base = d;
    bool lvalue = true;
    EXPECT_THROW(base.setIsLoopedProperty(lvalue), System::InvalidOperationException); // const bool&
    EXPECT_THROW(base.setIsLoopedProperty(true), System::InvalidOperationException);   // bool&&
    EXPECT_NO_THROW(base.setIsLoopedProperty(false));
    EXPECT_FALSE(d.getIsLoopedProperty());
}

// TQ-032: DynamicSoundEffectInstance.GetSampleSizeInBytes delegates to
// AudioFormat.SizeFromDuration (IL_0070-0077); AudioFormat IL_0000-0028 keeps
// fractional ms. At 8000 Hz, 1250 ticks = 1 frame: mono 2 bytes, stereo
// (1 + 1 % 2) * 4 = 8 bytes. 1249 ticks truncate to zero frames.
TEST(DynamicSoundEffectInstanceTest, SampleSizeUsesFractionalMillisecondsBeforeFrameRounding)
{
    DynamicSoundEffectInstance mono(8000, AudioChannels::Mono);
    DynamicSoundEffectInstance stereo(8000, AudioChannels::Stereo);
    struct Case { long long ticks; int mono; int stereo; };
    for (const Case c : {Case{1249, 0, 0}, {1250, 2, 8}, {2499, 2, 8},
                         {2500, 4, 8}, {3750, 6, 16}})
    {
        SCOPED_TRACE(c.ticks);
        const auto duration = System::TimeSpan::FromTicks(c.ticks);
        EXPECT_EQ(mono.GetSampleSizeInBytes(duration), c.mono);
        EXPECT_EQ(stereo.GetSampleSizeInBytes(duration), c.stereo);
    }
}

TEST(DynamicSoundEffectInstanceTest, SampleDurationRoundTrip)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    // 1 second of 16-bit stereo @ 44100 Hz = 44100 * 2ch * 2bytes = 176400 bytes.
    const int bytes = d.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0));
    EXPECT_EQ(bytes, 176400);
    EXPECT_NEAR(d.GetSampleDuration(176400).getTotalSecondsProperty(), 1.0, 1e-9);
}

// P9-DYNAMIC-008: the stereo round trip above doesn't independently exercise the channel-count
// divisor/multiplier -- mono halves the byte count for the same duration. XNA computes the frame
// count as (int)(ms * (double)(44100f / 1000f)), and 44100f / 1000f is 44.0999985 in float, so a
// second of mono is 44099 frames (88198 bytes); stereo recovers the frame through its
// frames % channels term (cna-killer KF-8).
TEST(DynamicSoundEffectInstanceTest, SampleDurationRoundTripMono)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    const int bytes = d.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0));
    EXPECT_EQ(bytes, 88198);
    EXPECT_NEAR(d.GetSampleDuration(88200).getTotalSecondsProperty(), 1.0, 1e-9);
}

// XNA DynamicSoundEffectInstance.GetSampleDuration (IL_002f-IL_0050): after the disposal check a
// negative size is ArgumentException, zero is TimeSpan.Zero, and the rest is the format's
// DurationFromSize -- whole milliseconds. Hand-derived: 2236 bytes at 31184 Hz stereo are 559
// frames, 17.926 ms -> 18 ms; 4 bytes (one frame, 0.0227 ms) -> 0 ms.
TEST(DynamicSoundEffectInstanceTest, GetSampleDurationRefusesANegativeSizeAndRoundsToMilliseconds)
{
    DynamicSoundEffectInstance d(31184, AudioChannels::Stereo);
    try { (void)d.GetSampleDuration(-1); FAIL(); }
    catch (const System::ArgumentOutOfRangeException&) { FAIL() << "a size refusal is a plain ArgumentException"; }
    catch (const System::ArgumentException& error)
    {
        // IL_0033-003d: ArgumentException(FrameworkResources.InvalidBufferSize), no ParamName.
        EXPECT_EQ(error.getParamNameProperty(), "");
        EXPECT_EQ(error.getMessageProperty(), "Buffer size cannot be negative.");
    }
    EXPECT_EQ(d.GetSampleDuration(0), System::TimeSpan::Zero);
    EXPECT_EQ(d.GetSampleDuration(2236).getTicksProperty(), 18 * System::TimeSpan::TicksPerMillisecond);
    EXPECT_EQ(d.GetSampleDuration(4), System::TimeSpan::Zero);
}

// XNA DynamicSoundEffectInstance.GetSampleSizeInBytes (IL_002f-IL_008a): a duration below zero
// or above int.MaxValue milliseconds is ArgumentOutOfRangeException("duration"); TimeSpan.Zero is
// 0; an OverflowException from SizeFromDuration becomes ArgumentOutOfRangeException("duration").
// Hand-derived overflow: 12 h at 48 kHz is 2,073,600,000 frames, times 4 bytes > int.MaxValue.
TEST(DynamicSoundEffectInstanceTest, GetSampleSizeInBytesRefusesWhatXnaRefuses)
{
    DynamicSoundEffectInstance d(48000, AudioChannels::Stereo);
    auto paramOf = [&](System::TimeSpan duration) -> std::string {
        try { (void)d.GetSampleSizeInBytes(duration); }
        catch (const System::ArgumentOutOfRangeException& error) { return error.getParamNameProperty(); }
        return "<no exception>";
    };
    EXPECT_EQ(paramOf(System::TimeSpan::FromTicks(-1)), "duration");
    EXPECT_EQ(paramOf(System::TimeSpan::FromMilliseconds(2147483648.0)), "duration");
    EXPECT_EQ(paramOf(System::TimeSpan::FromHours(12.0)), "duration");
    EXPECT_EQ(d.GetSampleSizeInBytes(System::TimeSpan::Zero), 0);
    // 6 h at 48 kHz stereo: 1,036,800,000 frames * 4 bytes = 4,147,200,000 > int.MaxValue too;
    // 3 h fits: 518,400,000 * 4 = 2,073,600,000.
    EXPECT_EQ(paramOf(System::TimeSpan::FromHours(6.0)), "duration");
    EXPECT_EQ(d.GetSampleSizeInBytes(System::TimeSpan::FromHours(3.0)), 2073600000);
}

// XNA's State and Stop(bool) belong to SoundEffectInstance and refuse a disposed instance
// (get_State IL_0012-IL_002f, Stop(bool) IL_0011-IL_002e) -- also for a dynamic one, whose
// ObjectDisposedException names its own type (GetType().Name). Pause is inherited unchanged.
TEST(DynamicSoundEffectInstanceTest, StateStopAndPauseRefuseADisposedInstance)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    try { (void)d.getStateProperty(); FAIL(); }
    catch (const System::ObjectDisposedException& error) { EXPECT_EQ(error.getObjectNameProperty(), "DynamicSoundEffectInstance"); }
    EXPECT_THROW(d.Stop(), System::ObjectDisposedException);
    EXPECT_THROW(d.Stop(true), System::ObjectDisposedException);
    EXPECT_THROW(d.Stop(false), System::ObjectDisposedException);
    SoundEffectInstance& base = d;
    EXPECT_THROW((void)base.getStateProperty(), System::ObjectDisposedException);
    EXPECT_THROW(base.Pause(), System::ObjectDisposedException);
}

TEST(DynamicSoundEffectInstanceTest, GetSampleDurationIgnoresFloatFormatMatchingFNA)
{
    // FNA's GetSampleSizeInBytes/GetSampleDuration always delegate to SoundEffect's versions,
    // which hardcode 16-bit PCM regardless of the instance's actual sample format -- after
    // SubmitFloatBufferEXT puts this instance in float (32-bit) mode, 1 second of stereo @
    // 44100 Hz must still be 176400 bytes, not double that (CP-6).
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<float> floatBuf(32, 0.0f);
    d.SubmitFloatBufferEXT(floatBuf);

    const int bytes = d.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0));
    EXPECT_EQ(bytes, 176400);
    EXPECT_NEAR(d.GetSampleDuration(176400).getTotalSecondsProperty(), 1.0, 1e-9);
}

TEST(DynamicSoundEffectInstanceTest, SubmitBufferQueuesWhileStopped)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(64, 0);
    d.SubmitBuffer(pcm);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
}

// P9-DYNAMIC-001: multiple SubmitBuffer calls while stopped must each add to
// PendingBufferCount, not just report 1 regardless of count.
TEST(DynamicSoundEffectInstanceTest, PendingBufferCountAccumulatesAcrossMultipleSubmits)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(64, 0);
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 3);
}

// Review rv-sfx #6: XNA's voice exists from the constructor (..ctor IL_0043-0048) and an
// immediate Stop flushes the buffers queued on it (KernelSoundEffectInstance.Stop IL_0011-0062),
// so buffers submitted before the first Play are gone after Stop()/Stop(true) and that Play starts
// empty. FNA (and CNA before) returned early without a voice and kept them. A non-immediate Stop
// before any Play changes nothing.
TEST(DynamicSoundEffectInstanceTest, ImmediateStopBeforeTheFirstPlayFlushesSubmittedBuffers)
{
    using Access = Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess;
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(64, 0);
    d.SubmitBuffer(pcm);
    d.Stop(false);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1) << "a non-immediate Stop flushes nothing";

    d.Stop();
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    EXPECT_TRUE(Access::QueuedBuffers(d).empty());
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);

    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    d.Stop(true);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
}

// Review rv-sfx #5: XNA submits to the voice whatever its state (AllocateData IL_001c-0029), so a
// buffer given to a paused instance is already on the stream when it resumes -- not staged until
// the next dispatcher update.
TEST(DynamicSoundEffectInstanceTest, ABufferSubmittedWhilePausedIsOnTheStreamAtResume)
{
    using Access = Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess;
    UseDummyAudioDriver();
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    try
    {
        d.Play();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    if (d.getStateProperty() != SoundState::Playing)
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    d.Pause();
    ASSERT_EQ(d.getStateProperty(), SoundState::Paused);
    d.SubmitBuffer(std::vector<unsigned char>(4 * 8000, 0)); // one second
    EXPECT_TRUE(Access::QueuedBuffers(d).empty()) << "submitted to the stream, not staged";
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
    d.Resume();
    EXPECT_EQ(d.getStateProperty(), SoundState::Playing);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
    d.Stop(true);
}

// TQ-032: SubmitBuffer(byte[],int,int) IL_0047-0051, 006a-0074,
// 007c-0087 and 00a2-00ac construct plain ArgumentException(message).
// Every refusal precedes AllocateData (IL_00ad-00b1): preserve an existing queue.
TEST(DynamicSoundEffectInstanceTest, SubmitBufferFailuresKeepQueueAndHaveNoParameterName)
{
    using Access = Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess;
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    const std::vector<unsigned char> pcm{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    d.SubmitBuffer(pcm, 4, 8);
    const std::vector<std::vector<unsigned char>> expected{{5,6,7,8,9,10,11,12}};
    const auto refuse = [&](const std::vector<unsigned char>& buffer, int offset, int count) {
        try { d.SubmitBuffer(buffer, offset, count); ADD_FAILURE() << "expected ArgumentException"; }
        catch (const System::ArgumentException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
            EXPECT_TRUE(error.getParamNameProperty().empty());
        }
        EXPECT_EQ(Access::QueuedBuffers(d), expected);
        EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
        EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
    };
    refuse({}, 0, 0);
    refuse(std::vector<unsigned char>(15), 0, 4);
    for (int offset : {-4, 2, 16}) refuse(pcm, offset, 4);
    for (int count : {-4, 0, 6, 16, 2147483644}) refuse(pcm, 4, count);
    d.Dispose();
    // Disposal takes precedence even with all arguments invalid (IL_0011-002e).
    EXPECT_THROW(d.SubmitBuffer({}, -1, -1), System::ObjectDisposedException);
    EXPECT_TRUE(Access::QueuedBuffers(d).empty());
}

TEST(DynamicSoundEffectInstanceTest, SubmitBufferRangeThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(16, 0);
    EXPECT_THROW(d.SubmitBuffer(pcm, -4, 4), System::ArgumentException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 0, -4), System::ArgumentException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 8, 16), System::ArgumentException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 16, 4), System::ArgumentException); // offset at the end
}

TEST(DynamicSoundEffectInstanceTest, SubmitFloatBufferRangeThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<float> buf(16, 0.0f);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf, -1, 4), System::ArgumentOutOfRangeException);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf, 0, 32), System::ArgumentOutOfRangeException);
}

// P9-VALIDATION-010/011: offset+count must be checked without computing the (possibly
// overflowing) sum in 32 bits -- see SoundEffect's identical fix/test. XNA refuses it with
// ArgumentException.
TEST(DynamicSoundEffectInstanceTest, SubmitBufferRangeIntegerOverflowThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(16, 0);
    constexpr int hugeOffset = 2000000000;
    constexpr int hugeCount  = 2000000000; // offset+count overflows int32
    EXPECT_THROW(d.SubmitBuffer(pcm, hugeOffset, hugeCount), System::ArgumentException);
}

// KF-12: XNA's SubmitBuffer takes whole sample frames only -- the buffer, the offset and the count
// must each be a multiple of the block alignment (4 bytes for 16-bit stereo), and an empty buffer
// is refused too, all with ArgumentException. FNA accepted any byte count (P9-DYNAMIC-009).
TEST(DynamicSoundEffectInstanceTest, SubmitBufferRefusesPartialFrames)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_THROW(d.SubmitBuffer(std::vector<unsigned char>(63, 0)), System::ArgumentException);
    EXPECT_THROW(d.SubmitBuffer(std::vector<unsigned char>{}), System::ArgumentException);
    const std::vector<unsigned char> pcm(64, 0);
    EXPECT_THROW(d.SubmitBuffer(pcm, 2, 32), System::ArgumentException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 0, 30), System::ArgumentException);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    EXPECT_NO_THROW(d.SubmitBuffer(pcm, 4, 32));
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
}

// P9-DYNAMIC-009: same as above, but for SubmitFloatBufferEXT, where `count` is a *sample* count
// (not bytes) -- 3 float samples for a Stereo instance isn't a whole number of stereo frames
// (3/2 = 1.5), mirroring the byte-count case above at the sample level. Still no validation in
// FNA or CNA.
TEST(DynamicSoundEffectInstanceTest, SubmitFloatBufferWithSampleCountNotDivisibleByChannelCountDoesNotThrow)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<float> buf(3, 0.0f); // 3 samples, not a whole number of stereo frames
    EXPECT_NO_THROW(d.SubmitFloatBufferEXT(buf));
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
}

// A refused partial-frame submission while playing leaves the buffer bookkeeping intact.
TEST(DynamicSoundEffectInstanceTest, SubmitBufferRefusesPartialFramesWhilePlaying)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    const int pending = d.getPendingBufferCountProperty();
    EXPECT_THROW(d.SubmitBuffer(std::vector<unsigned char>(63, 0)), System::ArgumentException);
    EXPECT_EQ(d.getPendingBufferCountProperty(), pending);
    EXPECT_NO_THROW(d.Update());
}

// P9-VALIDATION-011: without this, a caller that keeps submitting after Dispose() would grow
// queuedBuffers_ unboundedly (the buffers can never be consumed once track_ is gone).
TEST(DynamicSoundEffectInstanceTest, SubmitBufferAfterDisposeThrowsObjectDisposed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    std::vector<unsigned char> pcm(16, 0);
    EXPECT_THROW(d.SubmitBuffer(pcm), System::ObjectDisposedException);
}

TEST(DynamicSoundEffectInstanceTest, SubmitFloatBufferEXTAfterDisposeThrowsObjectDisposed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    std::vector<float> buf(16, 0.0f);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf), System::ObjectDisposedException);
}

TEST(DynamicSoundEffectInstanceTest, SubmitFloatBufferBeforePlayingIsAllowed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<float> buf(32, 0.25f);
    EXPECT_NO_THROW(d.SubmitFloatBufferEXT(buf));
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
}

// AUD-07-001/002/A-03: confirmed code risk from the 2026-07-17 deep audit -- a stopped instance
// that previously used SubmitFloatBufferEXT must be able to switch back to int16 mode via a
// plain SubmitBuffer() while Stopped (EnsureStream() rebuilds the stream from scratch on the
// next Play() anyway), rather than silently staying in float mode and feeding raw int16 bytes
// into what becomes a float-format SDL_AudioStream.
TEST(DynamicSoundEffectInstanceTest, SubmitBufferWhileStoppedSwitchesBackToIntModeAfterFloatSubmission)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    UseDummyAudioDriver();

    std::vector<float> floatBuf(4 * 256, 0.0f);
    d.SubmitFloatBufferEXT(floatBuf);

    // Still Stopped (never Played) -- committing back to int mode must be allowed.
    std::vector<unsigned char> pcm(4 * 256, 0);
    EXPECT_NO_THROW(d.SubmitBuffer(pcm));

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(d);
    ASSERT_EQ(track, nullptr) << "still stopped -- no track should exist yet";

    try
    {
        d.Play();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    if (d.getStateProperty() == SoundState::Stopped)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    track = SoundEffectInstanceTestAccess::GetTrack(d);
    ASSERT_NE(track, nullptr);
    SDL_AudioStream* stream = MIX_GetTrackAudioStream(track);
    ASSERT_NE(stream, nullptr);
    SDL_AudioSpec srcSpec{};
    SDL_AudioSpec dstSpec{};
    ASSERT_TRUE(SDL_GetAudioStreamFormat(stream, &srcSpec, &dstSpec));
    EXPECT_EQ(srcSpec.format, SDL_AUDIO_S16LE)
        << "stream must be int16, not left over as float from the earlier "
           "SubmitFloatBufferEXT call";
}

// AUD-07-008 (2026-07-17 deep audit, A-07 "strong risk"): EnsureStream() creates the
// SDL_AudioStream with a null destination spec (SDL3 uses the device's native format for output
// conversion) -- the audit flagged that SDL_PutAudioStreamData/SDL_GetAudioStreamData require
// valid specs at BOTH ends, and asked whether MIX_SetTrackAudioStream genuinely establishes the
// destination side before any data flows. Empirically confirmed via a direct probe: a stream's
// destination format is indeed invalid/absent immediately after SDL_CreateAudioStream(spec,
// nullptr) (SDL_GetAudioStreamFormat fails, "Stream has no destination format"), but
// MIX_SetTrackAudioStream immediately establishes it -- and Play()'s existing call order already
// has MIX_SetTrackAudioStream run before the first SubmitQueuedToStream()/SDL_PutAudioStreamData
// call (via QueueInitialBuffers(), itself called after MIX_SetTrackAudioStream succeeds). This
// test locks that down: both the source AND destination specs must be valid immediately after
// Play() returns, for a plain instance that never touched the float submission path at all.
TEST(DynamicSoundEffectInstanceTest, StreamDestinationFormatIsValidImmediatelyAfterPlay)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(d);
    ASSERT_NE(track, nullptr);
    SDL_AudioStream* stream = MIX_GetTrackAudioStream(track);
    ASSERT_NE(stream, nullptr);

    SDL_AudioSpec srcSpec{};
    SDL_AudioSpec dstSpec{};
    EXPECT_TRUE(SDL_GetAudioStreamFormat(stream, &srcSpec, &dstSpec))
        << "both source and destination formats must be valid once Play() has returned -- "
           "SDL_GetError(): " << SDL_GetError();
    EXPECT_EQ(srcSpec.format, SDL_AUDIO_S16LE);
    EXPECT_EQ(srcSpec.channels, 2);
    EXPECT_EQ(srcSpec.freq, 44100);
    EXPECT_NE(dstSpec.format, 0) << "destination format must not be left null/unset";
}

// AUD-07-001/002/A-03: symmetric to SubmitFloatAfterPlayingThrowsInvalidOperation below --
// submitting a plain int16 buffer into a *live* float-mode stream must throw rather than
// silently corrupt the stream's data with misinterpreted bytes.
TEST(DynamicSoundEffectInstanceTest, SubmitIntBufferAfterPlayingInFloatModeThrowsInvalidOperation)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadlessFloat(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    std::vector<unsigned char> pcm(32, 0);
    EXPECT_THROW(d.SubmitBuffer(pcm), System::InvalidOperationException);
}

TEST(DynamicSoundEffectInstanceTest, DisposeMarksDisposedAndIsIdempotent)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_TRUE(d.getIsDisposedProperty());
    EXPECT_NO_THROW(d.Dispose()); // idempotent
}

TEST(DynamicSoundEffectInstanceTest, PlayAfterDisposeThrowsObjectDisposed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_THROW(d.Play(), System::ObjectDisposedException);
}

// P9-VALIDATION-010: Resume() delegates to Play() when there's no active track_, which is
// also how a disposed instance surfaces this instead of silently no-op'ing -- see
// SoundEffectInstanceTests.cpp's identical base-class test for the full FNA-matching rationale.
// Resume() itself is the inherited SoundEffectInstance::Resume() (P13-DYNAMIC-001), whose Play()
// call dispatches virtually to this class's own Play() override.
TEST(DynamicSoundEffectInstanceTest, ResumeAfterDisposeThrowsObjectDisposed)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_THROW(d.Resume(), System::ObjectDisposedException);
}

TEST(DynamicSoundEffectInstanceTest, ResumeOnNeverPlayedInstanceStartsPlayback)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    UseDummyAudioDriver();
    std::vector<unsigned char> pcm(4 * 256, 0); // 256 stereo S16 frames of silence
    d.SubmitBuffer(pcm);

    ASSERT_EQ(d.getStateProperty(), SoundState::Stopped);
    try
    {
        d.Resume();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    EXPECT_EQ(d.getStateProperty(), SoundState::Playing);
}

TEST(DynamicSoundEffectInstanceTest, StopWhileStoppedIsSafe)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_NO_THROW(d.Stop());
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
}

TEST(DynamicSoundEffectInstanceTest, StopFalseWhileNeverPlayedIsSafeNoOp)
{
    // Matches FNA's handle==0 early return in Stop(bool): a non-immediate Stop before any
    // Play() has no active voice/track to be invalid about (CP-5).
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_NO_THROW(d.Stop(false));
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
}

TEST(DynamicSoundEffectInstanceTest, GetTypeNameIsFullyQualified)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    EXPECT_EQ(d.GetTypeName(), "Microsoft.Xna.Framework.Audio.DynamicSoundEffectInstance");
}

// ---------------------------------------------------------------------------
// Device-dependent tests (run under the SDL dummy driver; skipped if unavailable)
// ---------------------------------------------------------------------------

// T-2C: submitting a float buffer to an int-format instance after it starts must throw.
TEST(DynamicSoundEffectInstanceTest, SubmitFloatAfterPlayingThrowsInvalidOperation)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    std::vector<float> buf(32, 0.0f);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf), System::InvalidOperationException);
}

// CP-5: dynamic instances have no authored loop to release into, so a non-immediate Stop is
// invalid once playback has actually started (FNA: SoundEffectInstance.cs Stop(bool)).
TEST(DynamicSoundEffectInstanceTest, StopFalseAfterPlayingThrowsInvalidOperation)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    EXPECT_THROW(d.Stop(false), System::InvalidOperationException);
}

// T-2A: through a base reference on a playing dynamic instance the dynamic rule still applies,
// not the base setter's "cannot change while playing".
TEST(DynamicSoundEffectInstanceTest, IsLoopedViaBaseRefWhilePlayingFollowsTheDynamicRule)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    SoundEffectInstance& base = d;
    EXPECT_THROW(base.setIsLoopedProperty(true), System::InvalidOperationException);
    EXPECT_NO_THROW(base.setIsLoopedProperty(false));
    EXPECT_FALSE(d.getIsLoopedProperty());
}

// BufferNeeded must fire while the queue is starved during Update().
TEST(DynamicSoundEffectInstanceTest, BufferNeededFiresWhenStarved)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    int fired = 0;
    d.BufferNeeded += [&fired](System::Object*, const System::EventArgs&) { ++fired; };
    d.Update();
    EXPECT_GT(fired, 0);
}

// Owner decision 2026-11-12: XNA's cadence, not FNA's refill loop. The kernel posts one
// BufferNeeded when Play starts a Stopped voice with at most two buffers pending
// (KernelDynamicSoundEffectInstance.Play IL_001a-0030, IL_0055-0061) and FrameworkDispatcher.Update
// delivers it (DynamicSoundEffectInstance.OnBufferNeeded IL_0000-0012): one buffer pending, one
// notification, and none from Play itself.
TEST(DynamicSoundEffectInstanceTest, PlayFromStoppedPostsOneNotificationTheUpdateDelivers)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    int fired = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++fired; };
    if (!tryStartHeadless(d, 44100)) // one one-second buffer, then Play
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    EXPECT_EQ(fired, 0) << "Play never raises BufferNeeded itself";
    d.Update();
    EXPECT_EQ(fired, 1);
    d.Update();
    EXPECT_EQ(fired, 1) << "a pending buffer that has not finished posts nothing more";
}

// CP-4: a buffer must remain "pending" until the stream reports it was actually consumed, not
// the instant it's handed off to SDL. Pre-load 3 buffers (MINIMUM_BUFFER_CHECK,
// DynamicSoundEffectInstance.cpp) before playing, then Update() immediately -- nothing has had
// time to actually be consumed by playback yet, so BufferNeeded must not fire at all.
TEST(DynamicSoundEffectInstanceTest, BufferNeededDoesNotFireWhenStreamHasEnoughData)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    // One second per buffer, not the 256 frames (~6 ms) the other tests use: the audio device
    // thread pulls a whole device period from the stream as soon as Play() starts it, and three
    // 256-frame buffers fit inside one period. Whenever that first pull ran before the Update()
    // below -- under parallel load, routinely -- all three were genuinely consumed and
    // BufferNeeded fired three times, which is correct behaviour for a stream that has run dry
    // and not what this test is about. A buffer that takes a second to play cannot be consumed
    // in the moment between Play() and Update(), and a chunk only stops being pending once it is
    // consumed whole (AUDIO-BUFFER-001), so the stream really does have enough data here.
    std::vector<unsigned char> pcm(4 * 44100, 0); // 44100 stereo S16 frames of silence
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    if (!tryStartHeadless(d)) // submits a 3rd buffer, then Play()s
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    int fired = 0;
    d.BufferNeeded += [&fired](System::Object*, const System::EventArgs&) { ++fired; };
    d.Update();

    EXPECT_EQ(fired, 0);
}

// CP-15: Pause()/Resume() used to be silent no-ops on DynamicSoundEffectInstance -- the base
// class's implementation only ever touched the protected `track_` member, which a dynamic
// instance used to never populate (it managed its own separate `dynamicTrack_` instead). Fixed
// at the root by P13-DYNAMIC-001: DynamicSoundEffectInstance now shares `track_` directly, so the
// inherited (no longer overridden) Pause()/Resume() operate on the right field automatically.
TEST(DynamicSoundEffectInstanceTest, PauseThenResumeActuallyPausesAndResumesDynamicTrack)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);

    d.Pause();
    EXPECT_EQ(d.getStateProperty(), SoundState::Paused);

    d.Resume();
    EXPECT_EQ(d.getStateProperty(), SoundState::Playing);
}

// Pausing via a SoundEffectInstance& base reference must still work correctly (CP-15,
// P13-DYNAMIC-001) -- confirms the shared-`track_` fix works through ordinary virtual dispatch to
// the (now inherited, not overridden) base Pause()/Resume(), not just when a caller happens to
// use the concrete DynamicSoundEffectInstance type directly.
TEST(DynamicSoundEffectInstanceTest, PauseViaBaseRefResolvesToOverride)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    SoundEffectInstance& base = d;
    base.Pause();
    EXPECT_EQ(d.getStateProperty(), SoundState::Paused);

    base.Resume();
    EXPECT_EQ(d.getStateProperty(), SoundState::Playing);
}

// ===================== P13-DYNAMIC-001: Volume/Pitch/Pan/Apply3D on a live dynamic track =====================
//
// Before this fix, these four were inherited unchanged from SoundEffectInstance and operated on
// the protected `track_` member -- but DynamicSoundEffectInstance never populated `track_` at all,
// managing its own separate `dynamicTrack_` instead (the same root cause CP-15 already fixed for
// Pause()/Resume(), just never extended to these four). Calling any of them on a live, playing
// DynamicSoundEffectInstance was a complete, silent no-op on the real track. Fixed by removing
// `dynamicTrack_` entirely and sharing the inherited `track_`, matching FNA's own single-`handle`
// model (DynamicSoundEffectInstance.cs has no such split).

TEST(DynamicSoundEffectInstanceTest, SetVolumeAfterPlayActuallyChangesLiveTrackGain)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(d);
    ASSERT_NE(track, nullptr);

    d.setVolumeProperty(0.25f);
    EXPECT_NEAR(MIX_GetTrackGain(track), 0.25f, 1e-5f);
}

TEST(DynamicSoundEffectInstanceTest, SetPitchAfterPlayActuallyChangesLiveTrackFrequencyRatio)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(d);
    ASSERT_NE(track, nullptr);

    d.setPitchProperty(1.0f); // +1 octave -> ratio 2.0
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 2.0f, 1e-4f);
}

// Pitch set BEFORE Play() (while track_ is still null) must still be applied once the track is
// actually created -- Play() now shares SoundEffectInstance::Play()'s own composed-properties
// application instead of only ever setting the plain volume gain.
TEST(DynamicSoundEffectInstanceTest, SetPitchBeforePlayIsAppliedOncePlaying)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.setPitchProperty(-1.0f); // -1 octave -> ratio 0.5, set before any Play() call

    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(d);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 0.5f, 1e-4f);
}

// TQ-032: Dynamic Play IL_003f-0041 sets the inherited isPacketSubmitted;
// inherited set_Pan IL_002f-0050 and UnsafeApply3D IL_0031-0052 then lock mode.
// Pause/Stop do not clear that flag (Pause IL_002f-0056, Stop IL_002f-003b).
TEST(DynamicSoundEffectInstanceTest, PlayLocksSpatialChoiceThroughPauseAndStop)
{
    UseDummyAudioDriver();
    DynamicSoundEffectInstance spatial(8000, AudioChannels::Stereo);
    DynamicSoundEffectInstance panned(8000, AudioChannels::Stereo);
    AudioListener listener;
    AudioEmitter emitter;
    spatial.Apply3D(listener, emitter);
    panned.setPanProperty(0.5f);
    spatial.Play();
    panned.Play();
    ASSERT_EQ(spatial.getStateProperty(), SoundState::Playing);
    ASSERT_EQ(panned.getStateProperty(), SoundState::Playing);
    const auto check = [&] {
        EXPECT_THROW(spatial.setPanProperty(0.5f), System::InvalidOperationException);
        EXPECT_THROW(panned.Apply3D(listener, emitter), System::InvalidOperationException);
        EXPECT_TRUE(SoundEffectInstanceTestAccess::Is3D(spatial));
        EXPECT_FALSE(SoundEffectInstanceTestAccess::Is3D(panned));
        EXPECT_EQ(spatial.getPanProperty(), 0.0f);
        EXPECT_EQ(panned.getPanProperty(), 0.5f);
    };
    check();
    spatial.Pause();
    panned.Pause();
    ASSERT_EQ(spatial.getStateProperty(), SoundState::Paused);
    ASSERT_EQ(panned.getStateProperty(), SoundState::Paused);
    check();
    spatial.Stop(true);
    panned.Stop(true);
    ASSERT_EQ(spatial.getStateProperty(), SoundState::Stopped);
    ASSERT_EQ(panned.getStateProperty(), SoundState::Stopped);
    check();
}

TEST(DynamicSoundEffectInstanceTest, Apply3DOnPlayingInstanceAttenuatesLiveTrackGain)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    // XNA DynamicSoundEffectInstance.Play IL_003f-0041 locks the spatial mode.
    // Aim before Play, then the existing post-Play update below remains legal.
    AudioListener initialListener;
    AudioEmitter initialEmitter;
    d.Apply3D(initialListener, initialEmitter);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(d);
    ASSERT_NE(track, nullptr);

    AudioListener listener; // default position: origin
    AudioEmitter farEmitter;
    farEmitter.setPositionProperty({10000.0f, 0.0f, 0.0f}); // far -> strong attenuation
    d.Apply3D(listener, farEmitter);

    EXPECT_LT(MIX_GetTrackGain(track), 1.0f);
}

// P9-DYNAMIC-001: Pause() must not touch PendingBufferCount at all (matches FNA's Pause(), which
// only stops the voice -- no buffer bookkeeping). Submits a buffer while already playing (past
// the initial buffer submitted by tryStartHeadless), pauses immediately after, and confirms the
// count is exactly what it was right before pausing.
TEST(DynamicSoundEffectInstanceTest, PendingBufferCountUnaffectedAcrossPause)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    std::vector<unsigned char> pcm(4 * 256, 0);
    d.SubmitBuffer(pcm);
    const SharpRuntime::intcs beforePause = d.getPendingBufferCountProperty();

    d.Pause();
    EXPECT_EQ(d.getStateProperty(), SoundState::Paused);
    EXPECT_EQ(d.getPendingBufferCountProperty(), beforePause);

    d.Resume();
    EXPECT_EQ(d.getStateProperty(), SoundState::Playing);
    EXPECT_EQ(d.getPendingBufferCountProperty(), beforePause);
}

// P9-DYNAMIC-001: a real (immediate) Stop() after playback has actually started must clear all
// pending buffers, as XNA's immediate Stop flushes them; ImmediateStopBeforeTheFirstPlayFlushes-
// SubmittedBuffers above covers the never-played case.
TEST(DynamicSoundEffectInstanceTest, PendingBufferCountResetsToZeroAfterStopWhilePlaying)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    ASSERT_GT(d.getPendingBufferCountProperty(), 0);

    d.Stop();

    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
}

// P9-DYNAMIC-001: Dispose() must also clear pending buffers once playback has actually started
// (via its own Stop() call), matching FNA's Dispose(bool) -> Stop(true) -> ClearBuffers().
TEST(DynamicSoundEffectInstanceTest, PendingBufferCountResetsToZeroAfterDisposeWhilePlaying)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    ASSERT_GT(d.getPendingBufferCountProperty(), 0);

    d.Dispose();

    EXPECT_EQ(Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess::PendingBuffers(d), 0u);
    EXPECT_THROW((void)d.getPendingBufferCountProperty(), System::ObjectDisposedException);
}

// P9-DYNAMIC-001: SubmitBuffer while Playing must still increment PendingBufferCount immediately
// (the buffer counts as pending the instant it's accepted, whether staged or handed straight to
// the stream -- matches FNA, where a buffer stays in queuedBuffers.Count either way).
TEST(DynamicSoundEffectInstanceTest, SubmitBufferWhilePlayingIncrementsPendingBufferCount)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    const SharpRuntime::intcs before = d.getPendingBufferCountProperty();

    std::vector<unsigned char> pcm(4 * 256, 0);
    d.SubmitBuffer(pcm);

    EXPECT_EQ(d.getPendingBufferCountProperty(), before + 1);
}

// AUDIO-BUFFER-001 (external audit, 2026-07-16): Update()'s byte-accounting loop used to pop an
// entire submitted chunk from tracking the instant SDL reported ANY consumption at all (any
// `total > queuedBytes`), not once that chunk's own full byte count had actually been played --
// confirmed real by tracing the algorithm: `while (total > queuedBytes) { total -= front;
// pop_front(); }` drops a WHOLE chunk per iteration regardless of how much of the drop in
// `queuedBytes` actually came from that specific chunk, so two whole-second chunks could report
// PendingBufferCount == 1 after only ~50ms of real playback (should still be 2), and == 0 after
// ~1.2s (should still be about 1, since only the first chunk's second has actually elapsed).
TEST(DynamicSoundEffectInstanceTest, PendingBufferCountOnlyDropsOnceAWholeChunkIsActuallyConsumed)
{
    UseDummyAudioDriver();
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);

    // Two whole-second, 16-bit stereo chunks: 44100 frames/sec * 2 channels * 2 bytes/sample.
    std::vector<unsigned char> oneSecond(static_cast<std::size_t>(44100) * 2 * 2, 0);
    d.SubmitBuffer(oneSecond);
    d.SubmitBuffer(oneSecond);
    ASSERT_EQ(d.getPendingBufferCountProperty(), 2);

    try
    {
        d.Play();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    if (d.getStateProperty() == SoundState::Stopped)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    d.Update();
    EXPECT_EQ(d.getPendingBufferCountProperty(), 2)
        << "only ~50ms of a full second-long chunk has played -- neither whole chunk should have "
           "been dropped from tracking yet";

    std::this_thread::sleep_for(std::chrono::milliseconds(1150)); // ~1.2s total real playback
    d.Update();
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1)
        << "about 1.2s has passed -- exactly the first whole one-second chunk should now be "
           "confirmed consumed, leaving the second one still tracked";
}

// P9-DYNAMIC-001: BufferNeeded must fire once per every subscriber, not just the first --
// multiple independent subscribers (e.g. separate systems both tracking buffer starvation) must
// each observe every raise.
TEST(DynamicSoundEffectInstanceTest, BufferNeededFiresForEveryIndependentSubscriber)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    int firedA = 0, firedB = 0;
    d.BufferNeeded += [&firedA](System::Object*, const System::EventArgs&) { ++firedA; };
    d.BufferNeeded += [&firedB](System::Object*, const System::EventArgs&) { ++firedB; };

    d.Update();

    EXPECT_GT(firedA, 0);
    EXPECT_EQ(firedA, firedB);
}

// P9-DYNAMIC-007: a common event pattern is a subscriber that unsubscribes itself the first
// time it fires ("handle once"). Update()'s starvation loop raises BufferNeeded more than once
// per call whenever PendingBufferCount is more than 1 short of MINIMUM_BUFFER_CHECK, so this
// must not crash or corrupt the remaining subscriber's firing (matches System::EventHandler<T>'s
// snapshot-before-iterating Raise() semantics -- sharp-runtime).
TEST(DynamicSoundEffectInstanceTest, BufferNeededSubscriberCanRemoveItselfDuringCallbackWithoutCrashing)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    int firedOnce = 0, firedOther = 0;
    System::EventHandler<System::EventArgs>::Token onceToken = 0;
    onceToken = d.BufferNeeded.Add(
        [&](System::Object*, const System::EventArgs&)
        {
            ++firedOnce;
            d.BufferNeeded.Remove(onceToken);
        });
    d.BufferNeeded += [&firedOther](System::Object*, const System::EventArgs&) { ++firedOther; };

    EXPECT_NO_THROW(d.Update());

    EXPECT_EQ(firedOnce, 1);
    EXPECT_GT(firedOther, 0);
    EXPECT_EQ(d.BufferNeeded.Size(), 1u); // only the still-subscribed "other" handler remains
}

// AUD-04-009: the DynamicSoundEffectInstance counterpart to SoundEffectInstanceTests.cpp's
// MixerDestructionOrphansTrackWithoutUseAfterFree (AUD-04-008) -- same generation-check
// mechanism (shared via the inherited track_/trackMixerGeneration_), but exercised through
// DynamicSoundEffectInstance's own independent getStateProperty()/track_ access path, which does
// not share code with the base class's. Deterministic and in-process (no subprocess needed): as
// with the static-instance test, GetTrack() reads track_ raw (bypassing the check) to prove the
// dangling pointer is untouched immediately after DestroyMixer(), then the first real accessor
// call is shown to null it out as direct evidence the check ran.
TEST(DynamicSoundEffectInstanceTest, MixerDestructionOrphansTrackWithoutUseAfterFree)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);
    ASSERT_NE(SoundEffectInstanceTestAccess::GetTrack(d), nullptr);

    CNA::Internal::Audio::DestroyMixer();

    // Still the same (now-dangling) raw pointer value -- DestroyMixer() itself has no way to
    // reach into this instance, so nothing has cleared it yet.
    EXPECT_NE(SoundEffectInstanceTestAccess::GetTrack(d), nullptr);

    // getStateProperty() is the first real accessor call after DestroyMixer() -- it must detect
    // the stale generation and report Stopped without ever touching the freed MIX_Track*.
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(d), nullptr);

    // Every other operation a real caller could still reach must also be safe now that track_ is
    // null.
    EXPECT_NO_THROW(d.Pause());
    EXPECT_NO_THROW(d.Stop());
    EXPECT_NO_THROW(d.Dispose());
}

// AUD-15-006: a "producer" thread continuously calling SubmitBuffer() -- the class's own
// documented cross-thread boundary (queueMutex_ exists specifically to guard queuedBuffers_/
// submittedChunkSizes_ against concurrent producer submission vs. Update()'s drain; see
// AUD-07-020's "untrusted/buggy producers" wording and AUD-15-003's lock-ordering survey) --
// racing a "game" thread that randomly drives Play/Pause/Resume/Stop/Update, then Disposes the
// instance while the producer may still be mid-submit. Acceptance (from the plan): no deadlock,
// no data silently lost outside a documented Stop (Stop()/ClearBuffers() legitimately discard
// queued data -- that is not a bug), and getPendingBufferCountProperty() never reports a corrupt
// (implausibly large or negative) count.
TEST(DynamicSoundEffectInstanceTest, StressProducerConsumerWithRandomPauseStopDispose)
{
    UseDummyAudioDriver();

    auto d = std::make_unique<DynamicSoundEffectInstance>(44100, AudioChannels::Stereo);

    std::atomic<long long> chunksSubmitted{0};
    std::atomic<bool> producerDone{false};

    std::thread producer([&]() {
        const std::vector<unsigned char> pcm(4 * 64, 0); // 64 stereo S16 frames of silence
        for (int i = 0; i < 20000; ++i)
        {
            try
            {
                d->SubmitBuffer(pcm);
                chunksSubmitted.fetch_add(1, std::memory_order_relaxed);
            }
            catch (const System::ObjectDisposedException&)
            {
                // Expected once the "game" thread below disposes the instance while this
                // thread is still mid-loop -- submitting after Dispose() is documented to
                // throw (P9-VALIDATION-011), not a supported call pattern, so seeing it here
                // is the correct, intended outcome, not a bug.
                break;
            }
        }
        producerDone.store(true, std::memory_order_relaxed);
    });

    // Deterministic LCG (matches this project's other deterministic-fuzz harnesses, e.g.
    // XactParserFuzzTests.cpp) rather than <random>, so a failure is exactly reproducible.
    uint32_t seed = 0x9E3779B9u;
    auto nextRandom = [&seed]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7fffu;
    };

    // "Game" thread (this thread): randomly drive the public state-machine API while the
    // producer above concurrently submits buffers from another thread.
    for (int i = 0; i < 5000; ++i)
    {
        switch (nextRandom() % 5)
        {
            case 0: try { d->Play(); } catch (...) {} break;
            case 1: try { d->Pause(); } catch (...) {} break;
            case 2: try { d->Resume(); } catch (...) {} break;
            case 3: try { d->Stop(); } catch (...) {} break;
            case 4: d->Update(); break;
        }

        // A real corruption bug (e.g. a field the queue mutex doesn't actually cover) would
        // show up here as an implausibly large or negative pending count, not necessarily a
        // clean crash.
        const SharpRuntime::intcs pending = d->getPendingBufferCountProperty();
        ASSERT_GE(pending, 0) << "corrupt pending buffer count at iteration " << i;
        ASSERT_LT(pending, 1'000'000) << "pending buffer count grew implausibly large at iteration " << i;
    }

    d->Dispose();
    producer.join();

    EXPECT_TRUE(producerDone.load());
    EXPECT_TRUE(d->getIsDisposedProperty());
    EXPECT_GT(chunksSubmitted.load(), 0)
        << "producer thread never got a chance to submit anything before the instance was disposed";

    // Not asserted as exactly 0: SubmitBuffer()'s disposed-check and Dispose()'s
    // Stop()->ClearBuffers() aren't atomic with respect to each other (isDisposed_ only flips
    // true at the very end of Dispose(), after ClearBuffers() already ran) -- a producer chunk
    // can legitimately land in queuedBuffers_ in the window between those two steps, and under a
    // slow/instrumented build (observed under ASan: up to single digits, vs. usually 0-1 under a
    // normal build) that window can admit a handful more before the producer thread observes
    // isDisposed_. That is a harmless, inherent consequence of disposing an instance while a
    // foreign thread is still mid-submit (not a supported call pattern to begin with -- the
    // caller is expected to stop its own producer before disposing) -- not memory-unsafe, and
    // not "corrupt" in the sense this task's acceptance criteria means (implausibly large or
    // negative). Bounded generously here to still catch genuine unbounded growth.
    EXPECT_LT(Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess::PendingBuffers(*d), 1000u);
    EXPECT_THROW((void)d->getPendingBufferCountProperty(), System::ObjectDisposedException);
}

// AUD-07-003: T-2C's SubmitFloatAfterPlayingThrowsInvalidOperation above already locks down the
// single-threaded case; this task's own acceptance explicitly calls for the guard (float
// submission rejected while a live int stream is Playing/Paused) to be verified "under races and
// repeated play cycles" too -- a producer thread hammers SubmitFloatBufferEXT() while a "game"
// thread repeatedly cycles Play()->Stop(true)->Play()->Stop(true)..., the same field-level race
// this session's AUD-15-006 fix closed for SubmitBuffer() (state-check-and-submit now atomic
// under queueMutex_, isFloat_ now atomic<bool>) but verified there only via int submission --
// this test independently exercises the SubmitFloatBufferEXT() side of that same fix. The
// producer deliberately ALTERNATES SubmitBuffer()/SubmitFloatBufferEXT() calls rather than only
// ever submitting float: isFloat_ latches true after the first successful float submission and
// stays true forever unless something calls SubmitBuffer() to reset it back to false (see
// SubmitBuffer()'s own `isFloat_ = false;` at the end) -- a float-only producer would only ever
// exercise the `if (!isFloat_)` racy guard once, at the very start, then never again for the rest
// of the run (confirmed empirically: an all-float version of this test did not catch a
// deliberately-reintroduced pre-AUD-15-006 unlocked-guard regression under 5 ASan runs). Alternating
// keeps isFloat_ toggling and the guard genuinely live throughout. Acceptance: every call either
// succeeds cleanly or throws exactly InvalidOperationException/ObjectDisposedException -- never
// any other exception, and never a crash. A rendezvous phase guarantees opposite-format guard
// coverage over repeated real Play/Stop cycles; the original free-running stress then retains
// the teardown/submit interleavings. Without the rendezvous, a valid schedule can complete every
// Play/Stop pair before the producer runs and never exercise a live-format rejection.
TEST(DynamicSoundEffectInstanceTest, StressSubmitFloatBufferEXTAgainstRepeatedPlayCyclesNeverCorruptsLiveStream)
{
    UseDummyAudioDriver();

    auto d = std::make_unique<DynamicSoundEffectInstance>(44100, AudioChannels::Stereo);
    // Seed with an initial int16 buffer so the very first Play() has a real stream to attach to.
    d->SubmitBuffer(std::vector<unsigned char>(4 * 64, 0));
    try
    {
        d->Play();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    if (d->getStateProperty() == SoundState::Stopped)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    d->Stop(true);

    std::atomic<long long> callsAttempted{0};
    std::atomic<long long> callsThrown{0};
    std::atomic<bool> producerDone{false};
    constexpr int guardedCycles = 16;
    std::atomic<int> guardedRejections{0};
    std::binary_semaphore playbackReady{0};
    std::binary_semaphore submissionFinished{0};

    std::thread producer([&]() {
        const std::vector<float> floatBuf(4 * 32, 0.0f); // 32 stereo float frames of silence
        const std::vector<unsigned char> intBuf(4 * 64, 0); // 64 stereo S16 frames of silence
        for (int i = 0; i < guardedCycles; ++i)
        {
            playbackReady.acquire();
            try
            {
                if (i % 2 == 0) d->SubmitFloatBufferEXT(floatBuf);
                else d->SubmitBuffer(intBuf);
                ADD_FAILURE() << "live opposite-format submission was accepted in cycle " << i;
            }
            catch (const System::ObjectDisposedException&)
            {
                ADD_FAILURE() << "instance was not disposed during guarded cycle " << i;
            }
            catch (const System::InvalidOperationException&)
            {
                guardedRejections.fetch_add(1, std::memory_order_relaxed);
                callsThrown.fetch_add(1, std::memory_order_relaxed);
            }
            catch (...)
            {
                ADD_FAILURE() << "unexpected exception in guarded cycle " << i;
            }
            submissionFinished.release();
        }
        // Deliberately smaller than the other producer/consumer stress test above: each
        // alternating call here can trigger a real EnsureStream() rebuild (SDL_CreateAudioStream/
        // SDL_DestroyAudioStream), unlike that test's single fixed-format producer -- 3000 of
        // these is already enough to reliably exercise the race (confirmed via the probe
        // described above) without the run time exploding under a sanitizer build.
        for (int i = 0; i < 3000; ++i)
        {
            callsAttempted.fetch_add(1, std::memory_order_relaxed);
            try
            {
                if (i % 2 == 0) d->SubmitFloatBufferEXT(floatBuf);
                else d->SubmitBuffer(intBuf);
            }
            catch (const System::ObjectDisposedException&)
            {
                // Expected once the instance is disposed below while this thread is still mid-loop.
                break;
            }
            catch (const System::InvalidOperationException&)
            {
                // Expected: rejected because the live stream is currently in the OTHER format
                // while Playing/Paused -- exactly the guard this test exists to verify, not a
                // bug. ObjectDisposedException derives from InvalidOperationException, so it must
                // be caught first, above.
                callsThrown.fetch_add(1, std::memory_order_relaxed);
            }
        }
        producerDone.store(true, std::memory_order_relaxed);
    });

    // Stop cannot overtake the producer's checked submission. Alternate the live format so
    // both guards must reject, regardless of how the scheduler distributes CPU time.
    for (int i = 0; i < guardedCycles; ++i)
    {
        if (i % 2 == 0)
            EXPECT_NO_THROW(d->SubmitBuffer(std::vector<unsigned char>(4 * 64, 0)));
        else
            EXPECT_NO_THROW(d->SubmitFloatBufferEXT(std::vector<float>(4 * 32, 0.0f)));
        EXPECT_NO_THROW(d->Play());
        EXPECT_EQ(d->getStateProperty(), SoundState::Playing);
        playbackReady.release();
        submissionFinished.acquire();
        EXPECT_EQ(d->getStateProperty(), SoundState::Playing);
        EXPECT_NO_THROW(d->Stop(true));
    }

    // "Game" thread (this thread): repeated real Play()/Stop(true) cycles, the exact scenario
    // named in this task's acceptance criteria, while the producer above concurrently hammers
    // both submission entry points.
    for (int i = 0; i < 3000; ++i)
    {
        try { d->Play(); } catch (...) {}
        try { d->Stop(true); } catch (...) {}
    }

    d->Dispose();
    producer.join();

    EXPECT_TRUE(producerDone.load());
    EXPECT_TRUE(d->getIsDisposedProperty());
    EXPECT_GT(callsAttempted.load(), 0);
    EXPECT_EQ(guardedRejections.load(), guardedCycles);
    EXPECT_GT(callsThrown.load(), 0)
        << "the rendezvous must exercise live opposite-format rejection before free-running stress";
    // Not asserted that every call throws or that none do -- both are legitimate outcomes
    // depending on exactly how Play()/Stop() and Submit*() happened to interleave; what matters
    // is that calls were attempted, some were genuinely rejected by the guard, and none produced
    // anything other than the two expected exception types above (any other exception, or a
    // crash, would have already failed this test).
}

TEST(DynamicSoundEffectInstanceTest, Test010RejectedIntegerSubmissionPreservesFloatQueueAndFormat)
{
    using Access = Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess;
    DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);
    const std::vector<float> samples{0.25f, -0.25f, 0.5f, -0.5f};
    instance.SubmitFloatBufferEXT(samples);
    const auto before = Access::QueuedBuffers(instance);
    ASSERT_EQ(before.size(), 1U);
    ASSERT_TRUE(Access::IsFloat(instance));
    try
    {
        instance.SubmitBuffer(std::vector<SharpRuntime::bytecs>(8), 0, 6);
        ADD_FAILURE() << "partial frame accepted";
    }
    catch (const System::ArgumentException& error)
    {
        // XNA SubmitBuffer IL_00a2-00ac: message-only ArgumentException.
        EXPECT_TRUE(error.getParamNameProperty().empty());
        EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
    }
    EXPECT_EQ(Access::QueuedBuffers(instance), before);
    EXPECT_TRUE(Access::IsFloat(instance));
    EXPECT_EQ(instance.getPendingBufferCountProperty(), 1);
    EXPECT_EQ(instance.getStateProperty(), SoundState::Stopped);
    instance.SubmitFloatBufferEXT(samples);
    EXPECT_EQ(instance.getPendingBufferCountProperty(), 2);
    const auto after = Access::QueuedBuffers(instance);
    ASSERT_EQ(after.size(), 2U);
    EXPECT_EQ(after.front(), before.front());
    EXPECT_EQ(after.back(), before.front());
    instance.Dispose();
    EXPECT_TRUE(Access::QueuedBuffers(instance).empty());
}

TEST(DynamicSoundEffectInstanceTest, Test010RejectedFloatSubmissionPreservesIntegerQueueAndFormat)
{
    using Access = Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess;
    DynamicSoundEffectInstance instance(44100, AudioChannels::Stereo);
    const std::vector<SharpRuntime::bytecs> samples{1, 2, 3, 4, 5, 6, 7, 8};
    instance.SubmitBuffer(samples);
    const auto before = Access::QueuedBuffers(instance);
    ASSERT_EQ(before.size(), 1U);
    ASSERT_FALSE(Access::IsFloat(instance));
    EXPECT_THROW(instance.SubmitFloatBufferEXT(std::vector<float>(4), 2, 4),
                 System::ArgumentOutOfRangeException);
    EXPECT_EQ(Access::QueuedBuffers(instance), before);
    EXPECT_FALSE(Access::IsFloat(instance));
    EXPECT_EQ(instance.getPendingBufferCountProperty(), 1);
    EXPECT_EQ(instance.getStateProperty(), SoundState::Stopped);
    instance.SubmitBuffer(samples);
    EXPECT_EQ(instance.getPendingBufferCountProperty(), 2);
    const auto after = Access::QueuedBuffers(instance);
    ASSERT_EQ(after.size(), 2U);
    EXPECT_EQ(after.front(), before.front());
    EXPECT_EQ(after.back(), before.front());
    instance.Dispose();
    EXPECT_TRUE(Access::QueuedBuffers(instance).empty());
}

// TQ-032: XNA AllocateData IL_0017-0024 submits buffer+offset and count;
// FNA DynamicSoundEffectInstance.cs:198-199 copies that exact byte window.
// Literal distinct frames prove offset, count, order and ownership independently.
TEST(DynamicSoundEffectInstanceTest, SubmitBufferCopiesOnlySelectedBytesAndOwnsThem)
{
    using Access = Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstanceTestAccess;
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    std::vector<unsigned char> pcm{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    d.SubmitBuffer(pcm, 4, 8);
    pcm.assign(16, 255);
    d.SubmitBuffer(std::vector<unsigned char>{21,22,23,24});
    const std::vector<std::vector<unsigned char>> expected{
        {5,6,7,8,9,10,11,12}, {21,22,23,24}};
    EXPECT_EQ(Access::QueuedBuffers(d), expected);
    EXPECT_EQ(d.getPendingBufferCountProperty(), 2);
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
}

// Owner decision 2026-11-12: a dry stream gets XNA's single notification per start from Stopped
// (KernelDynamicSoundEffectInstance.Play IL_001a-0061), delivered by an update, and no stream of
// refill requests; Play while Playing and Resume from Paused post none, and Stop discards an
// undelivered one. An empty stream has exactly zero buffers regardless of device scheduling.
TEST(DynamicSoundEffectInstanceTest, DryStreamCadenceAndPauseStopGatesUseLiteralCounts)
{
    UseDummyAudioDriver();
    int calls = 0;
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    d.BufferNeeded += [&](System::Object* sender, const System::EventArgs&) {
        EXPECT_EQ(sender, static_cast<System::Object*>(&d));
        ++calls;
    };
    d.Update();
    EXPECT_EQ(calls, 0);
    ASSERT_NO_THROW(d.Play());
    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);
    EXPECT_EQ(calls, 0) << "Play posts; it does not raise";
    ASSERT_EQ(d.getPendingBufferCountProperty(), 0);
    d.Update();
    EXPECT_EQ(calls, 1);
    d.Update();
    EXPECT_EQ(calls, 1) << "no completion, no further notification";
    d.Play();
    d.Update();
    EXPECT_EQ(calls, 1) << "Play while Playing starts nothing";
    d.Pause();
    ASSERT_EQ(d.getStateProperty(), SoundState::Paused);
    d.Update();
    EXPECT_EQ(calls, 1);
    d.Resume();
    ASSERT_EQ(d.getStateProperty(), SoundState::Playing);
    d.Update();
    EXPECT_EQ(calls, 1) << "Resume from Paused is not a start from Stopped";
    d.Stop(true);
    ASSERT_EQ(d.getStateProperty(), SoundState::Stopped);
    d.Update();
    EXPECT_EQ(calls, 1);
    d.Play();
    d.Stop(true);
    d.Update();
    EXPECT_EQ(calls, 1) << "Stop discards the restart's undelivered notification";
    d.Play();
    d.Update();
    EXPECT_EQ(calls, 2) << "each start from Stopped posts one";
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
}

namespace
{
    // Plays d (already holding any submitted buffers) on the dummy driver; false without a device.
    bool PlayOnDummy(DynamicSoundEffectInstance& d)
    {
        UseDummyAudioDriver();
        try
        {
            d.Play();
        }
        catch (...)
        {
            return false;
        }
        return d.getStateProperty() == SoundState::Playing;
    }
}

// Review rv-fix8 #2: XNA's dispatcher clears its copied batch only after every callback returned
// (FrameworkDispatcher.Update IL_0031-0037, IL_00de-00e5, IL_0112-0117), so a handler that throws
// leaves the whole batch for the next update: three completions, a first callback that throws,
// then a clean update -- four callbacks in all.
TEST(DynamicSoundEffectInstanceTest, AThrowingHandlerLeavesTheWholeBatchForTheNextUpdate)
{
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    const std::vector<unsigned char> pcm(4 * 64, 0); // 8 ms each
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) {
        if (calls++ == 0) throw std::runtime_error("first handler call fails");
    };
    if (!PlayOnDummy(d)) GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (d.getPendingBufferCountProperty() != 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_EQ(d.getPendingBufferCountProperty(), 0);
    EXPECT_THROW(d.Update(), std::runtime_error);
    EXPECT_EQ(calls, 1);
    d.Update();
    EXPECT_EQ(calls, 4) << "the failed batch of three is delivered again in full";
    d.Update();
    EXPECT_EQ(calls, 4);
    d.Stop(true);
}

// Review rv-fix8 #6: the dispatcher takes every stream's batch before any handler runs, so a
// notification a handler causes on another stream waits for the next update. A is registered
// before B; A's handler restarts B, whose start from Stopped posts one.
TEST(DynamicSoundEffectInstanceTest, ANotificationPostedByAHandlerWaitsForTheNextUpdate)
{
    DynamicSoundEffectInstance a(8000, AudioChannels::Stereo);
    DynamicSoundEffectInstance b(8000, AudioChannels::Stereo);
    if (!PlayOnDummy(a) || !PlayOnDummy(b)) GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    int aCalls = 0, bCalls = 0;
    b.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++bCalls; };
    b.Update(); // delivers B's own start notification
    ASSERT_EQ(bCalls, 1);
    a.BufferNeeded += [&](System::Object*, const System::EventArgs&) {
        ++aCalls;
        b.Stop(true);
        b.Play();
    };
    Microsoft::Xna::Framework::FrameworkDispatcher::Update();
    EXPECT_EQ(aCalls, 1);
    EXPECT_EQ(bCalls, 1) << "B's new start notification belongs to the next update";
    Microsoft::Xna::Framework::FrameworkDispatcher::Update();
    EXPECT_EQ(bCalls, 2);
    a.Stop(true);
    b.Stop(true);
}

// Review rv-fix8 #7: a handler that destroys another stream must not leave the dispatcher calling
// into it (its snapshot holds a raw pointer); the destroyed stream has left Streams, which the
// dispatcher checks before every delivery. Meaningful under AddressSanitizer.
TEST(DynamicSoundEffectInstanceTest, AHandlerMayDestroyAnotherStreamDuringTheDispatch)
{
    auto a = std::make_unique<DynamicSoundEffectInstance>(8000, AudioChannels::Stereo);
    auto b = std::make_unique<DynamicSoundEffectInstance>(8000, AudioChannels::Stereo);
    if (!PlayOnDummy(*a) || !PlayOnDummy(*b)) GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    int bCalls = 0;
    b->BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++bCalls; };
    a->BufferNeeded += [&](System::Object*, const System::EventArgs&) { b.reset(); };
    EXPECT_NO_THROW(Microsoft::Xna::Framework::FrameworkDispatcher::Update());
    EXPECT_EQ(b, nullptr);
    EXPECT_EQ(bCalls, 0);
    a->Stop(true);
}

// Review rv-fix8 #5: XNA tests the pending count when each buffer completes (OnBufferEnd
// IL_004b-006a). Three short buffers that finish before three long ones are submitted have
// posted three notifications, even though nobody read the count in between.
TEST(DynamicSoundEffectInstanceTest, CompletionsBeforeALaterSubmissionStillPost)
{
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    const std::vector<unsigned char> shortPcm(4 * 64, 0); // 8 ms each
    d.SubmitBuffer(shortPcm);
    d.SubmitBuffer(shortPcm);
    d.SubmitBuffer(shortPcm);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++calls; };
    if (!PlayOnDummy(d)) GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    const std::vector<unsigned char> longPcm(4 * 8000, 0); // one second each
    d.SubmitBuffer(longPcm);
    d.SubmitBuffer(longPcm);
    d.SubmitBuffer(longPcm);
    d.Update();
    EXPECT_EQ(calls, 3) << "the three short buffers completed with two, one and none pending";
    d.Stop(true);
}

// ClearBuffersEXT (a CNA extension; XNA has none) empties a playing stream without stopping it, so
// the notification Play posted is still delivered -- a starving stream needs it. Only Stop discards.
TEST(DynamicSoundEffectInstanceTest, ClearBuffersKeepsANotificationPlayPosted)
{
    UseDummyAudioDriver();
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++calls; };
    d.SubmitBuffer(std::vector<unsigned char>(4 * 8000, 0));
    d.SubmitBuffer(std::vector<unsigned char>(4 * 8000, 0));
    try
    {
        d.Play();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    if (d.getStateProperty() != SoundState::Playing)
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    d.ClearBuffers();
    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
    d.Update();
    EXPECT_EQ(calls, 1) << "two buffers pending at Play posted one; clearing does not take it back";
    d.Stop(true);
}

// Owner decision 2026-11-12: three buffers pending at Play post nothing (more than two), and each
// completion that leaves at most two posts one (OnBufferEnd IL_004b-006a) -- three in all. The
// count drops as the buffers finish, before any update (get_PendingBufferCount IL_002f-0035 reads
// the live native count); the notifications wait for FrameworkDispatcher.Update.
TEST(DynamicSoundEffectInstanceTest, EachCompletedBufferPostsOneNotificationAndTheCountIsLive)
{
    UseDummyAudioDriver();
    DynamicSoundEffectInstance d(8000, AudioChannels::Stereo);
    int calls = 0;
    d.BufferNeeded += [&](System::Object*, const System::EventArgs&) { ++calls; };
    const std::vector<unsigned char> pcm(4 * 64, 0); // 64 stereo S16 frames: 8 ms each
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    d.SubmitBuffer(pcm);
    try
    {
        d.Play();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    if (d.getStateProperty() != SoundState::Playing)
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (d.getPendingBufferCountProperty() != 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_EQ(d.getPendingBufferCountProperty(), 0) << "played buffers stop being pending without an update";
    EXPECT_EQ(calls, 0) << "completions post; only the dispatcher delivers";
    Microsoft::Xna::Framework::FrameworkDispatcher::Update();
    EXPECT_EQ(calls, 3);
    Microsoft::Xna::Framework::FrameworkDispatcher::Update();
    EXPECT_EQ(calls, 3) << "a drained stream asks for nothing more";
    d.Stop(true);
}
