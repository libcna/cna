// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines rasterizer state for the graphics pipeline.
    class RasterizerState
    {
    public:
        static const RasterizerState CullClockwise;
        static const RasterizerState CullCounterClockwise;
        static const RasterizerState CullNone;

        RasterizerState();

        [[nodiscard]] CullMode getCullModeProperty() const;
        void setCullModeProperty(CullMode value);

        [[nodiscard]] float getDepthBiasProperty() const;
        void setDepthBiasProperty(float value);

        [[nodiscard]] FillMode getFillModeProperty() const;
        void setFillModeProperty(FillMode value);

        [[nodiscard]] bool getMultiSampleAntiAliasProperty() const;
        void setMultiSampleAntiAliasProperty(bool value);

        [[nodiscard]] bool getScissorTestEnableProperty() const;
        void setScissorTestEnableProperty(bool value);

        [[nodiscard]] float getSlopeScaleDepthBiasProperty() const;
        void setSlopeScaleDepthBiasProperty(float value);

    private:
        explicit RasterizerState(CullMode cullMode);

        CullMode cullMode_;
        float depthBias_;
        FillMode fillMode_;
        bool multiSampleAntiAlias_;
        bool scissorTestEnable_;
        float slopeScaleDepthBias_;
    };
}
