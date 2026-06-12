// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Interface for effects that support directional lighting.
    class IEffectLights
    {
    public:
        /// Virtual destructor.
        virtual ~IEffectLights() = default;

        /// Gets the ambient light color for the scene.
        [[nodiscard]] virtual Vector3 getAmbientLightColorProperty() const = 0;
        /// Sets the ambient light color for the scene.
        virtual void setAmbientLightColorProperty(const Vector3& value) = 0;

        /// Gets the first directional light.
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight0Property() = 0;
        /// Gets the second directional light.
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight1Property() = 0;
        /// Gets the third directional light.
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight2Property() = 0;

        /// Gets whether lighting is enabled.
        [[nodiscard]] virtual bool getLightingEnabledProperty() const = 0;
        /// Sets whether lighting is enabled.
        virtual void setLightingEnabledProperty(bool value) = 0;

        /// Enables default three-point lighting for the scene.
        virtual void EnableDefaultLighting() = 0;
    };
}
