// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <memory>

namespace CNA::Internal::Renderers::Rlgl
{
    class RlglRenderer;

    /**
     * @brief Creates the current RLGL two-dimensional texture implementation.
     * @param data Dimensions, format, mip count, and level-zero bytes.
     * @return Renderer-owned texture record.
     */
    [[nodiscard]] std::unique_ptr<ITextureRenderer> CreateTextureRenderer(
        const CNA::Internal::Graphics::ImageData& data);

    /**
     * @brief Creates the production low-level SpriteBatch implementation.
     * @param renderer Owning device used for viewport and sampler application.
     * @return Renderer-owned sprite scheduler.
     */
    [[nodiscard]] std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatchRenderer(
        RlglRenderer& renderer);
}
