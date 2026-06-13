// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Represents the position, orientation, and velocity of a 3D audio sound source. */
    class AudioEmitter
    {
    public:
        /** @brief Initializes a new AudioEmitter with default position, orientation, and velocity. */
        AudioEmitter();

        /**
         * @brief Gets the Doppler effect scale for this emitter.
         *
         * @return Doppler scale value.
         */
        [[nodiscard]] float getDopplerScaleProperty() const;

        /**
         * @brief Sets the Doppler effect scale for this emitter.
         *
         * @param value New Doppler scale (must be >= 0).
         */
        void setDopplerScaleProperty(float value);

        /**
         * @brief Gets the forward orientation vector of this emitter.
         *
         * @return Forward direction vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getForwardProperty() const;

        /**
         * @brief Sets the forward orientation vector of this emitter.
         *
         * @param value New forward direction vector.
         */
        void setForwardProperty(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Gets the position of this emitter in 3D space.
         *
         * @return Position vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getPositionProperty() const;

        /**
         * @brief Sets the position of this emitter in 3D space.
         *
         * @param value New position vector.
         */
        void setPositionProperty(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Gets the upward orientation vector of this emitter.
         *
         * @return Up direction vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getUpProperty() const;

        /**
         * @brief Sets the upward orientation vector of this emitter.
         *
         * @param value New up direction vector.
         */
        void setUpProperty(const Microsoft::Xna::Framework::Vector3& value);

        /**
         * @brief Gets the velocity of this emitter, used for Doppler calculations.
         *
         * @return Velocity vector.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Vector3 getVelocityProperty() const;

        /**
         * @brief Sets the velocity of this emitter, used for Doppler calculations.
         *
         * @param value New velocity vector.
         */
        void setVelocityProperty(const Microsoft::Xna::Framework::Vector3& value);

    private:
        float dopplerScale_;
        Microsoft::Xna::Framework::Vector3 forward_;
        Microsoft::Xna::Framework::Vector3 position_;
        Microsoft::Xna::Framework::Vector3 up_;
        Microsoft::Xna::Framework::Vector3 velocity_;
    };
}
