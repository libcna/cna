// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"

#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    AudioEmitter::AudioEmitter()
        : dopplerScale_(1.0f),
          forward_(Microsoft::Xna::Framework::Vector3::Forward),
          position_(Microsoft::Xna::Framework::Vector3::Zero),
          up_(Microsoft::Xna::Framework::Vector3::Up),
          velocity_(Microsoft::Xna::Framework::Vector3::Zero)
    {
    }

    float AudioEmitter::getDopplerScaleProperty() const
    {
        return dopplerScale_;
    }

    void AudioEmitter::setDopplerScaleProperty(float value)
    {
        // XNA: AOORE("value", message) for a negative scale; `bge.un` lets NaN through
        // (AudioEmitter.set_DopplerScale IL_0000-IL_0017).
        if (value < 0.0f)
        {
            // XNA 4.0 Refresh: AudioEmitter.set_DopplerScale IL_0008-0017 / InvalidEmitterDopplerScale.
            throw System::ArgumentOutOfRangeException(
                "value", "The doppler scale of an audio emitter must be greater than or equal to zero.");
        }
        dopplerScale_ = value;
    }

    Microsoft::Xna::Framework::Vector3 AudioEmitter::getForwardProperty() const
    {
        return forward_;
    }

    void AudioEmitter::setForwardProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        forward_ = value;
    }

    Microsoft::Xna::Framework::Vector3 AudioEmitter::getPositionProperty() const
    {
        return position_;
    }

    void AudioEmitter::setPositionProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        position_ = value;
    }

    Microsoft::Xna::Framework::Vector3 AudioEmitter::getUpProperty() const
    {
        return up_;
    }

    void AudioEmitter::setUpProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        up_ = value;
    }

    Microsoft::Xna::Framework::Vector3 AudioEmitter::getVelocityProperty() const
    {
        return velocity_;
    }

    void AudioEmitter::setVelocityProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        velocity_ = value;
    }
}
