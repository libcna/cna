// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <algorithm>

namespace CNA::Internal::Renderers::D3DCommon
{
    /** @brief Physical presentation rectangle and its logical coordinate-space dimensions. */
    struct D3DPresentationGeometry
    {
        /** @brief Physical left edge in drawable pixels. */
        float x = 0.0f;
        /** @brief Physical top edge in drawable pixels. */
        float y = 0.0f;
        /** @brief Physical width in drawable pixels. */
        float width = 0.0f;
        /** @brief Physical height in drawable pixels. */
        float height = 0.0f;
        /** @brief Width exposed to game code. */
        float logicalWidth = 0.0f;
        /** @brief Height exposed to game code. */
        float logicalHeight = 0.0f;
    };

    /**
     * @brief Computes the common D3D11/D3D12 presentation geometry.
     *
     * @param physicalWidth Current back-buffer width in drawable pixels.
     * @param physicalHeight Current back-buffer height in drawable pixels.
     * @param virtualWidth Requested game-space width.
     * @param virtualHeight Requested game-space height.
     * @param mode Presentation scaling policy.
     * @return Physical destination rectangle and logical canvas size.
     */
    [[nodiscard]] inline D3DPresentationGeometry ComputeD3DPresentationGeometry(
        int physicalWidth, int physicalHeight, int virtualWidth, int virtualHeight,
        CnaPresentationMode mode)
    {
        D3DPresentationGeometry geometry;
        geometry.width = geometry.logicalWidth =
            static_cast<float>(std::max(0, physicalWidth));
        geometry.height = geometry.logicalHeight =
            static_cast<float>(std::max(0, physicalHeight));
        if (physicalWidth <= 0 || physicalHeight <= 0)
            return geometry;
        if (mode == CnaPresentationMode::NativeBackBuffer ||
            virtualWidth <= 0 || virtualHeight <= 0)
            return geometry;

        geometry.logicalWidth = static_cast<float>(virtualWidth);
        geometry.logicalHeight = static_cast<float>(virtualHeight);
        if (mode == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            geometry.logicalWidth = geometry.logicalHeight *
                static_cast<float>(physicalWidth) / static_cast<float>(physicalHeight);
            return geometry;
        }
        if (mode == CnaPresentationMode::Stretch)
            return geometry;

        const float scaleX = static_cast<float>(physicalWidth) / geometry.logicalWidth;
        const float scaleY = static_cast<float>(physicalHeight) / geometry.logicalHeight;
        const float scale = mode == CnaPresentationMode::Overscan
            ? std::max(scaleX, scaleY)
            : std::min(scaleX, scaleY);
        geometry.width = geometry.logicalWidth * scale;
        geometry.height = geometry.logicalHeight * scale;
        geometry.x = (static_cast<float>(physicalWidth) - geometry.width) * 0.5f;
        geometry.y = (static_cast<float>(physicalHeight) - geometry.height) * 0.5f;
        return geometry;
    }

    /**
     * @brief Maps a drawable-space point into logical game coordinates.
     *
     * @param geometry Presentation geometry to use.
     * @param drawableX Physical drawable X coordinate.
     * @param drawableY Physical drawable Y coordinate.
     * @param logicalX Receives the logical X coordinate.
     * @param logicalY Receives the logical Y coordinate.
     * @return true when the point lies inside the presented rectangle.
     */
    [[nodiscard]] inline bool MapDrawableToLogical(
        const D3DPresentationGeometry& geometry, float drawableX, float drawableY,
        float& logicalX, float& logicalY)
    {
        if (geometry.width <= 0.0f || geometry.height <= 0.0f)
            return false;
        logicalX = (drawableX - geometry.x) * geometry.logicalWidth / geometry.width;
        logicalY = (drawableY - geometry.y) * geometry.logicalHeight / geometry.height;
        return drawableX >= geometry.x && drawableX < geometry.x + geometry.width &&
               drawableY >= geometry.y && drawableY < geometry.y + geometry.height;
    }

    /**
     * @brief Maps a logical game-space point into drawable pixels.
     *
     * @param geometry Presentation geometry to use.
     * @param logicalX Logical X coordinate.
     * @param logicalY Logical Y coordinate.
     * @param drawableX Receives the physical drawable X coordinate.
     * @param drawableY Receives the physical drawable Y coordinate.
     * @return true when the geometry has a valid logical canvas.
     */
    [[nodiscard]] inline bool MapLogicalToDrawable(
        const D3DPresentationGeometry& geometry, float logicalX, float logicalY,
        float& drawableX, float& drawableY)
    {
        if (geometry.logicalWidth <= 0.0f || geometry.logicalHeight <= 0.0f)
            return false;
        drawableX = geometry.x + logicalX * geometry.width / geometry.logicalWidth;
        drawableY = geometry.y + logicalY * geometry.height / geometry.logicalHeight;
        return true;
    }
}
