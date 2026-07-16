// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

namespace CNA::Internal::Audio
{
    /// Returns the shared SDL3_mixer device, creating it on first call.
    ///
    /// AUDIO-002: thread-safe as of this fix -- first-use creation and DestroyMixer() below now
    /// share a single mutex, so concurrent first callers can no longer both race through
    /// MIX_Init()/MIX_CreateMixerDevice(), and a call concurrent with DestroyMixer() can no
    /// longer observe a half-destroyed pointer. On a create failure, throws std::runtime_error
    /// and leaves the mixer uncreated, so a later call (from any thread) retries from scratch --
    /// unchanged from this function's pre-existing single-threaded retry behavior.
    MIX_Mixer* GetMixer();

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
    void DestroyMixer();
}
#endif
