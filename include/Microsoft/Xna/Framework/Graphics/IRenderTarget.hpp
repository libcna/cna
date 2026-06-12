// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Represents a render target surface.
    class IRenderTarget
    {
    public:
        virtual ~IRenderTarget() = default;

        [[nodiscard]] virtual int getWidthProperty() const = 0;
        [[nodiscard]] virtual int getHeightProperty() const = 0;
        [[nodiscard]] virtual int getLevelCountProperty() const = 0;
        [[nodiscard]] virtual RenderTargetUsage getRenderTargetUsageProperty() const = 0;
        [[nodiscard]] virtual DepthFormat getDepthStencilFormatProperty() const = 0;
        [[nodiscard]] virtual int getMultiSampleCountProperty() const = 0;
    };
}
