#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class IEffectFog
    {
    public:
        virtual ~IEffectFog() = default;

        [[nodiscard]] virtual Vector3 getFogColorProperty() const = 0;
        virtual void setFogColorProperty(const Vector3& value) = 0;

        [[nodiscard]] virtual bool getFogEnabledProperty() const = 0;
        virtual void setFogEnabledProperty(bool value) = 0;

        [[nodiscard]] virtual float getFogEndProperty() const = 0;
        virtual void setFogEndProperty(float value) = 0;

        [[nodiscard]] virtual float getFogStartProperty() const = 0;
        virtual void setFogStartProperty(float value) = 0;
    };
}
