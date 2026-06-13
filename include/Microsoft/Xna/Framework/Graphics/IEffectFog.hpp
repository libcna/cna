// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Interface for effects that support distance-based linear fog.
     */
    class IEffectFog
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IEffectFog() = default;

        /**
         * @brief Gets the fog color.
         *
         * @return The current fog color as a Vector3 (R, G, B in [0, 1]).
         */
        [[nodiscard]] virtual Vector3 getFogColorProperty() const = 0;

        /**
         * @brief Sets the fog color.
         *
         * @param value The new fog color as a Vector3 (R, G, B in [0, 1]).
         */
        virtual void setFogColorProperty(const Vector3& value) = 0;

        /**
         * @brief Gets whether fog is enabled.
         *
         * @return True if fog is currently enabled.
         */
        [[nodiscard]] virtual bool getFogEnabledProperty() const = 0;

        /**
         * @brief Sets whether fog is enabled.
         *
         * @param value True to enable fog; false to disable.
         */
        virtual void setFogEnabledProperty(bool value) = 0;

        /**
         * @brief Gets the camera-space distance at which fog reaches full density.
         *
         * @return The fog end distance.
         */
        [[nodiscard]] virtual float getFogEndProperty() const = 0;

        /**
         * @brief Sets the camera-space distance at which fog reaches full density.
         *
         * @param value The fog end distance.
         */
        virtual void setFogEndProperty(float value) = 0;

        /**
         * @brief Gets the camera-space distance at which fog begins.
         *
         * @return The fog start distance.
         */
        [[nodiscard]] virtual float getFogStartProperty() const = 0;

        /**
         * @brief Sets the camera-space distance at which fog begins.
         *
         * @param value The fog start distance.
         */
        virtual void setFogStartProperty(float value) = 0;
    };
}
