// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Represents a directional light source used by stock effects such as BasicEffect.
     *
     * A directional light has no position; all rays travel in the same direction with
     * uniform diffuse and specular contributions.
     */
    class DirectionalLight
    {
    public:
        /** @brief Constructs a disabled white diffuse light pointing down with no specular contribution. */
        DirectionalLight();

        /**
         * @brief Gets the diffuse color contribution of this light.
         *
         * @return The diffuse color as a Vector3 (R, G, B in [0, 1]).
         */
        [[nodiscard]] Vector3 getDiffuseColorProperty() const;

        /**
         * @brief Sets the diffuse color contribution of this light.
         *
         * @param value The new diffuse color as a Vector3.
         */
        void setDiffuseColorProperty(const Vector3& value);

        /**
         * @brief Gets the direction this light is shining toward.
         *
         * @return The light direction as a normalized Vector3.
         */
        [[nodiscard]] Vector3 getDirectionProperty() const;

        /**
         * @brief Sets the direction this light is shining toward.
         *
         * @param value The new light direction (should be normalized).
         */
        void setDirectionProperty(const Vector3& value);

        /**
         * @brief Gets the specular color contribution of this light.
         *
         * @return The specular color as a Vector3 (R, G, B in [0, 1]).
         */
        [[nodiscard]] Vector3 getSpecularColorProperty() const;

        /**
         * @brief Sets the specular color contribution of this light.
         *
         * @param value The new specular color as a Vector3.
         */
        void setSpecularColorProperty(const Vector3& value);

        /**
         * @brief Gets whether this directional light is active.
         *
         * @return True if the light is enabled.
         */
        [[nodiscard]] bool getEnabledProperty() const;

        /**
         * @brief Sets whether this directional light is active.
         *
         * @param value True to enable the light; false to disable.
         */
        void setEnabledProperty(bool value);

    private:
        Vector3 diffuseColor_;
        Vector3 direction_;
        Vector3 specularColor_;
        bool enabled_ = false;
    };
}
