// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexCollections.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    BoneWeight::BoneWeight(std::string boneName, SharpRuntime::Single weight)
    {
        // C++ has no null std::string, so the empty name carries both refusals XNA gives
        // (measured, boneweight/null_name and boneweight/empty_name).
        if (boneName.empty())
        {
            throw System::ArgumentNullException("boneName");
        }
        // The accepted range is [0, 1]; a NaN weight passes, because the guard is a pair of
        // comparisons and neither is true of NaN (measured, boneweight/weight_range).
        if (weight < 0.0f || weight > 1.0f)
        {
            throw System::ArgumentOutOfRangeException("weight");
        }
        boneName_ = std::move(boneName);
        weight_ = weight;
    }

    const std::string& BoneWeight::getBoneNameProperty() const noexcept { return boneName_; }

    SharpRuntime::Single BoneWeight::getWeightProperty() const noexcept { return weight_; }

    bool BoneWeight::operator==(const BoneWeight& other) const noexcept
    {
        return boneName_ == other.boneName_ && weight_ == other.weight_;
    }

    bool BoneWeight::operator!=(const BoneWeight& other) const noexcept { return !(*this == other); }

    std::string BoneWeight::ToString() const { return std::string(XnaTypeName); }

    void BoneWeight::DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<BoneWeight>& d)
    {
        (void)d;
    }

    void BoneWeightCollection::NormalizeWeights()
    {
        // Not `NormalizeWeights(Count)`: an empty collection refuses with the normalization
        // message, not with the maxWeights range check (measured,
        // boneweight/collection_normalize_empty).
        Normalize(getCountProperty());
    }

    void BoneWeightCollection::NormalizeWeights(SharpRuntime::intcs maxWeights)
    {
        if (maxWeights <= 0)
        {
            throw System::ArgumentOutOfRangeException("maxWeights");
        }
        Normalize(maxWeights);
    }

    void BoneWeightCollection::Normalize(SharpRuntime::intcs maxWeights)
    {
        std::vector<BoneWeight> weights;
        weights.reserve(static_cast<std::size_t>(getCountProperty()));
        for (SharpRuntime::intcs i = 0; i < getCountProperty(); ++i)
        {
            weights.push_back(static_cast<const Collection&>(*this)[i]);
        }
        // Largest first, keeping the order of equal weights, then the smallest ones dropped
        // (measured, boneweight/collection_normalize_ties).
        std::stable_sort(weights.begin(), weights.end(), [](const BoneWeight& left, const BoneWeight& right)
                         { return left.getWeightProperty() > right.getWeightProperty(); });
        // XNA's NormalizeWeights(int) IL drops every weight <= 0 and every weight past the first
        // maxWeights it keeps in one pass, summing the rest; a zero weight neither survives nor
        // takes one of the maxWeights places (museum wk-animation).
        std::vector<BoneWeight> kept;
        kept.reserve(weights.size());
        SharpRuntime::Single total = 0.0f;
        for (const BoneWeight& weight : weights)
        {
            if (weight.getWeightProperty() <= 0.0f || kept.size() >= static_cast<std::size_t>(maxWeights))
            {
                continue;
            }
            total += weight.getWeightProperty();
            kept.push_back(weight);
        }
        if (total == 0.0f)
        {
            throw InvalidContentException(
                "Error normalizing vertex bone weights. BoneWeightCollection does not contain any weighting values.");
        }
        // The IL scales by one reciprocal rather than dividing each weight by the total, which can
        // differ in the last bit.
        const SharpRuntime::Single scale = 1.0f / total;
        ClearItems();
        for (const BoneWeight& weight : kept)
        {
            Add(BoneWeight(weight.getBoneNameProperty(), weight.getWeightProperty() * scale));
        }
    }

    std::string BoneWeightCollection::ToString() const { return std::string(XnaTypeName); }

    void IndexCollection::AddRange(const std::vector<SharpRuntime::intcs>& indices)
    {
        for (const SharpRuntime::intcs index : indices)
        {
            Add(index);
        }
    }

    std::string IndexCollection::ToString() const { return std::string(XnaTypeName); }

    std::string PositionCollection::ToString() const { return std::string(XnaTypeName); }
}

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    bool BoneWeightCollection::operator==(const BoneWeightCollection& other) const noexcept
    {
        if (getCountProperty() != other.getCountProperty())
        {
            return false;
        }
        for (SharpRuntime::intcs i = 0; i < getCountProperty(); ++i)
        {
            if (!((*this)[i] == other[i]))
            {
                return false;
            }
        }
        return true;
    }
}
