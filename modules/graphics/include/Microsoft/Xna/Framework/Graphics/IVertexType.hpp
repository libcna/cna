// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class VertexDeclaration;

    /** @brief Implemented by vertex structures to expose their VertexDeclaration. */
    class IVertexType
    {
    public:
        /** @brief Virtual destructor. */
        NOXNA virtual ~IVertexType() = default;

        /** @brief Returns the vertex declaration describing the layout of this vertex type. */
        [[nodiscard]] virtual const VertexDeclaration& getVertexDeclarationProperty() const = 0;
    };
}
