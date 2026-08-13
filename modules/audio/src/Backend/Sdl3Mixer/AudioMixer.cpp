// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/AudioMixer.hpp"
#include "CNA/Internal/Audio/MixerEngine.hpp"
#include "CNA/Audio/Platform/IAudioDevice.hpp"
#include "Platform/AudioDeviceFactory.hpp"

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

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
        MixerFormat g_specOverride{};

        // AUD-04-008/009: bumped by DestroyMixer() below, read (lock-free) by
        // SoundEffectInstance::GetLiveTrackHandle() from any thread that touches a track --
        // an atomic, not the mutex above, so instance code never needs to take g_mixerMutex
        // just to check whether its own track is still valid.
        std::atomic<std::uint64_t> g_mixerGeneration{0};
        std::atomic<std::uint64_t> g_generatedBytes{0};
        std::atomic<bool> g_outputError{false};

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

        [[nodiscard]] CNA::Audio::Platform::AudioSampleFormat ToAudioSampleFormat(
            const MixerSampleFormat format)
        {
            switch (format)
            {
                case MixerSampleFormat::Signed16:
                    return CNA::Audio::Platform::AudioSampleFormat::Signed16;
                case MixerSampleFormat::Float32:
                    return CNA::Audio::Platform::AudioSampleFormat::Float32;
            }
            throw std::runtime_error("unsupported mixer sample format");
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
            const MixerFormat& format)
        {
            if (format.sampleRate <= 0 || format.channels <= 0
                || format.channels > std::numeric_limits<std::uint8_t>::max())
            {
                throw std::runtime_error("invalid mixer format");
            }
            return CNA::Audio::Platform::AudioFormat{
                static_cast<std::uint32_t>(format.sampleRate),
                static_cast<std::uint8_t>(format.channels),
                ToAudioSampleFormat(format.sampleFormat)};
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

    void SetMixerSpecOverrideForTests(const MixerFormat& format)
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        g_hasSpecOverride = true;
        g_specOverride = format;
    }

    void ClearMixerSpecOverrideForTests()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);
        g_hasSpecOverride = false;
    }

    MIX_Mixer* GetMixer()
    {
        std::lock_guard<std::mutex> lock(g_mixerMutex);

        if (!g_mixer)
        {
            if (!MIX_Init())
            {
                throw std::runtime_error(std::string("MIX_Init failed: ") + SDL_GetError());
            }

            const MixerFormat requestedFormat = g_hasSpecOverride
                ? g_specOverride
                : MixerFormat{44100, 2, MixerSampleFormat::Signed16};

            std::unique_ptr<CNA::Audio::Platform::IAudioDevice> audioDevice;
            std::shared_ptr<MixerBufferCallback> callback;
            MIX_Mixer* mixer = nullptr;
            try
            {
                const auto requested = ToAudioFormat(requestedFormat);
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
                          << static_cast<int>(requestedFormat.sampleFormat) << std::dec
                          << " channels=" << requestedFormat.channels
                          << " freq=" << requestedFormat.sampleRate
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
                    BeginMixerEngineShutdown();
                    MIX_DestroyMixer(mixer);
                    EndMixerEngineShutdown();
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
            // Invalidate every borrowed track and facade-owned native audio pointer before the
            // selected device or mixer starts tearing them down. MIX_Quit below destroys all
            // remaining MIX_Audio objects; PLAT-96 reloads them lazily from facade-owned source
            // data if their SoundEffect survives this generation.
            g_mixerGeneration.fetch_add(1, std::memory_order_acq_rel);
            if (g_audioDevice)
            {
                g_audioDevice->Stop();
                g_audioDevice->Close();
            }
            BeginMixerEngineShutdown();
            MIX_DestroyMixer(g_mixer);
            EndMixerEngineShutdown();
            g_mixer = nullptr;
            g_mixerCallback.reset();
            g_audioDevice.reset();
            g_generatedBytes.store(0, std::memory_order_release);
            g_outputError.store(false, std::memory_order_release);
            MIX_Quit();
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
