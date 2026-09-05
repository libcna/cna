// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/Collections/ObjectModel/Collection.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides properties describing the extent to which a vertex is deformed by one bone.
     */
    struct BoneWeight
    {
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.BoneWeight";

        /**
         * @brief Initializes an unset bone weight: no bone name and a weight of zero.
         *
         * This is `default(BoneWeight)` in C#, which is a value type here as it is there
         * (measured, tests/reference/xna40/graphics case boneweight/default_value).
         */
        BoneWeight() = default;

        /**
         * @brief Initializes a new instance of BoneWeight.
         *
         * @param boneName The name of the bone.
         * @param weight The amount of deformation, between 0 and 1 inclusive.
         * @throws System::ArgumentNullException when the bone name is empty.
         * @throws System::ArgumentOutOfRangeException when the weight is outside [0, 1].
         */
        BoneWeight(std::string boneName, SharpRuntime::Single weight);

        /**
         * @brief Gets the name of the bone.
         *
         * @return The bone name.
         */
        [[nodiscard]] const std::string& getBoneNameProperty() const noexcept;

        /**
         * @brief Gets the amount of deformation this bone applies.
         *
         * @return The weight.
         */
        [[nodiscard]] SharpRuntime::Single getWeightProperty() const noexcept;

        /**
         * @brief Compares two bone weights by name and weight.
         *
         * @param other The bone weight to compare with.
         * @return true when both name and weight are equal.
         */
        [[nodiscard]] bool operator==(const BoneWeight& other) const noexcept;

        /**
         * @brief Compares two bone weights by name and weight.
         *
         * @param other The bone weight to compare with.
         * @return true when they differ.
         */
        [[nodiscard]] bool operator!=(const BoneWeight& other) const noexcept;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Describes the bone weight for the intermediate serializer: nothing.
         *
         * Both properties are read-only, and XNA's serializer writes neither, so a bone weight is
         * an empty `<Item />` element (measured, boneweight/serialize).
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<BoneWeight>& d);

    private:
        std::string boneName_;
        SharpRuntime::Single weight_ = 0.0f;
    };

    /**
     * @brief Provides methods for maintaining a list of bone weights.
     */
    class BoneWeightCollection final : public System::Collections::ObjectModel::Collection<BoneWeight>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.BoneWeightCollection";

        /** @brief Initializes a new instance of BoneWeightCollection. */
        BoneWeightCollection() = default;

        /**
         * @brief Sorts the weights from largest to smallest and scales them to sum to one.
         *
         * @throws Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException when the
         *         weights sum to zero.
         */
        void NormalizeWeights();

        /**
         * @brief Keeps the largest weights, sorts them from largest to smallest and scales them to
         *        sum to one.
         *
         * @param maxWeights The greatest number of weights to keep.
         * @throws System::ArgumentOutOfRangeException when @p maxWeights is not positive.
         * @throws Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException when the
         *         kept weights sum to zero.
         */
        void NormalizeWeights(SharpRuntime::intcs maxWeights);

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        CNAEXT [[nodiscard]] std::string ToString() const;

    private:
        /**
         * @brief Sorts, truncates and scales the weights; the body both overloads share.
         *
         * @param maxWeights The greatest number of weights to keep.
         */
        void Normalize(SharpRuntime::intcs maxWeights);
    };

    /**
     * @brief Provides methods for maintaining a list of vertex indices.
     */
    class IndexCollection final : public System::Collections::ObjectModel::Collection<SharpRuntime::intcs>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.IndexCollection";

        /** @brief Initializes a new instance of IndexCollection. */
        IndexCollection() = default;

        /**
         * @brief Appends every index of a sequence to the collection.
         *
         * @param indices The indices to append.
         */
        void AddRange(const std::vector<SharpRuntime::intcs>& indices);

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        CNAEXT [[nodiscard]] std::string ToString() const;
    };

    /**
     * @brief Provides methods for maintaining a list of vertex positions.
     */
    class PositionCollection final : public System::Collections::ObjectModel::Collection<Vector3>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.PositionCollection";

        /** @brief Initializes a new instance of PositionCollection. */
        PositionCollection() = default;

        /**
         * @brief Returns the full name of this type, as XNA's `ToString` does.
         *
         * @return The .NET full name.
         */
        CNAEXT [[nodiscard]] std::string ToString() const;
    };
}
