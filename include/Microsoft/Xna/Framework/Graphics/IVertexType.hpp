// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class VertexDeclaration;

    /** @brief Implemented by vertex structures to expose their VertexDeclaration. */
    class IVertexType
    {
    public:
        virtual ~IVertexType() = default;

        /** @brief Returns the vertex declaration describing the layout of this vertex type. */
        [[nodiscard]] virtual const VertexDeclaration& getVertexDeclarationProperty() const = 0;
    };
}
