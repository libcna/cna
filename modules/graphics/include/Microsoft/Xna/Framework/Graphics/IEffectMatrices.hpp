// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Interface for effects that support world, view, and projection matrix transforms.
     */
    class IEffectMatrices
    {
    public:
        /** @brief Virtual destructor. */
        NOXNA virtual ~IEffectMatrices() = default;

        /**
         * @brief Gets the projection matrix.
         *
         * @return The current projection matrix.
         */
        [[nodiscard]] virtual Matrix getProjectionProperty() const = 0;

        /**
         * @brief Sets the projection matrix.
         *
         * @param value The new projection matrix.
         */
        virtual void setProjectionProperty(const Matrix& value) = 0;

        /**
         * @brief Gets the view matrix.
         *
         * @return The current view matrix.
         */
        [[nodiscard]] virtual Matrix getViewProperty() const = 0;

        /**
         * @brief Sets the view matrix.
         *
         * @param value The new view matrix.
         */
        virtual void setViewProperty(const Matrix& value) = 0;

        /**
         * @brief Gets the world matrix.
         *
         * @return The current world matrix.
         */
        [[nodiscard]] virtual Matrix getWorldProperty() const = 0;

        /**
         * @brief Sets the world matrix.
         *
         * @param value The new world matrix.
         */
        virtual void setWorldProperty(const Matrix& value) = 0;
    };
}
