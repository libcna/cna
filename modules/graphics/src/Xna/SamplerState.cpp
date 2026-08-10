// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const SamplerState SamplerState::AnisotropicClamp{"SamplerState.AnisotropicClamp", TextureFilter::Anisotropic, TextureAddressMode::Clamp,  TextureAddressMode::Clamp,  TextureAddressMode::Clamp};
    const SamplerState SamplerState::AnisotropicWrap {"SamplerState.AnisotropicWrap",  TextureFilter::Anisotropic, TextureAddressMode::Wrap,   TextureAddressMode::Wrap,   TextureAddressMode::Wrap};
    const SamplerState SamplerState::LinearClamp     {"SamplerState.LinearClamp",      TextureFilter::Linear,      TextureAddressMode::Clamp,  TextureAddressMode::Clamp,  TextureAddressMode::Clamp};
    const SamplerState SamplerState::LinearWrap      {"SamplerState.LinearWrap",       TextureFilter::Linear,      TextureAddressMode::Wrap,   TextureAddressMode::Wrap,   TextureAddressMode::Wrap};
    const SamplerState SamplerState::PointClamp      {"SamplerState.PointClamp",       TextureFilter::Point,       TextureAddressMode::Clamp,  TextureAddressMode::Clamp,  TextureAddressMode::Clamp};
    const SamplerState SamplerState::PointWrap       {"SamplerState.PointWrap",        TextureFilter::Point,       TextureAddressMode::Wrap,   TextureAddressMode::Wrap,   TextureAddressMode::Wrap};

    SamplerState::SamplerState()
        : addressU_(TextureAddressMode::Wrap)
        , addressV_(TextureAddressMode::Wrap)
        , addressW_(TextureAddressMode::Wrap)
        , filter_(TextureFilter::Linear)
        , maxAnisotropy_(4)
        , maxMipLevel_(0)
        , mipMapLevelOfDetailBias_(0.0f)
    {
    }

    SamplerState::SamplerState(const std::string& name,
                               TextureFilter filter,
                               TextureAddressMode addressU,
                               TextureAddressMode addressV,
                               TextureAddressMode addressW)
        : SamplerState()
    {
        setNameProperty(name);
        filter_   = filter;
        addressU_ = addressU;
        addressV_ = addressV;
        addressW_ = addressW;
    }

    TextureAddressMode SamplerState::getAddressUProperty() const { return addressU_; }
    void SamplerState::setAddressUProperty(TextureAddressMode v) { addressU_ = v; }

    TextureAddressMode SamplerState::getAddressVProperty() const { return addressV_; }
    void SamplerState::setAddressVProperty(TextureAddressMode v) { addressV_ = v; }

    TextureAddressMode SamplerState::getAddressWProperty() const { return addressW_; }
    void SamplerState::setAddressWProperty(TextureAddressMode v) { addressW_ = v; }

    TextureFilter SamplerState::getFilterProperty() const { return filter_; }
    void SamplerState::setFilterProperty(TextureFilter v) { filter_ = v; }

    int SamplerState::getMaxAnisotropyProperty() const { return maxAnisotropy_; }
    void SamplerState::setMaxAnisotropyProperty(int v) { maxAnisotropy_ = v; }

    int SamplerState::getMaxMipLevelProperty() const { return maxMipLevel_; }
    void SamplerState::setMaxMipLevelProperty(int v) { maxMipLevel_ = v; }

    float SamplerState::getMipMapLevelOfDetailBiasProperty() const { return mipMapLevelOfDetailBias_; }
    void SamplerState::setMipMapLevelOfDetailBiasProperty(float v) { mipMapLevelOfDetailBias_ = v; }

    GetTypeNameCPP(SamplerState, "Microsoft.Xna.Framework.Graphics.SamplerState")
}
