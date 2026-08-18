// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/LodGroupEXT.hpp"

#ifdef CNA_CNAEXT

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace CNA::Graphics {

    LodGroupEXT::LodGroupEXT() = default;

    void LodGroupEXT::addLevel(const float maxDistance,
                               Microsoft::Xna::Framework::Graphics::ModelMeshPart* part)
    {
        if (!(maxDistance > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::LodGroupEXT::addLevel: maxDistance must be positive");

        levels_.push_back(Level{part, maxDistance});
        sortLevels();
        lastIndex_ = -1;
    }

    void LodGroupEXT::sortLevels()
    {
        // Finest level first, always -- but "finest" reads the threshold differently in the two
        // modes: a *distance* threshold grows as detail falls, and a *pixel-size* threshold
        // shrinks. Sorting here rather than at selection keeps index 0 meaning the same thing in
        // both, which is what lets hysteresis and the returned index be mode-independent.
        const bool ascending = mode_ == LodSelectionMode::Distance;
        std::stable_sort(levels_.begin(), levels_.end(),
                         [ascending](const Level& left, const Level& right) {
                             return ascending ? left.MaxDistance < right.MaxDistance
                                              : left.MaxDistance > right.MaxDistance;
                         });
    }

    void LodGroupEXT::clear()
    {
        levels_.clear();
        lastIndex_ = -1;
    }

    const std::vector<LodGroupEXT::Level>& LodGroupEXT::getLevels() const { return levels_; }

    int LodGroupEXT::selectByDistance(const float distance) const
    {
        const auto position = std::upper_bound(
            levels_.begin(), levels_.end(), distance,
            [](const float value, const Level& level) { return value < level.MaxDistance; });
        if (position == levels_.end()) return -1;
        return static_cast<int>(position - levels_.begin());
    }

    int LodGroupEXT::selectByScreenSpaceError(const float distance) const
    {
        // The projected size falls as distance rises, so the finest level is the one whose
        // threshold the size still clears -- the list order is the same, the comparison is not.
        const float size = projectedRadiusPixels(distance);
        for (std::size_t i = 0; i < levels_.size(); ++i)
            if (size >= levels_[i].MaxDistance)
                return static_cast<int>(i);
        return -1;
    }

    int LodGroupEXT::applyHysteresis(const int candidate, const float distance) const
    {
        if (hysteresis_ <= 0.0f || lastIndex_ < 0 || candidate == lastIndex_) return candidate;
        if (lastIndex_ >= static_cast<int>(levels_.size())) return candidate;

        // Only the boundary between the remembered level and its neighbour is sticky, and only
        // within the margin: a distance that has moved several levels is a real change, not a
        // wobble, and holding it back would be worse than the flicker this prevents.
        const int step = candidate > lastIndex_ ? 1 : -1;
        if (candidate != lastIndex_ + step) return candidate;

        const std::size_t boundaryIndex =
            static_cast<std::size_t>(step > 0 ? lastIndex_ : candidate);
        if (boundaryIndex >= levels_.size()) return candidate;

        const float boundary = mode_ == LodSelectionMode::Distance
                                   ? levels_[boundaryIndex].MaxDistance
                                   : levels_[boundaryIndex].MaxDistance;
        const float value = mode_ == LodSelectionMode::Distance
                                ? distance
                                : projectedRadiusPixels(distance);
        return std::abs(value - boundary) < hysteresis_ ? lastIndex_ : candidate;
    }

    int LodGroupEXT::selectIndex(const float distance)
    {
        if (levels_.empty()) return -1;
        const float clamped = distance < 0.0f ? 0.0f : distance;
        const int candidate = mode_ == LodSelectionMode::Distance
                                  ? selectByDistance(clamped)
                                  : selectByScreenSpaceError(clamped);
        const int chosen = candidate < 0 ? candidate : applyHysteresis(candidate, clamped);
        lastIndex_ = chosen;
        return chosen;
    }

    Microsoft::Xna::Framework::Graphics::ModelMeshPart* LodGroupEXT::select(const float distance)
    {
        const int index = selectIndex(distance);
        return index < 0 ? nullptr : levels_[static_cast<std::size_t>(index)].Part;
    }

    void LodGroupEXT::setHysteresis(const float margin)
    {
        hysteresis_ = margin > 0.0f ? margin : 0.0f;
    }

    float LodGroupEXT::getHysteresis() const { return hysteresis_; }

    void LodGroupEXT::resetHysteresis() { lastIndex_ = -1; }

    LodSelectionMode LodGroupEXT::getSelectionMode() const { return mode_; }

    void LodGroupEXT::setSelectionMode(const LodSelectionMode mode)
    {
        if (mode == mode_) return;
        mode_ = mode;
        sortLevels();
        lastIndex_ = -1;
    }

    void LodGroupEXT::setScreenSpaceParameters(const float radius, const float verticalFov,
                                               const float viewportHeight)
    {
        if (!(radius > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::LodGroupEXT::setScreenSpaceParameters: radius must be positive");
        if (!(verticalFov > 0.0f) || verticalFov >= 3.14159265f)
            throw std::invalid_argument(
                "CNA::Graphics::LodGroupEXT::setScreenSpaceParameters: verticalFov must be in "
                "(0, pi)");
        if (!(viewportHeight > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::LodGroupEXT::setScreenSpaceParameters: viewportHeight must be "
                "positive");

        radius_ = radius;
        verticalFov_ = verticalFov;
        viewportHeight_ = viewportHeight;
        lastIndex_ = -1;
    }

    float LodGroupEXT::projectedRadiusPixels(const float distance) const
    {
        // At or behind the eye the projection is meaningless; the honest answer is "as large as it
        // gets", which selects the finest level rather than none.
        if (distance <= 0.0f) return std::numeric_limits<float>::max();
        const float halfExtent = 2.0f * std::tan(verticalFov_ * 0.5f) * distance;
        return radius_ * viewportHeight_ / halfExtent;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
