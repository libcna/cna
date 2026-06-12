// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Interface for effects that support distance-based fog.
    class IEffectFog
    {
    public:
        /// Virtual destructor.
        virtual ~IEffectFog() = default;

        /// Gets the fog color.
        [[nodiscard]] virtual Vector3 getFogColorProperty() const = 0;
        /// Sets the fog color.
        virtual void setFogColorProperty(const Vector3& value) = 0;

        /// Gets whether fog is enabled.
        [[nodiscard]] virtual bool getFogEnabledProperty() const = 0;
        /// Sets whether fog is enabled.
        virtual void setFogEnabledProperty(bool value) = 0;

        /// Gets the distance at which fog reaches full density.
        [[nodiscard]] virtual float getFogEndProperty() const = 0;
        /// Sets the distance at which fog reaches full density.
        virtual void setFogEndProperty(float value) = 0;

        /// Gets the distance at which fog begins.
        [[nodiscard]] virtual float getFogStartProperty() const = 0;
        /// Sets the distance at which fog begins.
        virtual void setFogStartProperty(float value) = 0;
    };
}
