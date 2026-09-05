// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Environment.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "SoundEffectInstanceTestAccess.hpp"
#include "SoundEffectTestAccess.hpp"
#include "CNA/Internal/Audio/AudioMixer.hpp"

#include <SDL3_mixer/SDL_mixer.h>
#include "System/Environment.hpp"

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::SoundEffectTestAccess;
using Microsoft::Xna::Framework::Audio::SoundState;
using Microsoft::Xna::Framework::Vector3;

// All SoundEffectInstance tests need a SoundEffect, whose construction opens the
// (dummy) audio device. The fixture skips every test if no device is available.
class SoundEffectInstanceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
        try
        {
            std::vector<unsigned char> pcm(4 * 2048, 0); // 2048 stereo S16 frames
            effect_ = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Stereo);
        }
        catch (...)
        {
            effect_.reset();
        }
    }

    SoundEffectInstance instance() { return effect_->CreateInstance(); }
    bool haveDevice() const { return effect_ != nullptr; }

    std::unique_ptr<SoundEffect> effect_;
};

#define REQUIRE_DEVICE() do { if (!haveDevice()) GTEST_SKIP() << "no audio device"; } while (0)

namespace
{
    // SoundEffect::DistanceScale is a shared static -- save/restore it around any test that
    // changes it, so a failing assertion (or any early return) can't leak a new value into
    // unrelated tests that assume the default (1.0f).
    struct DistanceScaleGuard
    {
        float saved = SoundEffect::getDistanceScaleProperty();
        ~DistanceScaleGuard() { SoundEffect::setDistanceScaleProperty(saved); }
    };

    // Same rationale as DistanceScaleGuard, for SoundEffect::DopplerScale (default 1.0f).
    struct DopplerScaleGuard
    {
        float saved = SoundEffect::getDopplerScaleProperty();
        ~DopplerScaleGuard() { SoundEffect::setDopplerScaleProperty(saved); }
    };
}

TEST_F(SoundEffectInstanceTest, DefaultState)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    EXPECT_FALSE(inst.getIsDisposedProperty());
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
    EXPECT_FALSE(inst.getIsLoopedProperty());
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 1.0f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.0f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 0.0f);
}

TEST_F(SoundEffectInstanceTest, VolumePassesThroughUnclamped)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setVolumeProperty(2.0f); // FNA does not clamp
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 2.0f);
    inst.setVolumeProperty(-0.5f);
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), -0.5f);
    inst.setVolumeProperty(0.3f); // move overload
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.3f);
}

// P10-SEI-002: FNA's Volume setter (SoundEffectInstance.cs) has no IsDisposed/hasStarted guard
// at all -- it always accepts the new value and only conditionally pushes it to the live voice.
// The remaining tests below lock this down at every lifecycle stage the property matrix calls
// for (during play / after pause / after stop / after dispose), matching the same pattern the
// Pitch tests below use.
TEST_F(SoundEffectInstanceTest, VolumeSetWhilePlayingDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);
    inst.setVolumeProperty(0.4f);
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.4f);
}

TEST_F(SoundEffectInstanceTest, VolumeSetAfterPauseDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Pause();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Paused);
    inst.setVolumeProperty(0.6f);
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.6f);
}

TEST_F(SoundEffectInstanceTest, VolumeSetAfterStopDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Stop();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Stopped);
    inst.setVolumeProperty(0.7f);
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.7f);
}

TEST_F(SoundEffectInstanceTest, VolumeSetAfterDisposeDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Dispose();
    ASSERT_TRUE(inst.getIsDisposedProperty());
    inst.setVolumeProperty(0.8f); // unlike Pan, FNA's Volume setter has no IsDisposed check
    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.8f);
}

TEST_F(SoundEffectInstanceTest, PanRangeAndDisposed)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setPanProperty(0.5f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.5f);
    inst.setPanProperty(-1.0f); // move overload, boundary
    EXPECT_FLOAT_EQ(inst.getPanProperty(), -1.0f);
    EXPECT_THROW(inst.setPanProperty(1.5f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(inst.setPanProperty(-1.5f), System::ArgumentOutOfRangeException);
    inst.Dispose();
    EXPECT_THROW(inst.setPanProperty(0.0f), System::ObjectDisposedException);
}

// P10-SEI-002: unlike Volume/Pitch, FNA's Pan setter (SoundEffectInstance.cs) is gated only by
// IsDisposed -- never by hasStarted -- so it must keep working across every other lifecycle
// stage (during play / after pause / after stop), with only the disposed case above throwing.
TEST_F(SoundEffectInstanceTest, PanSetWhilePlayingDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);
    inst.setPanProperty(0.25f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.25f);
}

TEST_F(SoundEffectInstanceTest, PanSetAfterPauseDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Pause();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Paused);
    inst.setPanProperty(-0.25f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), -0.25f);
}

TEST_F(SoundEffectInstanceTest, PanSetAfterStopDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Stop();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Stopped);
    inst.setPanProperty(0.6f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.6f);
}

// P11-PAN-001 (RFC-1): setPanProperty now writes into the shared cooked-callback DSP state
// (filterState_->pan) instead of computing MIX_SetTrackStereo's per-channel gains directly --
// verify that wiring directly via GetPanState, since SDL3_mixer itself still exposes no
// stereo-pan getter of its own (the reason this couldn't be verified this way before RFC-1).
TEST_F(SoundEffectInstanceTest, PanSetterWritesPanIntoDspState)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.setPanProperty(0.5f);
    EXPECT_FLOAT_EQ(SoundEffectInstanceTestAccess::GetPanState(inst), 0.5f);
    inst.setPanProperty(-0.75f);
    EXPECT_FLOAT_EQ(SoundEffectInstanceTestAccess::GetPanState(inst), -0.75f);
}

// P11-PAN-001: Play() must establish the DSP state (and register the shared cooked callback)
// even at the default pan of 0.0f -- RFC-1 requires every playing track to own its stereo image
// via the callback, not just tracks that happen to set a nonzero pan.
TEST_F(SoundEffectInstanceTest, PlayEstablishesDspStateAtDefaultPan)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    EXPECT_FLOAT_EQ(SoundEffectInstanceTestAccess::GetPanState(inst), 0.0f);
}

// P11-PAN-001: setPanProperty() before Play() (no track yet) must not crash and must not
// fabricate DSP state -- matches the existing null-track guards elsewhere in this file.
TEST_F(SoundEffectInstanceTest, PanSetBeforePlayDoesNotCrashOrCreateDspState)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    EXPECT_NO_THROW(inst.setPanProperty(0.5f));
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.5f);
    EXPECT_FLOAT_EQ(SoundEffectInstanceTestAccess::GetPanState(inst), 0.0f);
}

TEST_F(SoundEffectInstanceTest, PitchClampsToRange)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setPitchProperty(0.5f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 0.5f);
    inst.setPitchProperty(2.0f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 1.0f);
    inst.setPitchProperty(-2.0f); // move overload
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), -1.0f);
}

// P12-PITCH-001: setPitchProperty() must actually push the exponential-octave-curve ratio to the
// live track, not just update the Pitch_ property -- verified via the real MIX_Track (SDL3_mixer
// has a real MIX_GetTrackFrequencyRatio getter, unlike stereo pan before P11-PAN-001).
TEST_F(SoundEffectInstanceTest, PitchSetterAppliesExponentialRatioToLiveTrack)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);

    inst.setPitchProperty(1.0f); // +1 octave -> 2x speed
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 2.0f, 1e-5f);

    inst.setPitchProperty(-1.0f); // -1 octave -> half speed
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 0.5f, 1e-5f);

    // The regression case: the old linear formula gave 1.5 here instead of the correct
    // 2^0.5 ~= 1.41421356 (~1 semitone off, audible) -- this is the value that would have failed
    // against the pre-fix code (see this task's git-stash verification).
    inst.setPitchProperty(0.5f);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), std::pow(2.0f, 0.5f), 1e-5f);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.41421356f, 1e-5f);

    inst.setPitchProperty(0.0f); // center: both formulas agree here, sanity check only
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.0f, 1e-5f);
}

// P10-SEI-002: like Volume, FNA's Pitch setter has no IsDisposed/hasStarted guard at all -- it
// always accepts and clamps the new value, only conditionally pushing it to the live voice.
TEST_F(SoundEffectInstanceTest, PitchSetWhilePlayingDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);
    inst.setPitchProperty(0.3f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 0.3f);
}

TEST_F(SoundEffectInstanceTest, PitchSetAfterPauseDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Pause();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Paused);
    inst.setPitchProperty(-0.3f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), -0.3f);
}

TEST_F(SoundEffectInstanceTest, PitchSetAfterStopDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Stop();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Stopped);
    inst.setPitchProperty(0.2f);
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), 0.2f);
}

TEST_F(SoundEffectInstanceTest, PitchSetAfterDisposeDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Dispose();
    ASSERT_TRUE(inst.getIsDisposedProperty());
    inst.setPitchProperty(-0.1f); // unlike Pan, FNA's Pitch setter has no IsDisposed check
    EXPECT_FLOAT_EQ(inst.getPitchProperty(), -0.1f);
}

TEST_F(SoundEffectInstanceTest, IsLoopedBeforePlay)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setIsLoopedProperty(true);
    EXPECT_TRUE(inst.getIsLoopedProperty());
    inst.setIsLoopedProperty(false);
    EXPECT_FALSE(inst.getIsLoopedProperty());
}

TEST_F(SoundEffectInstanceTest, IsLoopedAfterPlayThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    EXPECT_THROW(inst.setIsLoopedProperty(true), System::InvalidOperationException);
}

// P10-SEI-002: FNA's IsLooped setter (SoundEffectInstance.cs) is gated purely by `hasStarted`,
// a one-way latch set the moment Play() first succeeds and never reset -- not by Pause()/Stop(),
// and (unlike Pan) not by IsDisposed either. These lock down the two lifecycle stages that
// IsLoopedAfterPlayThrows above doesn't reach on its own.
TEST_F(SoundEffectInstanceTest, IsLoopedAfterPauseStillThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Pause();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Paused);
    EXPECT_THROW(inst.setIsLoopedProperty(true), System::InvalidOperationException);
}

TEST_F(SoundEffectInstanceTest, IsLoopedAfterStopStillThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Stop();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Stopped);
    EXPECT_THROW(inst.setIsLoopedProperty(true), System::InvalidOperationException);
}

// The converse asymmetry: an instance that is disposed *without ever having played* has never
// set the hasStarted latch, so IsLooped's setter -- having no IsDisposed check of its own --
// does not throw at all here, unlike Pan's ObjectDisposedException in PanRangeAndDisposed above.
TEST_F(SoundEffectInstanceTest, IsLoopedAfterDisposeWithoutEverPlayingDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    ASSERT_TRUE(inst.getIsDisposedProperty());
    inst.setIsLoopedProperty(true);
    EXPECT_TRUE(inst.getIsLoopedProperty());
}

TEST_F(SoundEffectInstanceTest, PlayStopTransitions)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
    inst.Stop();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
}

TEST_F(SoundEffectInstanceTest, StopFalseDoesNotCutOffLoopedPlaybackImmediately)
{
    // Non-immediate Stop must not cut playback off right away -- it only removes the loop so
    // the track finishes its current pass and stops naturally afterward (CP-13).
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setIsLoopedProperty(true);
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);

    inst.Stop(false);
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
}

// P10-LOOP-005: the *immediate* Stop() path (default/true), as its own dedicated case distinct
// from the non-immediate one above -- a looping instance must stop right away instead of
// finishing its current pass first.
TEST_F(SoundEffectInstanceTest, StopTrueCutsOffLoopedPlaybackImmediately)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setIsLoopedProperty(true);
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);

    inst.Stop(true);
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
}

// P10-LOOP-005: Dispose() while an instance is actively looping must cleanly stop and tear down
// the track rather than leaving it playing or crashing -- general dispose-cascade tests exist
// elsewhere (e.g. DisposeIsIdempotent), but none specifically exercise a *looping* instance.
TEST_F(SoundEffectInstanceTest, DisposeWhileLoopingStopsCleanly)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setIsLoopedProperty(true);
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);

    inst.Dispose();
    EXPECT_TRUE(inst.getIsDisposedProperty());
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr);
}

TEST_F(SoundEffectInstanceTest, RepeatedPlayWhileAlreadyPlayingDoesNotRestartTrack)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);

    // Simulate playback having progressed partway through (the fixture's PCM buffer is 2048
    // stereo S16 frames, so 500 is comfortably in bounds), then call Play() again while still
    // Playing. FNA no-ops in this case; a naive re-Play would call MIX_PlayTrack again, which
    // (per SDL_mixer docs) restarts mixing from MIX_PROP_PLAY_START_FRAME_NUMBER's default of 0.
    ASSERT_TRUE(MIX_SetTrackPlaybackPosition(track, 500));
    inst.Play();

    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
    EXPECT_GE(MIX_GetTrackPlaybackPosition(track), 500);
}

TEST_F(SoundEffectInstanceTest, MoveConstructorTransfersTrackAndProperties)
{
    REQUIRE_DEVICE();
    SoundEffectInstance src = instance();
    src.setVolumeProperty(0.5f);
    src.Play();
    ASSERT_EQ(src.getStateProperty(), SoundState::Playing);
    MIX_Track* originalTrack = SoundEffectInstanceTestAccess::GetTrack(src);
    ASSERT_NE(originalTrack, nullptr);

    SoundEffectInstance dst(std::move(src));

    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(dst), originalTrack);
    EXPECT_FLOAT_EQ(dst.getVolumeProperty(), 0.5f);
    EXPECT_EQ(dst.getStateProperty(), SoundState::Playing);

    // The moved-from instance must look disposed so its destructor (which no-ops when
    // isDisposed_ is already true) doesn't try to destroy the track dst now owns.
    EXPECT_TRUE(src.getIsDisposedProperty());
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(src), nullptr);
}

TEST_F(SoundEffectInstanceTest, MoveAssignmentTransfersTrackAndDestroysPreviousOne)
{
    REQUIRE_DEVICE();
    SoundEffectInstance src = instance();
    src.setPanProperty(0.25f);
    src.Play();
    MIX_Track* originalTrack = SoundEffectInstanceTestAccess::GetTrack(src);
    ASSERT_NE(originalTrack, nullptr);

    SoundEffectInstance dst = instance();
    dst.Play();
    ASSERT_NE(SoundEffectInstanceTestAccess::GetTrack(dst), nullptr);

    dst = std::move(src);

    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(dst), originalTrack);
    EXPECT_FLOAT_EQ(dst.getPanProperty(), 0.25f);
    EXPECT_EQ(dst.getStateProperty(), SoundState::Playing);

    EXPECT_TRUE(src.getIsDisposedProperty());
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(src), nullptr);
}

TEST_F(SoundEffectInstanceTest, PauseResume)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    inst.Pause();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Paused);
    inst.Resume();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
    inst.Stop(true);
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
}

// P9-VALIDATION-010: matches FNA (SoundEffectInstance.cs Resume(): "XNA4 just plays if we've not
// started yet") -- Resume() on a never-played instance isn't a no-op, it starts playback.
TEST_F(SoundEffectInstanceTest, ResumeOnNeverPlayedInstanceStartsPlayback)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Stopped);
    inst.Resume();
    EXPECT_EQ(inst.getStateProperty(), SoundState::Playing);
}

// P9-VALIDATION-010: Resume() delegates to Play() when there's no active track, which is also
// how a disposed instance (Dispose() nulls track_) surfaces this instead of silently no-op'ing.
TEST_F(SoundEffectInstanceTest, ResumeAfterDisposeThrowsObjectDisposed)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    EXPECT_THROW(inst.Resume(), System::ObjectDisposedException);
}

TEST_F(SoundEffectInstanceTest, Apply3DSingleListener)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    EXPECT_NO_THROW(inst.Apply3D(listener, emitter));
}

// CABI-6/CABI-32: XNA places no count restriction on this overload, so a count of two is accepted
// rather than refused. This test asserted the opposite refusal until CABI-32; it did not fail when
// the implementation changed under it, because REQUIRE_DEVICE() skips the whole case wherever no
// audio device exists -- which is every headless run. A skipped test defends nothing.
TEST_F(SoundEffectInstanceTest, Apply3DArrayOverload)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    AudioListener listeners[2];
    AudioEmitter emitter;
    EXPECT_NO_THROW(inst.Apply3D(listeners, 1, emitter));
    EXPECT_NO_THROW(inst.Apply3D(listeners, 2, emitter));
    EXPECT_THROW(inst.Apply3D(nullptr, 1, emitter), System::ArgumentNullException);
    // Zero is refused explicitly: XNA hands the count to XACT and surfaces whatever it returns,
    // an outcome not established here, so CNA refuses rather than guessing.
    EXPECT_THROW(inst.Apply3D(listeners, 0, emitter), System::ArgumentOutOfRangeException);
    EXPECT_THROW(inst.Apply3D(listeners, -1, emitter), System::ArgumentOutOfRangeException);
}

// CABI-32: the multi-listener rule is "the nearest listener decides", not "listeners[0] decides".
// The geometry is chosen so those two rules give opposite answers: the emitter sits hard LEFT of
// the far listener and hard RIGHT of the near one, so a +1 pan can only come from the near
// listener. Written this way deliberately -- an assertion that both rules satisfy would pass
// against a first-element implementation and prove nothing.
TEST_F(SoundEffectInstanceTest, Apply3DMultiListenerNearestListenerDominates)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioEmitter emitter;
    emitter.setPositionProperty({0.0f, 0.0f, 0.0f});

    AudioListener far;                                  // emitter is 100 units to its -X: pan -1
    far.setPositionProperty({100.0f, 0.0f, 0.0f});
    AudioListener near;                                 // emitter is 10 units to its +X: pan +1
    near.setPositionProperty({-10.0f, 0.0f, 0.0f});

    const AudioListener farFirst[2]  = {far, near};
    const AudioListener nearFirst[2] = {near, far};

    inst.Apply3D(farFirst, 2, emitter);
    inst.Play();
    const float withFarFirst = SoundEffectInstanceTestAccess::GetPanState(inst);
    EXPECT_NEAR(withFarFirst, 1.0f, 1e-5f) << "the nearer listener must win regardless of position";

    // Order-independence: the same set in the other order must give the same answer.
    inst.Apply3D(nearFirst, 2, emitter);
    EXPECT_NEAR(SoundEffectInstanceTestAccess::GetPanState(inst), withFarFirst, 1e-5f);

    // And the discriminator itself: the far listener alone really does give the opposite pan, so
    // the +1 above could not have come from it.
    inst.Apply3D(far, emitter);
    EXPECT_NEAR(SoundEffectInstanceTestAccess::GetPanState(inst), -1.0f, 1e-5f);
}

TEST_F(SoundEffectInstanceTest, Apply3DDoesNotModifyVolumeOrPanProperties)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setVolumeProperty(0.42f);
    inst.setPanProperty(-0.75f);

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f}); // off to one side, well outside DistanceScale
    inst.Apply3D(listener, emitter);

    EXPECT_FLOAT_EQ(inst.getVolumeProperty(), 0.42f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), -0.75f);
}

// P11-PAN-001 (RFC-1): Apply3D's own computed pan approximation (distinct from the Pan property,
// per CP-20/the test above) must land in the shared DSP state the crossfeed matrix reads --
// directly verifiable now via GetPanState, unlike before RFC-1 (see the now-updated note on
// Apply3DAppliesFullVolumeWithinDistanceScale below).
TEST_F(SoundEffectInstanceTest, Apply3DWritesComputedPanIntoDspState)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f}); // hard right at the default orientation
    inst.Apply3D(listener, emitter);
    inst.Play();

    // Matches CalculatePanIsFullyRightWhenEmitterDirectlyToTheRight's own (10,10)->1.0 result --
    // rightDisplacement and distance are both 10 here (emitter purely along +X).
    EXPECT_NEAR(SoundEffectInstanceTestAccess::GetPanState(inst), 1.0f, 1e-5f);
}

// P9-3D-010: Apply3D must not throw with a rotated (non-default) listener orientation, and
// rotation must only affect the pan projection -- distance attenuation is purely a function of
// Euclidean distance and must stay identical whether the listener faces the default -Z or is
// rotated to face +X.
TEST_F(SoundEffectInstanceTest, Apply3DWithRotatedListenerAppliesSameDistanceAttenuation)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    listener.setForwardProperty(Vector3(1.0f, 0.0f, 0.0f)); // rotated 90 degrees to face +X
    listener.setUpProperty(Vector3::Up);
    AudioEmitter emitter;
    emitter.setPositionProperty({0.0f, 0.0f, 20.0f}); // beyond DistanceScale
    EXPECT_NO_THROW(inst.Apply3D(listener, emitter));
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    // distance == 20, DistanceScale == 10 -> normalizedDistance == 2 -> atten == 1/2, same
    // formula/value regardless of listener orientation (P9-3D-003's inverse law only depends on
    // Euclidean distance, never on Forward/Up).
    EXPECT_NEAR(MIX_GetTrackGain(track), 0.5f, 1e-5f);
}

// P9-3D-003: matches FAudio's F3DAudio.c ComputeDistanceAttenuation's no-custom-curve branch --
// full volume (no attenuation at all) for any distance strictly within DistanceScale
// (CurveDistanceScaler). Verified via the real MIX_Track gain (MIX_GetTrackGain has a getter);
// stereo pan is now also directly verifiable the same way, via GetPanState (P11-PAN-001, see
// Apply3DWritesComputedPanIntoDspState above) -- SDL3_mixer itself still has no pan getter of its
// own, but CNA's own DSP state does since RFC-1 landed.
TEST_F(SoundEffectInstanceTest, Apply3DAppliesFullVolumeWithinDistanceScale)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();

    AudioListener listener; // default position: origin
    AudioEmitter emitter;
    emitter.setPositionProperty({5.0f, 0.0f, 0.0f}); // half of DistanceScale -- still "close"
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackGain(track), 1.0f, 1e-5f);
}

// P9-3D-003: the exact boundary case that distinguishes the fixed formula from the old one --
// real XNA/FNA is still at full volume exactly at distance == DistanceScale (X3DAudio's curve
// only starts falling off strictly beyond it); the old `1/(1+x)` formula was already at half
// volume here.
TEST_F(SoundEffectInstanceTest, Apply3DAppliesFullVolumeExactlyAtDistanceScaleBoundary)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f}); // exactly == DistanceScale
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackGain(track), 1.0f, 1e-5f);
}

// P9-3D-003: beyond DistanceScale, attenuation follows the inverse-distance law
// (gain = DistanceScale / distance), not a continued linear-ish falloff.
TEST_F(SoundEffectInstanceTest, Apply3DAppliesInverseDistanceLawBeyondDistanceScale)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({20.0f, 0.0f, 0.0f}); // 2x DistanceScale
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackGain(track), 0.5f, 1e-5f); // 10/20
}

// P9-3D-005: matches FAudio's F3DAudio.c CalculateDoppler exactly. Listener at origin
// (stationary), emitter at (10,0,0) moving in +X (directly away from the listener) at half the
// speed of sound (343.5 default / 2 = 171.75). emitterVelComponent = dot((-10,0,0),(171.75,0,0))
// / 10 = -171.75; dopplerFactor = 343.5 / (343.5 - (-171.75)) = 343.5/515.25 = 2/3. A receding
// emitter must pitch the sound *down*.
TEST_F(SoundEffectInstanceTest, Apply3DAppliesDopplerPitchDownWhenEmitterRecedes)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener; // default position origin, default velocity zero
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({171.75f, 0.0f, 0.0f}); // moving away from the listener
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 2.0f / 3.0f, 1e-4f);
}

// Same geometry, emitter moving in -X (toward the listener) instead: emitterVelComponent =
// dot((-10,0,0),(-171.75,0,0))/10 = +171.75; dopplerFactor = 343.5/(343.5-171.75) = 2.0. An
// approaching emitter must pitch the sound *up*.
TEST_F(SoundEffectInstanceTest, Apply3DAppliesDopplerPitchUpWhenEmitterApproaches)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({-171.75f, 0.0f, 0.0f}); // moving toward the listener
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 2.0f, 1e-4f);
}

// A moving *listener* produces the same kind of shift as a moving emitter: listener at origin
// moving in +X (toward the stationary emitter at (10,0,0)) at half the speed of sound.
// listenerVelComponent = dot((-10,0,0),(171.75,0,0))/10 = -171.75;
// dopplerFactor = (343.5-(-171.75))/343.5 = 515.25/343.5 = 1.5.
TEST_F(SoundEffectInstanceTest, Apply3DAppliesDopplerPitchUpWhenListenerApproaches)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    listener.setVelocityProperty({171.75f, 0.0f, 0.0f}); // moving toward the emitter
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.5f, 1e-4f);
}

// Matches FNA's UpdatePitch() exactly: `if (!is3D || dopplerScale == 0.0f) doppler = 1.0f;` --
// setting the global SoundEffect.DopplerScale to 0 disables Doppler entirely, regardless of
// relative velocity.
TEST_F(SoundEffectInstanceTest, Apply3DDopplerIsNoOpWhenGlobalDopplerScaleIsZero)
{
    REQUIRE_DEVICE();
    DopplerScaleGuard guard;
    SoundEffect::setDopplerScaleProperty(0.0f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({171.75f, 0.0f, 0.0f}); // would otherwise pitch down
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.0f, 1e-5f);
}

// AUD-09-003 (2026-07-17 deep audit): distinct from Apply3DDopplerIsNoOpWhenGlobalDopplerScaleIsZero
// above -- this leaves DopplerScale at its real XNA default (1.0f) and instead zeroes out both
// listener and emitter velocity. A stationary source/listener pair must never shift pitch purely
// from being positioned in 3D space (position-only, no motion).
TEST_F(SoundEffectInstanceTest, Apply3DZeroVelocitiesYieldNeutralDopplerRegardlessOfDefaultScale)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener; // velocity defaults to zero
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 5.0f, -3.0f}); // position only, no motion
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.0f, 1e-5f);
}

// SAMPLE-059/AUD-09-009: XNA scales the listener/emitter velocities used by the Doppler
// calculation. It does not multiply the completed frequency ratio by the global scale. A
// stationary source therefore remains pitch-neutral even when the global scale is fractional.
TEST_F(SoundEffectInstanceTest, Apply3DZeroVelocitiesRemainNeutralWithFractionalGlobalDopplerScale)
{
    REQUIRE_DEVICE();
    DopplerScaleGuard guard;
    SoundEffect::setDopplerScaleProperty(0.1f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 5.0f, -3.0f});
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.0f, 1e-5f);
}

// SAMPLE-059/AUD-09-009: XNA first combines SoundEffect.DopplerScale and
// AudioEmitter.DopplerScale, then uses that single effective scaler inside X3DAudioCalculate.
// With a receding emitter at half the speed of sound and both scales at 0.5, the effective
// velocity is one eighth the speed of sound and the expected ratio is 343.5 / 386.4375 = 8/9.
TEST_F(SoundEffectInstanceTest, Apply3DCombinesGlobalAndEmitterDopplerScalesInsideCalculation)
{
    REQUIRE_DEVICE();
    DopplerScaleGuard guard;
    SoundEffect::setDopplerScaleProperty(0.5f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setDopplerScaleProperty(0.5f);
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({171.75f, 0.0f, 0.0f});
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 8.0f / 9.0f, 1e-4f);
}

// AUD-09-005: equal listener/emitter velocity (parallel motion, e.g. both riding the same moving
// platform) must produce no *relative* Doppler shift -- both velocity components project onto the
// same emitter-to-listener axis identically, so the ratio's numerator and denominator must cancel
// to exactly 1.0 by construction (ComputeDopplerFactor's own math), not just approximately.
TEST_F(SoundEffectInstanceTest, Apply3DEqualListenerAndEmitterVelocityYieldsNeutralDoppler)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    listener.setVelocityProperty({50.0f, 0.0f, 0.0f});
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({50.0f, 0.0f, 0.0f}); // identical velocity -- moving together
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.0f, 1e-4f);
}

// AUD-09-007: tangential motion (perpendicular to the emitter-to-listener axis) must not shift
// pitch -- only the *radial* velocity component (along that axis) matters for Doppler. Emitter at
// (10,0,0) moving in +Y (straight "sideways" relative to the listener at the origin) has zero
// radial component: dot((-10,0,0),(0,100,0)) == 0.
TEST_F(SoundEffectInstanceTest, Apply3DTangentialMotionYieldsNeutralDoppler)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({0.0f, 100.0f, 0.0f}); // purely tangential, no radial component
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.0f, 1e-4f);
}

// AUD-09-011: coincident listener/emitter positions (distance == 0) must not produce NaN/Inf --
// ComputeDopplerFactor's own distance!=0.0f guard should leave both velocity components at zero
// (undefined direction, matches FAudio's own div-by-zero avoidance), yielding a neutral ratio
// rather than propagating a NaN into MIX_SetTrackFrequencyRatio.
TEST_F(SoundEffectInstanceTest, Apply3DCoincidentPositionsDoesNotProduceNaNOrInf)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    listener.setVelocityProperty({100.0f, 0.0f, 0.0f});
    AudioEmitter emitter;
    emitter.setPositionProperty({0.0f, 0.0f, 0.0f}); // same position as the listener
    emitter.setVelocityProperty({-100.0f, 0.0f, 0.0f});
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    const float ratio = MIX_GetTrackFrequencyRatio(track);
    EXPECT_FALSE(std::isnan(ratio));
    EXPECT_FALSE(std::isinf(ratio));
    EXPECT_NEAR(ratio, 1.0f, 1e-5f);
}

// AUD-09-011: extreme (unrealistically large) velocities must clamp, not overflow/NaN. Real
// F3DAudio.c documents "limit the pitch shifting to 2 octaves up and 1 octave down"
// (ComputeDopplerFactor's own clamp to [0.5, 4.0]).
TEST_F(SoundEffectInstanceTest, Apply3DExtremeVelocityClampsToDocumentedRange)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({-1.0e9f, 0.0f, 0.0f}); // absurdly fast approach
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    const float ratio = MIX_GetTrackFrequencyRatio(track);
    EXPECT_FALSE(std::isnan(ratio));
    EXPECT_FALSE(std::isinf(ratio));
    EXPECT_LE(ratio, 4.0f);
    EXPECT_GE(ratio, 0.5f);
}

// CABI-25: the 3D-versus-pan choice is free until playback begins and fixed afterwards, which is
// XNA's own rule (SoundEffectInstance.cs:130-138 and :376-383, decompiled reference). This
// replaces CP-20's earlier reading, which followed FNA's `if (is3D) return;` and kept the latch
// set for the instance's whole life. SDL3_mixer has no stereo-pan getter, so the state machine is
// observed through SoundEffectInstanceTestAccess rather than the output matrix.
TEST_F(SoundEffectInstanceTest, SetPanBeforePlaybackTakesTheInstanceOutOf3D)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    EXPECT_FALSE(SoundEffectInstanceTestAccess::Is3D(inst));

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    inst.Apply3D(listener, emitter);
    EXPECT_TRUE(SoundEffectInstanceTestAccess::Is3D(inst));

    // Nothing has played yet, so Pan is free to take the instance back out of 3D -- and does.
    inst.setPanProperty(0.9f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.9f);
    EXPECT_FALSE(SoundEffectInstanceTestAccess::Is3D(inst));

    // And back again, still before playback.
    inst.Apply3D(listener, emitter);
    EXPECT_TRUE(SoundEffectInstanceTestAccess::Is3D(inst));
}

// The other half of the same rule: once playing, the choice is fixed and the other call refuses.
TEST_F(SoundEffectInstanceTest, PlaybackFixesTheChoiceBetween3DAndPan)
{
    REQUIRE_DEVICE();
    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});

    SoundEffectInstance spatial = instance();
    spatial.Apply3D(listener, emitter);
    spatial.Play();
    EXPECT_THROW(spatial.setPanProperty(0.5f), System::InvalidOperationException);
    // Stopping releases the choice, so the instance can be aimed again.
    spatial.Stop();
    EXPECT_NO_THROW(spatial.setPanProperty(0.5f));

    SoundEffectInstance panned = instance();
    panned.setPanProperty(0.5f);
    panned.Play();
    EXPECT_THROW(panned.Apply3D(listener, emitter), System::InvalidOperationException);
    panned.Stop();
    EXPECT_NO_THROW(panned.Apply3D(listener, emitter));
}

// The case the two tests above leave out, and the one that matters most in practice: an instance
// that was never aimed at all. XNA's Play() submits the packet, so a plain Play() followed by
// Apply3D() takes the `!isPacketSubmitted` branch as false, never latches `is3d`, and throws
// (SoundEffectInstance.cs:285-289 and :376-383, decompiled reference).
//
// Worth pinning explicitly. Every spatial test in this file used to open with `inst.Play();` and
// aim afterwards, which XNA does not allow; the gate turned all fifteen of them red at once and no
// case here described the transition, because both tests above aim the instance before playing it.
// Aim first, then play.
TEST_F(SoundEffectInstanceTest, Apply3DOnAnInstanceThatWasPlayedWithoutBeingAimedThrows)
{
    REQUIRE_DEVICE();
    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});

    SoundEffectInstance inst = instance();
    inst.Play();
    ASSERT_FALSE(SoundEffectInstanceTestAccess::Is3D(inst));
    EXPECT_THROW(inst.Apply3D(listener, emitter), System::InvalidOperationException);

    // Stop releases the packet, so the instance can be aimed and then played.
    inst.Stop();
    EXPECT_NO_THROW(inst.Apply3D(listener, emitter));
    EXPECT_TRUE(SoundEffectInstanceTestAccess::Is3D(inst));
    EXPECT_NO_THROW(inst.Play());
}

// ===================== AUDIO-001: persistent spatial state =====================
//
// Before this fix, Apply3D()'s computed attenuation/pan/Doppler were applied directly to the
// live track and nowhere else -- "one-shot" per the removed source comment. The four sequences
// below were each a real, previously-untested gap (the pre-existing Apply3D tests above only ever
// call Apply3D() AFTER Play(), and never call Play()/setVolumeProperty()/setPitchProperty() again
// afterward).

// Scenario 1 (AUDIO-001 observable failure #1): Apply3D() called before the very first Play()
// had nothing to write to yet (track_ was still null, so EnsureTrackDspState() no-op'd) and
// nothing was persisted to reapply once Play() actually created the track -- the whole call was
// silently lost.
TEST_F(SoundEffectInstanceTest, Apply3DBeforePlayPersistsSpatialStateOntoLiveTrack)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();
    ASSERT_EQ(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr); // no Play() yet

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({20.0f, 0.0f, 0.0f}); // 2x DistanceScale -> atten == 0.5
    inst.Apply3D(listener, emitter);

    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackGain(track), 0.5f, 1e-5f);
    EXPECT_NEAR(SoundEffectInstanceTestAccess::GetPanState(inst), 1.0f, 1e-5f); // hard right
}

// Scenario 2 (AUDIO-001 observable failure #2, Volume): setVolumeProperty() used to write
// MIX_SetTrackGain(track, Volume_) directly, discarding whatever distance attenuation the last
// Apply3D() call had established.
TEST_F(SoundEffectInstanceTest, SetVolumeAfterApply3DPreservesDistanceAttenuation)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({20.0f, 0.0f, 0.0f}); // atten == 0.5
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    ASSERT_NEAR(MIX_GetTrackGain(track), 0.5f, 1e-5f);

    inst.setVolumeProperty(0.4f);
    EXPECT_NEAR(MIX_GetTrackGain(track), 0.2f, 1e-5f); // 0.4 * 0.5, not a bare 0.4
}

// Scenario 2 (AUDIO-001 observable failure #2, Pitch): setPitchProperty() used to write
// MIX_SetTrackFrequencyRatio(track, pow(2, Pitch_)) directly, with no Doppler term at all,
// discarding whatever Doppler shift the last Apply3D() call had established.
TEST_F(SoundEffectInstanceTest, SetPitchAfterApply3DPreservesDopplerFactor)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({171.75f, 0.0f, 0.0f}); // receding -> doppler == 2/3
    inst.Apply3D(listener, emitter);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    ASSERT_NEAR(MIX_GetTrackFrequencyRatio(track), 2.0f / 3.0f, 1e-4f);

    inst.setPitchProperty(0.5f);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), std::pow(2.0f, 0.5f) * (2.0f / 3.0f), 1e-4f);
}

// Related to observable failure #3 (Pan already retains its own latch via is3D_/CP-20, see
// SetPanAfterApply3DDoesNotClearIs3DLatch above) -- this instead covers a Stop()/replay cycle:
// FNA's dspSettings/is3D persist on the instance across a Stop()->Play() cycle (only Dispose()
// releases them), so a replay with no fresh Apply3D() call must still reapply the last one.
TEST_F(SoundEffectInstanceTest, StopThenReplayReappliesLastApply3DState)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({20.0f, 0.0f, 0.0f}); // atten == 0.5
    inst.Apply3D(listener, emitter);
    inst.Play();

    inst.Stop();
    inst.Play(); // replay -- no new Apply3D() call

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackGain(track), 0.5f, 1e-5f);
}

TEST_F(SoundEffectInstanceTest, Apply3DAfterDisposeThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    AudioListener listener;
    AudioEmitter emitter;
    EXPECT_THROW(inst.Apply3D(listener, emitter), System::ObjectDisposedException);
}

TEST_F(SoundEffectInstanceTest, DisposeIsIdempotent)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    EXPECT_TRUE(inst.getIsDisposedProperty());
    EXPECT_NO_THROW(inst.Dispose());
}

TEST_F(SoundEffectInstanceTest, PlayAfterDisposeThrows)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Dispose();
    EXPECT_THROW(inst.Play(), System::ObjectDisposedException);
}

// AUD-04-008: DestroyMixer() frees every MIX_Track the mixer owned (confirmed against real
// SDL3_mixer source, MIX_DestroyMixer -> MIX_DestroyTrack -> SDL_aligned_free) -- a live
// SoundEffectInstance's track_ becomes a dangling pointer the instant that runs. This test proves
// SoundEffectInstance::GetLiveTrackHandle()'s generation check (AUD-04-008/009) actually detects
// that and clears track_ BEFORE any accessor can dereference it -- directly, deterministically,
// and in-process (no subprocess/crash-detection needed, unlike
// tools/audio/mixer_destroy_active_static_voice_harness.cpp's end-to-end safety net): GetTrack()
// reads track_ raw (bypassing the check) to prove the pointer is still the same dangling value
// immediately after DestroyMixer(), then the first real accessor call is shown to null it out as
// a side effect, which is direct evidence the check ran rather than merely "nothing crashed."
TEST_F(SoundEffectInstanceTest, MixerDestructionOrphansTrackWithoutUseAfterFree)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    ASSERT_EQ(inst.getStateProperty(), SoundState::Playing);
    ASSERT_NE(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr);

    CNA::Internal::Audio::DestroyMixer();

    // Still the same (now-dangling) raw pointer value -- DestroyMixer() itself has no way to
    // reach into this instance, so nothing has cleared it yet.
    EXPECT_NE(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr);

    // getStateProperty() is the first real accessor call after DestroyMixer() -- it must detect
    // the stale generation and report Stopped without ever touching the freed MIX_Track*.
    EXPECT_EQ(inst.getStateProperty(), SoundState::Stopped);
    EXPECT_EQ(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr);

    // Every other operation a real caller could still reach must also be safe now that track_ is
    // null.
    EXPECT_NO_THROW(inst.Pause());
    EXPECT_NO_THROW(inst.Stop());
    EXPECT_NO_THROW(inst.Dispose());
}

TEST_F(SoundEffectInstanceTest, GetTypeName)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    EXPECT_EQ(inst.GetTypeName(), "Microsoft.Xna.Framework.Audio.SoundEffectInstance");
}

// ===================== DSP filters/reverb (T-4C) =====================

// SDL3_mixer has no aux-send/return bus; INTERNAL_applyReverb is a documented no-op (matching
// FNA, which never calls it either -- FACT applies XACT reverb routing natively).
TEST_F(SoundEffectInstanceTest, ApplyReverbDoesNotThrow)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    EXPECT_NO_THROW(SoundEffectInstanceTestAccess::ApplyReverb(inst, 0.5f));
}

// P14-ORDER-002: applying a filter BEFORE Play() now establishes the pending filter state
// immediately (in filterState_) instead of being a no-op -- matches FACT's own atomic
// track-plus-filter creation, instead of copying FNA's `handle == IntPtr.Zero` guard on its own
// dead-code (never actually called for the XACT path) equivalent. Same first-sample
// state-variable filter math as LowPassFilterFirstSampleMatchesStateVariableFilterMath below
// proves the filter is genuinely live, not just recorded -- the recursion doesn't depend on
// whether a live track exists at all.
TEST_F(SoundEffectInstanceTest, ApplyLowPassFilterBeforePlayPersistsAndProcessesSamplesImmediately)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    ASSERT_EQ(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr); // no Play() yet

    SoundEffectInstanceTestAccess::ApplyLowPassFilter(inst, 0.5f);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 1); // FilterState::Kind::LowPass
    EXPECT_FLOAT_EQ(frequency, 0.5f);

    float pcm[2] = {2.0f, 2.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_NEAR(pcm[0], 0.0f, 1e-6f);
    EXPECT_NEAR(pcm[1], 0.0f, 1e-6f);
}

// P14-ORDER-002: the pending filter state established before Play() must still be correctly
// attached (not lost/reset) once the real track is actually created -- proves order-independence
// end to end, not just that the state getter reports something immediately after the pre-Play()
// call above.
TEST_F(SoundEffectInstanceTest, ApplyLowPassFilterBeforePlayAttachesOncePlaying)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    SoundEffectInstanceTestAccess::ApplyLowPassFilter(inst, 0.5f);

    inst.Play();
    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 1); // FilterState::Kind::LowPass -- still established, not reset by Play()
    EXPECT_FLOAT_EQ(frequency, 0.5f);
}

// P14-ORDER-002: a NEW scenario the pending-filter-state fix makes meaningful for the first time
// -- previously, applying a filter before Play() was a no-op, so there was nothing for a move to
// carry over. filterState_ can now hold a real pending filter even with no live track at all;
// verify the move constructor correctly transfers that heap-owned pending state (not just an
// already-registered callback, the scenario LowPassFilterSurvivesMoveConstruction below already
// covers) and that Play() on the MOVED-TO instance still correctly attaches it.
TEST_F(SoundEffectInstanceTest, LowPassFilterAppliedBeforePlaySurvivesMoveConstructionThenAttaches)
{
    REQUIRE_DEVICE();
    SoundEffectInstance src = instance();
    ASSERT_EQ(SoundEffectInstanceTestAccess::GetTrack(src), nullptr); // no Play() yet
    SoundEffectInstanceTestAccess::ApplyLowPassFilter(src, 0.5f);

    SoundEffectInstance dst(std::move(src));
    ASSERT_EQ(SoundEffectInstanceTestAccess::GetTrack(dst), nullptr); // still no track after the move

    dst.Play();
    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(dst);
    ASSERT_NE(track, nullptr);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(dst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 1); // FilterState::Kind::LowPass -- survived the move and attached on Play()
    EXPECT_FLOAT_EQ(frequency, 0.5f);
}

// Regression coverage for the exact state-variable filter FAudio uses (FAudio_internal.c's
// FAudio_INTERNAL_FilterVoice), not just "some" filter: from a fresh (zero) filter state, the
// first sample's outputs are exactly predictable --
//   Yl(1) = Yl(0) + F*Yb(0) = 0
//   Yh(1) = x - Yl(1) - Q1*Yb(0) = x
//   Yb(1) = F*Yh(1) + Yb(0) = F*x
// -- so this pins down the actual math, not just "the value changed".
// P9-BUILD-007: ApplyXFilter() requires a live track_ to attach the REAL SDL3_mixer cooked
// callback to (INTERNAL_apply*Filter's own "no-op if the track hasn't been created yet" guard),
// so Play() has to run first -- but that also starts the real background mixing thread, which
// (even under the SDL dummy driver, which simulates real-time playback via its own timer thread)
// will periodically invoke that SAME real callback concurrently with this test's synchronous
// ProcessFilterSamplesForTest() calls, racing on the shared FilterState (yl/yb). This was a real,
// confirmed-flaky bug (~15-25% failure rate on repeated runs, found while verifying P9-BUILD-007's
// "all audio tests pass" claim) -- not a marginal-numerics issue in the filter math itself (a
// standalone, single-threaded re-run of the exact same recursion converges reliably by iteration
// ~40). Stop()ing the track immediately after applying the filter (but before touching pcm data)
// halts the real mixing thread's involvement with this track without discarding filterState_
// (Stop() never touches it, only Dispose() does), so every ProcessFilterSamplesForTest() call
// after this point is safely single-threaded.
TEST_F(SoundEffectInstanceTest, LowPassFilterFirstSampleMatchesStateVariableFilterMath)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyLowPassFilter(inst, 0.5f);
    inst.Stop(true);

    float pcm[2] = {2.0f, 2.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_NEAR(pcm[0], 0.0f, 1e-6f);
    EXPECT_NEAR(pcm[1], 0.0f, 1e-6f);
}

TEST_F(SoundEffectInstanceTest, HighPassFilterFirstSampleMatchesStateVariableFilterMath)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyHighPassFilter(inst, 0.5f);
    inst.Stop(true);

    float pcm[2] = {2.0f, 2.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_NEAR(pcm[0], 2.0f, 1e-6f);
    EXPECT_NEAR(pcm[1], 2.0f, 1e-6f);
}

TEST_F(SoundEffectInstanceTest, BandPassFilterFirstSampleMatchesStateVariableFilterMath)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyBandPassFilter(inst, 0.5f);
    inst.Stop(true);

    float pcm[2] = {2.0f, 2.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_NEAR(pcm[0], 1.0f, 1e-6f); // F * x = 0.5 * 2.0
    EXPECT_NEAR(pcm[1], 1.0f, 1e-6f);
}

// P11-PAN-001 (RFC-1): the crossfeed pan matrix runs in the same shared cooked callback as the
// filter above (ProcessFilterState calls ApplyPanCrossfeed after any filter). SetPanState/
// ProcessFilterSamples drive it directly, same synchronous-hook pattern as the filter tests
// above, for the same "avoid racing the real mixing thread" reason (Stop() before touching pcm).
TEST_F(SoundEffectInstanceTest, PanCrossfeedAtCenterPanIsIdentity)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play(); // default pan 0.0f -- no SetPanState call needed
    inst.Stop(true);

    float pcm[2] = {1.0f, 3.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_FLOAT_EQ(pcm[0], 1.0f);
    EXPECT_FLOAT_EQ(pcm[1], 3.0f);
}

// CHECKLIST.md CP-19, the deviation RFC-1 fixes: at hard-left pan, the left speaker blends both
// input channels (0.5*L + 0.5*R) and the right speaker goes silent -- the right channel's content
// is preserved (crossfed), not discarded outright the way the old per-channel-only gain did.
TEST_F(SoundEffectInstanceTest, PanCrossfeedAtHardLeftBlendsBothChannelsIntoLeftSpeakerOnly)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::SetPanState(inst, -1.0f);
    inst.Stop(true);

    float pcm[2] = {1.0f, 3.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_NEAR(pcm[0], 2.0f, 1e-6f); // 0.5*1 + 0.5*3
    EXPECT_NEAR(pcm[1], 0.0f, 1e-6f);
}

TEST_F(SoundEffectInstanceTest, PanCrossfeedAtHardRightBlendsBothChannelsIntoRightSpeakerOnly)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::SetPanState(inst, 1.0f);
    inst.Stop(true);

    float pcm[2] = {1.0f, 3.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_NEAR(pcm[0], 0.0f, 1e-6f);
    EXPECT_NEAR(pcm[1], 2.0f, 1e-6f); // 0.5*1 + 0.5*3
}

// Defensive guard (ApplyPanCrossfeed's `channels != 2` early-out) -- in practice SDL3_mixer's
// forced-stereo mode (ApplyTrackProperties's unity MIX_SetTrackStereo) guarantees channels == 2
// for every real callback invocation, but this pins down the fallback behavior anyway.
TEST_F(SoundEffectInstanceTest, PanCrossfeedIgnoresNonStereoChannelCounts)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::SetPanState(inst, -1.0f);
    inst.Stop(true);

    float pcm[1] = {5.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 1, 1);
    EXPECT_FLOAT_EQ(pcm[0], 5.0f);
}

// Proves the RFC-1 design claim that motivated this whole feature (plans/plan_audio.md P10-PAN-003):
// filter and pan crossfeed can share the single SDL3_mixer cooked-callback slot without one
// clobbering the other, since both are independent, sequential float-PCM transforms on the same
// buffer (filter first, then crossfeed -- ProcessFilterState's ordering).
TEST_F(SoundEffectInstanceTest, PanCrossfeedComposesWithFilterInTheSharedCallback)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyHighPassFilter(inst, 0.5f);
    SoundEffectInstanceTestAccess::SetPanState(inst, -1.0f);
    inst.Stop(true);

    // High-pass's first sample from a fresh (zero) filter state passes the input through
    // unchanged (Yh(1) = x - Yl(1) - Q1*Yb(0) = x, matching
    // HighPassFilterFirstSampleMatchesStateVariableFilterMath above), so the crossfeed matrix
    // that runs afterward sees the original {1,3} input and blends it exactly like
    // PanCrossfeedAtHardLeftBlendsBothChannelsIntoLeftSpeakerOnly above.
    float pcm[2] = {1.0f, 3.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_NEAR(pcm[0], 2.0f, 1e-6f); // 0.5*1 + 0.5*3
    EXPECT_NEAR(pcm[1], 0.0f, 1e-6f);
}

// "Needs verification" (CHECKLIST.md/plans/plan_audio.md T-4C): filter coefficient locking follows
// SDL3_mixer's documented practice (MIX_LockMixer/UnlockMixer around FilterState's kind/
// frequency/oneOverQ, matching "the SDL audio device thread holds this same lock while actual
// mixing is in progress") but had never been stress-tested under real concurrency. Unlike the
// three FirstSampleMatchesStateVariableFilterMath tests above (which deliberately Stop() the
// track before touching filter state, precisely to AVOID racing the real mixing thread --
// P9-BUILD-007's own comment documents a confirmed ~15-25%-flaky race when that precaution isn't
// taken), this test does the opposite on purpose: it hammers the real production
// INTERNAL_apply{Low,High,Band}PassFilter setters from a second thread while a real background
// mixing thread (spun up by Play(), even under the SDL dummy driver -- see P9-BUILD-007's
// comment) is concurrently invoking the real SDL3_mixer cooked callback against the very same
// FilterState. Intended to be run under ThreadSanitizer (see plans/plan_audio.md's P9-CATEGORY-011
// follow-up note for the exact TSan build/run commands and result) -- under a normal ASan/UBSan
// or unsanitized build this only checks for crashes/hangs, not data races.
TEST_F(SoundEffectInstanceTest, ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.setIsLoopedProperty(true); // keeps the mixing thread pulling real data for the duration
    inst.Play();
    if (!SoundEffectInstanceTestAccess::GetTrack(inst))
    {
        GTEST_SKIP() << "no real track created";
    }

    std::atomic<bool> stop{false};
    std::thread hammer([&]()
    {
        float f = 0.05f;
        int dir = 1;
        while (!stop.load(std::memory_order_relaxed))
        {
            SoundEffectInstanceTestAccess::ApplyLowPassFilter(inst, f);
            SoundEffectInstanceTestAccess::ApplyHighPassFilter(inst, f);
            SoundEffectInstanceTestAccess::ApplyBandPassFilter(inst, f);
            f += 0.05f * static_cast<float>(dir);
            if (f > 1.9f || f < 0.05f) dir = -dir;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    stop.store(true, std::memory_order_relaxed);
    hammer.join();

    inst.Stop(true);
}

// ===================== XACT track filter wiring (P9-XACT-011) =====================

// Matches FAudio's FACT_INTERNAL_CalculateFilterFrequency exactly: 2*sin(pi*min(f/sr, 0.5)).
TEST(SoundEffectInstanceFilterMathTest, CalculateFilterCutoffMatchesFAudioFormula)
{
    const float expected = 2.0f * std::sin(
        std::numbers::pi_v<float> * std::min(8000.0f / 44100.0f, 0.5f));
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculateFilterCutoff(8000.0f, 44100.0f),
                expected, 1e-6f);
}

// FAudio clamps the cutoff formula's argument to at most 0.5 ("behaves badly as the filter
// frequency gets too high as a fraction of the sample rate") -- a desired frequency at or above
// the Nyquist rate must not exceed the clamp's result.
TEST(SoundEffectInstanceFilterMathTest, CalculateFilterCutoffClampsAtNyquist)
{
    const float atNyquist  = SoundEffectInstanceTestAccess::CalculateFilterCutoff(22050.0f, 44100.0f);
    const float pastNyquist = SoundEffectInstanceTestAccess::CalculateFilterCutoff(44100.0f, 44100.0f);
    EXPECT_NEAR(atNyquist, 2.0f, 1e-6f);   // 2*sin(pi*0.5) == 2
    EXPECT_NEAR(pastNyquist, 2.0f, 1e-6f); // clamped to the same value, not sin(pi*1.0)==0
}

// Matches FAudio's inline `1.0f / (qfactor / 3.0f)` clamped to at most 1.0f
// (FACT_internal.c, SOUND_FLAG_COMPLEX track-init site).
TEST(SoundEffectInstanceFilterMathTest, CalculateFilterOneOverQMatchesFAudioFormula)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculateFilterOneOverQ(6), 0.5f, 1e-6f);
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculateFilterOneOverQ(3), 1.0f, 1e-6f); // 3/3==1, unclamped
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculateFilterOneOverQ(1), 1.0f, 1e-6f); // 3/1==3, clamped to 1
}

// qfactorRaw == 0 would divide by zero in FAudio's own formula; CNA defends against it by
// falling back to the "no filter"/unity default instead (real XACT tool output never emits 0).
TEST(SoundEffectInstanceFilterMathTest, CalculateFilterOneOverQGuardsDivideByZero)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculateFilterOneOverQ(0), 1.0f, 1e-6f);
}

// P12-PITCH-001: pure-math coverage for the Pitch-to-frequency-ratio exponential octave curve
// (FNA's SoundEffectInstance.cs:589-591, `Math.Pow(2.0, INTERNAL_pitch)`).
TEST(SoundEffectInstanceFilterMathTest, CalculatePitchRatioIsOneAtCenterPitch)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePitchRatio(0.0f), 1.0f, 1e-6f);
}

TEST(SoundEffectInstanceFilterMathTest, CalculatePitchRatioIsDoubleAtPlusOneOctave)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePitchRatio(1.0f), 2.0f, 1e-6f);
}

TEST(SoundEffectInstanceFilterMathTest, CalculatePitchRatioIsHalfAtMinusOneOctave)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePitchRatio(-1.0f), 0.5f, 1e-6f);
}

// The actual regression case: this is where the old linear formula (`1.0f + pitch`) diverges from
// the correct exponential curve -- 1.5 (old, wrong) vs. 2^0.5 ~= 1.41421356 (correct), a ~6%/~1
// semitone difference. Endpoints (-1, 0, 1) coincidentally agree between both formulas, which is
// exactly why this bug went undetected: no pre-existing test ever exercised a non-endpoint pitch.
TEST(SoundEffectInstanceFilterMathTest, CalculatePitchRatioAtHalfOctaveMatchesExponentialNotLinearCurve)
{
    const float ratio = SoundEffectInstanceTestAccess::CalculatePitchRatio(0.5f);
    EXPECT_NEAR(ratio, std::pow(2.0f, 0.5f), 1e-6f);
    EXPECT_NEAR(ratio, 1.41421356f, 1e-5f);
    EXPECT_GT(std::abs(ratio - 1.5f), 0.05f); // clearly NOT the old linear formula's 1.5
}

TEST(SoundEffectInstanceFilterMathTest, CalculatePitchRatioAtNegativeHalfOctaveMatchesExponentialNotLinearCurve)
{
    const float ratio = SoundEffectInstanceTestAccess::CalculatePitchRatio(-0.5f);
    EXPECT_NEAR(ratio, std::pow(2.0f, -0.5f), 1e-6f);
    EXPECT_NEAR(ratio, 0.70710678f, 1e-5f);
    EXPECT_GT(std::abs(ratio - 0.75f), 0.03f); // clearly NOT the old linear formula's 0.75
}

// P9-3D-007: Apply3D's pan formula, unit-tested directly since SDL3_mixer has no
// MIX_GetTrackStereo getter to verify the applied result through (P9-3D-001's finding).
// `dx` is listener-to-emitter X displacement (emitter.X - listener.X); an emitter directly to
// the right of the listener (positive dx, distance == dx) must pan fully right (+1.0).
TEST(SoundEffectInstanceFilterMathTest, CalculatePanIsFullyRightWhenEmitterDirectlyToTheRight)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePan(10.0f, 10.0f), 1.0f, 1e-6f);
}

// An emitter directly to the left (negative dx) must pan fully left (-1.0).
TEST(SoundEffectInstanceFilterMathTest, CalculatePanIsFullyLeftWhenEmitterDirectlyToTheLeft)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePan(-10.0f, 10.0f), -1.0f, 1e-6f);
}

// An emitter directly in front of or behind the listener (dx == 0, all displacement on Z) must
// pan dead center -- this linear approximation only accounts for the X axis (no front/back
// distinction, a documented limitation of the pan-only-no-elevation approach, CHECKLIST.md).
TEST(SoundEffectInstanceFilterMathTest, CalculatePanIsCenteredWhenEmitterDirectlyAheadOrBehind)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePan(0.0f, 10.0f), 0.0f, 1e-6f);
}

// An emitter at a diagonal must produce a partial (non-extreme) pan value proportional to the X
// component of its direction -- e.g. at 45 degrees (dx == dz, distance == dx*sqrt(2)), pan ==
// 1/sqrt(2) =~ 0.7071.
TEST(SoundEffectInstanceFilterMathTest, CalculatePanIsPartialAtADiagonal)
{
    const float dx = 10.0f;
    const float distance = dx * std::sqrt(2.0f); // dx and dz equal magnitude
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePan(dx, distance), 1.0f / std::sqrt(2.0f), 1e-4f);
}

// Same position (distance == 0) has no meaningful direction -- must center rather than divide by
// zero.
TEST(SoundEffectInstanceFilterMathTest, CalculatePanIsCenteredAtZeroDistance)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePan(0.0f, 0.0f), 0.0f, 1e-6f);
}

// The formula clamps to [-1,1] even if dx somehow exceeded distance (shouldn't happen
// geometrically, but matches the Pan property's own valid range defensively).
TEST(SoundEffectInstanceFilterMathTest, CalculatePanClampsToValidRange)
{
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePan(20.0f, 10.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(SoundEffectInstanceTestAccess::CalculatePan(-20.0f, 10.0f), -1.0f, 1e-6f);
}

// P11-PAN-001 (RFC-1): pure-math coverage for the 4-coefficient stereo crossfeed pan matrix
// (FNA's SetPanMatrixCoefficients, SrcChannelCount==2/DstChannelCount==2 branch).
TEST(SoundEffectInstanceFilterMathTest, PanCrossfeedMatrixIsIdentityAtCenterPan)
{
    float ll, rl, lr, rr;
    SoundEffectInstanceTestAccess::CalculatePanCrossfeedMatrix(0.0f, ll, rl, lr, rr);
    EXPECT_NEAR(ll, 1.0f, 1e-6f);
    EXPECT_NEAR(rl, 0.0f, 1e-6f);
    EXPECT_NEAR(lr, 0.0f, 1e-6f);
    EXPECT_NEAR(rr, 1.0f, 1e-6f);
}

// CHECKLIST.md CP-19, the deviation RFC-1 fixes: hard panning must NOT eliminate an entire
// channel -- both source channels blend into whichever speaker `pan` favors, and only the OTHER
// speaker goes silent.
TEST(SoundEffectInstanceFilterMathTest, PanCrossfeedMatrixAtHardLeftBlendsIntoLeftSpeakerOnly)
{
    float ll, rl, lr, rr;
    SoundEffectInstanceTestAccess::CalculatePanCrossfeedMatrix(-1.0f, ll, rl, lr, rr);
    EXPECT_NEAR(ll, 0.5f, 1e-6f);
    EXPECT_NEAR(rl, 0.5f, 1e-6f);
    EXPECT_NEAR(lr, 0.0f, 1e-6f);
    EXPECT_NEAR(rr, 0.0f, 1e-6f);
}

TEST(SoundEffectInstanceFilterMathTest, PanCrossfeedMatrixAtHardRightBlendsIntoRightSpeakerOnly)
{
    float ll, rl, lr, rr;
    SoundEffectInstanceTestAccess::CalculatePanCrossfeedMatrix(1.0f, ll, rl, lr, rr);
    EXPECT_NEAR(ll, 0.0f, 1e-6f);
    EXPECT_NEAR(rl, 0.0f, 1e-6f);
    EXPECT_NEAR(lr, 0.5f, 1e-6f);
    EXPECT_NEAR(rr, 0.5f, 1e-6f);
}

TEST(SoundEffectInstanceFilterMathTest, PanCrossfeedMatrixAtPartialLeftPan)
{
    float ll, rl, lr, rr;
    SoundEffectInstanceTestAccess::CalculatePanCrossfeedMatrix(-0.5f, ll, rl, lr, rr);
    EXPECT_NEAR(ll, 0.75f, 1e-6f);
    EXPECT_NEAR(rl, 0.25f, 1e-6f);
    EXPECT_NEAR(lr, 0.0f, 1e-6f);
    EXPECT_NEAR(rr, 0.5f, 1e-6f);
}

TEST(SoundEffectInstanceFilterMathTest, PanCrossfeedMatrixAtPartialRightPan)
{
    float ll, rl, lr, rr;
    SoundEffectInstanceTestAccess::CalculatePanCrossfeedMatrix(0.5f, ll, rl, lr, rr);
    EXPECT_NEAR(ll, 0.5f, 1e-6f);
    EXPECT_NEAR(rl, 0.0f, 1e-6f);
    EXPECT_NEAR(lr, 0.25f, 1e-6f);
    EXPECT_NEAR(rr, 0.75f, 1e-6f);
}

// The FAVORED destination speaker's own two coefficients always sum to exactly 1.0 -- e.g. at
// pan<=0 the left speaker blends 100% of the combined signal ((0.5*pan+1) + 0.5*-pan == 1),
// while the right (non-favored) speaker's row instead shrinks toward 0 as pan moves further from
// center (lr+rr == pan+1 for pan<=0). This asymmetry is exactly what makes the matrix reproduce
// FNA's separate mono-source formula automatically when fed a duplicated-mono signal (L == R),
// verified directly by the next test.
TEST(SoundEffectInstanceFilterMathTest, PanCrossfeedMatrixFavoredSpeakerRowSumsToOne)
{
    for (float pan : {-1.0f, -0.5f, -0.1f, 0.0f, 0.1f, 0.5f, 1.0f})
    {
        float ll, rl, lr, rr;
        SoundEffectInstanceTestAccess::CalculatePanCrossfeedMatrix(pan, ll, rl, lr, rr);
        if (pan <= 0.0f)
            EXPECT_NEAR(ll + rl, 1.0f, 1e-6f) << "pan=" << pan;
        else
            EXPECT_NEAR(lr + rr, 1.0f, 1e-6f) << "pan=" << pan;
    }
}

// Direct proof that the crossfeed matrix alone covers every source channel count SDL3_mixer's
// forced-stereo mode can produce: feeding a duplicated-mono signal (L==R) through it must match
// FNA's separate mono-source formula exactly (SoundEffectInstance.cs's SrcChannelCount==1 branch:
// outputMatrix[0] = (pan>0)?(1-pan):1.0, outputMatrix[1] = (pan<0)?(1+pan):1.0) -- so no separate
// mono branch is needed anywhere in the production code (ApplyPanCrossfeed, SoundEffectInstance.cpp).
TEST(SoundEffectInstanceFilterMathTest, PanCrossfeedMatrixOnDuplicatedMonoMatchesMonoFormula)
{
    for (float pan : {-1.0f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 1.0f})
    {
        float ll, rl, lr, rr;
        SoundEffectInstanceTestAccess::CalculatePanCrossfeedMatrix(pan, ll, rl, lr, rr);

        const float x = 3.0f;
        const float outL = x * ll + x * rl;
        const float outR = x * lr + x * rr;

        const float monoLeftGain  = (pan > 0.0f) ? (1.0f - pan) : 1.0f;
        const float monoRightGain = (pan < 0.0f) ? (1.0f + pan) : 1.0f;

        EXPECT_NEAR(outL, x * monoLeftGain, 1e-6f) << "pan=" << pan;
        EXPECT_NEAR(outR, x * monoRightGain, 1e-6f) << "pan=" << pan;
    }
}

// P9-3D-010: for the default listener orientation (Forward=(0,0,-1), Up=(0,1,0)), the listener's
// own right axis must reduce to exactly world Vector3.Right -- this is what makes the rotation-
// aware pan projection a strict generalization of the old world-X-only approximation instead of a
// behavior change for the common (unrotated) case.
TEST(SoundEffectInstanceFilterMathTest, CalculateListenerRightMatchesWorldRightForDefaultOrientation)
{
    const Vector3 right = SoundEffectInstanceTestAccess::CalculateListenerRight(
        Vector3::Forward, Vector3::Up);
    EXPECT_NEAR(right.X, Vector3::Right.X, 1e-6f);
    EXPECT_NEAR(right.Y, Vector3::Right.Y, 1e-6f);
    EXPECT_NEAR(right.Z, Vector3::Right.Z, 1e-6f);
}

// A listener rotated 90 degrees to face world +X (instead of the default -Z) must have its own
// right axis rotate correspondingly, to world +Z -- proves the projection actually tracks the
// listener's facing direction rather than a hardcoded axis.
TEST(SoundEffectInstanceFilterMathTest, CalculateListenerRightRotatesWithListenerFacingDirection)
{
    const Vector3 facingPositiveX(1.0f, 0.0f, 0.0f);
    const Vector3 right = SoundEffectInstanceTestAccess::CalculateListenerRight(
        facingPositiveX, Vector3::Up);
    EXPECT_NEAR(right.X, 0.0f, 1e-6f);
    EXPECT_NEAR(right.Y, 0.0f, 1e-6f);
    EXPECT_NEAR(right.Z, 1.0f, 1e-6f);
}

// A degenerate orientation (Forward and Up parallel, so their cross product is zero) must fall
// back to world Vector3.Right rather than dividing by (near) zero and producing NaN/Inf.
TEST(SoundEffectInstanceFilterMathTest, CalculateListenerRightFallsBackToWorldRightWhenDegenerate)
{
    const Vector3 right = SoundEffectInstanceTestAccess::CalculateListenerRight(
        Vector3::Forward, Vector3::Forward);
    EXPECT_NEAR(right.X, Vector3::Right.X, 1e-6f);
    EXPECT_NEAR(right.Y, Vector3::Right.Y, 1e-6f);
    EXPECT_NEAR(right.Z, Vector3::Right.Z, 1e-6f);
}

namespace
{
    // P10-3D-003: reproduces Apply3D's own composition of the two pan primitives (listener-right
    // projection, then the distance-normalized pan formula) instead of feeding an already-isolated
    // dx/distance pair -- this is what actually exercises the full pipeline for the six canonical
    // emitter directions, not just the formula in isolation.
    float ComposedPan(const Vector3& listenerToEmitter)
    {
        const Vector3 right = SoundEffectInstanceTestAccess::CalculateListenerRight(
            Vector3::Forward, Vector3::Up); // default listener orientation
        const float rightDisplacement =
            listenerToEmitter.X * right.X + listenerToEmitter.Y * right.Y + listenerToEmitter.Z * right.Z;
        return SoundEffectInstanceTestAccess::CalculatePan(rightDisplacement, listenerToEmitter.Length());
    }
}

// P10-3D-003: emitter directly to the listener's right -- full geometry pipeline must agree with
// the isolated-formula test (CalculatePanIsFullyRightWhenEmitterDirectlyToTheRight) above.
TEST(SoundEffectInstanceFilterMathTest, ComposedPanIsFullyRightWhenEmitterDirectlyToTheRight)
{
    EXPECT_NEAR(ComposedPan(Vector3(10.0f, 0.0f, 0.0f)), 1.0f, 1e-6f);
}

// P10-3D-003: emitter directly to the listener's left.
TEST(SoundEffectInstanceFilterMathTest, ComposedPanIsFullyLeftWhenEmitterDirectlyToTheLeft)
{
    EXPECT_NEAR(ComposedPan(Vector3(-10.0f, 0.0f, 0.0f)), -1.0f, 1e-6f);
}

// P10-3D-003: emitter directly ahead of the listener (along the default Forward, (0,0,-1)) has no
// rightward component at all -- must pan dead center.
TEST(SoundEffectInstanceFilterMathTest, ComposedPanIsCenteredWhenEmitterDirectlyAhead)
{
    EXPECT_NEAR(ComposedPan(Vector3(0.0f, 0.0f, -10.0f)), 0.0f, 1e-6f);
}

// P10-3D-003: emitter directly behind the listener -- same "no rightward component" reasoning as
// directly ahead; this pan-only approximation cannot and does not distinguish front from behind
// (documented limitation, CHECKLIST.md).
TEST(SoundEffectInstanceFilterMathTest, ComposedPanIsCenteredWhenEmitterDirectlyBehind)
{
    EXPECT_NEAR(ComposedPan(Vector3(0.0f, 0.0f, 10.0f)), 0.0f, 1e-6f);
}

// P10-3D-003: emitter directly above the listener. Previously entirely untested (plans/plan_audio.md's
// gap note) -- vertical displacement is orthogonal to the listener's right axis by construction,
// so it must produce zero rightward projection and pan dead center, never a divide-by-zero or
// stray nonzero value.
TEST(SoundEffectInstanceFilterMathTest, ComposedPanIsCenteredWhenEmitterDirectlyAbove)
{
    EXPECT_NEAR(ComposedPan(Vector3(0.0f, 10.0f, 0.0f)), 0.0f, 1e-6f);
}

// P10-3D-003: emitter directly below the listener -- same reasoning as directly above.
TEST(SoundEffectInstanceFilterMathTest, ComposedPanIsCenteredWhenEmitterDirectlyBelow)
{
    EXPECT_NEAR(ComposedPan(Vector3(0.0f, -10.0f, 0.0f)), 0.0f, 1e-6f);
}

// INTERNAL_applyXactTrackFilter dispatches filterType 0/1/2 to the matching Low/Band/HighPass
// method and threads the converted oneOverQ through -- verified via the test-only filter-state
// getter rather than the recursive math (already pinned down above for the plain float-cutoff
// entry points).
TEST_F(SoundEffectInstanceTest, ApplyXactTrackFilterDispatchesHighPassWithConvertedOneOverQ)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/2, 8000.0f, /*qfactorRaw=*/6);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 2); // FilterState::Kind::HighPass
    EXPECT_NEAR(oneOverQ, 0.5f, 1e-6f);
    // P11-TEST-001: exact value, not just non-zero -- compare against the same Hz->cutoff
    // conversion the production code uses, against the real mixer's own sample rate.
    SDL_AudioSpec spec{};
    ASSERT_TRUE(MIX_GetMixerFormat(CNA::Internal::Audio::GetMixer(), &spec));
    EXPECT_NEAR(frequency,
                SoundEffectInstanceTestAccess::CalculateFilterCutoff(8000.0f, static_cast<float>(spec.freq)),
                1e-6f);
}

TEST_F(SoundEffectInstanceTest, ApplyXactTrackFilterDispatchesLowPassType)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/0, 4000.0f, /*qfactorRaw=*/3);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 1); // FilterState::Kind::LowPass
    EXPECT_NEAR(oneOverQ, 1.0f, 1e-6f);
}

TEST_F(SoundEffectInstanceTest, ApplyXactTrackFilterDispatchesBandPassType)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/1, 4000.0f, /*qfactorRaw=*/6);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 3); // FilterState::Kind::BandPass
    EXPECT_NEAR(oneOverQ, 0.5f, 1e-6f);
}

// An unrecognized filterType (not reachable via XactParser.cpp's real bit-decode, but the method
// takes a raw uint8_t) must leave the instance without an active filter, matching the defensive
// default: case-less values fall through and do nothing.
TEST_F(SoundEffectInstanceTest, ApplyXactTrackFilterIgnoresUnrecognizedType)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/0xAB, 4000.0f, 3);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 0); // FilterState::Kind::None
}

// P14-ORDER-002: calling this before Play() now persists the XACT-authored filter too (the mixer's
// own format, needed to convert frequencyHz -> a normalized cutoff, is a device-level property
// available as soon as the shared mixer exists, independent of whether THIS instance has a track
// yet) -- matches ApplyXactTrackFilterDispatchesHighPassWithConvertedOneOverQ's own fixture
// values, just called before Play() instead of after, to prove the result is identical either way.
TEST_F(SoundEffectInstanceTest, ApplyXactTrackFilterBeforePlayPersistsAndAttachesOncePlaying)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    ASSERT_EQ(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr); // no Play() yet

    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/2, 8000.0f, /*qfactorRaw=*/6);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 2); // FilterState::Kind::HighPass
    EXPECT_NEAR(oneOverQ, 0.5f, 1e-6f);

    inst.Play();
    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);

    int kind2 = -1; float frequency2 = -1.0f, oneOverQ2 = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind2, frequency2, oneOverQ2);
    EXPECT_EQ(kind2, kind); // still HighPass -- not reset by Play()
    EXPECT_NEAR(frequency2, frequency, 1e-6f);
    EXPECT_NEAR(oneOverQ2, oneOverQ, 1e-6f);
}

// P14-ORDER-002: the continuous RPC filter override (P10-FILTER-002/003) is also order-independent
// of Play() now -- a non-`None` filter kind already implies a base filter was established (which
// itself requires the mixer to already exist), so overriding frequency/Q before a track exists is
// safe and correctly persists into filterState_, the same way the base filter does above.
TEST_F(SoundEffectInstanceTest, ApplyRpcFilterOverrideBeforePlayPersistsAndAttachesOncePlaying)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    ASSERT_EQ(SoundEffectInstanceTestAccess::GetTrack(inst), nullptr); // no Play() yet

    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/0, 4000.0f, /*qfactorRaw=*/3);
    SoundEffectInstanceTestAccess::ApplyRpcFilterOverride(inst, /*rpcFrequencyHz=*/8000.0f, /*rpcQFactor=*/4.0f);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 1); // FilterState::Kind::LowPass, unchanged by the RPC override
    EXPECT_NEAR(oneOverQ, 0.25f, 1e-6f); // matches FAudio's plain `1.0f / rpcResult`

    inst.Play();
    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);

    int kind2 = -1; float frequency2 = -1.0f, oneOverQ2 = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind2, frequency2, oneOverQ2);
    EXPECT_EQ(kind2, kind);
    EXPECT_NEAR(frequency2, frequency, 1e-6f); // the 8000Hz override, not the 4000Hz base
    EXPECT_NEAR(oneOverQ2, oneOverQ, 1e-6f);
}

// ===================== INTERNAL_applyRpcFilterOverride (P10-FILTER-002/003) =====================
//
// Base filter established via ApplyXactTrackFilter(filterType=0/LowPass, 4000Hz, qfactorRaw=3 ->
// oneOverQ=1.0f), matching ApplyXactTrackFilterDispatchesLowPassType's own fixture values above.

TEST_F(SoundEffectInstanceTest, ApplyRpcFilterOverrideOverridesFrequencyOnlyWhenQFactorSentinelIsNegative)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/0, 4000.0f, /*qfactorRaw=*/3);
    int kind = -1; float baseFrequency = -1.0f, baseOneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, baseFrequency, baseOneOverQ);

    SoundEffectInstanceTestAccess::ApplyRpcFilterOverride(inst, /*rpcFrequencyHz=*/8000.0f, /*rpcQFactor=*/-1.0f);

    int kind2 = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind2, frequency, oneOverQ);
    EXPECT_EQ(kind2, kind); // never changes the filter's kind
    EXPECT_NE(frequency, baseFrequency); // a higher RPC-targeted Hz value converts to a different cutoff
    EXPECT_NEAR(oneOverQ, baseOneOverQ, 1e-6f); // Q axis untouched -- falls back to the base value
}

TEST_F(SoundEffectInstanceTest, ApplyRpcFilterOverrideOverridesQFactorOnlyWhenFrequencySentinelIsNegative)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/0, 4000.0f, /*qfactorRaw=*/3);
    int kind = -1; float baseFrequency = -1.0f, baseOneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, baseFrequency, baseOneOverQ);

    SoundEffectInstanceTestAccess::ApplyRpcFilterOverride(inst, /*rpcFrequencyHz=*/-1.0f, /*rpcQFactor=*/2.0f);

    int kind2 = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind2, frequency, oneOverQ);
    EXPECT_NEAR(frequency, baseFrequency, 1e-6f); // frequency axis untouched -- falls back to base
    EXPECT_NEAR(oneOverQ, 0.5f, 1e-6f); // matches FAudio's plain `1.0f / rpcResult` (FACT_internal.c)
}

TEST_F(SoundEffectInstanceTest, ApplyRpcFilterOverrideOverridesBothAxesWhenBothProvided)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/0, 4000.0f, /*qfactorRaw=*/3);

    SoundEffectInstanceTestAccess::ApplyRpcFilterOverride(inst, /*rpcFrequencyHz=*/8000.0f, /*rpcQFactor=*/4.0f);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_NEAR(oneOverQ, 0.25f, 1e-6f);
    // P11-TEST-001: exact value, not just non-zero -- the RPC override (8000Hz) wins over the
    // 4000Hz base, compared against the real mixer's own sample rate.
    SDL_AudioSpec spec{};
    ASSERT_TRUE(MIX_GetMixerFormat(CNA::Internal::Audio::GetMixer(), &spec));
    EXPECT_NEAR(frequency,
                SoundEffectInstanceTestAccess::CalculateFilterCutoff(8000.0f, static_cast<float>(spec.freq)),
                1e-6f);
}

// Both axes at the "no override" sentinel must reproduce exactly the base frequency/Q -- a
// steady-state cue whose RPC bindings don't currently target either filter axis this tick.
TEST_F(SoundEffectInstanceTest, ApplyRpcFilterOverrideWithBothSentinelsReproducesBaseValues)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, /*filterType=*/0, 4000.0f, /*qfactorRaw=*/3);
    int kind = -1; float baseFrequency = -1.0f, baseOneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, baseFrequency, baseOneOverQ);

    SoundEffectInstanceTestAccess::ApplyRpcFilterOverride(inst, -1.0f, -1.0f);

    int kind2 = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind2, frequency, oneOverQ);
    EXPECT_NEAR(frequency, baseFrequency, 1e-6f);
    EXPECT_NEAR(oneOverQ, baseOneOverQ, 1e-6f);
}

// Matches FAudio's own guard (`if (sound->sound->tracks[i].filter != 0xFF)`) -- a track with no
// filter at all must not have one spuriously created by an RPC filter-targeting curve.
TEST_F(SoundEffectInstanceTest, ApplyRpcFilterOverrideIsNoOpWhenNoFilterIsActive)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();

    SoundEffectInstanceTestAccess::ApplyRpcFilterOverride(inst, 8000.0f, 2.0f);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 0); // FilterState::Kind::None -- still no filter was ever created
}

// A constant (DC) signal repeatedly fed through the low-pass filter must converge to unity gain
// -- verifies the recursive state (yl/yb) persists correctly across multiple calls, not just a
// single-sample transient.
TEST_F(SoundEffectInstanceTest, LowPassFilterConvergesToUnityGainForConstantSignal)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyLowPassFilter(inst, 0.5f);
    inst.Stop(true); // P9-BUILD-007: stop the real mixing thread's involvement, see the comment above

    float pcm[2];
    for (int i = 0; i < 2000; ++i)
    {
        pcm[0] = 3.0f;
        pcm[1] = 3.0f;
        SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    }
    EXPECT_NEAR(pcm[0], 3.0f, 0.01f);
    EXPECT_NEAR(pcm[1], 3.0f, 0.01f);
}

// A constant (DC) signal fed through the high-pass filter must converge to zero.
TEST_F(SoundEffectInstanceTest, HighPassFilterConvergesToZeroForConstantSignal)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    inst.Play();
    SoundEffectInstanceTestAccess::ApplyHighPassFilter(inst, 0.5f);
    inst.Stop(true); // P9-BUILD-007: stop the real mixing thread's involvement, see the comment above

    float pcm[2];
    for (int i = 0; i < 2000; ++i)
    {
        pcm[0] = 3.0f;
        pcm[1] = 3.0f;
        SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    }
    EXPECT_NEAR(pcm[0], 0.0f, 0.01f);
    EXPECT_NEAR(pcm[1], 0.0f, 0.01f);
}

// Regression coverage for filterState_'s move-safety design (T-4C): filterState_ is heap-owned
// via unique_ptr specifically so a move transfers ownership without changing the FilterState
// object's address, meaning the SDL3_mixer callback registered on the (also-transferred) track_
// stays valid with no re-registration. Verify the filter keeps working correctly (not just
// "doesn't crash") on the moved-to instance, including continuity of its recursive state.
TEST_F(SoundEffectInstanceTest, LowPassFilterSurvivesMoveConstruction)
{
    REQUIRE_DEVICE();
    SoundEffectInstance src = instance();
    src.Play();
    SoundEffectInstanceTestAccess::ApplyLowPassFilter(src, 0.5f);
    src.Stop(true); // P9-BUILD-007: stop the real mixing thread's involvement, see the comment above
    // track_ itself (and its cooked-callback registration) survives Stop() -- only Dispose()
    // destroys it -- so this still meaningfully exercises "the same real callback stays valid
    // across the move", just without a live mixing thread racing the manual loop below.

    float pcm[2] = {2.0f, 2.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(src, pcm, 2, 2);
    ASSERT_NEAR(pcm[0], 0.0f, 1e-6f); // first-sample transient from a fresh filter state

    SoundEffectInstance dst(std::move(src));

    // Continue feeding the same constant signal on the MOVED-TO instance; state must carry over
    // (i.e. this is a continuation of the same recursion, not a freshly-reset filter).
    for (int i = 0; i < 2000; ++i)
    {
        pcm[0] = 2.0f;
        pcm[1] = 2.0f;
        SoundEffectInstanceTestAccess::ProcessFilterSamples(dst, pcm, 2, 2);
    }
    EXPECT_NEAR(pcm[0], 2.0f, 0.01f);
    EXPECT_NEAR(pcm[1], 2.0f, 0.01f);
}

// ===================== Bounded loop region real playback behavior (P10-LOOP-003/004) =====================
//
// CHECKLIST.md's CP-17 entry and this file's own prior comments asserted that
// MIX_PROP_PLAY_MAX_FRAME_NUMBER "also truncates the very first, pre-loop playthrough... not just
// later iterations" -- but that claim was never actually verified against real decoded audio (the
// note explicitly said so: "the actual mixed effect can't be black-box-verified... without
// decoding the real audio output"). It no longer holds: MIX_SetTrackRawCallback lets a test
// observe the real decoded PCM in playback order directly, and doing so here shows the intro
// plays exactly once, then only the loop region repeats -- matching XNA/XAudio2's
// LoopBegin/LoopLength semantics exactly, via nothing more than the LOOP_START_FRAME_NUMBER +
// MAX_FRAME_NUMBER combination already in place. See plans/plan_audio.md's P10-LOOP-003/004 note for
// the corrected finding and the now-stale CHECKLIST.md/docs rows this invalidates.
namespace
{
    struct BoundedLoopRawCallbackCtx
    {
        std::atomic<long long> totalFrames{0};
        std::atomic<bool> sawLoopRegion{false};
        std::atomic<bool> introSeenAfterLoopRegionStarted{false};
    };

    // Classifies each raw-callback buffer by its first sample: the fixture below fills the intro
    // region with a strongly positive value and the loop region with a strongly negative one, so
    // a buffer's very first sample unambiguously identifies which region it came from (channels=1
    // means `samples` here IS frame count, matching MIX_TrackMixCallback's own convention --
    // see ProcessFilterState's identical `samples == frames * channels` usage above).
    void SDLCALL BoundedLoopRawCallback(void* userdata, MIX_Track*, const SDL_AudioSpec*,
                                         float* pcm, int samples)
    {
        auto* ctx = static_cast<BoundedLoopRawCallbackCtx*>(userdata);
        if (samples <= 0) return;
        ctx->totalFrames.fetch_add(samples);

        const bool isIntroSample = pcm[0] > 0.5f;
        const bool isLoopSample  = pcm[0] < -0.5f;
        if (isLoopSample) ctx->sawLoopRegion.store(true);
        if (isIntroSample && ctx->sawLoopRegion.load())
            ctx->introSeenAfterLoopRegionStarted.store(true);
    }
}

TEST_F(SoundEffectInstanceTest, BoundedLoopRegionPlaysIntroOnceThenRepeatsOnlyTheLoopRegion)
{
    REQUIRE_DEVICE();
    constexpr int introFrames = 200; // strongly positive samples
    constexpr int loopFrames  = 100; // strongly negative samples
    std::vector<unsigned char> pcm((introFrames + loopFrames) * 2);
    int16_t* samples = reinterpret_cast<int16_t*>(pcm.data());
    for (int i = 0; i < introFrames; ++i) samples[i] = 30000;
    for (int i = 0; i < loopFrames; ++i) samples[introFrames + i] = -30000;

    SoundEffect effect(pcm, 0, static_cast<int>(pcm.size()), 44100, AudioChannels::Mono,
                        introFrames, loopFrames);
    SoundEffectInstance inst = effect.CreateInstance();
    inst.setIsLoopedProperty(true);
    inst.Play();

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);

    BoundedLoopRawCallbackCtx ctx;
    MIX_SetTrackRawCallback(track, BoundedLoopRawCallback, &ctx);

    // Long enough for the real mixing thread to wrap the tiny 100-frame loop region many times
    // over (100 frames is ~2.3ms at 44100Hz) -- proving genuine repeated looping, not a single
    // pass that then stalls or falls silent.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    inst.Stop(true);

    EXPECT_FALSE(ctx.introSeenAfterLoopRegionStarted.load())
        << "the pre-loop intro region was observed again after the loop region had already "
           "started playing -- it must play exactly once, then only the loop region repeats";
    EXPECT_GT(ctx.totalFrames.load(), static_cast<long long>(loopFrames) * 5)
        << "expected several real wraps of the loop region within the observation window";
}

// AUD-15-005: stress test creating/playing/destroying thousands of short-lived instances from a
// shared SoundEffect, checking for leaks or unbounded internal registry growth
// (SoundEffect::Impl::instances, the per-effect live-instance list used for the Dispose cascade,
// T-3G) -- not just "does it crash," but "does the bookkeeping actually stay bounded." Real games
// commonly create-play-discard many short SoundEffectInstances per frame (footsteps, impacts,
// UI blips) from a small set of shared SoundEffect assets, exactly this pattern.
//
// This test was originally written using only indirect signals (wall-clock timing over 5000
// iterations, plus "does one more instance still work afterwards"). Verified by deliberately
// breaking SoundEffect::UnregisterInstance() into a no-op and re-running: the test still PASSED
// (12ms, no crash) with the registry silently leaking 5000 stale pointers. Root cause: 5000 plain
// vector push_backs are trivially fast even while "leaking" (no super-linear blowup at this
// scale), and each loop iteration's SoundEffectInstance reuses the same stack slot, so a stale
// pointer left in the registry doesn't reliably fault when the registry is later walked -- unlike
// a heap-based use-after-free, this is not something ASan reliably catches either. Replaced with a
// direct introspection check (SoundEffectTestAccess::GetLiveInstanceCount) that asserts the
// registry size directly, both mid-stress (bounded, not accumulating without limit) and at zero
// once every instance has gone out of scope -- re-verified to genuinely fail against the same
// broken UnregisterInstance() probe before this fix was accepted.
TEST_F(SoundEffectInstanceTest, StressCreatePlayDisposeThousandsOfShortLivedInstancesFromSharedEffect)
{
    REQUIRE_DEVICE();

    ASSERT_EQ(SoundEffectTestAccess::GetLiveInstanceCount(*effect_), 0u)
        << "registry should start empty before the stress loop";

    constexpr int kIterations = 5000;

    for (int i = 0; i < kIterations; ++i)
    {
        SoundEffectInstance inst = instance();
        inst.Play();
        // Deliberately destroyed (via scope exit) without an explicit Stop()/Dispose() call --
        // the destructor's own Dispose() cascade must handle a still-"playing" instance cleanly,
        // exactly like a real game dropping a fire-and-forget sound's instance handle.

        // Each instance is registered on construction and must be unregistered again by the time
        // its scope exits -- the registry must never accumulate more than the single in-flight
        // instance from this iteration. A leaking UnregisterInstance() would show up here as a
        // monotonically growing count (1, 2, 3, ... 5000), not a bounded one.
        ASSERT_LE(SoundEffectTestAccess::GetLiveInstanceCount(*effect_), 1u)
            << "live-instance registry grew unboundedly at iteration " << i
            << " -- UnregisterInstance() is not removing disposed instances";
    }

    // After all 5000 instances have gone out of scope, the registry must be back to exactly zero.
    EXPECT_EQ(SoundEffectTestAccess::GetLiveInstanceCount(*effect_), 0u)
        << "live-instance registry did not shrink back to zero after all instances were destroyed";

    // The shared SoundEffect itself must still work correctly after all that churn -- proving
    // Impl::instances wasn't left corrupted (e.g. containing dangling pointers from improperly
    // unregistered instances) by the high-churn create/destroy cycle.
    SoundEffectInstance finalInstance = instance();
    finalInstance.Play();
    EXPECT_EQ(finalInstance.getStateProperty(), SoundState::Playing);
}
