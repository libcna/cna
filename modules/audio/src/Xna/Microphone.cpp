// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"
#include "Platform/AudioDeviceFactory.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

namespace Microsoft::Xna::Framework::Audio
{
    namespace
    {
        CNA::Audio::Platform::IAudioRecordingDeviceProvider* GetRecordingProvider()
        {
#ifdef SOUND_ENABLED
            static auto provider =
                CNA::Audio::Platform::CreateSelectedAudioRecordingDeviceProvider();
            return provider.get();
#else
            return nullptr;
#endif
        }
    }

    std::vector<std::unique_ptr<Microphone>> Microphone::microphoneStorage_;
    std::vector<Microphone*>* Microphone::micList = nullptr;

    Microphone::Microphone(const std::uint64_t id, std::string name)
        : Name(std::move(name)),
          bufferDuration_(System::TimeSpan::FromSeconds(1.0)),
          recordingDeviceId_(id),
          state_(MicrophoneState::Stopped)
    {
    }

    Microphone::~Microphone()
    {
        Stop();
    }

    const std::vector<Microphone*>& Microphone::getAllProperty()
    {
        if (micList == nullptr)
        {
            microphoneStorage_.clear();

            if (auto* provider = GetRecordingProvider())
            {
                for (const auto& device : provider->GetDevices())
                {
                    microphoneStorage_.push_back(std::unique_ptr<Microphone>(
                        new Microphone(device.id, device.name)));
                }
            }

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
        return sampleRate_;
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

        // P9-AUDIT-002: offset+count must never be computed as a plain intcs addition -- the same
        // int32-overflow class P9-VALIDATION-003 already fixed in SoundEffect's buffer/range ctor
        // and DynamicSoundEffectInstance::SubmitBuffer/SubmitFloatBufferEXT, just missed here
        // since this file wasn't in that task's named scope. FNA gets away without this check
        // (Microphone.cs: `(offset + count) > buffer.Length`) because C#'s array bounds checking
        // is the real safety net there; C++ has none, so this has to be exact.
        const auto off = static_cast<std::size_t>(offset);
        const auto cnt = static_cast<std::size_t>(count);
        if (count <= 0 || cnt > buffer.size() - off)
        {
            throw System::ArgumentException("count");
        }

        if (captureDevice_)
        {
            auto* first = reinterpret_cast<std::byte*>(buffer.data() + offset);
            const auto result = captureDevice_->Read(
                std::span<std::byte>(first, static_cast<std::size_t>(count)));
            if (result.status == CNA::Audio::Platform::AudioRecordingIoStatus::Success
                && result.byteCount <= static_cast<std::size_t>(
                    std::numeric_limits<SharpRuntime::intcs>::max()))
            {
                return static_cast<SharpRuntime::intcs>(result.byteCount);
            }
        }

        // No capture session open, nothing available yet, or a platform error: report 0 bytes read
        // and leave the buffer untouched, matching FNA (Microphone.GetData delegates straight to
        // the platform read with no fallback zeroing of unread bytes).
        return 0;
    }

    System::TimeSpan Microphone::GetSampleDuration(SharpRuntime::intcs sizeInBytes) const
    {
        return SoundEffect::GetSampleDuration(sizeInBytes, getSampleRateProperty(), AudioChannels::Mono);
    }

    SharpRuntime::intcs Microphone::GetSampleSizeInBytes(System::TimeSpan duration) const
    {
        return SoundEffect::GetSampleSizeInBytes(duration, getSampleRateProperty(), AudioChannels::Mono);
    }

    void Microphone::Start()
    {
        if (!captureDevice_)
        {
            if (auto* provider = GetRecordingProvider())
            {
                auto device = provider->CreateDevice(recordingDeviceId_);
                if (device)
                {
                    try
                    {
                        const auto format = device->Open({
                            static_cast<std::uint32_t>(SAMPLERATE), 1,
                            CNA::Audio::Platform::AudioSampleFormat::Signed16});
                        // XNA microphone data is 16-bit mono. The provider may negotiate rate,
                        // which the SampleRate property and duration helpers expose exactly.
                        if (format.channels == 1
                            && format.sampleFormat
                                == CNA::Audio::Platform::AudioSampleFormat::Signed16)
                        {
                            sampleRate_ = static_cast<SharpRuntime::intcs>(format.sampleRate);
                            captureDevice_ = std::move(device);
                        }
                        else
                        {
                            device->Close();
                        }
                    }
                    catch (...)
                    {
                        // FNA ignores native start failure and still transitions public state.
                    }
                }
            }
        }

        state_ = MicrophoneState::Started;
    }

    void Microphone::Stop()
    {
        if (captureDevice_)
        {
            captureDevice_->Close();
            captureDevice_.reset();
        }

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
        if (captureDevice_)
        {
            const auto result = captureDevice_->GetAvailableBytes();
            if (result.status == CNA::Audio::Platform::AudioRecordingIoStatus::Success)
            {
                const auto bounded = std::min(
                    result.byteCount,
                    static_cast<std::size_t>(std::numeric_limits<SharpRuntime::intcs>::max()));
                return static_cast<SharpRuntime::intcs>(bounded);
            }
        }

        return 0;
    }

    GetTypeNameCPP(Microphone, "Microsoft.Xna.Framework.Audio.Microphone")
}
