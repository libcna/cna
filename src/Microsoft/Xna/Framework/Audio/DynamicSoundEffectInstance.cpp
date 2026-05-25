#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"

#include <algorithm>
#include <stdexcept>

#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    DynamicSoundEffectInstance::DynamicSoundEffectInstance(SharpRuntime::intcs sampleRate, AudioChannels channels)
        : SoundEffectInstance(),
          sampleRate_(sampleRate),
          channels_(channels),
          format_{},
          disposed_(false)
    {
        format_.wFormatTag = 1;
        format_.nChannels = static_cast<std::uint16_t>(channels_);
        format_.nSamplesPerSec = static_cast<std::uint32_t>(sampleRate_);
        format_.wBitsPerSample = 16;
        format_.nBlockAlign = static_cast<std::uint16_t>(2 * format_.nChannels);
        format_.nAvgBytesPerSec = format_.nBlockAlign * format_.nSamplesPerSec;
        format_.cbSize = 0;
    }

    DynamicSoundEffectInstance::~DynamicSoundEffectInstance()
    {
        ClearBuffers();
        disposed_ = true;
    }

    SharpRuntime::intcs DynamicSoundEffectInstance::getPendingBufferCountProperty() const
    {
        std::lock_guard<std::mutex> lock(queuedBuffersMutex_);
        return static_cast<SharpRuntime::intcs>(queuedBuffers_.size());
    }

    bool DynamicSoundEffectInstance::getIsLoopedProperty() const
    {
        return false;
    }

    void DynamicSoundEffectInstance::setIsLoopedProperty(bool value)
    {
        (void) value;
    }

    bool DynamicSoundEffectInstance::getIsDisposedProperty() const
    {
        return disposed_;
    }

    System::TimeSpan DynamicSoundEffectInstance::GetSampleDuration(SharpRuntime::intcs sizeInBytes) const
    {
        const double seconds =
            static_cast<double>(sizeInBytes) /
            static_cast<double>(sampleRate_ * getBytesPerSampleFrame());

        return System::TimeSpan::FromSeconds(seconds);
    }

    SharpRuntime::intcs DynamicSoundEffectInstance::GetSampleSizeInBytes(System::TimeSpan duration) const
    {
        const double seconds = duration.getTotalSecondsProperty();
        return static_cast<SharpRuntime::intcs>(seconds * sampleRate_ * getBytesPerSampleFrame());
    }

    void DynamicSoundEffectInstance::Play()
    {
        Update();

        SoundEffectInstance::Play();

        std::lock_guard<std::mutex> lock(Microsoft::Xna::Framework::FrameworkDispatcher::StreamsMutex);
        auto& streams = Microsoft::Xna::Framework::FrameworkDispatcher::Streams;
        if (std::find(streams.begin(), streams.end(), this) == streams.end())
        {
            streams.push_back(this);
        }
    }

    void DynamicSoundEffectInstance::SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer)
    {
        SubmitBuffer(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    void DynamicSoundEffectInstance::SubmitBuffer(
        const std::vector<SharpRuntime::bytecs>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count
    ) {
        if (offset < 0 || count < 0 || offset > static_cast<SharpRuntime::intcs>(buffer.size()) ||
            offset + count > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw std::out_of_range("buffer range");
        }

        std::vector<SharpRuntime::bytecs> next(
            buffer.begin() + offset,
            buffer.begin() + offset + count
        );

        std::lock_guard<std::mutex> lock(queuedBuffersMutex_);
        queuedBuffers_.push_back(std::move(next));
        queuedSizes_.push_back(static_cast<std::uint32_t>(count));
    }

    void DynamicSoundEffectInstance::SubmitFloatBufferEXT(const std::vector<float>& buffer)
    {
        SubmitFloatBufferEXT(buffer, 0, static_cast<SharpRuntime::intcs>(buffer.size()));
    }

    void DynamicSoundEffectInstance::SubmitFloatBufferEXT(
        const std::vector<float>& buffer,
        SharpRuntime::intcs offset,
        SharpRuntime::intcs count
    ) {
        if (getStateProperty() != SoundState::Stopped && format_.wFormatTag == 1)
        {
            throw std::logic_error("Submit a float buffer before Playing!");
        }

        if (offset < 0 || count < 0 || offset > static_cast<SharpRuntime::intcs>(buffer.size()) ||
            offset + count > static_cast<SharpRuntime::intcs>(buffer.size()))
        {
            throw std::out_of_range("buffer range");
        }

        format_.wFormatTag = 3;
        format_.wBitsPerSample = 32;
        format_.nBlockAlign = static_cast<std::uint16_t>(4 * format_.nChannels);
        format_.nAvgBytesPerSec = format_.nBlockAlign * format_.nSamplesPerSec;

        const auto* bytes = reinterpret_cast<const SharpRuntime::bytecs*>(buffer.data() + offset);
        const std::size_t byteCount = static_cast<std::size_t>(count) * sizeof(float);

        std::vector<SharpRuntime::bytecs> next(bytes, bytes + byteCount);

        std::lock_guard<std::mutex> lock(queuedBuffersMutex_);
        queuedBuffers_.push_back(std::move(next));
        queuedSizes_.push_back(static_cast<std::uint32_t>(byteCount));
    }

    void DynamicSoundEffectInstance::QueueInitialBuffers()
    {
        // The platform audio backend should submit queuedBuffers_ to its source voice here.
        // The queued data is intentionally kept so PendingBufferCount remains meaningful.
    }

    void DynamicSoundEffectInstance::ClearBuffers()
    {
        std::lock_guard<std::mutex> lock(queuedBuffersMutex_);
        queuedBuffers_.clear();
        queuedSizes_.clear();
    }

    void DynamicSoundEffectInstance::Update()
    {
        if (getStateProperty() != SoundState::Playing)
        {
            return;
        }

        for (
            SharpRuntime::intcs i = MINIMUM_BUFFER_CHECK - getPendingBufferCountProperty();
            (i > 0) && !BufferNeeded.Empty();
            --i
        ) {
            BufferNeeded.Raise(this, System::EventArgs::Empty);
        }
    }

    SharpRuntime::intcs DynamicSoundEffectInstance::getBytesPerSampleFrame() const
    {
        return static_cast<SharpRuntime::intcs>(channels_) *
               static_cast<SharpRuntime::intcs>(format_.wBitsPerSample / 8);
    }

    GetTypeNameCPP(DynamicSoundEffectInstance, DynamicSoundEffectInstance)
}
