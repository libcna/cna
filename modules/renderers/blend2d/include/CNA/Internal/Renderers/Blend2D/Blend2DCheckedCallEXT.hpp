#pragma once

#include <blend2d/blend2d.h>

#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Blend2D
{
    /**
     * @brief CNAEXT. Throws std::runtime_error with @p what plus Blend2D's own error message when
     * @p result is not BL_SUCCESS; returns normally otherwise.
     *
     * Blend2D reports failure through BLResult return codes, not exceptions -- every BLContext/
     * BLImage call in this renderer that can genuinely fail (a non-finite transform, an
     * out-of-bounds image area, an allocation failure, an unattached context) must route through
     * this helper rather than discarding the result, so CNA never reports a draw/clear/transform
     * as having succeeded when Blend2D actually rejected it.
     *
     * @param result The BLResult returned by the Blend2D call just made.
     * @param what   Short description of the failed operation, used in the thrown message.
     */
    inline void Blend2DCheckEXT(BLResult result, const char* what)
    {
        if (result != BL_SUCCESS)
        {
            throw std::runtime_error(std::string("Blend2D: ") + what + " failed (BLResult=" +
                                     std::to_string(result) + ").");
        }
    }
} // namespace CNA::Internal::Renderers::Blend2D
