// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/AudioMixer.hpp"

#ifdef SOUND_ENABLED
#include <atomic>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Audio
{
    namespace
    {
        // AUDIO-002: g_mixer's lazy-init check-then-create sequence, and DestroyMixer()'s
        // check-then-destroy sequence, now share this single mutex -- previously neither was
        // synchronized at all (only an assumed, unenforced main-thread-only contract), so two
        // concurrent first GetMixer() callers could both observe g_mixer == nullptr and both race
        // through MIX_Init()/MIX_CreateMixerDevice(), and a GetMixer() call concurrent with
        // DestroyMixer() could read a half-destroyed pointer. Holding the lock for the entire
        // function body below (not just around the null check) is what makes this safe -- there
        // is no unlocked window between "check g_mixer" and "create/destroy/return it" for a
        // second thread to slip into.
        std::mutex g_mixerMutex;
        MIX_Mixer* g_mixer = nullptr;

        // AUD-04-004: test-only override for the spec requested below. Guarded by the same
        // mutex as g_mixer since it is read/written from the same lazy-init sequence.
        bool g_hasSpecOverride = false;
        SDL_AudioSpec g_specOverride{};

        // AUD-04-008/009: bumped by DestroyMixer() below, read (lock-free) by
        // SoundEffectInstance::GetLiveTrackHandle() from any thread that touches a track --
        // an atomic, not the mutex above, so instance code never needs to take g_mixerMutex
        // just to check whether its own track is still valid.
        std::atomic<std::uint64_t> g_mixerGeneration{0};

        // AUD-04-008/009: an extra, permanently-held SDL_INIT_AUDIO reference, acquired once
        // (guarded by g_mixerMutex, same as g_mixer) and never released. Without this,
        // MIX_DestroyMixer() below can bring the *global* SDL audio subsystem's own refcount to
        // zero via its own internal SDL_QuitSubSystem(SDL_INIT_AUDIO) call -- confirmed via a
        // real crash (SDL_WasInit(SDL_INIT_AUDIO) observed 0 at the crash site; ASan-symbolized
        // SEGV inside SDL_UnbindAudioStream_REAL) when DynamicSoundEffectInstance::DestroyStream()
        // later called SDL_DestroyAudioStream() on its own independently-owned, subsystem-level
        // (not mixer/track-owned) audio stream. DestroyMixer() has no registry of every
        // SDL_AudioStream CNA might still hold elsewhere (dynamic instances, Microphone capture
        // streams, ...), so destroying its own mixer must never be what fully deinitializes the
        // subsystem those depend on.
        bool g_audioSubsystemPinned = false;
    }

    void SetMixerSpecOverrideForTests(const SDL_AudioSpec& spec)
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        g_hasSpecOverride = true;
        g_specOverride = spec;
    }

    void ClearMixerSpecOverrideForTests()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        g_hasSpecOverride = false;
    }

    MIX_Mixer* GetMixer()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);

        // AUD-04-008/009: acquire the permanent subsystem pin (see g_audioSubsystemPinned's own
        // comment) before anything else touches the audio subsystem below -- retried on every
        // call until it succeeds (e.g. a prior attempt with no audio hardware), same "retry from
        // scratch" philosophy as g_mixer's own lazy init.
        if (!g_audioSubsystemPinned)
        {
            g_audioSubsystemPinned = SDL_InitSubSystem(SDL_INIT_AUDIO);
        }

        if (!g_mixer)
        {
            if (!MIX_Init())
            {
                throw std::runtime_error(std::string("MIX_Init failed: ") + SDL_GetError());
            }

            SDL_AudioSpec spec{};
            if (g_hasSpecOverride)
            {
                spec = g_specOverride;
            }
            else
            {
                spec.format = SDL_AUDIO_S16;
                spec.channels = 2;
                spec.freq = 44100;
            }

            g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
            if (!g_mixer)
            {
                // IN-11: MIX_Init()/MIX_Quit() are reference-counted; without this, every
                // failed retry (e.g. no audio hardware present) leaks another MIX_Init()
                // refcount that's never balanced by a matching MIX_Quit().
                MIX_Quit();
                throw std::runtime_error(std::string("MIX_CreateMixerDevice failed: ") + SDL_GetError());
            }

            // AUD-04-001 (2026-07-17 deep audit, A-06): log the requested vs. actually negotiated
            // mixer format once, at first creation -- CNA requests S16 stereo 44100 Hz, but SDL
            // may internally convert to whatever the physical device actually uses, and this was
            // previously completely unobservable (a real diagnostic gap when investigating a
            // 44.1/48 kHz-family pitch report). Not fatal if the query itself fails (best-effort
            // diagnostic only, matching this codebase's established std::cerr convention).
            SDL_AudioSpec actual{};
            if (MIX_GetMixerFormat(g_mixer, &actual))
            {
                std::cerr << "[AudioMixer] Requested format=0x" << std::hex << spec.format << std::dec
                          << " channels=" << spec.channels << " freq=" << spec.freq
                          << "; negotiated format=0x"
                          << std::hex << actual.format << std::dec
                          << " channels=" << actual.channels << " freq=" << actual.freq << "\n";
            }
            else
            {
                std::cerr << "[AudioMixer] MIX_GetMixerFormat failed after mixer creation: "
                          << SDL_GetError() << "\n";
            }
        }
        return g_mixer;
    }

    void DestroyMixer()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        if (g_mixer)
        {
            MIX_DestroyMixer(g_mixer);
            g_mixer = nullptr;
            MIX_Quit();
            // AUD-04-008/009: every track this mixer owned was just freed by MIX_DestroyMixer
            // above -- bump the generation so any instance still holding one of those tracks
            // detects the invalidation on its next access instead of dereferencing freed memory.
            g_mixerGeneration.fetch_add(1, std::memory_order_release);
        }
    }

    std::uint64_t GetMixerGeneration()
    {
        return g_mixerGeneration.load(std::memory_order_acquire);
    }
}
#endif
