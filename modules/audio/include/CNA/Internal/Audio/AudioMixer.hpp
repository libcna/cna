// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <cstdint>

namespace CNA::Internal::Audio
{
    /// Returns the shared SDL3_mixer device, creating it on first call.
    ///
    /// AUD-04-005: the requested spec is always the fixed reference S16 stereo 44100 Hz --
    /// deliberately never native-device or platform-specific. See NEXTaudio.md's "Mixer
    /// output-format policy" for the full rationale.
    ///
    /// AUDIO-002: thread-safe as of this fix -- first-use creation and DestroyMixer() below now
    /// share a single mutex, so concurrent first callers can no longer both race through
    /// MIX_Init()/MIX_CreateMixerDevice(), and a call concurrent with DestroyMixer() can no
    /// longer observe a half-destroyed pointer. On a create failure, throws std::runtime_error
    /// and leaves the mixer uncreated, so a later call (from any thread) retries from scratch --
    /// unchanged from this function's pre-existing single-threaded retry behavior.
    ///
    /// AUD-04-001 (2026-07-17 deep audit): the requested spec (S16 stereo 44100 Hz) and the
    /// actually negotiated device format (queried via MIX_GetMixerFormat right after creation)
    /// are logged to stderr once, at first creation -- previously completely unobservable, a real
    /// diagnostic gap when investigating a reported 44.1/48 kHz-family pitch/speed symptom.
    MIX_Mixer* GetMixer();

    /// AUD-04-004: test-only override for the spec `GetMixer()` requests on its next
    /// mixer-creating call. Has no effect on an already-created mixer -- callers must
    /// `DestroyMixer()` first (or call before any `GetMixer()` in the process) for the
    /// override to actually take effect, matching `GetMixer()`'s existing "recreates from
    /// scratch" semantics. Exists so device-negotiation-adjacent tests can force 22.05/44.1/
    /// 48/96 kHz and mono/stereo mixers without touching production code paths, which always
    /// request the fixed S16 stereo 44100 Hz default.
    void SetMixerSpecOverrideForTests(const SDL_AudioSpec& spec);

    /// AUD-04-004: clears a previous `SetMixerSpecOverrideForTests()` call, restoring the
    /// production default (S16 stereo 44100 Hz) for the next mixer-creating `GetMixer()` call.
    void ClearMixerSpecOverrideForTests();

    /// Destroys the shared SDL3_mixer device.
    ///
    /// P9-AUDIT-003/AUDIO-002: still no caller anywhere in this codebase today -- the MIX_Init()/
    /// MIX_Quit() refcount and the SDL audio device are otherwise only reclaimed by the OS on
    /// process exit. A future caller wiring real shutdown (e.g. into Game's dispose path) MUST
    /// first ensure no live SoundEffectInstance, DynamicSoundEffectInstance, Microphone, or
    /// AudioEngine/Cue/SoundBank/WaveBank still depends on SDL audio -- this function only
    /// serializes the mixer pointer itself against a concurrent GetMixer() call, it has no way to
    /// know about (or wait on) those higher-level objects' own lifetimes. A GetMixer() call after
    /// this one simply recreates the mixer from scratch, the same as the very first call ever
    /// made.
    ///
    /// AUD-04-008/009: every MIX_Track/MIX_Audio the destroyed mixer owned is freed as part of
    /// this call (confirmed against real SDL3_mixer source, MIX_DestroyMixer -> MIX_DestroyTrack),
    /// so any SoundEffectInstance/DynamicSoundEffectInstance that still holds one becomes a
    /// dangling pointer. This function bumps the counter GetMixerGeneration() returns so those
    /// instances can detect the invalidation instead of dereferencing freed memory -- see
    /// SoundEffectInstance::GetLiveTrackHandle().
    ///
    /// AUD-04-008/009: does NOT deinitialize the global SDL audio subsystem, even though the real
    /// MIX_DestroyMixer() call underneath this does call SDL_QuitSubSystem(SDL_INIT_AUDIO)
    /// internally -- GetMixer() pins an extra, permanently-held subsystem reference on first use
    /// specifically so that internal call can never bring the subsystem's own refcount to zero.
    /// Confirmed necessary via a real crash: without the pin, any independently-owned
    /// SDL_AudioStream CNA still holds (e.g. DynamicSoundEffectInstance's own audio stream, not
    /// owned by any MIX_Track) becomes unsafe to destroy the moment this function runs, since
    /// SDL_DestroyAudioStream's internal unbind logic depends on subsystem-global state.
    void DestroyMixer();

    /// AUD-04-008/009: monotonically increases by exactly one every time DestroyMixer() actually
    /// destroys a mixer (never on a call where no mixer existed, since nothing was invalidated).
    /// SoundEffectInstance captures this value when it creates a MIX_Track (Play()) and compares
    /// it before every later use, so a track orphaned by a DestroyMixer() call is detected instead
    /// of dereferenced. Not tied to GetMixer()'s creation -- only destruction invalidates
    /// previously-issued tracks.
    std::uint64_t GetMixerGeneration();
}
#endif
