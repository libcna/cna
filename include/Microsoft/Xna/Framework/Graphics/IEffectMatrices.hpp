// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Interface for effects that support world, view, and projection matrices.
    class IEffectMatrices
    {
    public:
        /// Virtual destructor.
        virtual ~IEffectMatrices() = default;

        /// Gets the projection matrix.
        [[nodiscard]] virtual Matrix getProjectionProperty() const = 0;
        /// Sets the projection matrix.
        virtual void setProjectionProperty(const Matrix& value) = 0;

        /// Gets the view matrix.
        [[nodiscard]] virtual Matrix getViewProperty() const = 0;
        /// Sets the view matrix.
        virtual void setViewProperty(const Matrix& value) = 0;

        /// Gets the world matrix.
        [[nodiscard]] virtual Matrix getWorldProperty() const = 0;
        /// Sets the world matrix.
        virtual void setWorldProperty(const Matrix& value) = 0;
    };
}
