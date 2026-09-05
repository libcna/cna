// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannel.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Builds a mesh a triangle at a time: positions and channels first, then one call per
     *        triangle corner.
     */
    class MeshBuilder final : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.MeshBuilder";

        /**
         * @brief Begins a mesh of the given name.
         *
         * @param name The name the finished mesh is given.
         * @return The builder.
         */
        [[nodiscard]] static std::shared_ptr<MeshBuilder> StartMesh(const std::string& name);

        /** @brief A builder is created through `StartMesh`, never directly. */
        MeshBuilder(const MeshBuilder&) = delete;

        /** @brief A builder is created through `StartMesh`, never directly. */
        MeshBuilder& operator=(const MeshBuilder&) = delete;

        /**
         * @brief Gets whether positions no further apart than the tolerance are merged.
         *
         * @return true when they are merged.
         */
        [[nodiscard]] bool getMergeDuplicatePositionsProperty() const noexcept;

        /**
         * @brief Sets whether positions no further apart than the tolerance are merged.
         *
         * @param value true to merge them.
         */
        void setMergeDuplicatePositionsProperty(bool value) noexcept;

        /**
         * @brief Gets the greatest distance two positions may be apart and still merge.
         *
         * @return The tolerance.
         */
        [[nodiscard]] SharpRuntime::Single getMergePositionToleranceProperty() const noexcept;

        /**
         * @brief Sets the greatest distance two positions may be apart and still merge.
         *
         * @param value The tolerance.
         */
        void setMergePositionToleranceProperty(SharpRuntime::Single value) noexcept;

        /**
         * @brief Gets the name the finished mesh is given.
         *
         * @return The name.
         */
        [[nodiscard]] const std::string& getNameProperty() const noexcept;

        /**
         * @brief Sets the name the finished mesh is given.
         *
         * @param value The name.
         */
        void setNameProperty(std::string value);

        /**
         * @brief Gets whether each triangle's winding order is reversed.
         *
         * @return true when it is reversed.
         */
        [[nodiscard]] bool getSwapWindingOrderProperty() const noexcept;

        /**
         * @brief Sets whether each triangle's winding order is reversed.
         *
         * @param value true to reverse it.
         */
        void setSwapWindingOrderProperty(bool value) noexcept;

        /**
         * @brief Adds one corner of a triangle, carrying the channel data set so far.
         *
         * @param indexIntoVertexCollection The index a `CreatePosition` call answered.
         */
        void AddTriangleVertex(SharpRuntime::intcs indexIntoVertexCollection);

        /**
         * @brief Adds a position the triangles can name.
         *
         * @param pos The position.
         * @return Its index.
         */
        SharpRuntime::intcs CreatePosition(const Vector3& pos);

        /**
         * @brief Adds a position the triangles can name.
         *
         * @param x The x coordinate.
         * @param y The y coordinate.
         * @param z The z coordinate.
         * @return Its index.
         */
        SharpRuntime::intcs CreatePosition(SharpRuntime::Single x, SharpRuntime::Single y,
                                           SharpRuntime::Single z);

        /**
         * @brief Adds a vertex channel; every channel must be created before the first triangle
         *        corner is added.
         *
         * @tparam T The channel's element type.
         * @param usage The channel name, such as `VertexChannelNames::Normal()`.
         * @return The channel index `SetVertexChannelData` names.
         * @throws System::ArgumentNullException when the name is empty.
         * @throws System::ArgumentException when a channel of that name already exists.
         * @throws System::InvalidOperationException when a triangle corner has already been added.
         */
        template<typename T>
        SharpRuntime::intcs CreateVertexChannel(const std::string& usage)
        {
            if (usage.empty())
            {
                throw System::ArgumentNullException("name");
            }
            auto channel = std::make_shared<VertexChannel<T>>(usage, std::vector<T>{});
            return AddChannel(std::move(channel), std::string(ContentTypeName<T>::Name()));
        }

        /**
         * @brief Finishes the mesh; calling it again answers the same mesh.
         *
         * @return The mesh.
         * @throws System::InvalidOperationException when the triangle corners are not a multiple
         *         of three.
         */
        [[nodiscard]] std::shared_ptr<MeshContent> FinishMesh();

        /**
         * @brief Sets the material the geometry is built with.
         *
         * @param material The material, or null for none.
         */
        void SetMaterial(std::shared_ptr<MaterialContent> material);

        /**
         * @brief Sets the opaque data the geometry is built with.
         *
         * @param opaqueData The data, or null for none.
         */
        void SetOpaqueData(const OpaqueDataDictionary* opaqueData);

        /**
         * @brief Sets the value one channel carries into the corners that follow.
         *
         * @param vertexDataIndex The channel index `CreateVertexChannel` answered.
         * @param channelData The value; it must be of the channel's element type.
         * @throws System::ArgumentOutOfRangeException when the index names no channel.
         * @throws System::InvalidOperationException when the value is of another type.
         */
        void SetVertexChannelData(SharpRuntime::intcs vertexDataIndex, const ContentObject& channelData);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        /** @brief A builder is created through `StartMesh`, never directly. */
        MeshBuilder() = default;

        /** @brief One channel under construction, with the value the next corner will carry. */
        struct Channel
        {
            std::shared_ptr<VertexChannelBase> channel;
            std::string elementTypeName;
            bool hasValue = false;
            ContentObject value;
        };

        /**
         * @brief Registers a channel, checking the name and the ordering rule.
         *
         * @param channel The channel.
         * @param elementTypeName The .NET name of its element type, for the refusal message.
         * @return The channel index.
         */
        SharpRuntime::intcs AddChannel(std::shared_ptr<VertexChannelBase> channel, std::string elementTypeName);

        std::string name_;
        bool mergeDuplicatePositions_ = false;
        SharpRuntime::Single mergePositionTolerance_ = 0.0f;
        bool swapWindingOrder_ = false;
        std::vector<Vector3> positions_;
        std::vector<Channel> channels_;
        std::vector<SharpRuntime::intcs> vertexPositions_;
        std::vector<SharpRuntime::intcs> indices_;
        std::shared_ptr<MaterialContent> material_;
        std::shared_ptr<OpaqueDataDictionary> opaqueData_;
        std::shared_ptr<MeshContent> finished_;
    };
}
