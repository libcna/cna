// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"

#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const SamplerState SamplerState::AnisotropicClamp{"SamplerState.AnisotropicClamp", TextureFilter::Anisotropic, TextureAddressMode::Clamp,  TextureAddressMode::Clamp,  TextureAddressMode::Clamp};
    const SamplerState SamplerState::AnisotropicWrap {"SamplerState.AnisotropicWrap",  TextureFilter::Anisotropic, TextureAddressMode::Wrap,   TextureAddressMode::Wrap,   TextureAddressMode::Wrap};
    const SamplerState SamplerState::LinearClamp     {"SamplerState.LinearClamp",      TextureFilter::Linear,      TextureAddressMode::Clamp,  TextureAddressMode::Clamp,  TextureAddressMode::Clamp};
    const SamplerState SamplerState::LinearWrap      {"SamplerState.LinearWrap",       TextureFilter::Linear,      TextureAddressMode::Wrap,   TextureAddressMode::Wrap,   TextureAddressMode::Wrap};
    const SamplerState SamplerState::PointClamp      {"SamplerState.PointClamp",       TextureFilter::Point,       TextureAddressMode::Clamp,  TextureAddressMode::Clamp,  TextureAddressMode::Clamp};
    const SamplerState SamplerState::PointWrap       {"SamplerState.PointWrap",        TextureFilter::Point,       TextureAddressMode::Wrap,   TextureAddressMode::Wrap,   TextureAddressMode::Wrap};

    SamplerState::SamplerState()
        : state_(std::make_shared<State>())
    {
    }

    SamplerState::SamplerState(const SamplerState& other)
        : GraphicsResource(other)
        , state_(std::make_shared<State>(*other.state_))
    {
        // Copy construction is CNA's mutable value-initialization spelling for an XNA state.
        state_->isBound = false;
        state_->isDisposed = false;
    }

    SamplerState& SamplerState::operator=(const SamplerState& other)
    {
        if (this == &other)
            return *this;

        if (bindOnAssignment_)
            other.BindForUse(bindingDevice_);

        const bool bindOnAssignment = bindOnAssignment_;
        GraphicsDevice* const bindingDevice = bindingDevice_;
        GraphicsResource::operator=(other);
        state_ = other.state_;
        ShareResourceIdentityWith(other);
        bindOnAssignment_ = bindOnAssignment;
        bindingDevice_ = bindingDevice;
        return *this;
    }

    void SamplerState::Dispose()
    {
        state_->isDisposed = true;
        GraphicsResource::Dispose();
    }

    SamplerState::SamplerState(const std::string& name,
                               TextureFilter filter,
                               TextureAddressMode addressU,
                               TextureAddressMode addressV,
                               TextureAddressMode addressW)
        : SamplerState()
    {
        setNameProperty(name);
        state_->filter   = filter;
        state_->addressU = addressU;
        state_->addressV = addressV;
        state_->addressW = addressW;
        state_->isBound = true;
    }

    TextureAddressMode SamplerState::getAddressUProperty() const { return state_->addressU; }
    void SamplerState::setAddressUProperty(TextureAddressMode v) { ThrowIfBound(); state_->addressU = v; }

    TextureAddressMode SamplerState::getAddressVProperty() const { return state_->addressV; }
    void SamplerState::setAddressVProperty(TextureAddressMode v) { ThrowIfBound(); state_->addressV = v; }

    TextureAddressMode SamplerState::getAddressWProperty() const { return state_->addressW; }
    void SamplerState::setAddressWProperty(TextureAddressMode v) { ThrowIfBound(); state_->addressW = v; }

    TextureFilter SamplerState::getFilterProperty() const { return state_->filter; }
    void SamplerState::setFilterProperty(TextureFilter v) { ThrowIfBound(); state_->filter = v; }

    int SamplerState::getMaxAnisotropyProperty() const { return state_->maxAnisotropy; }
    void SamplerState::setMaxAnisotropyProperty(int v) { ThrowIfBound(); state_->maxAnisotropy = v; }

    int SamplerState::getMaxMipLevelProperty() const { return state_->maxMipLevel; }
    void SamplerState::setMaxMipLevelProperty(int v) { ThrowIfBound(); state_->maxMipLevel = v; }

    float SamplerState::getMipMapLevelOfDetailBiasProperty() const { return state_->mipMapLevelOfDetailBias; }
    void SamplerState::setMipMapLevelOfDetailBiasProperty(float v) { ThrowIfBound(); state_->mipMapLevelOfDetailBias = v; }

    void SamplerState::ThrowIfBound() const
    {
        if (state_->isBound)
        {
            throw System::InvalidOperationException(
                "Cannot modify a SamplerState after it has been bound to a GraphicsDevice.");
        }
    }

    void SamplerState::BindForUse(GraphicsDevice* device) const
    {
        if (state_->isDisposed || getIsDisposedProperty())
            throw System::ObjectDisposedException("SamplerState");
        state_->isBound = true;
        BindSharedResourceIdentityToDevice(device);
    }

    void SamplerState::MarkCollectionSlot(GraphicsDevice* device)
    {
        bindOnAssignment_ = true;
        bindingDevice_ = device;
        BindForUse(device);
    }

    GetTypeNameCPP(SamplerState, "Microsoft.Xna.Framework.Graphics.SamplerState")
}
