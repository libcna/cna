// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <algorithm>

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#endif

namespace Microsoft::Xna::Framework::Audio
{
    std::vector<std::unique_ptr<Microphone>> Microphone::microphoneStorage_;
    std::vector<Microphone*>* Microphone::micList = nullptr;

    Microphone::Microphone(SharpRuntime::uintcs id, std::string name)
        : Name(std::move(name)),
          bufferDuration_(System::TimeSpan::FromSeconds(1.0)),
          handle_(id),
          state_(MicrophoneState::Stopped)
    {
    }

    Microphone::~Microphone()
    {
#ifdef SOUND_ENABLED
        if (captureStream_ != nullptr)
        {
            SDL_DestroyAudioStream(captureStream_);
        }
#endif
    }

    const std::vector<Microphone*>& Microphone::getAllProperty()
    {
        if (micList == nullptr)
        {
            microphoneStorage_.clear();

#ifdef SOUND_ENABLED
            SDL_InitSubSystem(SDL_INIT_AUDIO);

            int numDev = 0;
            SDL_AudioDeviceID* devices = SDL_GetAudioRecordingDevices(&numDev);
            if (numDev >= 1)
            {
                // FNA (SDL3_FNAPlatform.GetMicrophones) always leads with a synthetic "Default
                // Device" entry bound to the SDL default-recording sentinel. Unlike FNA, no
                // device is opened here: CNA opens capture devices lazily in Start() (matching
                // AudioMixer's lazy-open convention for playback), not at enumeration time.
                microphoneStorage_.push_back(std::unique_ptr<Microphone>(
                    new Microphone(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, "Default Device")));

                for (int i = 0; i < numDev; ++i)
                {
                    const char* name = SDL_GetAudioDeviceName(devices[i]);
                    microphoneStorage_.push_back(std::unique_ptr<Microphone>(
                        new Microphone(devices[i], name != nullptr ? name : "")));
                }
            }
            SDL_free(devices);
#endif

            static std::vector<Microphone*> list;
            list.clear();
            for (const auto& microphone : microphoneStorage_)
            {
                list.push_back(microphone.get());
            }

            micList = &list;
        }

        return *micList;
    }

    Microphone* Microphone::getDefaultProperty()
    {
        const auto& all = getAllProperty();
        if (all.empty())
        {
            return nullptr;
        }

        return all[0];
    }

    System::TimeSpan Microphone::getBufferDurationProperty() const
    {
        return bufferDuration_;
    }

    void Microphone::setBufferDurationProperty(System::TimeSpan value)
    {
        const auto milliseconds = value.getMillisecondsProperty();

        // getMillisecondsProperty() is the sub-second component, bounded to [-999, 999], so the
        // "> 1000" branch below can never be true; kept as-is to match FNA (Microphone.cs:60).
        if (milliseconds < 100 || milliseconds > 1000 || milliseconds % 10 != 0)
        {
            throw System::ArgumentOutOfRangeException("BufferDuration");
        }

        bufferDuration_ = value;
    }

    bool Microphone::getIsHeadsetProperty() const
    {
        return false;
    }

    SharpRuntime::intcs Microphone::getSampleRateProperty() const
    {
        return SAMPLERATE;
    }

    MicrophoneState Microphone::getStateProperty() const
    {
        return state_;
    }

    SharpRuntime::intcs Microphone::GetData(std::vector<SharpRuntime::bytecs>& buffer)
    {
        return GetData(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    SharpRuntime::intcs Microphone::GetData(
        std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count
    )
    {
        if (offset < 0 || offset > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw System::ArgumentException("offset");
        }

        if (count <= 0 || offset + count > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw System::ArgumentException("count");
        }

        // Platform capture should fill the requested buffer range and return bytes read.
        // For now no capture backend is connected, so no bytes are available.
        std::fill(buffer.begin() + offset, buffer.begin() + offset + count, SharpRuntime::bytecs{0});
        return 0;
    }

    System::TimeSpan Microphone::GetSampleDuration(SharpRuntime::intcs sizeInBytes) const
    {
        const double seconds =
            static_cast<double>(sizeInBytes) /
            static_cast<double>(SAMPLERATE * static_cast<SharpRuntime::intcs>(AudioChannels::Mono) * 2);

        return System::TimeSpan::FromSeconds(seconds);
    }

    SharpRuntime::intcs Microphone::GetSampleSizeInBytes(System::TimeSpan duration) const
    {
        const double seconds = duration.getTotalSecondsProperty();
        return static_cast<SharpRuntime::intcs>(
            seconds * SAMPLERATE * static_cast<SharpRuntime::intcs>(AudioChannels::Mono) * 2
        );
    }

    void Microphone::Start()
    {
#ifdef SOUND_ENABLED
        if (captureStream_ == nullptr)
        {
            SDL_InitSubSystem(SDL_INIT_AUDIO);

            SDL_AudioSpec want{};
            want.format = SDL_AUDIO_S16;
            want.channels = static_cast<int>(AudioChannels::Mono);
            want.freq = SAMPLERATE;

            // FNA opens the device (and a bound stream) once, up front, at enumeration time
            // (SDL3_FNAPlatform.GetMicrophones). CNA defers that to here to keep enumeration
            // free of side effects; a failed open is tolerated silently, matching FNA, which
            // never checks FNAPlatform.StartMicrophone's result either.
            captureStream_ = SDL_OpenAudioDeviceStream(handle_, &want, nullptr, nullptr);
            if (captureStream_ != nullptr)
            {
                SDL_ResumeAudioStreamDevice(captureStream_);
            }
        }
#endif

        state_ = MicrophoneState::Started;
    }

    void Microphone::Stop()
    {
#ifdef SOUND_ENABLED
        if (captureStream_ != nullptr)
        {
            SDL_DestroyAudioStream(captureStream_);
            captureStream_ = nullptr;
        }
#endif

        state_ = MicrophoneState::Stopped;
    }

    void Microphone::CheckBuffer()
    {
        if (!BufferReady.Empty() && GetSampleDuration(GetQueuedBytes()) > bufferDuration_)
        {
            BufferReady.Raise(this, System::EventArgs::Empty);
        }
    }

    void Microphone::CheckAllBuffers()
    {
        if (micList != nullptr)
        {
            for (Microphone* microphone : *micList)
            {
                if (microphone != nullptr)
                {
                    microphone->CheckBuffer();
                }
            }
        }
    }

    SharpRuntime::intcs Microphone::GetQueuedBytes() const
    {
        // Platform capture should report the number of queued capture bytes.
        return 0;
    }

    GetTypeNameCPP(Microphone, "Microsoft.Xna.Framework.Audio.Microphone")
}
