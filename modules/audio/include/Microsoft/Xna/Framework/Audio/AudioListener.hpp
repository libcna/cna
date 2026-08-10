// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Represents the position, orientation, and velocity of a 3D audio listener. */
    class AudioListener
    {
    public:
        /** @brief Initializes a new AudioListener with default position, orientation, and velocity. */
        AudioListener();

        /**
         * @brief Gets the forward orientation vector of this listener.
         *
         * @return Forward direction vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getForwardProperty() const;

        /**
         * @brief Sets the forward orientation vector of this listener.
         *
         * @param value New forward direction vector.
         */
        void setForwardProperty(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Gets the position of this listener in 3D space.
         *
         * @return Position vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getPositionProperty() const;

        /**
         * @brief Sets the position of this listener in 3D space.
         *
         * @param value New position vector.
         */
        void setPositionProperty(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Gets the upward orientation vector of this listener.
         *
         * @return Up direction vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getUpProperty() const;

        /**
         * @brief Sets the upward orientation vector of this listener.
         *
         * @param value New up direction vector.
         */
        void setUpProperty(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Gets the velocity of this listener, used for Doppler calculations.
         *
         * @return Velocity vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getVelocityProperty() const;

        /**
         * @brief Sets the velocity of this listener, used for Doppler calculations.
         *
         * @param value New velocity vector.
         */
        void setVelocityProperty(const Microsoft::Xna::Framework::Vector3& value);

    private:
        Microsoft::Xna::Framework::Vector3 forward_;
        Microsoft::Xna::Framework::Vector3 position_;
        Microsoft::Xna::Framework::Vector3 up_;
        Microsoft::Xna::Framework::Vector3 velocity_;
    };
}
