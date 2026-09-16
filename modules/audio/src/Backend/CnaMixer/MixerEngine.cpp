// SPDX-License-Identifier: MS-PL
//
// MixerEngine.hpp over CNA's own mixer (Backend/CnaMixer/CnaMixer.hpp), for the audio platforms
// that have no SDL3_mixer (plans/plan_x11.md X11-0151). The SDL3_mixer implementation of the same
// facade is Backend/Sdl3Mixer/MixerEngine.cpp; exactly one of the two is compiled.
//
// One process-wide mixer, created with the selected output device the first time anything needs
// it. Every facade call, and the device's own callback, holds one recursive mutex -- recursive
// because the XNA layer holds MixerLock while it calls back into the facade, as SDL3_mixer's lock
// allows. Callbacks the mixer makes run under that lock, on the device's thread or, for an
// explicit Stop, on the caller's.

#include "CNA/Internal/Audio/MixerEngine.hpp"
#include "CNA/Internal/PathUtf8.hpp"

#include <optional>

#include "Backend/CnaMixer/CnaMixer.hpp"
#include "Platform/AudioDeviceFactory.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Audio
{
    namespace
    {
        namespace Platform = CNA::Audio::Platform;

        // Frames the device callback mixes at a time; a larger request is done in pieces.
        constexpr int kChunkFrames = 2048;

        class DeviceCallback final : public Platform::IAudioBufferCallback
        {
        public:
            void FillBuffer(std::span<std::byte> output, std::size_t sampleCount) noexcept override;
        };

        struct Engine
        {
            std::recursive_mutex mutex;
            std::unique_ptr<CnaMixer> mixer;
            std::unique_ptr<Platform::IAudioDevice> device;
            Platform::AudioFormat format{};
            std::vector<float> scratch;
            std::string error;
            bool exitHandlerInstalled = false;
        };

        // Immortal, like the SDL3_mixer facade's lifetime holders: the device's thread can still be
        // running while other translation units' statics are destroyed.
        Engine& GetEngine()
        {
            static Engine* engine = new Engine();
            return *engine;
        }

        void SetError(Engine& engine, std::string message)
        {
            std::lock_guard lock(engine.mutex);
            engine.error = std::move(message);
        }

        void CloseDeviceAtExit()
        {
            // Without the lock: Close() waits for a callback that may itself be waiting for it.
            Engine& engine = GetEngine();
            Platform::IAudioDevice* device = nullptr;
            {
                std::lock_guard lock(engine.mutex);
                device = engine.device.get();
            }
            if (device != nullptr)
            {
                // After this no callback runs, so nothing the mixer calls back into can be touched
                // while the rest of the process's statics are torn down.
                device->Close();
            }
        }

        void DeviceCallback::FillBuffer(const std::span<std::byte> output,
                                        const std::size_t sampleCount) noexcept
        {
            Engine& engine = GetEngine();
            std::lock_guard lock(engine.mutex);
            const Platform::AudioFormat& format = engine.format;
            if (!engine.mixer || format.channels == 0)
            {
                std::fill(output.begin(), output.end(), std::byte{});
                return;
            }
            const std::size_t channels = format.channels;
            const std::size_t frames = sampleCount / channels;
            const bool asFloat = format.sampleFormat == Platform::AudioSampleFormat::Float32;
            std::byte* destination = output.data();

            for (std::size_t done = 0; done < frames;)
            {
                const int chunk = static_cast<int>(std::min<std::size_t>(kChunkFrames, frames - done));
                float* mixed = engine.scratch.data();
                engine.mixer->Render(mixed, chunk);
                for (int frame = 0; frame < chunk; ++frame)
                {
                    const float left = mixed[frame * 2];
                    const float right = mixed[frame * 2 + 1];
                    for (std::size_t channel = 0; channel < channels; ++channel)
                    {
                        // Stereo into stereo as it is; a mono device gets the average; any further
                        // channels of a surround device stay silent.
                        const float value = channels == 1 ? (left + right) * 0.5f
                                            : channel == 0 ? left
                                            : channel == 1 ? right
                                                           : 0.0f;
                        if (asFloat)
                        {
                            std::memcpy(destination, &value, sizeof(float));
                            destination += sizeof(float);
                        }
                        else
                        {
                            const float clamped = std::clamp(value, -1.0f, 1.0f);
                            const auto sample = static_cast<std::int16_t>(std::lrintf(clamped * 32767.0f));
                            std::memcpy(destination, &sample, sizeof(std::int16_t));
                            destination += sizeof(std::int16_t);
                        }
                    }
                }
                done += static_cast<std::size_t>(chunk);
            }
        }

        CnaMixer* Mixer() noexcept
        {
            return GetEngine().mixer.get();
        }
    }

    void EnsureMixer()
    {
        Engine& engine = GetEngine();
        std::lock_guard lock(engine.mutex);
        if (engine.mixer)
        {
            return;
        }

        std::unique_ptr<Platform::IAudioDevice> device;
        Platform::AudioFormat format{};
        try
        {
            device = Platform::CreateSelectedAudioDevice();
            // CNA's mixer works in float, so float is asked for; the device may answer otherwise.
            format = device->Open({44100, 2, Platform::AudioSampleFormat::Float32},
                                  std::make_shared<DeviceCallback>());
        }
        catch (const std::exception& exception)
        {
            engine.error = exception.what();
            throw std::runtime_error(engine.error);
        }

        engine.format = format;
        engine.scratch.assign(static_cast<std::size_t>(kChunkFrames) * 2, 0.0f);
        engine.mixer = std::make_unique<CnaMixer>(static_cast<int>(format.sampleRate));
        engine.device = std::move(device);
        try
        {
            // The device's first callback waits for this lock, so the mixer is complete first.
            engine.device->Start();
        }
        catch (const std::exception& exception)
        {
            engine.error = exception.what();
            engine.device->Close();
            engine.device.reset();
            engine.mixer.reset();
            throw std::runtime_error(engine.error);
        }
        if (!engine.exitHandlerInstalled)
        {
            // Registered after every static constructed before the first sound, so it runs before
            // their destructors: the device is quiet by the time they go.
            engine.exitHandlerInstalled = std::atexit(CloseDeviceAtExit) == 0;
        }
    }

    bool TryEnsureMixer() noexcept
    {
        try
        {
            EnsureMixer();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::string GetMixerError()
    {
        Engine& engine = GetEngine();
        std::lock_guard lock(engine.mutex);
        return engine.error.empty() ? std::string("unknown mixer error") : engine.error;
    }

    std::uint64_t GetMixerEngineGeneration()
    {
        // This mixer is never torn down and rebuilt, so every track belongs to generation 1.
        return 1;
    }

    void BeginMixerEngineShutdown() noexcept
    {
        Engine& engine = GetEngine();
        std::lock_guard lock(engine.mutex);
        if (engine.mixer)
        {
            engine.mixer->DrainDeferred();
        }
    }

    void EndMixerEngineShutdown() noexcept {}

    MixerLock::MixerLock()
    {
        EnsureMixer();
        auto& mutex = GetEngine().mutex;
        mutex.lock();
        mixer_ = &mutex;
    }

    MixerLock::~MixerLock()
    {
        if (mixer_ != nullptr)
        {
            static_cast<std::recursive_mutex*>(mixer_)->unlock();
        }
    }

    namespace
    {
        MixerAudioPtr Finish(std::shared_ptr<MixerAudioData> data)
        {
            auto audio = std::make_shared<MixerAudio>();
            audio->format = {data->sampleRate, data->channels,
                             data->encoding == MixerAudioData::Encoding::Pcm16
                                 ? MixerSampleFormat::Signed16
                                 : MixerSampleFormat::Float32};
            audio->durationFrames = data->frames;
            audio->data = std::move(data);
            return audio;
        }

        MixerAudioPtr Decode(const std::span<const std::byte> bytes, const std::string& origin,
                             const bool predecode)
        {
            std::string error;
            std::shared_ptr<MixerAudioData> data = DecodeMixerAudio(bytes, origin, predecode, error);
            if (!data)
            {
                SetError(GetEngine(), error);
                return {};
            }
            return Finish(std::move(data));
        }
    }

    MixerAudioPtr LoadMixerAudioFile(const std::string& path, const bool predecode)
    {
        // As the SDL3_mixer facade does: loading needs the mixer, so a machine without an output
        // device finds out here -- which the XNA layer turns into NoAudioHardwareException.
        EnsureMixer();
        // The mixer interface takes UTF-8, as SDL3_mixer does on the other backend; the narrow
        // ifstream overload would read it as ANSI code page bytes here.
        const std::optional<std::filesystem::path> native = CNA::Internal::TryPathFromUtf8(path);
        if (!native) { return {}; }
        std::ifstream file(*native, std::ios::binary);
        if (!file)
        {
            SetError(GetEngine(), "'" + path + "' could not be opened");
            return {};
        }
        const std::vector<char> contents((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
        return Decode(std::as_bytes(std::span<const char>(contents.data(), contents.size())), path,
                      predecode);
    }

    MixerAudioPtr LoadMixerAudioMemory(const std::span<const std::byte> encodedData)
    {
        if (encodedData.empty())
        {
            return {};
        }
        EnsureMixer();
        return Decode(encodedData, "audio in memory", true);
    }

    MixerAudioPtr LoadMixerRawAudio(const std::span<const std::byte> pcm, const MixerFormat& format)
    {
        EnsureMixer();
        std::string error;
        std::shared_ptr<MixerAudioData> data = WrapMixerRawAudio(pcm, format, error);
        if (!data)
        {
            SetError(GetEngine(), error);
            return {};
        }
        MixerAudioPtr audio = Finish(std::move(data));
        // Raw data reports the representation it was given.
        audio->format.sampleFormat = format.sampleFormat;
        return audio;
    }

    MixerFormat GetMixerAudioFormat(const MixerAudio* audio) noexcept
    {
        return audio != nullptr ? audio->format : MixerFormat{};
    }

    std::int64_t GetMixerAudioDuration(const MixerAudio* audio) noexcept
    {
        return audio != nullptr ? audio->durationFrames : -1;
    }

    float GetMixerMasterGain()
    {
        EnsureMixer();
        std::lock_guard lock(GetEngine().mutex);
        return Mixer()->GetMasterGain();
    }

    void SetMixerMasterGain(const float gain)
    {
        EnsureMixer();
        std::lock_guard lock(GetEngine().mutex);
        Mixer()->SetMasterGain(gain);
    }

    int GetMixerSampleRate()
    {
        EnsureMixer();
        std::lock_guard lock(GetEngine().mutex);
        return Mixer()->GetSampleRate();
    }

    bool SetMixerPostMixCallback(const MixerPostMixCallback callback, void* userdata)
    {
        EnsureMixer();
        // Under the lock the mix is not running, so the old callback has returned: the barrier
        // the facade promises.
        std::lock_guard lock(GetEngine().mutex);
        Mixer()->SetPostMixCallback(callback, userdata);
        return true;
    }

    MixerTrack* CreateMixerTrack()
    {
        EnsureMixer();
        std::lock_guard lock(GetEngine().mutex);
        // The safe point for tracks destroyed from their own stopped callback, as in the
        // SDL3_mixer facade.
        Mixer()->DrainDeferred();
        return Mixer()->CreateTrack();
    }

    void DestroyMixerTrack(MixerTrack* track) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        if (CnaMixer* mixer = Mixer())
        {
            mixer->DestroyTrack(track);
        }
    }

    bool SetMixerTrackAudio(MixerTrack* track, MixerAudio* audio) noexcept
    {
        if (track == nullptr || audio == nullptr)
        {
            return false;
        }
        std::lock_guard lock(GetEngine().mutex);
        CnaMixer* mixer = Mixer();
        return mixer != nullptr && mixer->BindAudio(track, audio);
    }

    bool SetMixerTrackStream(MixerTrack* track, MixerStream* stream) noexcept
    {
        if (track == nullptr)
        {
            return false;
        }
        std::lock_guard lock(GetEngine().mutex);
        CnaMixer* mixer = Mixer();
        if (mixer == nullptr)
        {
            return false;
        }
        mixer->BindStream(track, stream);
        return true;
    }

    void SetMixerTrackGain(MixerTrack* track, const float gain) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        track->gain = gain < 0.0f ? 0.0f : gain;
    }

    void SetMixerTrackStereoUnity(MixerTrack*) noexcept
    {
        // Every track is mixed as stereo at unity already: mono doubled into both channels,
        // stereo as it is. The XNA layer's own pan owns the stereo image.
    }

    void SetMixerTrackFrequencyRatio(MixerTrack* track, const float ratio) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        track->ratio = ratio < 0.01f ? 0.01f : (ratio > 100.0f ? 100.0f : ratio);
    }

    void SetMixerTrackLoops(MixerTrack* track, const int loops) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        track->loopsRemaining = loops < -1 ? -1 : loops;
    }

    void SetMixerTrackMixCallback(MixerTrack* track, const MixerTrackMixCallback callback,
                                  void* userdata) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        track->mixCallback = callback;
        track->mixUserdata = callback != nullptr ? userdata : nullptr;
    }

    void SetMixerTrackStoppedCallback(MixerTrack* track, const MixerTrackStoppedCallback callback,
                                      void* userdata) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        track->stoppedCallback = callback;
        track->stoppedUserdata = callback != nullptr ? userdata : nullptr;
    }

    bool PlayMixerTrack(MixerTrack* track, const MixerPlayOptions& options) noexcept
    {
        if (track == nullptr)
        {
            return false;
        }
        std::lock_guard lock(GetEngine().mutex);
        CnaMixer* mixer = Mixer();
        return mixer != nullptr && mixer->Play(track, options);
    }

    void StopMixerTrack(MixerTrack* track) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        if (CnaMixer* mixer = Mixer())
        {
            mixer->Stop(track);
        }
    }

    void PauseMixerTrack(MixerTrack* track) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        if (CnaMixer* mixer = Mixer())
        {
            mixer->Pause(track);
        }
    }

    void ResumeMixerTrack(MixerTrack* track) noexcept
    {
        if (track == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        if (CnaMixer* mixer = Mixer())
        {
            mixer->Resume(track);
        }
    }

    bool IsMixerTrackPaused(const MixerTrack* track) noexcept
    {
        if (track == nullptr)
        {
            return false;
        }
        std::lock_guard lock(GetEngine().mutex);
        return track->state == MixerTrack::State::Paused;
    }

    bool IsMixerTrackPlaying(const MixerTrack* track) noexcept
    {
        if (track == nullptr)
        {
            return false;
        }
        std::lock_guard lock(GetEngine().mutex);
        return track->state == MixerTrack::State::Playing;
    }

    MixerStream* CreateMixerStream(const MixerFormat& sourceFormat) noexcept
    {
        if (sourceFormat.sampleRate <= 0 || sourceFormat.channels <= 0 || sourceFormat.channels > 8)
        {
            SetError(GetEngine(), "a queued stream needs a positive rate and 1 to 8 channels");
            return nullptr;
        }
        auto* stream = new (std::nothrow) MixerStream{};
        if (stream != nullptr)
        {
            stream->format = sourceFormat;
        }
        return stream;
    }

    MixerStream* CreateMixerPlaybackStream(const MixerFormat& sourceFormat) noexcept
    {
        MixerStream* stream = CreateMixerStream(sourceFormat);
        if (stream == nullptr)
        {
            return nullptr;
        }
        try
        {
            MixerTrack* track = CreateMixerTrack();
            std::lock_guard lock(GetEngine().mutex);
            stream->playbackTrack = track;
            Mixer()->BindStream(track, stream);
            MixerPlayOptions options;
            options.haltWhenExhausted = false;
            if (!Mixer()->Play(track, options))
            {
                DestroyMixerStream(stream);
                return nullptr;
            }
            Mixer()->Pause(track);
            return stream;
        }
        catch (const std::exception& exception)
        {
            SetError(GetEngine(), exception.what());
            DestroyMixerStream(stream);
            return nullptr;
        }
    }

    void DestroyMixerStream(MixerStream* stream) noexcept
    {
        if (stream == nullptr)
        {
            return;
        }
        {
            std::lock_guard lock(GetEngine().mutex);
            if (CnaMixer* mixer = Mixer())
            {
                if (stream->playbackTrack != nullptr)
                {
                    mixer->Stop(stream->playbackTrack);
                    mixer->DestroyTrack(stream->playbackTrack);
                    stream->playbackTrack = nullptr;
                }
                mixer->DetachStream(stream);
            }
        }
        delete stream;
    }

    void ClearMixerStream(MixerStream* stream) noexcept
    {
        if (stream == nullptr)
        {
            return;
        }
        std::lock_guard lock(GetEngine().mutex);
        stream->queue.clear();
        stream->readOffset = 0;
    }

    void SetMixerStreamGain(MixerStream* stream, const float gain) noexcept
    {
        if (stream != nullptr && stream->playbackTrack != nullptr)
        {
            SetMixerTrackGain(stream->playbackTrack, gain);
        }
    }

    void PauseMixerStream(MixerStream* stream) noexcept
    {
        if (stream != nullptr && stream->playbackTrack != nullptr)
        {
            PauseMixerTrack(stream->playbackTrack);
        }
    }

    void ResumeMixerStream(MixerStream* stream) noexcept
    {
        if (stream != nullptr && stream->playbackTrack != nullptr)
        {
            ResumeMixerTrack(stream->playbackTrack);
        }
    }

    bool IsMixerStreamPaused(const MixerStream* stream) noexcept
    {
        return stream != nullptr && stream->playbackTrack != nullptr &&
               IsMixerTrackPaused(stream->playbackTrack);
    }

    int GetMixerStreamQueuedBytes(const MixerStream* stream) noexcept
    {
        if (stream == nullptr)
        {
            return -1;
        }
        std::lock_guard lock(GetEngine().mutex);
        return static_cast<int>(std::min<std::size_t>(stream->Queued(), INT_MAX));
    }

    bool PutMixerStreamData(MixerStream* stream, const std::span<const std::byte> data) noexcept
    {
        if (stream == nullptr || data.size() > static_cast<std::size_t>(INT_MAX))
        {
            return false;
        }
        std::lock_guard lock(GetEngine().mutex);
        try
        {
            // Drop what has been played before growing, so the queue stays about as large as
            // what is actually waiting.
            if (stream->readOffset > 0 && stream->readOffset * 2 >= stream->queue.size())
            {
                stream->queue.erase(stream->queue.begin(),
                                    stream->queue.begin() +
                                        static_cast<std::ptrdiff_t>(stream->readOffset));
                stream->readOffset = 0;
            }
            stream->queue.insert(stream->queue.end(), data.begin(), data.end());
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
