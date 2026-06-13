// SPDX-License-Identifier: MS-PL
#pragma once

#include <initializer_list>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Describes the byte layout of a single vertex (stride + element list).
     */
    class VertexDeclaration
    {
    public:
        /** @brief Constructs an empty VertexDeclaration with zero stride. */
        VertexDeclaration() = default;

        /**
         * @brief Constructs a VertexDeclaration with an explicit stride and element list.
         * @param vertexStride Size in bytes of one vertex.
         * @param elements     Initializer list of vertex attribute descriptors.
         */
        VertexDeclaration(int vertexStride,
                          std::initializer_list<VertexElement> elements)
            : vertexStride_(vertexStride), elements_(elements)
        {
        }

        /**
         * @brief Constructs a VertexDeclaration with an explicit stride and element vector.
         * @param vertexStride Size in bytes of one vertex.
         * @param elements     Vector of vertex attribute descriptors (moved).
         */
        VertexDeclaration(int vertexStride,
                          std::vector<VertexElement> elements)
            : vertexStride_(vertexStride), elements_(std::move(elements))
        {
        }

        /**
         * @brief Returns the size in bytes of one vertex described by this declaration.
         * @return The vertex stride in bytes.
         */
        [[nodiscard]] int getVertexStrideProperty() const { return vertexStride_; }

        /**
         * @brief Returns the list of vertex element descriptors.
         * @return Const reference to the vector of VertexElement entries.
         */
        [[nodiscard]] const std::vector<VertexElement>& GetVertexElements() const
        {
            return elements_;
        }

    private:
        int vertexStride_ = 0;
        std::vector<VertexElement> elements_;
    };
}
