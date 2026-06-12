// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines sampler state for texture sampling.
    class SamplerState
    {
    public:
        static const SamplerState AnisotropicClamp;
        static const SamplerState AnisotropicWrap;
        static const SamplerState LinearClamp;
        static const SamplerState LinearWrap;
        static const SamplerState PointClamp;
        static const SamplerState PointWrap;

        SamplerState();

        [[nodiscard]] TextureAddressMode getAddressUProperty() const;
        void setAddressUProperty(TextureAddressMode value);

        [[nodiscard]] TextureAddressMode getAddressVProperty() const;
        void setAddressVProperty(TextureAddressMode value);

        [[nodiscard]] TextureAddressMode getAddressWProperty() const;
        void setAddressWProperty(TextureAddressMode value);

        [[nodiscard]] TextureFilter getFilterProperty() const;
        void setFilterProperty(TextureFilter value);

        [[nodiscard]] int getMaxAnisotropyProperty() const;
        void setMaxAnisotropyProperty(int value);

        [[nodiscard]] int getMaxMipLevelProperty() const;
        void setMaxMipLevelProperty(int value);

        [[nodiscard]] float getMipMapLevelOfDetailBiasProperty() const;
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
