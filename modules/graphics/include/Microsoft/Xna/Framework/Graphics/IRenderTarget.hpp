// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a render target surface. */
    class IRenderTarget
    {
    public:
        /** @brief Virtual destructor. */
        NOXNA virtual ~IRenderTarget() = default;

        /** @brief Returns the width of the render target in pixels. */
        [[nodiscard]] virtual int getWidthProperty() const = 0;
        /** @brief Returns the height of the render target in pixels. */
        [[nodiscard]] virtual int getHeightProperty() const = 0;
        /** @brief Returns the number of mip levels in the render target. */
        [[nodiscard]] virtual int getLevelCountProperty() const = 0;
        /** @brief Returns the usage mode of this render target. */
        [[nodiscard]] virtual RenderTargetUsage getRenderTargetUsageProperty() const = 0;
        /** @brief Returns the depth-stencil buffer format of this render target. */
        [[nodiscard]] virtual DepthFormat getDepthStencilFormatProperty() const = 0;
        /** @brief Returns the number of multisample locations. */
        [[nodiscard]] virtual int getMultiSampleCountProperty() const = 0;
    };
}
