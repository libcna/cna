// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    /// Represents the position, orientation, and velocity of a 3D audio sound source.
    class AudioEmitter
    {
    public:
        AudioEmitter();

        [[nodiscard]] float getDopplerScaleProperty() const;
        void setDopplerScaleProperty(float value);

        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getForwardProperty() const;
        void setForwardProperty(const Microsoft::Xna::Framework::Vector3& value);

        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getPositionProperty() const;
        void setPositionProperty(const Microsoft::Xna::Framework::Vector3& value);

        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getUpProperty() const;
        void setUpProperty(const Microsoft::Xna::Framework::Vector3& value);

        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getVelocityProperty() const;
        void setVelocityProperty(const Microsoft::Xna::Framework::Vector3& value);

    private:
        float dopplerScale_;
        Microsoft::Xna::Framework::Vector3 forward_;
        Microsoft::Xna::Framework::Vector3 position_;
        Microsoft::Xna::Framework::Vector3 up_;
        Microsoft::Xna::Framework::Vector3 velocity_;
    };
}
