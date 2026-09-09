// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"

#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const BlendState BlendState::Additive        {"BlendState.Additive",        Blend::SourceAlpha, Blend::SourceAlpha, Blend::One,                Blend::One};
    const BlendState BlendState::AlphaBlend      {"BlendState.AlphaBlend",      Blend::One,         Blend::One,         Blend::InverseSourceAlpha, Blend::InverseSourceAlpha};
    const BlendState BlendState::NonPremultiplied{"BlendState.NonPremultiplied",Blend::SourceAlpha, Blend::SourceAlpha, Blend::InverseSourceAlpha, Blend::InverseSourceAlpha};
    const BlendState BlendState::Opaque          {"BlendState.Opaque",          Blend::One,         Blend::One,         Blend::Zero,               Blend::Zero};

    BlendState::BlendState()
        : state_(std::make_shared<State>())
    {
    }

    BlendState::BlendState(const BlendState& other)
        : GraphicsResource(other)
        , state_(std::make_shared<State>(*other.state_))
    {
        // C++ value initialization from an XNA preset is CNA's construction spelling; it must
        // produce the mutable instance that C# would create with `new BlendState()`.
        state_->isBound = false;
        state_->isDisposed = false;
    }

    BlendState& BlendState::operator=(const BlendState& other)
    {
        if (this != &other)
        {
            GraphicsResource::operator=(other);
            state_ = other.state_;
        }
        return *this;
    }

    void BlendState::Dispose()
    {
        state_->isDisposed = true;
        GraphicsResource::Dispose();
    }

    BlendState::BlendState(const std::string& name, Blend colorSrc, Blend alphaSrc, Blend colorDst, Blend alphaDst)
        : BlendState()
    {
        setNameProperty(name);
        state_->colorSourceBlend      = colorSrc;
        state_->alphaSourceBlend      = alphaSrc;
        state_->colorDestinationBlend = colorDst;
        state_->alphaDestinationBlend = alphaDst;
        state_->isBound = true;
    }

    BlendFunction BlendState::getAlphaBlendFunctionProperty() const { return state_->alphaBlendFunction; }
    void BlendState::setAlphaBlendFunctionProperty(BlendFunction v) { ThrowIfBound(); state_->alphaBlendFunction = v; }

    Blend BlendState::getAlphaDestinationBlendProperty() const { return state_->alphaDestinationBlend; }
    void BlendState::setAlphaDestinationBlendProperty(Blend v) { ThrowIfBound(); state_->alphaDestinationBlend = v; }

    Blend BlendState::getAlphaSourceBlendProperty() const { return state_->alphaSourceBlend; }
    void BlendState::setAlphaSourceBlendProperty(Blend v) { ThrowIfBound(); state_->alphaSourceBlend = v; }

    BlendFunction BlendState::getColorBlendFunctionProperty() const { return state_->colorBlendFunction; }
    void BlendState::setColorBlendFunctionProperty(BlendFunction v) { ThrowIfBound(); state_->colorBlendFunction = v; }

    Blend BlendState::getColorDestinationBlendProperty() const { return state_->colorDestinationBlend; }
    void BlendState::setColorDestinationBlendProperty(Blend v) { ThrowIfBound(); state_->colorDestinationBlend = v; }

    Blend BlendState::getColorSourceBlendProperty() const { return state_->colorSourceBlend; }
    void BlendState::setColorSourceBlendProperty(Blend v) { ThrowIfBound(); state_->colorSourceBlend = v; }

    ColorWriteChannels BlendState::getColorWriteChannelsProperty() const { return state_->colorWriteChannels; }
    void BlendState::setColorWriteChannelsProperty(ColorWriteChannels v) { ThrowIfBound(); state_->colorWriteChannels = v; }

    ColorWriteChannels BlendState::getColorWriteChannels1Property() const { return state_->colorWriteChannels1; }
    void BlendState::setColorWriteChannels1Property(ColorWriteChannels v) { ThrowIfBound(); state_->colorWriteChannels1 = v; }

    ColorWriteChannels BlendState::getColorWriteChannels2Property() const { return state_->colorWriteChannels2; }
    void BlendState::setColorWriteChannels2Property(ColorWriteChannels v) { ThrowIfBound(); state_->colorWriteChannels2 = v; }

    ColorWriteChannels BlendState::getColorWriteChannels3Property() const { return state_->colorWriteChannels3; }
    void BlendState::setColorWriteChannels3Property(ColorWriteChannels v) { ThrowIfBound(); state_->colorWriteChannels3 = v; }

    Color BlendState::getBlendFactorProperty() const { return state_->blendFactor; }
    void BlendState::setBlendFactorProperty(const Color& v) { ThrowIfBound(); state_->blendFactor = v; }

    int BlendState::getMultiSampleMaskProperty() const { return state_->multiSampleMask; }
    void BlendState::setMultiSampleMaskProperty(int v) { ThrowIfBound(); state_->multiSampleMask = v; }

    void BlendState::ThrowIfBound() const
    {
        if (state_->isBound)
        {
            throw System::InvalidOperationException(
                "Cannot modify a BlendState after it has been bound to a GraphicsDevice.");
        }
    }

    void BlendState::BindForUse() const
    {
        if (state_->isDisposed || getIsDisposedProperty())
            throw System::ObjectDisposedException("BlendState");
        state_->isBound = true;
    }

    GetTypeNameCPP(BlendState, "Microsoft.Xna.Framework.Graphics.BlendState")
}
