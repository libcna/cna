// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines sampler state for texture sampling. */
    class SamplerState : public GraphicsResource
    {
    public:
        /** @brief Preset: anisotropic filtering with clamp addressing on all axes. */
        static const SamplerState AnisotropicClamp;
        /** @brief Preset: anisotropic filtering with wrap addressing on all axes. */
        static const SamplerState AnisotropicWrap;
        /** @brief Preset: linear filtering with clamp addressing on all axes. */
        static const SamplerState LinearClamp;
        /** @brief Preset: linear filtering with wrap addressing on all axes. */
        static const SamplerState LinearWrap;
        /** @brief Preset: point filtering with clamp addressing on all axes. */
        static const SamplerState PointClamp;
        /** @brief Preset: point filtering with wrap addressing on all axes. */
        static const SamplerState PointWrap;

        /** @brief Creates a SamplerState with XNA-compatible default values. */
        SamplerState();

        /** @brief Returns the fully-qualified .NET type name of this object. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Gets the texture address mode for the U (horizontal) coordinate.
         * @return Current U address mode.
         */
        [[nodiscard]] TextureAddressMode getAddressUProperty() const;
        /**
         * @brief Sets the texture address mode for the U (horizontal) coordinate.
         * @param value New U address mode.
         */
        void setAddressUProperty(TextureAddressMode value);

        /**
         * @brief Gets the texture address mode for the V (vertical) coordinate.
         * @return Current V address mode.
         */
        [[nodiscard]] TextureAddressMode getAddressVProperty() const;
        /**
         * @brief Sets the texture address mode for the V (vertical) coordinate.
         * @param value New V address mode.
         */
        void setAddressVProperty(TextureAddressMode value);

        /**
         * @brief Gets the texture address mode for the W (depth) coordinate.
         * @return Current W address mode.
         */
        [[nodiscard]] TextureAddressMode getAddressWProperty() const;
        /**
         * @brief Sets the texture address mode for the W (depth) coordinate.
         * @param value New W address mode.
         */
        void setAddressWProperty(TextureAddressMode value);

        /**
         * @brief Gets the texture filtering mode.
         * @return Current texture filter.
         */
        [[nodiscard]] TextureFilter getFilterProperty() const;
        /**
         * @brief Sets the texture filtering mode.
         * @param value New texture filter.
         */
        void setFilterProperty(TextureFilter value);

        /**
         * @brief Gets the maximum anisotropy level used for anisotropic filtering.
         * @return Current maximum anisotropy.
         */
        [[nodiscard]] int getMaxAnisotropyProperty() const;
        /**
         * @brief Sets the maximum anisotropy level used for anisotropic filtering.
         * @param value New maximum anisotropy.
         */
        void setMaxAnisotropyProperty(int value);

        /**
         * @brief Gets the maximum mipmap level index.
         * @return Current maximum mipmap level.
         */
        [[nodiscard]] int getMaxMipLevelProperty() const;
        /**
         * @brief Sets the maximum mipmap level index.
         * @param value New maximum mipmap level.
         */
        void setMaxMipLevelProperty(int value);

        /**
         * @brief Gets the mipmap level-of-detail bias.
         * @return Current mipmap LOD bias.
         */
        [[nodiscard]] float getMipMapLevelOfDetailBiasProperty() const;
        /**
         * @brief Sets the mipmap level-of-detail bias.
         * @param value New mipmap LOD bias.
         */
        void setMipMapLevelOfDetailBiasProperty(float value);

    private:
        SamplerState(TextureFilter filter,
                     TextureAddressMode addressU,
                     TextureAddressMode addressV,
                     TextureAddressMode addressW);

        TextureAddressMode addressU_;
        TextureAddressMode addressV_;
        TextureAddressMode addressW_;
        TextureFilter filter_;
        int maxAnisotropy_;
        int maxMipLevel_;
        float mipMapLevelOfDetailBias_;
    };
}
