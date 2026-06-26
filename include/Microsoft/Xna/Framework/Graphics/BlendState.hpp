// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Represents a sprite and 3D blending configuration. */
    class BlendState : public GraphicsResource
    {
    public:
        /** @brief Preset: additive blending (source added on top of destination). */
        static const BlendState Additive;
        /** @brief Preset: alpha blending for premultiplied-alpha textures. */
        static const BlendState AlphaBlend;
        /** @brief Preset: alpha blending for non-premultiplied-alpha textures. */
        static const BlendState NonPremultiplied;
        /** @brief Preset: opaque blending (source overwrites destination). */
        static const BlendState Opaque;

        /** @brief Creates a BlendState with XNA-compatible default values. */
        BlendState();

        /** @brief Returns the fully-qualified .NET type name of this object. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Gets the blend function for the alpha channel.
         * @return Current alpha blend function.
         */
        [[nodiscard]] BlendFunction getAlphaBlendFunctionProperty() const;
        /**
         * @brief Sets the blend function for the alpha channel.
         * @param value New alpha blend function.
         */
        void setAlphaBlendFunctionProperty(BlendFunction value);

        /**
         * @brief Gets the destination blend factor for the alpha channel.
         * @return Current destination alpha blend factor.
         */
        [[nodiscard]] Blend getAlphaDestinationBlendProperty() const;
        /**
         * @brief Sets the destination blend factor for the alpha channel.
         * @param value New destination alpha blend factor.
         */
        void setAlphaDestinationBlendProperty(Blend value);

        /**
         * @brief Gets the source blend factor for the alpha channel.
         * @return Current source alpha blend factor.
         */
        [[nodiscard]] Blend getAlphaSourceBlendProperty() const;
        /**
         * @brief Sets the source blend factor for the alpha channel.
         * @param value New source alpha blend factor.
         */
        void setAlphaSourceBlendProperty(Blend value);

        /**
         * @brief Gets the blend function for the color channels.
         * @return Current color blend function.
         */
        [[nodiscard]] BlendFunction getColorBlendFunctionProperty() const;
        /**
         * @brief Sets the blend function for the color channels.
         * @param value New color blend function.
         */
        void setColorBlendFunctionProperty(BlendFunction value);

        /**
         * @brief Gets the destination blend factor for the color channels.
         * @return Current destination color blend factor.
         */
        [[nodiscard]] Blend getColorDestinationBlendProperty() const;
        /**
         * @brief Sets the destination blend factor for the color channels.
         * @param value New destination color blend factor.
         */
        void setColorDestinationBlendProperty(Blend value);

        /**
         * @brief Gets the source blend factor for the color channels.
         * @return Current source color blend factor.
         */
        [[nodiscard]] Blend getColorSourceBlendProperty() const;
        /**
         * @brief Sets the source blend factor for the color channels.
         * @param value New source color blend factor.
         */
        void setColorSourceBlendProperty(Blend value);

        /**
         * @brief Gets the color write mask for render target 0.
         * @return Current color write channels for target 0.
         */
        [[nodiscard]] ColorWriteChannels getColorWriteChannelsProperty() const;
        /**
         * @brief Sets the color write mask for render target 0.
         * @param value New color write channels for target 0.
         */
        void setColorWriteChannelsProperty(ColorWriteChannels value);

        /**
         * @brief Gets the color write mask for render target 1.
         * @return Current color write channels for target 1.
         */
        [[nodiscard]] ColorWriteChannels getColorWriteChannels1Property() const;
        /**
         * @brief Sets the color write mask for render target 1.
         * @param value New color write channels for target 1.
         */
        void setColorWriteChannels1Property(ColorWriteChannels value);

        /**
         * @brief Gets the color write mask for render target 2.
         * @return Current color write channels for target 2.
         */
        [[nodiscard]] ColorWriteChannels getColorWriteChannels2Property() const;
        /**
         * @brief Sets the color write mask for render target 2.
         * @param value New color write channels for target 2.
         */
        void setColorWriteChannels2Property(ColorWriteChannels value);

        /**
         * @brief Gets the color write mask for render target 3.
         * @return Current color write channels for target 3.
         */
        [[nodiscard]] ColorWriteChannels getColorWriteChannels3Property() const;
        /**
         * @brief Sets the color write mask for render target 3.
         * @param value New color write channels for target 3.
         */
        void setColorWriteChannels3Property(ColorWriteChannels value);

        /**
         * @brief Gets the four-component blend factor used when BlendFactor or InverseBlendFactor is specified.
         * @return Current blend factor color.
         */
        [[nodiscard]] Color getBlendFactorProperty() const;
        /**
         * @brief Sets the four-component blend factor.
         * @param value New blend factor color.
         */
        void setBlendFactorProperty(const Color& value);

        /**
         * @brief Gets the multisample mask bitmask.
         * @return Current multisample mask.
         */
        [[nodiscard]] int getMultiSampleMaskProperty() const;
        /**
         * @brief Sets the multisample mask bitmask.
         * @param value New multisample mask.
         */
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
