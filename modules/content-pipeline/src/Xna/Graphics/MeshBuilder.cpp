// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshBuilder.hpp"

#include <algorithm>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    std::shared_ptr<MeshBuilder> MeshBuilder::StartMesh(const std::string& name)
    {
        // A null name is accepted and becomes the mesh's own (measured,
        // meshbuilder/refusals answers accepted for it).
        auto builder = std::shared_ptr<MeshBuilder>(new MeshBuilder());
        builder->name_ = name;
        return builder;
    }

    bool MeshBuilder::getMergeDuplicatePositionsProperty() const noexcept { return mergeDuplicatePositions_; }

    void MeshBuilder::setMergeDuplicatePositionsProperty(bool value) noexcept
    {
        mergeDuplicatePositions_ = value;
    }

    SharpRuntime::Single MeshBuilder::getMergePositionToleranceProperty() const noexcept
    {
        return mergePositionTolerance_;
    }

    void MeshBuilder::setMergePositionToleranceProperty(SharpRuntime::Single value) noexcept
    {
        mergePositionTolerance_ = value;
    }

    const std::string& MeshBuilder::getNameProperty() const noexcept { return name_; }

    void MeshBuilder::setNameProperty(std::string value) { name_ = std::move(value); }

    bool MeshBuilder::getSwapWindingOrderProperty() const noexcept { return swapWindingOrder_; }

    void MeshBuilder::setSwapWindingOrderProperty(bool value) noexcept { swapWindingOrder_ = value; }

    SharpRuntime::intcs MeshBuilder::AddChannel(std::shared_ptr<VertexChannelBase> channel,
                                                std::string elementTypeName)
    {
        if (!vertexPositions_.empty())
        {
            throw System::InvalidOperationException(
                "The function CreateVertexChannel<T> can only be called before calling "
                "AddTriangleVertex. All of the create functions must be called to set up the mesh "
                "before per-triangle data is added.");
        }
        for (const Channel& existing : channels_)
        {
            if (existing.channel->getNameProperty() == channel->getNameProperty())
            {
                throw System::ArgumentException("VertexChannelCollection already contains a channel with name \"" +
                                                channel->getNameProperty() + "\".");
            }
        }
        channels_.push_back(Channel{std::move(channel), std::move(elementTypeName), false, ContentObject{}});
        return static_cast<SharpRuntime::intcs>(channels_.size()) - 1;
    }

    void MeshBuilder::AddTriangleVertex(SharpRuntime::intcs indexIntoVertexCollection)
    {
        // An index naming no position is taken as it comes; XNA does not range-check it here
        // (measured, meshbuilder/refusals answers accepted for badVertexIndex).
        vertexPositions_.push_back(indexIntoVertexCollection);
        indices_.push_back(static_cast<SharpRuntime::intcs>(vertexPositions_.size()) - 1);
        for (Channel& channel : channels_)
        {
            // The value set once is carried into every corner that follows (measured,
            // meshbuilder/channel_data_persistence).
            if (channel.hasValue)
            {
                channel.channel->AddEntry(channel.value);
            }
            else
            {
                channel.channel->InsertDefaultEntry(channel.channel->getCountProperty());
            }
        }
    }

    SharpRuntime::intcs MeshBuilder::CreatePosition(const Vector3& pos)
    {
        positions_.push_back(pos);
        return static_cast<SharpRuntime::intcs>(positions_.size()) - 1;
    }

    SharpRuntime::intcs MeshBuilder::CreatePosition(SharpRuntime::Single x, SharpRuntime::Single y,
                                                    SharpRuntime::Single z)
    {
        return CreatePosition(Vector3(x, y, z));
    }

    std::shared_ptr<MeshContent> MeshBuilder::FinishMesh()
    {
        // Finishing twice answers the same mesh (measured, meshbuilder/finish_twice).
        if (finished_ != nullptr)
        {
            return finished_;
        }
        if (vertexPositions_.size() % 3 != 0)
        {
            throw System::InvalidOperationException(
                "MeshBuilder only supports triangle lists. The number of calls to AddTriangleVertex "
                "must be a multiple of three.");
        }
        auto mesh = std::make_shared<MeshContent>();
        mesh->setNameProperty(name_);
        for (const Vector3& position : positions_)
        {
            mesh->getPositionsProperty().Add(position);
        }
        if (!vertexPositions_.empty())
        {
            auto geometry = std::make_shared<GeometryContent>();
            mesh->getGeometryProperty().Add(geometry);
            geometry->getVerticesProperty().AddRange(vertexPositions_);
            geometry->getIndicesProperty().AddRange(indices_);
            for (Channel& channel : channels_)
            {
                geometry->getVerticesProperty().getChannelsProperty().InsertChannel(
                    geometry->getVerticesProperty().getChannelsProperty().getCountProperty(), channel.channel);
            }
            if (material_ != nullptr)
            {
                geometry->setMaterialProperty(material_);
            }
            if (opaqueData_ != nullptr)
            {
                for (const std::string& key : opaqueData_->getKeysProperty())
                {
                    ContentObject value;
                    if (opaqueData_->TryGetValue(key, value))
                    {
                        geometry->getOpaqueDataProperty().Add(key, value);
                    }
                }
            }
            // The positions merge first, so two corners whose positions merged and whose channel
            // data agree collapse into one vertex (measured, meshbuilder/duplicate_positions).
            if (mergeDuplicatePositions_)
            {
                MeshHelper::MergeDuplicatePositions(mesh, mergePositionTolerance_);
            }
            MeshHelper::MergeDuplicateVertices(mesh);
            if (swapWindingOrder_)
            {
                MeshHelper::SwapWindingOrder(mesh);
            }
            // A mesh built without normals is given them; one that carries its own keeps them
            // (measured, meshbuilder/material_and_opaque_data against meshbuilder/quad).
            MeshHelper::CalculateNormals(mesh, false);
        }
        finished_ = mesh;
        return finished_;
    }

    void MeshBuilder::SetMaterial(std::shared_ptr<MaterialContent> material) { material_ = std::move(material); }

    void MeshBuilder::SetOpaqueData(const OpaqueDataDictionary* opaqueData)
    {
        if (opaqueData == nullptr)
        {
            opaqueData_.reset();
            return;
        }
        opaqueData_ = std::make_shared<OpaqueDataDictionary>();
        for (const std::string& key : opaqueData->getKeysProperty())
        {
            ContentObject value;
            if (opaqueData->TryGetValue(key, value))
            {
                opaqueData_->Add(key, value);
            }
        }
    }

    void MeshBuilder::SetVertexChannelData(SharpRuntime::intcs vertexDataIndex, const ContentObject& channelData)
    {
        if (vertexDataIndex < 0 || static_cast<std::size_t>(vertexDataIndex) >= channels_.size())
        {
            throw System::ArgumentOutOfRangeException(
                "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
        }
        Channel& channel = channels_[static_cast<std::size_t>(vertexDataIndex)];
        const std::string given = channelData.StableType();
        if (given != channel.elementTypeName)
        {
            throw System::InvalidOperationException(
                "SetVertexChannelData cannot be called with the parameter of type " + given + " for index " +
                std::to_string(vertexDataIndex) +
                ". The type must match the type of data channel created for index " +
                std::to_string(vertexDataIndex) + ", which was " + channel.elementTypeName + ".");
        }
        channel.hasValue = true;
        channel.value = channelData;
    }

    const std::string& MeshBuilder::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
