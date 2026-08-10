// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    AudioListener::AudioListener()
        : forward_(Microsoft::Xna::Framework::Vector3::Forward),
          position_(Microsoft::Xna::Framework::Vector3::Zero),
          up_(Microsoft::Xna::Framework::Vector3::Up),
          velocity_(Microsoft::Xna::Framework::Vector3::Zero)
    {
    }

    Microsoft::Xna::Framework::Vector3 AudioListener::getForwardProperty() const
    {
        return forward_;
    }

    void AudioListener::setForwardProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        forward_ = value;
    }

    Microsoft::Xna::Framework::Vector3 AudioListener::getPositionProperty() const
    {
        return position_;
    }

    void AudioListener::setPositionProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        position_ = value;
    }

    Microsoft::Xna::Framework::Vector3 AudioListener::getUpProperty() const
    {
        return up_;
    }

    void AudioListener::setUpProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        up_ = value;
    }

    Microsoft::Xna::Framework::Vector3 AudioListener::getVelocityProperty() const
    {
        return velocity_;
    }

    void AudioListener::setVelocityProperty(const Microsoft::Xna::Framework::Vector3& value)
    {
        velocity_ = value;
    }
}
