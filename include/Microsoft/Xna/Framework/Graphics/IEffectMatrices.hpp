#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class IEffectMatrices
    {
    public:
        virtual ~IEffectMatrices() = default;

        [[nodiscard]] virtual Matrix getProjectionProperty() const = 0;
        virtual void setProjectionProperty(const Matrix& value) = 0;

        [[nodiscard]] virtual Matrix getViewProperty() const = 0;
        virtual void setViewProperty(const Matrix& value) = 0;

        [[nodiscard]] virtual Matrix getWorldProperty() const = 0;
        virtual void setWorldProperty(const Matrix& value) = 0;
    };
}
