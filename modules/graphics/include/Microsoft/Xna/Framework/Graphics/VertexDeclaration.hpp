// SPDX-License-Identifier: MS-PL
#pragma once

#include <initializer_list>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /**
     * @brief Describes the byte layout of a single vertex (stride + element list).
     */
    class VertexDeclaration : public GraphicsResource
    {
    public:
        /** @brief Constructs an empty VertexDeclaration with zero stride. */
        VertexDeclaration() = default;

        /** @brief Returns the fully-qualified .NET type name of this object. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Constructs a VertexDeclaration from an element list, computing stride automatically.
         *
         * The stride is computed as the maximum of (element.Offset + sizeof(element.Format))
         * across all elements, matching FNA's VertexDeclaration(params VertexElement[]).
         *
         * @param elements Initializer list of vertex attribute descriptors.
         * @throws System::ArgumentNullException if @p elements is empty.
         */
        explicit VertexDeclaration(std::initializer_list<VertexElement> elements);

        /**
         * @brief Constructs a VertexDeclaration from an element vector, deriving the stride.
         *
         * The vector counterpart of the initializer-list constructor above: XNA's
         * `VertexDeclaration(params VertexElement[] elements)` accepts an array variable as
         * readily as an array literal, and a C++ game holding its elements in a vector -- what
         * `GetVertexElements()` hands back -- has no literal to spell.
         *
         * @param elements Vector of vertex attribute descriptors (moved).
         * @throws System::ArgumentNullException if @p elements is empty.
         */
        explicit VertexDeclaration(std::vector<VertexElement> elements);

        /**
         * @brief Constructs a VertexDeclaration with an explicit stride and element list.
         * @param vertexStride Size in bytes of one vertex.
         * @param elements     Initializer list of vertex attribute descriptors.
         * @throws System::ArgumentNullException if @p elements is empty.
         * @throws System::ArgumentOutOfRangeException if @p vertexStride is not positive.
         */
        VertexDeclaration(int vertexStride,
                          std::initializer_list<VertexElement> elements);

        /**
         * @brief Constructs a VertexDeclaration with an explicit stride and element vector.
         * @param vertexStride Size in bytes of one vertex.
         * @param elements     Vector of vertex attribute descriptors (moved).
         * @throws System::ArgumentNullException if @p elements is empty.
         * @throws System::ArgumentOutOfRangeException if @p vertexStride is not positive.
         */
        VertexDeclaration(int vertexStride,
                          std::vector<VertexElement> elements);

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
