// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <array>

namespace CNA::Internal::Renderers::WebGPU
{
    /**
     * @brief Resolves a rotated sprite corner before projection or effect-owned vertex processing.
     * @param x Rotated, destination-offset X.
     * @param y Rotated, destination-offset Y.
     * @param depth Submitted layer depth.
     * @param transform SpriteBatch Begin matrix.
     * @param effectOwnsPosition Whether a compiled pass consumes raw sprite coordinates.
     * @return Homogeneous XYZW, without division or clipping.
     */
    [[nodiscard]] std::array<float, 4> ResolveSpritePositionEXT(
        float x, float y, float depth, const Microsoft::Xna::Framework::Matrix& transform,
        bool effectOwnsPosition);

    /**
     * @brief Projects homogeneous sprite coordinates into viewport-local clip space.
     * @param position Homogeneous pixel XYZW.
     * @param width Logical projection width.
     * @param height Logical projection height.
     * @return Clip XYZW for hardware clipping and perspective interpolation.
     */
    [[nodiscard]] std::array<float, 4> ProjectSpritePositionEXT(
        const std::array<float, 4>& position, float width, float height);

    /**
     * @brief Reconstructs homogeneous pixel coordinates for a descriptor sprite vertex program.
     * @param clip Clip XYZW.
     * @param width Logical projection width.
     * @param height Logical projection height.
     * @return Homogeneous pixel XYZW, including zero or negative W.
     */
    [[nodiscard]] std::array<float, 4> DescriptorSpritePositionEXT(
        const std::array<float, 4>& clip, float width, float height);

    /**
     * @brief Builds the stock vertex-stage matrix inherited by pixel-only compiled effects.
     * @param transform SpriteBatch Begin matrix.
     * @param width Logical projection width.
     * @param height Logical projection height.
     * @return Begin matrix multiplied by the sprite projection.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Matrix SpriteMatrixTransformEXT(
        const Microsoft::Xna::Framework::Matrix& transform, int width, int height);
}
