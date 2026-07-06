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
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "SoundEffectInstanceTestAccess.hpp"

#include <SDL3_mixer/SDL_mixer.h>

using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::AudioEmitter;
using Microsoft::Xna::Framework::Audio::AudioListener;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Audio::SoundEffectInstance;
using Microsoft::Xna::Framework::Audio::SoundEffectInstanceTestAccess;
using Microsoft::Xna::Framework::Audio::SoundState;
using Microsoft::Xna::Framework::Vector3;

// All SoundEffectInstance tests need a SoundEffect, whose construction opens the
// (dummy) audio device. The fixture skips every test if no device is available.
class SoundEffectInstanceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
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

TEST_F(SoundEffectInstanceTest, Apply3DArrayOverload)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    AudioListener listeners[2];
    AudioEmitter emitter;
    EXPECT_NO_THROW(inst.Apply3D(listeners, 1, emitter));
    EXPECT_THROW(inst.Apply3D(listeners, 2, emitter), System::NotSupportedException);
    EXPECT_THROW(inst.Apply3D(nullptr, 1, emitter), System::ArgumentNullException);
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
    inst.Play();

    AudioListener listener;
    listener.setForwardProperty(Vector3(1.0f, 0.0f, 0.0f)); // rotated 90 degrees to face +X
    listener.setUpProperty(Vector3::Up);
    AudioEmitter emitter;
    emitter.setPositionProperty({0.0f, 0.0f, 20.0f}); // beyond DistanceScale
    EXPECT_NO_THROW(inst.Apply3D(listener, emitter));

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    // distance == 20, DistanceScale == 10 -> normalizedDistance == 2 -> atten == 1/2, same
    // formula/value regardless of listener orientation (P9-3D-003's inverse law only depends on
    // Euclidean distance, never on Forward/Up).
    EXPECT_NEAR(MIX_GetTrackGain(track), 0.5f, 1e-5f);
}

// P9-3D-003: matches FAudio's F3DAudio.c ComputeDistanceAttenuation's no-custom-curve branch --
// full volume (no attenuation at all) for any distance strictly within DistanceScale
// (CurveDistanceScaler). Verified via the real MIX_Track gain (MIX_GetTrackGain has a getter,
// unlike stereo pan -- see CP-19/P9-3D-001's note on why that one can't be verified this way).
TEST_F(SoundEffectInstanceTest, Apply3DAppliesFullVolumeWithinDistanceScale)
{
    REQUIRE_DEVICE();
    DistanceScaleGuard guard;
    SoundEffect::setDistanceScaleProperty(10.0f);

    SoundEffectInstance inst = instance();
    inst.Play();

    AudioListener listener; // default position: origin
    AudioEmitter emitter;
    emitter.setPositionProperty({5.0f, 0.0f, 0.0f}); // half of DistanceScale -- still "close"
    inst.Apply3D(listener, emitter);

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
    inst.Play();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f}); // exactly == DistanceScale
    inst.Apply3D(listener, emitter);

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
    inst.Play();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({20.0f, 0.0f, 0.0f}); // 2x DistanceScale
    inst.Apply3D(listener, emitter);

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
    inst.Play();

    AudioListener listener; // default position origin, default velocity zero
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({171.75f, 0.0f, 0.0f}); // moving away from the listener
    inst.Apply3D(listener, emitter);

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
    inst.Play();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({-171.75f, 0.0f, 0.0f}); // moving toward the listener
    inst.Apply3D(listener, emitter);

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
    inst.Play();

    AudioListener listener;
    listener.setVelocityProperty({171.75f, 0.0f, 0.0f}); // moving toward the emitter
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    inst.Apply3D(listener, emitter);

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
    inst.Play();

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    emitter.setVelocityProperty({171.75f, 0.0f, 0.0f}); // would otherwise pitch down
    inst.Apply3D(listener, emitter);

    MIX_Track* track = SoundEffectInstanceTestAccess::GetTrack(inst);
    ASSERT_NE(track, nullptr);
    EXPECT_NEAR(MIX_GetTrackFrequencyRatio(track), 1.0f, 1e-5f);
}

// CP-20: matches FNA's `is3D` latch (SoundEffectInstance.cs) -- once Apply3D has run, it (not
// setPanProperty) should keep governing the real track output. SDL3_mixer has no stereo-pan
// getter to directly observe the output matrix (same limitation as CP-3/T-4B's Apply3D
// coverage), so this verifies the is3D_ state machine that setPanProperty() actually branches
// on, via SoundEffectInstanceTestAccess.
TEST_F(SoundEffectInstanceTest, SetPanAfterApply3DDoesNotClearIs3DLatch)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    EXPECT_FALSE(SoundEffectInstanceTestAccess::Is3D(inst));

    AudioListener listener;
    AudioEmitter emitter;
    emitter.setPositionProperty({10.0f, 0.0f, 0.0f});
    inst.Apply3D(listener, emitter);
    EXPECT_TRUE(SoundEffectInstanceTestAccess::Is3D(inst));

    // Setting Pan afterward must still update the property (matches FNA) but must not clear the
    // latch -- Apply3D's own pan approximation should keep governing the real output until
    // Apply3D runs again, not this call.
    inst.setPanProperty(0.9f);
    EXPECT_FLOAT_EQ(inst.getPanProperty(), 0.9f);
    EXPECT_TRUE(SoundEffectInstanceTestAccess::Is3D(inst));
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

// Matches FNA's `handle == IntPtr.Zero` guard: applying a filter before the track exists must
// not create filter state that later processes samples.
TEST_F(SoundEffectInstanceTest, ApplyLowPassFilterBeforePlayIsNoOp)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    SoundEffectInstanceTestAccess::ApplyLowPassFilter(inst, 0.5f);

    float pcm[2] = {2.0f, 2.0f};
    SoundEffectInstanceTestAccess::ProcessFilterSamples(inst, pcm, 2, 2);
    EXPECT_FLOAT_EQ(pcm[0], 2.0f);
    EXPECT_FLOAT_EQ(pcm[1], 2.0f);
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

// "Needs verification" (CHECKLIST.md/plan_audio.md T-4C): filter coefficient locking follows
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
// FilterState. Intended to be run under ThreadSanitizer (see plan_audio.md's P9-CATEGORY-011
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

// P10-3D-003: emitter directly above the listener. Previously entirely untested (plan_audio.md's
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
    EXPECT_GT(frequency, 0.0f);
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

// Matches every other INTERNAL_apply*Filter's `handle == IntPtr.Zero` guard -- no track means no
// filter state gets created at all.
TEST_F(SoundEffectInstanceTest, ApplyXactTrackFilterBeforePlayIsNoOp)
{
    REQUIRE_DEVICE();
    SoundEffectInstance inst = instance();
    SoundEffectInstanceTestAccess::ApplyXactTrackFilter(inst, 2, 8000.0f, 6);

    int kind = -1; float frequency = -1.0f, oneOverQ = -1.0f;
    SoundEffectInstanceTestAccess::GetFilterState(inst, kind, frequency, oneOverQ);
    EXPECT_EQ(kind, 0); // FilterState::Kind::None
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
