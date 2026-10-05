// SPDX-License-Identifier: MS-PL

#include "VirtualMouseWarp.hpp"

#include <cmath>
#include <limits>

namespace CNA::Platform::Sdl3::Detail {

    namespace {
        int SaturatingCoordinate(const int target, const float delta)
        {
            const double value = static_cast<double>(target) + static_cast<double>(delta);
            if (!std::isfinite(value))
                return target;
            if (value <= static_cast<double>(std::numeric_limits<int>::min()))
                return std::numeric_limits<int>::min();
            if (value >= static_cast<double>(std::numeric_limits<int>::max()))
                return std::numeric_limits<int>::max();
            return static_cast<int>(value);
        }
    }

    void VirtualMouseWarp::Anchor(const float rawX, const float rawY,
                                  const int targetX, const int targetY)
    {
        rawAnchorX_ = rawX;
        rawAnchorY_ = rawY;
        targetX_ = targetX;
        targetY_ = targetY;
        active_ = true;
    }

    void VirtualMouseWarp::Apply(const float rawX, const float rawY, int& x, int& y) const
    {
        if (!active_)
        {
            x = SaturatingCoordinate(0, rawX);
            y = SaturatingCoordinate(0, rawY);
            return;
        }

        x = SaturatingCoordinate(targetX_, rawX - rawAnchorX_);
        y = SaturatingCoordinate(targetY_, rawY - rawAnchorY_);
    }

    void VirtualMouseWarp::Reset() { active_ = false; }

    bool VirtualMouseWarp::IsActive() const { return active_; }

} // namespace CNA::Platform::Sdl3::Detail
