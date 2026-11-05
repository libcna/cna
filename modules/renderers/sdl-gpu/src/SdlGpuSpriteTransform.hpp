// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <array>

namespace CNA::Internal::Renderers::SdlGpu::Detail
{
    // Kept renderer-local so its arithmetic can be checked without creating an SDL GPU device.
    inline std::array<float, 4> TransformSpritePositionEXT(
        float x, float y, float depth,
        const Microsoft::Xna::Framework::Matrix& transform, bool rawCompiledPosition)
    {
        // Refresh RenderBatch IL_0022-0055 applies the custom pass after the stock VS;
        // FNA SpriteBatch.cs:1369-1450 supplies raw XYZ to either vertex shader.
        if (rawCompiledPosition)
            return {x, y, depth, 1.0f};

        // Refresh SetRenderState IL_0176-0188 retains all of T*P. Keep XYZW intact for
        // hardware clipping and reciprocal-W interpolation, including zero/negative W.
        return {x * transform.M11 + y * transform.M21 + depth * transform.M31 + transform.M41,
                x * transform.M12 + y * transform.M22 + depth * transform.M32 + transform.M42,
                x * transform.M13 + y * transform.M23 + depth * transform.M33 + transform.M43,
                x * transform.M14 + y * transform.M24 + depth * transform.M34 + transform.M44};
    }
}
