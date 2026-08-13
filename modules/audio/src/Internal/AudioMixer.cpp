// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "CNA/Audio/Platform/IAudioDevice.hpp"
#include "Platform/AudioDeviceFactory.hpp"

#ifdef SOUND_ENABLED
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

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
        std::unique_ptr<CNA::Audio::Platform::IAudioDevice> g_audioDevice;

        // AUD-04-004: test-only override for the spec requested below. Guarded by the same
        // mutex as g_mixer since it is read/written from the same lazy-init sequence.
        bool g_hasSpecOverride = false;
        SDL_AudioSpec g_specOverride{};

        // AUD-04-008/009: bumped by DestroyMixer() below, read (lock-free) by
        // SoundEffectInstance::GetLiveTrackHandle() from any thread that touches a track --
        // an atomic, not the mutex above, so instance code never needs to take g_mixerMutex
        // just to check whether its own track is still valid.
        std::atomic<std::uint64_t> g_mixerGeneration{0};
        std::atomic<std::uint64_t> g_generatedBytes{0};
        std::atomic<bool> g_outputError{false};

        // AUD-04-008/009: an extra, permanently-held SDL_INIT_AUDIO reference, acquired once
        // (guarded by g_mixerMutex, same as g_mixer) and never released. Before PLAT-95,
        // MIX_DestroyMixer() could drop the global audio refcount to zero; now the same hazard
        // occurs when the selected SDL3 IAudioDevice is closed. Legacy DynamicSoundEffectInstance
        // and Microphone streams do not yet own subsystem leases themselves, so retain this
        // compatibility pin until their PLAT-96/97 migrations. NULL never takes it.
        bool g_audioSubsystemPinned = false;

        class MixerBufferCallback final : public CNA::Audio::Platform::IAudioBufferCallback
        {
        public:
            void Configure(MIX_Mixer* mixer,
                           const CNA::Audio::Platform::AudioFormat format) noexcept
            {
                mixer_ = mixer;
                format_ = format;
            }

            void FillBuffer(const std::span<std::byte> output,
                            const std::size_t sampleCount) noexcept override
            {
                const std::size_t sampleBytes =
                    CNA::Audio::Platform::BytesPerSample(format_.sampleFormat);
                if (!mixer_ || sampleBytes == 0
                    || sampleCount > std::numeric_limits<std::size_t>::max() / sampleBytes
                    || output.size() != sampleCount * sampleBytes
                    || output.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                {
                    std::fill(output.begin(), output.end(), std::byte{});
                    g_outputError.store(true, std::memory_order_release);
                    return;
                }

                if (MIX_Generate(mixer_, output.data(), static_cast<int>(output.size())) < 0)
                {
                    std::fill(output.begin(), output.end(), std::byte{});
                    g_outputError.store(true, std::memory_order_release);
                    return;
                }
                g_generatedBytes.fetch_add(output.size(), std::memory_order_relaxed);
            }

        private:
            MIX_Mixer* mixer_ = nullptr;
            CNA::Audio::Platform::AudioFormat format_{};
        };

        std::shared_ptr<MixerBufferCallback> g_mixerCallback;

        [[nodiscard]] CNA::Audio::Platform::AudioSampleFormat FromSdlFormat(
            const SDL_AudioFormat format)
        {
            switch (format)
            {
                case SDL_AUDIO_S16:
                    return CNA::Audio::Platform::AudioSampleFormat::Signed16;
                case SDL_AUDIO_F32:
                    return CNA::Audio::Platform::AudioSampleFormat::Float32;
                default:
                    throw std::runtime_error("unsupported mixer sample format");
            }
        }

        [[nodiscard]] SDL_AudioFormat ToSdlFormat(
            const CNA::Audio::Platform::AudioSampleFormat format)
        {
            switch (format)
            {
                case CNA::Audio::Platform::AudioSampleFormat::Signed16:
                    return SDL_AUDIO_S16;
                case CNA::Audio::Platform::AudioSampleFormat::Float32:
                    return SDL_AUDIO_F32;
                case CNA::Audio::Platform::AudioSampleFormat::Unknown:
                    break;
            }
            throw std::runtime_error("unsupported negotiated mixer sample format");
        }

        [[nodiscard]] CNA::Audio::Platform::AudioFormat ToAudioFormat(
            const SDL_AudioSpec& spec)
        {
            if (spec.freq <= 0 || spec.channels <= 0
                || spec.channels > std::numeric_limits<std::uint8_t>::max())
            {
                throw std::runtime_error("invalid mixer format");
            }
            return CNA::Audio::Platform::AudioFormat{
                static_cast<std::uint32_t>(spec.freq),
                static_cast<std::uint8_t>(spec.channels),
                FromSdlFormat(spec.format)};
        }

        [[nodiscard]] SDL_AudioSpec ToSdlSpec(
            const CNA::Audio::Platform::AudioFormat& format)
        {
            if (format.sampleRate > static_cast<std::uint32_t>(
                    std::numeric_limits<int>::max()))
            {
                throw std::runtime_error("negotiated mixer rate is not representable");
            }
            SDL_AudioSpec spec{};
            spec.format = ToSdlFormat(format.sampleFormat);
            spec.channels = static_cast<int>(format.channels);
            spec.freq = static_cast<int>(format.sampleRate);
            return spec;
        }
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

        // AUD-04-008/009: acquire the temporary permanent subsystem pin (see
        // g_audioSubsystemPinned's own comment) before legacy independent SDL streams can exist.
        // NULL must never touch SDL audio merely because it was selected; PLAT-99 will provide
        // its device after PLAT-96 has retired those independent streams.
#if defined(CNA_AUDIO_PLATFORM_SDL3)
        // Acquire the permanent subsystem pin (see g_audioSubsystemPinned's own
        // comment) before anything else touches the audio subsystem below -- retried on every
        // call until it succeeds (e.g. a prior attempt with no audio hardware), same "retry from
        // scratch" philosophy as g_mixer's own lazy init.
        if (!g_audioSubsystemPinned)
        {
            g_audioSubsystemPinned = SDL_InitSubSystem(SDL_INIT_AUDIO);
        }
#endif

        if (!g_mixer)
        {
            if (!MIX_Init())
            {
                throw std::runtime_error(std::string("MIX_Init failed: ") + SDL_GetError());
            }

            SDL_AudioSpec requestedSpec{};
            if (g_hasSpecOverride)
            {
                requestedSpec = g_specOverride;
            }
            else
            {
                requestedSpec.format = SDL_AUDIO_S16;
                requestedSpec.channels = 2;
                requestedSpec.freq = 44100;
            }

            std::unique_ptr<CNA::Audio::Platform::IAudioDevice> audioDevice;
            std::shared_ptr<MixerBufferCallback> callback;
            MIX_Mixer* mixer = nullptr;
            try
            {
                const auto requested = ToAudioFormat(requestedSpec);
                audioDevice = CNA::Audio::Platform::CreateSelectedAudioDevice();
                callback = std::make_shared<MixerBufferCallback>();
                const auto negotiated = audioDevice->Open(requested, callback);
                const SDL_AudioSpec mixerSpec = ToSdlSpec(negotiated);

                mixer = MIX_CreateMixer(&mixerSpec);
                if (!mixer)
                {
                    throw std::runtime_error(
                        std::string("MIX_CreateMixer failed: ") + SDL_GetError());
                }

                std::cerr << "[AudioMixer] Requested format=0x" << std::hex
                          << requestedSpec.format << std::dec
                          << " channels=" << requestedSpec.channels
                          << " freq=" << requestedSpec.freq
                          << "; application format=0x" << std::hex << mixerSpec.format
                          << std::dec << " channels=" << mixerSpec.channels
                          << " freq=" << mixerSpec.freq << "\n";

                callback->Configure(mixer, negotiated);
                g_generatedBytes.store(0, std::memory_order_release);
                g_outputError.store(false, std::memory_order_release);
                audioDevice->Start();

                g_mixer = mixer;
                g_audioDevice = std::move(audioDevice);
                g_mixerCallback = std::move(callback);
            }
            catch (...)
            {
                if (audioDevice)
                {
                    audioDevice->Close();
                }
                if (mixer)
                {
                    MIX_DestroyMixer(mixer);
                }
                MIX_Quit();
                throw;
            }
        }
        return g_mixer;
    }

    void DestroyMixer()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        if (g_mixer)
        {
            if (g_audioDevice)
            {
                g_audioDevice->Stop();
                g_audioDevice->Close();
            }
            MIX_DestroyMixer(g_mixer);
            g_mixer = nullptr;
            g_mixerCallback.reset();
            g_audioDevice.reset();
            g_generatedBytes.store(0, std::memory_order_release);
            g_outputError.store(false, std::memory_order_release);
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

    std::uint64_t GetMixerGeneratedByteCount()
    {
        return g_generatedBytes.load(std::memory_order_acquire);
    }

    bool HasMixerOutputError()
    {
        return g_outputError.load(std::memory_order_acquire);
    }
}
#endif
