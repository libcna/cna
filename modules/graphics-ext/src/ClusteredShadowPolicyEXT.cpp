// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/ClusteredShadowPolicyEXT.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::BoundingFrustum;
    using Microsoft::Xna::Framework::BoundingSphere;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    namespace {

        /// Rec. 709 luminance: a green light and a blue one of the same numeric intensity do not
        /// carry the same weight in a picture, and the budget should follow the picture.
        float Luminance(const Vector3& colour)
        {
            return 0.2126f * colour.X + 0.7152f * colour.Y + 0.0722f * colour.Z;
        }

        /// The same windowed inverse square ClusteredForwardEffect shades with, so the ranking and
        /// the image agree about which light is brightest.
        float Falloff(const float distance, const float range)
        {
            if (distance >= range) return 0.0f;
            const float ratio = distance / std::max(range, 1e-4f);
            const float window = std::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
            return window * window / std::max(distance * distance, 1e-4f);
        }

    } // namespace

    ClusteredShadowPolicyEXT::ClusteredShadowPolicyEXT(const int budget) : budget_(budget)
    {
        if (budget < 0)
            throw std::invalid_argument(
                "CNA::Graphics::ClusteredShadowPolicyEXT: the budget cannot be negative; zero is "
                "the way to ask for no shadows at all");
    }

    int  ClusteredShadowPolicyEXT::getBudget() const { return budget_; }
    void ClusteredShadowPolicyEXT::setBudget(const int value)
    {
        if (value >= 0) budget_ = value;
    }

    float ClusteredShadowPolicyEXT::getHysteresis() const { return hysteresis_; }
    void  ClusteredShadowPolicyEXT::setHysteresis(const float value)
    {
        if (value >= 1.0f) hysteresis_ = value;
    }

    void ClusteredShadowPolicyEXT::reset()
    {
        selected_.clear();
        scores_.clear();
        requestCount_ = 0;
    }

    void ClusteredShadowPolicyEXT::select(const ClusteredLightSetEXT& lights, const Matrix& view,
                                          const Matrix& projection, const Vector3& cameraPosition)
    {
        const std::vector<int> incumbents = selected_;
        const BoundingFrustum frustum(view * projection);

        scores_.assign(static_cast<std::size_t>(lights.getCount()), 0.0f);
        requestCount_ = 0;

        for (int index = 0; index < lights.getCount(); ++index)
        {
            const ClusteredLightEXT& light = lights.getAt(index);
            if (!light.CastsShadows) continue;
            ++requestCount_;

            const BoundingSphere bounds = lights.getBoundsAt(index);
            // A shadow nobody can see is the cheapest thing to drop, and it is dropped by scoring
            // zero rather than by being removed, so getScore can still explain why it lost.
            if (!frustum.Intersects(bounds)) continue;

            const float dx = light.Position.X - cameraPosition.X;
            const float dy = light.Position.Y - cameraPosition.Y;
            const float dz = light.Position.Z - cameraPosition.Z;
            const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

            // At the camera's own position the falloff diverges; a light the camera is standing
            // inside is as important as a light can be, so it takes the value at one unit rather
            // than an infinity that would make every comparison meaningless.
            const float reach = Falloff(std::max(distance, 1.0f), light.Range);
            scores_[static_cast<std::size_t>(index)] =
                Luminance(light.Color) * light.Intensity * reach;
        }

        std::vector<int> candidates;
        for (int index = 0; index < lights.getCount(); ++index)
            if (scores_[static_cast<std::size_t>(index)] > 0.0f) candidates.push_back(index);

        // The stickiness is applied as a bonus to the incumbent's score rather than as a special
        // case in the comparison, which keeps the ordering a total order -- a rule of the form
        // "unless the challenger beats it by X" applied pairwise is not transitive, and a sort
        // given an intransitive comparator is entitled to do anything at all.
        std::vector<float> effective(scores_);
        for (const int index : incumbents)
            if (index < static_cast<int>(effective.size()))
                effective[static_cast<std::size_t>(index)] *= hysteresis_;

        std::stable_sort(candidates.begin(), candidates.end(),
                         [&effective](const int a, const int b) {
                             return effective[static_cast<std::size_t>(a)] >
                                    effective[static_cast<std::size_t>(b)];
                         });

        if (static_cast<int>(candidates.size()) > budget_)
            candidates.resize(static_cast<std::size_t>(budget_));
        selected_ = std::move(candidates);
    }

    const std::vector<int>& ClusteredShadowPolicyEXT::getSelected() const { return selected_; }

    bool ClusteredShadowPolicyEXT::isSelected(const int lightIndex) const
    {
        return std::find(selected_.begin(), selected_.end(), lightIndex) != selected_.end();
    }

    float ClusteredShadowPolicyEXT::getScore(const int lightIndex) const
    {
        if (lightIndex < 0 || lightIndex >= static_cast<int>(scores_.size()))
            throw std::out_of_range(
                "CNA::Graphics::ClusteredShadowPolicyEXT::getScore: no light of that index was in "
                "the last selection");
        return scores_[static_cast<std::size_t>(lightIndex)];
    }

    int ClusteredShadowPolicyEXT::getRequestCount() const { return requestCount_; }

    int ClusteredShadowPolicyEXT::getRefusedCount() const
    {
        return std::max(0, requestCount_ - static_cast<int>(selected_.size()));
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
