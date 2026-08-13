// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef SOUND_ENABLED
#include <cstdint>

struct MIX_Mixer;

namespace CNA::Internal::Audio
{
    struct MixerFormat;

    /// Returns the shared native mixer engine, creating it on first call.
    ///
    /// AUD-04-005: the requested spec is always the fixed reference S16 stereo 44100 Hz --
    /// deliberately never native-device or platform-specific. See NEXTaudio.md's "Mixer
    /// output-format policy" for the full rationale.
    ///
    /// PLAT-95: the mixer is memory-backed (`MIX_CreateMixer`); the selected `IAudioDevice` owns
    /// native acquisition and requests whole buffers, which `MIX_Generate` fills. The mixer keeps
    /// its track/decode/mixing loops, but no longer owns or submits to the playback device.
    ///
    /// AUDIO-002: first-use creation and `DestroyMixer` share one mutex. On any mixer/device-open
    /// failure this throws `std::runtime_error`, releases partial state, and a later call retries
    /// from scratch.
    MIX_Mixer* GetMixer();

    /// AUD-04-004: test-only override for the spec `GetMixer()` requests on its next
    /// mixer-creating call. Has no effect on an already-created mixer -- callers must
    /// `DestroyMixer()` first (or call before any `GetMixer()` in the process) for the
    /// override to actually take effect, matching `GetMixer()`'s existing "recreates from
    /// scratch" semantics. Exists so device-negotiation-adjacent tests can force 22.05/44.1/
    /// 48/96 kHz and mono/stereo mixers without touching production code paths, which always
    /// request the fixed S16 stereo 44100 Hz default.
    void SetMixerSpecOverrideForTests(const MixerFormat& format);

    /// AUD-04-004: clears a previous `SetMixerSpecOverrideForTests()` call, restoring the
    /// production default (S16 stereo 44100 Hz) for the next mixer-creating `GetMixer()` call.
    void ClearMixerSpecOverrideForTests();

    /// Destroys the shared mixer and selected playback device.
    ///
    /// P9-AUDIT-003/AUDIO-002: still no production caller anywhere in this codebase today; native
    /// mixer state and the selected audio device are otherwise only reclaimed by the OS on
    /// process exit. A future caller wiring real shutdown (e.g. into Game's dispose path) must
    /// first ensure no live SoundEffectInstance, DynamicSoundEffectInstance, Microphone, or
    /// AudioEngine/Cue/SoundBank/WaveBank is actively using audio. This function only
    /// serializes the mixer pointer itself against a concurrent GetMixer() call, it has no way to
    /// know about (or wait on) those higher-level objects' own lifetimes. A GetMixer() call after
    /// this one simply recreates the mixer from scratch, the same as the very first call ever
    /// made.
    ///
    /// AUD-04-008/009: every native track is freed as part of this call, so any live instance's
    /// borrowed track handle becomes stale. This function bumps the counter returned by
    /// GetMixerGeneration() before teardown so those instances detect invalidation instead of
    /// dereferencing freed memory -- see
    /// SoundEffectInstance::GetLiveTrackHandle().
    ///
    /// The selected playback device is stopped first, making its callback barrier complete before
    /// tracks and mixer memory are freed. Playback and recording implementations each balance
    /// their own subsystem ownership; no permanent compatibility reference remains.
    void DestroyMixer();

    /// AUD-04-008/009: monotonically increases by exactly one every time DestroyMixer() actually
    /// destroys a mixer (never on a call where no mixer existed, since nothing was invalidated).
    /// SoundEffectInstance captures this value when it creates a track in Play() and compares
    /// it before every later use, so a track orphaned by a DestroyMixer() call is detected instead
    /// of dereferenced. Not tied to GetMixer()'s creation -- only destruction invalidates
    /// previously-issued tracks.
    std::uint64_t GetMixerGeneration();

    /// Returns bytes successfully generated into selected-device callback buffers since creation.
    [[nodiscard]] std::uint64_t GetMixerGeneratedByteCount();

    /// Returns whether mixer generation has failed since the current mixer was created.
    [[nodiscard]] bool HasMixerOutputError();
}
#endif
