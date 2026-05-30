#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"

#include <algorithm>
#include <stdexcept>

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

    const std::vector<Microphone*>& Microphone::getAllProperty()
    {
        if (micList == nullptr)
        {
            microphoneStorage_.clear();

            // Platform microphone enumeration should populate microphoneStorage_ here.
            // Until platform capture exists, the list is empty, matching systems with no microphones.

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

        if (milliseconds < 100 || milliseconds > 1000 || milliseconds % 10 != 0)
        {
            throw std::out_of_range("BufferDuration");
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
            throw std::out_of_range("offset");
        }

        if (count <= 0 || offset + count > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw std::out_of_range("count");
        }

        (void)handle_;

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
        state_ = MicrophoneState::Started;
    }

    void Microphone::Stop()
    {
        state_ = MicrophoneState::Stopped;
    }

    void Microphone::CheckBuffer()
    {
        if (!BufferReady.Empty() && GetSampleDuration(GetQueuedBytes()) > bufferDuration_)
        {
            BufferReady.Raise(this, System::EventArgs::Empty);
        }
    }

    SharpRuntime::intcs Microphone::GetQueuedBytes() const
    {
        // Platform capture should report the number of queued capture bytes.
        return 0;
    }
}
