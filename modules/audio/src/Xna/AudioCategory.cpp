// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioCategory.hpp"

#include <any>
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "System/ArgumentException.hpp"

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
        // Refresh native Engine refusal is E_INVALIDARG; Helpers.GetExceptionFromResult IL_0235-023a.
        if (parent_ && parent_->getIsDisposedProperty())
            throw System::ArgumentException();
        if (parent_)
            parent_->PauseCategoryInternal(index_);
    }

    void AudioCategory::Resume()
    {
        // Refresh native Engine refusal is E_INVALIDARG; Helpers.GetExceptionFromResult IL_0235-023a.
        if (parent_ && parent_->getIsDisposedProperty())
            throw System::ArgumentException();
        if (parent_)
            parent_->ResumeCategoryInternal(index_);
    }

    void AudioCategory::SetVolume(float volume)
    {
        // XNA refuses a negative volume before it reaches the engine (AudioCategory.SetVolume
        // IL_0000-IL_0012: `bge.un`, so NaN is not refused here) with a plain ArgumentException.
        if (volume < 0.0f)
            throw System::ArgumentException("Volume must be a positive float value.");
        // Refresh native Engine refusal is E_INVALIDARG; Helpers.GetExceptionFromResult IL_0235-023a.
        if (parent_ && parent_->getIsDisposedProperty())
            throw System::ArgumentException();
        if (parent_)
            parent_->SetCategoryVolumeInternal(index_, volume);
    }

    void AudioCategory::Stop(AudioStopOptions options)
    {
        // Refresh native Engine refusal is E_INVALIDARG; Helpers.GetExceptionFromResult IL_0235-023a.
        if (parent_ && parent_->getIsDisposedProperty())
            throw System::ArgumentException();
        if (parent_)
            parent_->StopCategoryInternal(index_, options == AudioStopOptions::Immediate);
    }

    bool AudioCategory::Equals(const std::any& obj) const
    {
        const AudioCategory* other = std::any_cast<AudioCategory>(&obj);
        return other != nullptr && Equals(*other);
    }

    std::string AudioCategory::ToString() const
    {
        // XNA returns the category name, substituting an empty string for a null one; a
        // default-constructed AudioCategory carries an empty name, which is that same case.
        return name_;
    }

    bool AudioCategory::Equals(const AudioCategory& other) const
    {
        return parent_ == other.parent_ && index_ == other.index_;
    }

    int AudioCategory::GetHashCode() const
    {
        return static_cast<int>(index_) ^ (parent_ ? parent_->GetHashCode() : 0);
    }

    bool AudioCategory::operator==(const AudioCategory& other) const { return Equals(other); }
    bool AudioCategory::operator!=(const AudioCategory& other) const { return !Equals(other); }
}
