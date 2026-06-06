#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"

#include <stdexcept>

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
        if (value < 0.0f)
        {
            throw std::out_of_range("AudioEmitter.DopplerScale must be >= 0.0f");
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
