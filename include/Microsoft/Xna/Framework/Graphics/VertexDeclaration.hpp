#pragma once

#include <initializer_list>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief XNA 4.0 `VertexDeclaration` (stride + element list).
     *
     * Describes the byte layout of a single vertex.
     *
     * @note Status: STUB. The CNA backend does not yet consume the element
     *       list; only the `VertexStride` is meaningful at the moment, and
     *       even that is hard-coded for the built-in vertex types in the
     *       EasyGL backend. The class exists so that XNA-style sample code
     *       compiles and runs unchanged.
     */
    class VertexDeclaration
    {
    public:
        VertexDeclaration() = default;

        VertexDeclaration(int vertexStride,
                          std::initializer_list<VertexElement> elements)
            : vertexStride_(vertexStride), elements_(elements)
        {
        }

        VertexDeclaration(int vertexStride,
                          std::vector<VertexElement> elements)
            : vertexStride_(vertexStride), elements_(std::move(elements))
        {
        }

        /** Size in bytes of one vertex described by this declaration. */
        [[nodiscard]] int VertexStride() const { return vertexStride_; }

        /** Returns the element list (semantic + format + offset entries). */
        [[nodiscard]] const std::vector<VertexElement>& GetVertexElements() const
        {
            return elements_;
        }

    private:
        int vertexStride_ = 0;
        std::vector<VertexElement> elements_;
    };
}
