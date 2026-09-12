// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <memory>

namespace CNA::Internal::Renderers::Rlgl
{
    /**
     * @brief Creates the current RLGL two-dimensional texture implementation.
     * @param data Dimensions, format, mip count, and level-zero bytes.
     * @return Renderer-owned texture record.
     */
    [[nodiscard]] std::unique_ptr<ITextureRenderer> CreateTextureRenderer(
        const CNA::Internal::Graphics::ImageData& data);
}
