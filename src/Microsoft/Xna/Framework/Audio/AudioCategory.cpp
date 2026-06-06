#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    AudioCategory::AudioCategory(AudioEngine* engine, unsigned short index, std::string name)
        : parent_(engine), index_(index), name_(std::move(name))
    {
    }

    const std::string& AudioCategory::getNameProperty() const
    {
        return name_;
    }

    void AudioCategory::Pause()
    {
        // SDL3_mixer has no category-level pause; no-op.
        if (parent_ && parent_->getIsDisposedProperty()) return;
    }

    void AudioCategory::Resume()
    {
        // SDL3_mixer has no category-level resume; no-op.
        if (parent_ && parent_->getIsDisposedProperty()) return;
    }

    void AudioCategory::SetVolume(float /*volume*/)
    {
        // SDL3_mixer has no category-level volume; no-op.
        if (parent_ && parent_->getIsDisposedProperty()) return;
    }

    void AudioCategory::Stop(AudioStopOptions /*options*/)
    {
        // SDL3_mixer has no category-level stop; no-op.
        if (parent_ && parent_->getIsDisposedProperty()) return;
    }

    bool AudioCategory::Equals(const AudioCategory& other) const
    {
        return GetHashCode() == other.GetHashCode();
    }

    int AudioCategory::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(name_));
    }

    bool AudioCategory::operator==(const AudioCategory& other) const
    {
        return Equals(other);
    }

    bool AudioCategory::operator!=(const AudioCategory& other) const
    {
        return !Equals(other);
    }
}
