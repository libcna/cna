#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    class VertexDeclaration;

    /// Implemented by vertex structures to expose their VertexDeclaration.
    class IVertexType
    {
    public:
        virtual ~IVertexType() = default;

        [[nodiscard]] virtual const VertexDeclaration& getVertexDeclarationProperty() const = 0;
    };
}
