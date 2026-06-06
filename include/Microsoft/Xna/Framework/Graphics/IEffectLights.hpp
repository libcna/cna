#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class IEffectLights
    {
    public:
        virtual ~IEffectLights() = default;

        [[nodiscard]] virtual Vector3 getAmbientLightColorProperty() const = 0;
        virtual void setAmbientLightColorProperty(const Vector3& value) = 0;

        [[nodiscard]] virtual DirectionalLight& getDirectionalLight0Property() = 0;
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight1Property() = 0;
        [[nodiscard]] virtual DirectionalLight& getDirectionalLight2Property() = 0;

        [[nodiscard]] virtual bool getLightingEnabledProperty() const = 0;
        virtual void setLightingEnabledProperty(bool value) = 0;

        virtual void EnableDefaultLighting() = 0;
    };
}
