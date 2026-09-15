// SPDX-License-Identifier: MS-PL

#include "SurfaceFrameFitting.hpp"

namespace CNA::Platform::Common {

    PresentRect ComputePresentRect(const PresentScaleMode mode, const int frameWidth, const int frameHeight,
                                   const int targetWidth, const int targetHeight)
    {
        PresentRect rect;
        switch (mode)
        {
            case PresentScaleMode::Stretch:
                rect = {0, 0, targetWidth, targetHeight};
                break;
            case PresentScaleMode::Letterbox:
            case PresentScaleMode::Overscan:
            {
                const double scaleX = static_cast<double>(targetWidth) / frameWidth;
                const double scaleY = static_cast<double>(targetHeight) / frameHeight;
                const double scale = mode == PresentScaleMode::Letterbox ? std::min(scaleX, scaleY)
                                                                         : std::max(scaleX, scaleY);
                rect.width = std::max(1, static_cast<int>(frameWidth * scale));
                rect.height = std::max(1, static_cast<int>(frameHeight * scale));
                rect.x = (targetWidth - rect.width) / 2;
                rect.y = (targetHeight - rect.height) / 2;
                break;
            }
            case PresentScaleMode::None:
                rect = {(targetWidth - frameWidth) / 2, (targetHeight - frameHeight) / 2, frameWidth, frameHeight};
                break;
            case PresentScaleMode::Native:
                rect = {0, 0, frameWidth, frameHeight};
                break;
        }
        return rect;
    }

} // namespace CNA::Platform::Common
