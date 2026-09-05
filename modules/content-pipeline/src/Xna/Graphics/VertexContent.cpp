// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexContent.hpp"

#include <algorithm>

#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    namespace
    {
        [[noreturn]] void ThrowIndexOutOfRange()
        {
            throw System::ArgumentOutOfRangeException(
                "index", "Index was out of range. Must be non-negative and less than the size of the collection.");
        }
    }

    // ------------------------------------------------------------------------------------------
    // IndirectPositionCollection
    // ------------------------------------------------------------------------------------------

    IndirectPositionCollection::IndirectPositionCollection(const VertexContent* owner) noexcept : owner_(owner) {}

    SharpRuntime::intcs IndirectPositionCollection::getCountProperty() const noexcept
    {
        return owner_ == nullptr ? 0 : owner_->getVertexCountProperty();
    }

    Vector3 IndirectPositionCollection::operator[](SharpRuntime::intcs index) const
    {
        if (owner_ == nullptr || index < 0 || index >= getCountProperty())
        {
            ThrowIndexOutOfRange();
        }
        const GeometryContent* geometry = owner_->Owner();
        const MeshContent* mesh = geometry == nullptr ? nullptr : geometry->getParentProperty();
        if (mesh == nullptr)
        {
            // Without a mesh above it there is nothing to look the index up in; XNA reaches the
            // same dead end through a null reference.
            throw System::ArgumentException("The vertex content has no parent mesh to read positions from.");
        }
        const SharpRuntime::intcs positionIndex = owner_->getPositionIndicesProperty().At(index);
        const auto& positions =
            static_cast<const System::Collections::ObjectModel::Collection<Vector3>&>(mesh->getPositionsProperty());
        if (positionIndex < 0 || positionIndex >= positions.getCountProperty())
        {
            ThrowIndexOutOfRange();
        }
        return positions[positionIndex];
    }

    bool IndirectPositionCollection::Contains(const Vector3& value) const { return IndexOf(value) >= 0; }

    void IndirectPositionCollection::CopyTo(std::vector<Vector3>& destination, SharpRuntime::intcs index) const
    {
        if (index < 0)
        {
            throw System::ArgumentOutOfRangeException("index");
        }
        if (destination.size() < static_cast<std::size_t>(index + getCountProperty()))
        {
            throw System::ArgumentException("The positions do not fit into the destination.", "destination");
        }
        for (SharpRuntime::intcs i = 0; i < getCountProperty(); ++i)
        {
            destination[static_cast<std::size_t>(index + i)] = (*this)[i];
        }
    }

    SharpRuntime::intcs IndirectPositionCollection::IndexOf(const Vector3& value) const
    {
        for (SharpRuntime::intcs i = 0; i < getCountProperty(); ++i)
        {
            if ((*this)[i] == value)
            {
                return i;
            }
        }
        return -1;
    }

    const std::string& IndirectPositionCollection::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string IndirectPositionCollection::ToString() const { return std::string(XnaTypeName); }

    // ------------------------------------------------------------------------------------------
    // VertexContent
    // ------------------------------------------------------------------------------------------

    VertexContent::VertexContent(GeometryContent* owner) noexcept
        : owner_(owner),
          positionIndices_(std::make_shared<VertexChannel<SharpRuntime::intcs>>(std::string("PositionIndices"))),
          channels_(std::make_shared<VertexChannelCollection>(this)),
          positions_(std::make_shared<IndirectPositionCollection>(this))
    {
    }

    VertexChannelCollection& VertexContent::getChannelsProperty() noexcept { return *channels_; }

    const VertexChannelCollection& VertexContent::getChannelsProperty() const noexcept { return *channels_; }

    VertexChannel<SharpRuntime::intcs>& VertexContent::getPositionIndicesProperty() noexcept
    {
        return *positionIndices_;
    }

    const VertexChannel<SharpRuntime::intcs>& VertexContent::getPositionIndicesProperty() const noexcept
    {
        return *positionIndices_;
    }

    const IndirectPositionCollection& VertexContent::getPositionsProperty() const noexcept { return *positions_; }

    const std::shared_ptr<VertexChannelCollection>& VertexContent::ChannelsPointer() const noexcept
    {
        return channels_;
    }

    const std::shared_ptr<VertexChannel<SharpRuntime::intcs>>& VertexContent::PositionIndicesPointer() const noexcept
    {
        return positionIndices_;
    }

    SharpRuntime::intcs VertexContent::getVertexCountProperty() const noexcept
    {
        return positionIndices_->getCountProperty();
    }

    SharpRuntime::intcs VertexContent::Add(SharpRuntime::intcs positionIndex)
    {
        const SharpRuntime::intcs index = getVertexCountProperty();
        Insert(index, positionIndex);
        return index;
    }

    void VertexContent::AddRange(const std::vector<SharpRuntime::intcs>& positionIndexCollection)
    {
        InsertRange(getVertexCountProperty(), positionIndexCollection);
    }

    void VertexContent::Insert(SharpRuntime::intcs index, SharpRuntime::intcs positionIndex)
    {
        InsertRange(index, std::vector<SharpRuntime::intcs>{positionIndex});
    }

    void VertexContent::InsertRange(SharpRuntime::intcs index,
                                    const std::vector<SharpRuntime::intcs>& positionIndexCollection)
    {
        if (index < 0 || index > getVertexCountProperty())
        {
            ThrowIndexOutOfRange();
        }
        std::vector<SharpRuntime::intcs>& indices = positionIndices_->Items();
        indices.insert(indices.begin() + static_cast<std::ptrdiff_t>(index), positionIndexCollection.begin(),
                       positionIndexCollection.end());
        // Every channel grows with the vertices, one default entry per new vertex.
        for (const std::shared_ptr<VertexChannelBase>& channel : *channels_)
        {
            for (std::size_t i = 0; i < positionIndexCollection.size(); ++i)
            {
                channel->InsertDefaultEntry(index + static_cast<SharpRuntime::intcs>(i));
            }
        }
    }

    void VertexContent::RemoveAt(SharpRuntime::intcs index) { RemoveRange(index, 1); }

    void VertexContent::RemoveRange(SharpRuntime::intcs index, SharpRuntime::intcs count)
    {
        if (index < 0 || count < 0 || index + count > getVertexCountProperty())
        {
            ThrowIndexOutOfRange();
        }
        std::vector<SharpRuntime::intcs>& indices = positionIndices_->Items();
        indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(index),
                      indices.begin() + static_cast<std::ptrdiff_t>(index + count));
        for (const std::shared_ptr<VertexChannelBase>& channel : *channels_)
        {
            for (SharpRuntime::intcs i = 0; i < count; ++i)
            {
                if (index < channel->getCountProperty())
                {
                    channel->RemoveEntry(index);
                }
            }
        }
    }

    GeometryContent* VertexContent::Owner() const noexcept { return owner_; }

    void VertexContent::SetOwner(GeometryContent* owner) noexcept { owner_ = owner; }

    void VertexContent::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<VertexContent>& d)
    {
        // The position indices are packed text, the channels their own elements (measured,
        // tests/reference/xna40/graphics case mesh/serialize).
        d.ReadOnlyProperty("PositionIndices",
                           [](VertexContent& vertices) -> std::shared_ptr<VertexChannel<SharpRuntime::intcs>>&
                           { return vertices.positionIndices_; });
        d.ReadOnlyProperty("Channels", [](VertexContent& vertices) -> std::shared_ptr<VertexChannelCollection>&
                           { return vertices.channels_; });
    }

    const std::string& VertexContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    std::string VertexContent::ToString() const { return std::string(XnaTypeName); }
}
