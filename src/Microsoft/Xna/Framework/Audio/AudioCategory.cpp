// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"

#include <functional>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    AudioCategory::AudioCategory(AudioEngine* engine, SharpRuntime::ushortcs index, std::string name)
        : parent_(engine), index_(index), name_(std::move(name))
    {
    }

    const std::string& AudioCategory::getNameProperty() const { return name_; }

    void AudioCategory::Pause()
    {
        if (parent_ && !parent_->getIsDisposedProperty())
            parent_->PauseCategoryInternal(index_);
    }

    void AudioCategory::Resume()
    {
        if (parent_ && !parent_->getIsDisposedProperty())
            parent_->ResumeCategoryInternal(index_);
    }

    void AudioCategory::SetVolume(float volume)
    {
        if (parent_ && !parent_->getIsDisposedProperty())
            parent_->SetCategoryVolumeInternal(index_, volume);
    }

    void AudioCategory::Stop(AudioStopOptions options)
    {
        if (parent_ && !parent_->getIsDisposedProperty())
            parent_->StopCategoryInternal(index_, options == AudioStopOptions::Immediate);
    }

    bool AudioCategory::Equals(const AudioCategory& other) const
    {
        return name_ == other.name_;
    }

    int AudioCategory::GetHashCode() const
    {
        return static_cast<int>(std::hash<std::string>{}(name_));
    }

    bool AudioCategory::operator==(const AudioCategory& other) const { return Equals(other); }
    bool AudioCategory::operator!=(const AudioCategory& other) const { return !Equals(other); }
}
