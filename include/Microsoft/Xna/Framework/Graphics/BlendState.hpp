// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Represents a sprite and 3D blending configuration.
    class BlendState
    {
    public:
        static const BlendState Additive;
        static const BlendState AlphaBlend;
        static const BlendState NonPremultiplied;
        static const BlendState Opaque;

        BlendState();

        [[nodiscard]] BlendFunction getAlphaBlendFunctionProperty() const;
        void setAlphaBlendFunctionProperty(BlendFunction value);

        [[nodiscard]] Blend getAlphaDestinationBlendProperty() const;
        void setAlphaDestinationBlendProperty(Blend value);

        [[nodiscard]] Blend getAlphaSourceBlendProperty() const;
        void setAlphaSourceBlendProperty(Blend value);

        [[nodiscard]] BlendFunction getColorBlendFunctionProperty() const;
        void setColorBlendFunctionProperty(BlendFunction value);

        [[nodiscard]] Blend getColorDestinationBlendProperty() const;
        void setColorDestinationBlendProperty(Blend value);

        [[nodiscard]] Blend getColorSourceBlendProperty() const;
        void setColorSourceBlendProperty(Blend value);

        [[nodiscard]] ColorWriteChannels getColorWriteChannelsProperty() const;
        void setColorWriteChannelsProperty(ColorWriteChannels value);

        [[nodiscard]] ColorWriteChannels getColorWriteChannels1Property() const;
        void setColorWriteChannels1Property(ColorWriteChannels value);

        [[nodiscard]] ColorWriteChannels getColorWriteChannels2Property() const;
        void setColorWriteChannels2Property(ColorWriteChannels value);

        [[nodiscard]] ColorWriteChannels getColorWriteChannels3Property() const;
        void setColorWriteChannels3Property(ColorWriteChannels value);

        [[nodiscard]] Color getBlendFactorProperty() const;
        void setBlendFactorProperty(const Color& value);

        [[nodiscard]] int getMultiSampleMaskProperty() const;
        void setMultiSampleMaskProperty(int value);

    private:
        BlendState(Blend colorSrc, Blend alphaSrc, Blend colorDst, Blend alphaDst);

        BlendFunction alphaBlendFunction_;
        Blend alphaDestinationBlend_;
        Blend alphaSourceBlend_;
        BlendFunction colorBlendFunction_;
        Blend colorDestinationBlend_;
        Blend colorSourceBlend_;
        ColorWriteChannels colorWriteChannels_;
        ColorWriteChannels colorWriteChannels1_;
        ColorWriteChannels colorWriteChannels2_;
        ColorWriteChannels colorWriteChannels3_;
        Color blendFactor_;
        int multiSampleMask_;
    };
}
