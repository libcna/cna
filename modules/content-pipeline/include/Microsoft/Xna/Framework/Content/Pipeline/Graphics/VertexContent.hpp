// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannel.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    class GeometryContent;
    class VertexContent;

    /**
     * @brief Provides a read-only view of the positions a vertex content refers to: the parent
     *        mesh's positions, in the order the position indices name them.
     */
    class IndirectPositionCollection final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.IndirectPositionCollection";

        /**
         * @brief Initializes a view over a vertex content.
         *
         * @param owner The vertex content whose positions these are.
         */
        CNAEXT explicit IndirectPositionCollection(const VertexContent* owner) noexcept;

        /**
         * @brief Gets the number of positions, which is the vertex count.
         *
         * @return The position count.
         */
        [[nodiscard]] SharpRuntime::intcs getCountProperty() const noexcept;

        /**
         * @brief Gets the position of one vertex.
         *
         * @param index The vertex index.
         * @return The position.
         * @throws System::ArgumentOutOfRangeException when the index is outside the collection.
         */
        [[nodiscard]] Vector3 operator[](SharpRuntime::intcs index) const;

        /**
         * @brief Determines whether any vertex has the given position.
         *
         * @param value The position to look for.
         * @return true when a vertex has it.
         */
        [[nodiscard]] bool Contains(const Vector3& value) const;

        /**
         * @brief Copies the positions into a list, starting at the given index.
         *
         * @param destination The list receiving the positions.
         * @param index The first index to write at.
         * @throws System::ArgumentOutOfRangeException when the index is negative.
         * @throws System::ArgumentException when the positions do not fit.
         */
        void CopyTo(std::vector<Vector3>& destination, SharpRuntime::intcs index) const;

        /**
         * @brief Gets the index of the first vertex with the given position.
         *
         * @param value The position to look for.
         * @return The index, or -1 when no vertex has it.
         */
        [[nodiscard]] SharpRuntime::intcs IndexOf(const Vector3& value) const;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

    private:
        const VertexContent* owner_;
    };

    /**
     * @brief Provides methods and properties for maintaining the vertex data of a geometry batch:
     *        one position index per vertex, plus any number of named channels alongside.
     */
    class VertexContent final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.VertexContent";

        /**
         * @brief Initializes vertex data belonging to a geometry batch.
         *
         * XNA creates this only through `GeometryContent`; the parameter is the owning batch, and
         * null for the free-standing instance the intermediate serializer fills.
         *
         * @param owner The geometry batch these vertices belong to.
         */
        CNAEXT explicit VertexContent(GeometryContent* owner = nullptr) noexcept;

        /** @brief Vertex data is owned by its batch and never copied. */
        VertexContent(const VertexContent&) = delete;

        /** @brief Vertex data is owned by its batch and never copied. */
        VertexContent& operator=(const VertexContent&) = delete;

        /**
         * @brief Gets the vertex channels alongside the positions.
         *
         * @return The channels.
         */
        [[nodiscard]] VertexChannelCollection& getChannelsProperty() noexcept;

        /**
         * @brief Gets the vertex channels alongside the positions.
         *
         * @return The channels.
         */
        [[nodiscard]] const VertexChannelCollection& getChannelsProperty() const noexcept;

        /**
         * @brief Gets the index into the parent mesh's positions for each vertex.
         *
         * @return The position indices.
         */
        [[nodiscard]] VertexChannel<SharpRuntime::intcs>& getPositionIndicesProperty() noexcept;

        /**
         * @brief Gets the index into the parent mesh's positions for each vertex.
         *
         * @return The position indices.
         */
        [[nodiscard]] const VertexChannel<SharpRuntime::intcs>& getPositionIndicesProperty() const noexcept;

        /**
         * @brief Gets the position of each vertex, read through the parent mesh.
         *
         * @return The positions.
         */
        [[nodiscard]] const IndirectPositionCollection& getPositionsProperty() const noexcept;

        /**
         * @brief Gets the number of vertices.
         *
         * @return The vertex count.
         */
        [[nodiscard]] SharpRuntime::intcs getVertexCountProperty() const noexcept;

        /**
         * @brief Appends a vertex naming one of the parent mesh's positions.
         *
         * @param positionIndex The index into the parent mesh's positions.
         * @return The index of the new vertex.
         */
        SharpRuntime::intcs Add(SharpRuntime::intcs positionIndex);

        /**
         * @brief Appends one vertex per position index.
         *
         * @param positionIndexCollection The indices into the parent mesh's positions.
         */
        void AddRange(const std::vector<SharpRuntime::intcs>& positionIndexCollection);

        /**
         * @brief Inserts a vertex naming one of the parent mesh's positions.
         *
         * @param index The vertex index to insert at.
         * @param positionIndex The index into the parent mesh's positions.
         * @throws System::ArgumentOutOfRangeException when the index is outside the collection.
         */
        void Insert(SharpRuntime::intcs index, SharpRuntime::intcs positionIndex);

        /**
         * @brief Inserts one vertex per position index.
         *
         * @param index The vertex index to insert at.
         * @param positionIndexCollection The indices into the parent mesh's positions.
         * @throws System::ArgumentOutOfRangeException when the index is outside the collection.
         */
        void InsertRange(SharpRuntime::intcs index, const std::vector<SharpRuntime::intcs>& positionIndexCollection);

        /**
         * @brief Removes one vertex, and its entry in every channel.
         *
         * @param index The vertex index.
         * @throws System::ArgumentOutOfRangeException when the index is outside the collection.
         */
        void RemoveAt(SharpRuntime::intcs index);

        /**
         * @brief Removes a run of vertices, and their entries in every channel.
         *
         * @param index The first vertex index.
         * @param count How many vertices to remove.
         * @throws System::ArgumentOutOfRangeException when the run is outside the collection.
         */
        void RemoveRange(SharpRuntime::intcs index, SharpRuntime::intcs count);

        /**
         * @brief Gets the geometry batch these vertices belong to.
         *
         * @return The owning batch, or null for a free-standing instance.
         */
        CNAEXT [[nodiscard]] GeometryContent* Owner() const noexcept;

        /**
         * @brief Attaches these vertices to a geometry batch.
         *
         * @param owner The owning batch.
         */
        CNAEXT void SetOwner(GeometryContent* owner) noexcept;

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

        /** @brief The channel collection as the serializer holds it: an owned reference. */
        CNAEXT [[nodiscard]] const std::shared_ptr<VertexChannelCollection>& ChannelsPointer() const noexcept;

        /** @brief The position indices as the serializer holds them: an owned reference. */
        CNAEXT [[nodiscard]] const std::shared_ptr<VertexChannel<SharpRuntime::intcs>>& PositionIndicesPointer()
            const noexcept;

        /**
         * @brief Describes the vertices for the intermediate serializer: the position indices,
         *        then the channels.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(
            Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate::ContentTypeDescriptor<
                VertexContent>& d);

    private:
        GeometryContent* owner_;
        std::shared_ptr<VertexChannel<SharpRuntime::intcs>> positionIndices_;
        std::shared_ptr<VertexChannelCollection> channels_;
        std::shared_ptr<IndirectPositionCollection> positions_;
    };
}
