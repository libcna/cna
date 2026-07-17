// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
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

#include <SDL3_mixer/SDL_mixer.h>

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::SoundState;

namespace
{
    // Tries to start playback under the SDL "dummy" audio driver so device-dependent
    // behaviour can be exercised headlessly. Returns true if the instance reached a
    // non-Stopped state; otherwise the caller should skip the assertion.
    bool tryStartHeadless(DynamicSoundEffectInstance& d)
    {
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
        std::vector<unsigned char> pcm(4 * 256, 0); // 256 stereo S16 frames of silence
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

// P10-DYN-001/002/003 (2026-07-06 audit, Phase 10): real FNA's constructor (DynamicSoundEffectInstance.cs)
// stores `sampleRate`/`channels` directly into a FAudioWaveFormatEx with zero validation -- no
// range check, no throw, for ANY value (confirmed by reading the FNA source line-by-line: the
// ctor body is a straight field-assignment + FAudioWaveFormatEx construction, no guard at all).
// This diverges from MSDN's *documented* contract for this constructor (8,000-48,000 Hz,
// ArgumentOutOfRangeException otherwise) -- an XNA-docs-vs-FNA-behavior split, resolved here in
// favor of matching real FNA behavior (this project's established practical-compatibility
// policy, consistent with e.g. P9-VALIDATION-001's identical resolution for SoundEffect's own
// constructors). CNA's constructor already has zero validation, matching FNA -- these tests lock
// that decision down instead of it being an untested, accidental gap.
TEST(DynamicSoundEffectInstanceTest, ConstructorAcceptsSampleRateBelowXnaDocumentedMinimum)
{
    // MSDN documents 8000 as the minimum; FNA itself never enforces it.
    EXPECT_NO_THROW(DynamicSoundEffectInstance d(4000, AudioChannels::Mono));
}

TEST(DynamicSoundEffectInstanceTest, ConstructorAcceptsSampleRateAboveXnaDocumentedMaximum)
{
    // MSDN documents 48000 as the maximum; FNA itself never enforces it.
    EXPECT_NO_THROW(DynamicSoundEffectInstance d(96000, AudioChannels::Stereo));
}

TEST(DynamicSoundEffectInstanceTest, ConstructorAcceptsZeroSampleRate)
{
    EXPECT_NO_THROW(DynamicSoundEffectInstance d(0, AudioChannels::Mono));
}

TEST(DynamicSoundEffectInstanceTest, ConstructorAcceptsNegativeSampleRate)
{
    EXPECT_NO_THROW(DynamicSoundEffectInstance d(-1, AudioChannels::Mono));
}

// P10-DYN-004/005: real FNA's GetSampleDuration/GetSampleSizeInBytes (DynamicSoundEffectInstance.cs)
// delegate straight to the static SoundEffect helpers using the stored sampleRate/channels
// fields, with no `IsDisposed` guard at all -- confirmed by reading the FNA source. Matching that
// (not adding a new ObjectDisposedException guard CNA-side) is the resolved decision; these tests
// lock it down.
TEST(DynamicSoundEffectInstanceTest, GetSampleDurationAfterDisposeDoesNotThrow)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_NO_THROW({ auto result = d.GetSampleDuration(4000); (void)result; });
}

TEST(DynamicSoundEffectInstanceTest, GetSampleSizeInBytesAfterDisposeDoesNotThrow)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    d.Dispose();
    EXPECT_NO_THROW({ auto result = d.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0)); (void)result; });
}

TEST(DynamicSoundEffectInstanceTest, IsLoopedSetterIsNoOpDirect)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    EXPECT_NO_THROW(d.setIsLoopedProperty(true));
    EXPECT_FALSE(d.getIsLoopedProperty());
}

// Regression for the override bug (T-2A): going through a SoundEffectInstance&
// must dispatch to the dynamic no-op override, not the base setter.
TEST(DynamicSoundEffectInstanceTest, IsLoopedSetterIsNoOpViaBaseRefWhenStopped)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    SoundEffectInstance& base = d;
    bool lvalue = true;
    EXPECT_NO_THROW(base.setIsLoopedProperty(lvalue)); // const bool& overload
    EXPECT_NO_THROW(base.setIsLoopedProperty(true));   // bool&& overload
    EXPECT_FALSE(d.getIsLoopedProperty());
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
// divisor/multiplier -- mono halves the byte count for the same duration (matches FNA's
// SoundEffect.GetSampleDuration/GetSampleSizeInBytes, which both divide/multiply by
// (int) AudioChannels directly).
TEST(DynamicSoundEffectInstanceTest, SampleDurationRoundTripMono)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Mono);
    // 1 second of 16-bit mono @ 44100 Hz = 44100 * 1ch * 2bytes = 88200 bytes.
    const int bytes = d.GetSampleSizeInBytes(System::TimeSpan::FromSeconds(1.0));
    EXPECT_EQ(bytes, 88200);
    EXPECT_NEAR(d.GetSampleDuration(88200).getTotalSecondsProperty(), 1.0, 1e-9);
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

// P9-DYNAMIC-001: matches FNA's Stop()/Stop(bool) guard (`handle == IntPtr.Zero -> return`,
// SoundEffectInstance.cs) -- calling the no-arg Stop() directly (not via Stop(bool)) on an
// instance that was never played must be a safe no-op, not clear staged buffers. Previously
// DynamicSoundEffectInstance::Stop() duplicated StopInternal()'s unconditional buffer-clearing
// logic instead of delegating through Stop(bool)'s existing guard, so calling it directly here
// used to drop PendingBufferCount to 0.
TEST(DynamicSoundEffectInstanceTest, StopDirectCallWhileNeverPlayedDoesNotClearPendingBuffers)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(64, 0);
    d.SubmitBuffer(pcm);
    ASSERT_EQ(d.getPendingBufferCountProperty(), 1);

    d.Stop();

    EXPECT_EQ(d.getPendingBufferCountProperty(), 1);
    EXPECT_EQ(d.getStateProperty(), SoundState::Stopped);
}

TEST(DynamicSoundEffectInstanceTest, SubmitBufferRangeThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(16, 0);
    EXPECT_THROW(d.SubmitBuffer(pcm, -1, 4), System::ArgumentOutOfRangeException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 0, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(d.SubmitBuffer(pcm, 8, 16), System::ArgumentOutOfRangeException);
}

TEST(DynamicSoundEffectInstanceTest, SubmitFloatBufferRangeThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<float> buf(16, 0.0f);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf, -1, 4), System::ArgumentOutOfRangeException);
    EXPECT_THROW(d.SubmitFloatBufferEXT(buf, 0, 32), System::ArgumentOutOfRangeException);
}

// P9-VALIDATION-010/011: offset+count must be checked without computing the (possibly
// overflowing) sum directly -- see SoundEffect's identical fix/test for the full rationale.
TEST(DynamicSoundEffectInstanceTest, SubmitBufferRangeIntegerOverflowThrows)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(16, 0);
    constexpr int hugeOffset = 2000000000;
    constexpr int hugeCount  = 2000000000; // offset+count overflows int32
    EXPECT_THROW(d.SubmitBuffer(pcm, hugeOffset, hugeCount), System::ArgumentOutOfRangeException);
}

// P9-DYNAMIC-009: matches FNA's SubmitBuffer exactly -- there is no block-alignment validation
// at all (FAudio's FACTSoundBank_Prepare-adjacent buffer submission just stores whatever byte
// count is given; FAudioBuffer.PlayLength = AudioBytes / channels / bytesPerSample truncates via
// plain integer division for a non-frame-aligned count, it never throws). A 16-bit stereo frame
// is 4 bytes; 63 is deliberately not a multiple of that.
TEST(DynamicSoundEffectInstanceTest, SubmitBufferWithNonFrameAlignedByteCountDoesNotThrowWhileStopped)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(63, 0); // not a multiple of 4 (2ch * 2 bytes/sample)
    EXPECT_NO_THROW(d.SubmitBuffer(pcm));
    EXPECT_EQ(d.getPendingBufferCountProperty(), 1); // a whole buffer either way, alignment-agnostic
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

// P9-DYNAMIC-009: a non-frame-aligned submission while actually playing must not crash or wedge
// subsequent buffer bookkeeping (Update()'s byte-based consumption tracking is alignment-agnostic
// by construction -- it compares total submitted bytes against SDL_GetAudioStreamQueued(), never
// frame counts -- but this exercises the real SDL3_mixer/SDL_AudioStream path end-to-end).
TEST(DynamicSoundEffectInstanceTest, SubmitBufferWithNonFrameAlignedByteCountWhilePlayingDoesNotThrow)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    std::vector<unsigned char> misaligned(63, 0);
    EXPECT_NO_THROW(d.SubmitBuffer(misaligned));
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
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
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

// T-2A: setting IsLooped through a base reference on a playing dynamic instance must
// remain a no-op (the base setter would otherwise throw "cannot change while playing").
TEST(DynamicSoundEffectInstanceTest, IsLoopedViaBaseRefWhilePlayingDoesNotThrow)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    SoundEffectInstance& base = d;
    EXPECT_NO_THROW(base.setIsLoopedProperty(true));
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

// P9-DYNAMIC-001/005: matches FNA's exact starvation loop (`for (i = MINIMUM_BUFFER_CHECK -
// PendingBufferCount; i > 0 && BufferNeeded != null; i -= 1) BufferNeeded(...)`, DynamicSoundEffectInstance.cs)
// -- Update() must raise BufferNeeded exactly (MINIMUM_BUFFER_CHECK - PendingBufferCount) times,
// not merely "at least once". tryStartHeadless() submits exactly 1 buffer before Play(), so
// immediately after (before any real consumption could have happened), PendingBufferCount == 1
// and MINIMUM_BUFFER_CHECK(3) - 1 == 2 raises are expected.
TEST(DynamicSoundEffectInstanceTest, BufferNeededFiresExactlyTheStarvedCount)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    if (!tryStartHeadless(d))
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    ASSERT_EQ(d.getPendingBufferCountProperty(), 1);

    int fired = 0;
    d.BufferNeeded += [&fired](System::Object*, const System::EventArgs&) { ++fired; };
    d.Update();
    EXPECT_EQ(fired, 2); // MINIMUM_BUFFER_CHECK(3) - PendingBufferCount(1)
}

// CP-4: a buffer must remain "pending" until the stream reports it was actually consumed, not
// the instant it's handed off to SDL. Pre-load 3 buffers (MINIMUM_BUFFER_CHECK,
// DynamicSoundEffectInstance.cpp) before playing, then Update() immediately -- nothing has had
// time to actually be consumed by playback yet, so BufferNeeded must not fire at all.
TEST(DynamicSoundEffectInstanceTest, BufferNeededDoesNotFireWhenStreamHasEnoughData)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(4 * 256, 0); // 256 stereo S16 frames of silence
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

TEST(DynamicSoundEffectInstanceTest, Apply3DOnPlayingInstanceAttenuatesLiveTrackGain)
{
    DynamicSoundEffectInstance d(44100, AudioChannels::Stereo);
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
// pending buffers, matching FNA's Stop(true) -> ClearBuffers() -- distinct from
// StopDirectCallWhileNeverPlayedDoesNotClearPendingBuffers above, which covers the guarded
// never-played case.
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

    EXPECT_EQ(d.getPendingBufferCountProperty(), 0);
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
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
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
