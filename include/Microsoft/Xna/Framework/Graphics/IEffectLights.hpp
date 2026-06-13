// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Interface for effects that support directional lighting with up to three lights.
     */
    class IEffectLights
    {
    public:
        /** @brief Virtual destructor. */
        NOXNA virtual ~IEffectLights() = default;

        /**
         * @brief Gets the ambient light color applied to the scene.
         *
         * @return The ambient light color as a Vector3 (R, G, B in [0, 1]).
         */
        [[nodiscard]] virtual Vector3 getAmbientLightColorProperty() const = 0;

        /**
         * @brief Sets the ambient light color applied to the scene.
         *
         * @param value The new ambient light color as a Vector3.
         */
        virtual void setAmbientLightColorProperty(const Vector3& value) = 0;

        /**
         * @brief Gets the first directional light source.
         *
         * @return Reference to the first DirectionalLight.
         */
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight0Property() = 0;

        /**
         * @brief Gets the second directional light source.
         *
         * @return Reference to the second DirectionalLight.
         */
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight1Property() = 0;

        /**
         * @brief Gets the third directional light source.
         *
         * @return Reference to the third DirectionalLight.
         */
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight2Property() = 0;

        /**
         * @brief Gets whether per-vertex lighting is enabled.
         *
         * @return True if lighting is currently enabled.
         */
        [[nodiscard]] virtual bool getLightingEnabledProperty() const = 0;

        /**
         * @brief Sets whether per-vertex lighting is enabled.
         *
         * @param value True to enable lighting; false to disable.
         */
        virtual void setLightingEnabledProperty(bool value) = 0;

        /**
         * @brief Configures three-point lighting using standard key, fill, and back light directions.
         */
        virtual void EnableDefaultLighting() = 0;
    };
}
