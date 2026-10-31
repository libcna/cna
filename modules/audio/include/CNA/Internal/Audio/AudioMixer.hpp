// SPDX-License-Identifier: MS-PL
#pragma once

// SDL3_mixer's own mixer handle, for the SDL3 audio platform only. SOUND_ENABLED means "a mixer
// exists", and under CNA_AUDIO_PLATFORM=ALSA that mixer is CNA's own (Backend/CnaMixer), which has
// no MIX_Mixer to hand out (X11-0151).
#if defined(SOUND_ENABLED) && defined(CNA_AUDIO_PLATFORM_SDL3)
#include <cstdint>

struct MIX_Mixer;

namespace CNA::Internal::Audio
{
    struct MixerFormat;

    /// Returns the shared native mixer engine, creating it on first call.
    ///
    /// AUD-04-005: the requested spec is always the fixed reference S16 stereo 44100 Hz --
    /// deliberately never native-device or platform-specific: output stays deterministic, the SDL
    /// device-open floor already sits at that spec (AUD-04-004), and a per-device format would widen
    /// the verification matrix for no identified benefit.
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
    /// P9-AUDIT-003/AUDIO-002/MUSEUM-AUDIO-002: first successful mixer creation registers this
    /// function with `std::atexit`, so the selected device is stopped and joined before earlier
    /// audio-related statics are destroyed. Internal tests may also call it earlier to exercise a
    /// controlled destroy/recreate cycle. Such an explicit early call must not race higher-level
    /// audio operations: this function serializes the mixer pointer against GetMixer(), but it
    /// cannot discover or wait on every SoundEffectInstance, DynamicSoundEffectInstance,
    /// Microphone, AudioEngine, Cue, SoundBank, or WaveBank using it. A later GetMixer() call
    /// recreates the mixer from scratch.
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
