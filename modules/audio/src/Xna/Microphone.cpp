// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Internal/AudioFormatMath.hpp"
#include "CNA/Internal/Audio/FrameworkUpdates.hpp"
#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"
#include "CNA/Internal/Audio/RecordingProvider.hpp"
#include "Platform/AudioDeviceFactory.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/NoMicrophoneConnectedException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>

namespace CNA::Internal::Audio
{
    CNA::Audio::Platform::IAudioRecordingDeviceProvider* RecordingProvider()
    {
#ifdef SOUND_ENABLED
        static auto provider = CNA::Audio::Platform::CreateSelectedAudioRecordingDeviceProvider();
        return provider.get();
#else
        return nullptr;
#endif
    }
}

namespace Microsoft::Xna::Framework::Audio
{
    namespace
    {
        CNA::Audio::Platform::IAudioRecordingDeviceProvider* GetRecordingProvider()
        {
            return CNA::Internal::Audio::RecordingProvider();
        }
    }

    std::vector<std::unique_ptr<Microphone>> Microphone::microphoneStorage_;
    std::vector<Microphone*>* Microphone::micList = nullptr;

    Microphone::Microphone(const std::uint64_t id, std::string name,
                           CNA::Audio::Platform::IAudioRecordingDeviceProvider* provider)
        : Name(std::move(name)),
          bufferDuration_(System::TimeSpan::FromSeconds(1.0)),
          recordingDeviceId_(id),
          state_(MicrophoneState::Stopped),
          recordingProvider_(provider)
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
            // Microphones exist from here on; FrameworkDispatcher::Update() checks their buffers.
            CNA::Internal::FrameworkUpdates::Install(CNA::Internal::FrameworkUpdates::Slot::Microphones,
                                                     &Microphone::CheckAllBuffers);
            microphoneStorage_.clear();

            if (auto* provider = GetRecordingProvider())
            {
                for (const auto& device : provider->GetDevices())
                {
                    microphoneStorage_.push_back(std::unique_ptr<Microphone>(
                        new Microphone(device.id, device.name, provider)));
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
        // XNA's set_BufferDuration reads get_TotalMilliseconds: IL_0018 `ldc.r8 100. blt.s`,
        // IL_002a `ldc.r8 1000. bgt.s`, IL_003c `ldc.r8 10. rem bne`. FNA/src/Audio/Microphone.cs:57-60
        // reads value.Milliseconds, the sub-second component bounded to [-999, 999], which made
        // the "> 1000" branch unreachable and -- worse -- refused 1000 ms, the exact value this
        // property REPORTS on a microphone nothing has configured. So `mic.BufferDuration =
        // mic.BufferDuration` threw, while 1100, 1500 and 2500 ms were accepted because their
        // sub-second components are 100, 500 and 500. CLAUDE.md settles the direction: XNA wins.
        const auto milliseconds = static_cast<long long>(value.getTotalMillisecondsProperty());

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
        // XNA checks the whole buffer before delegating: empty, or ending in half a sample.
        if (buffer.empty() || buffer.size() % 2 != 0)
            throw System::ArgumentException("The buffer is empty or not a whole number of samples.", "buffer");
        return GetData(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    SharpRuntime::intcs Microphone::GetData(
        std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count
    )
    {
        // XNA's Microphone.GetData, in its order: a non-empty buffer of whole 16-bit samples, an
        // offset strictly inside it on a sample boundary, a positive count of whole samples that
        // fits, and finally a count long enough to last at least one rounded millisecond. FNA
        // checked only the bounds. The sum is taken in 64 bits (P9-AUDIT-002): C#'s checked
        // addition refuses a wrapped int, C++ has no bounds check to fall back on.
        const auto size = static_cast<std::int64_t>(buffer.size());
        if (size == 0 || size % 2 != 0)
            throw System::ArgumentException("The buffer is empty or not a whole number of samples.", "buffer");
        if (offset < 0 || offset >= size || offset % 2 != 0)
            throw System::ArgumentException("The offset is outside the buffer or not on a sample.", "offset");
        if (count <= 0 || static_cast<std::int64_t>(offset) + count > size || count % 2 != 0)
            throw System::ArgumentException("The count is outside the buffer or not whole samples.", "count");
        if (CNA::Internal::Audio::Pcm16DurationFromSize(count, sampleRate_, 1) == System::TimeSpan::Zero)
            throw System::ArgumentException("The count is shorter than a millisecond of audio.", "count");

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
        // XNA's Microphone checks only the size; its own format supplies the rest.
        if (sizeInBytes < 0)
            throw System::ArgumentException("The buffer size cannot be negative.");
        if (sizeInBytes == 0)
            return System::TimeSpan::Zero;
        return CNA::Internal::Audio::Pcm16DurationFromSize(sizeInBytes, getSampleRateProperty(), 1);
    }

    SharpRuntime::intcs Microphone::GetSampleSizeInBytes(System::TimeSpan duration) const
    {
        const double milliseconds = duration.getTotalMillisecondsProperty();
        if (milliseconds < 0.0 || milliseconds > 2147483647.0)
            throw System::ArgumentOutOfRangeException("duration");
        if (duration == System::TimeSpan::Zero)
            return 0;
        const auto bytes =
            CNA::Internal::Audio::Pcm16SizeFromDuration(duration, getSampleRateProperty(), 1);
        if (!bytes)
            throw System::ArgumentOutOfRangeException("duration");
        return *bytes;
    }

    void Microphone::Start()
    {
        if (captureDevice_ && captureDevice_->IsOpen() && captureDevice_->IsConnected())
        {
            return;
        }

        Stop();
        if (!recordingProvider_)
        {
            throw NoMicrophoneConnectedException();
        }
        auto device = recordingProvider_->CreateDevice(recordingDeviceId_);
        if (!device)
        {
            throw NoMicrophoneConnectedException();
        }

        // XNA propagates native start failures. Publish the new session only after Open
        // succeeds; FNA's unconditional state assignment is not the measured XNA contract.
        const auto format = device->Open({
            static_cast<std::uint32_t>(SAMPLERATE), 1,
            CNA::Audio::Platform::AudioSampleFormat::Signed16});
        if (!CNA::Audio::Platform::IsValid(format)
            || format.channels != 1
            || format.sampleFormat != CNA::Audio::Platform::AudioSampleFormat::Signed16
            || format.sampleRate > static_cast<std::uint32_t>(
                std::numeric_limits<SharpRuntime::intcs>::max())
            || !device->IsOpen())
        {
            device->Close();
            throw System::InvalidOperationException("Recording device did not start with a valid mono PCM16 format.");
        }
        if (!device->IsConnected())
        {
            device->Close();
            throw NoMicrophoneConnectedException();
        }

        sampleRate_ = static_cast<SharpRuntime::intcs>(format.sampleRate);
        captureDevice_ = std::move(device);
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
