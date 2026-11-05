// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/WebGPU/WebGPUSpritePosition.hpp"

namespace CNA::Internal::Renderers::WebGPU
{
    using Microsoft::Xna::Framework::Matrix;

    std::array<float, 4> ResolveSpritePositionEXT(
        float x, float y, float depth, const Matrix& transform, bool effectOwnsPosition)
    {
        // Refresh SpriteBatch.SetRenderState IL_0177-0188 retains the full matrix;
        // RenderBatch IL_0022-0055 applies custom passes after the internal stock pass.
        if (effectOwnsPosition)
            return {x, y, depth, 1.0f};
        return {
            x * transform.M11 + y * transform.M21 + depth * transform.M31 + transform.M41,
            x * transform.M12 + y * transform.M22 + depth * transform.M32 + transform.M42,
            x * transform.M13 + y * transform.M23 + depth * transform.M33 + transform.M43,
            x * transform.M14 + y * transform.M24 + depth * transform.M34 + transform.M44};
    }

    std::array<float, 4> ProjectSpritePositionEXT(
        const std::array<float, 4>& position, float width, float height)
    {
        // Keep the existing native pixel-center convention. Offsets scale by W;
        // hardware must retain the reciprocal W used for interpolation and clipping.
        return {2.0f * position[0] / width - position[3],
                position[3] - 2.0f * position[1] / height, position[2], position[3]};
    }

    std::array<float, 4> DescriptorSpritePositionEXT(
        const std::array<float, 4>& clip, float width, float height)
    {
        return {(clip[0] + clip[3]) * 0.5f * width,
                (clip[3] - clip[1]) * 0.5f * height, clip[2], clip[3]};
    }

    Matrix SpriteMatrixTransformEXT(const Matrix& transform, int width, int height)
    {
        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f, -1.0f);
        return transform * projection;
    }
}
