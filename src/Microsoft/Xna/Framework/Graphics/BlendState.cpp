// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const BlendState BlendState::Additive       {Blend::SourceAlpha, Blend::SourceAlpha, Blend::One,                Blend::One};
    const BlendState BlendState::AlphaBlend     {Blend::One,         Blend::One,         Blend::InverseSourceAlpha, Blend::InverseSourceAlpha};
    const BlendState BlendState::NonPremultiplied{Blend::SourceAlpha, Blend::SourceAlpha, Blend::InverseSourceAlpha, Blend::InverseSourceAlpha};
    const BlendState BlendState::Opaque         {Blend::One,         Blend::One,         Blend::Zero,               Blend::Zero};

    BlendState::BlendState()
        : alphaBlendFunction_(BlendFunction::Add)
        , alphaDestinationBlend_(Blend::Zero)
        , alphaSourceBlend_(Blend::One)
        , colorBlendFunction_(BlendFunction::Add)
        , colorDestinationBlend_(Blend::Zero)
        , colorSourceBlend_(Blend::One)
        , colorWriteChannels_(ColorWriteChannels::All)
        , colorWriteChannels1_(ColorWriteChannels::All)
        , colorWriteChannels2_(ColorWriteChannels::All)
        , colorWriteChannels3_(ColorWriteChannels::All)
        , blendFactor_(Color::White)
        , multiSampleMask_(-1)
    {
    }

    BlendState::BlendState(Blend colorSrc, Blend alphaSrc, Blend colorDst, Blend alphaDst)
        : BlendState()
    {
        colorSourceBlend_      = colorSrc;
        alphaSourceBlend_      = alphaSrc;
        colorDestinationBlend_ = colorDst;
        alphaDestinationBlend_ = alphaDst;
    }

    BlendFunction BlendState::getAlphaBlendFunctionProperty() const { return alphaBlendFunction_; }
    void BlendState::setAlphaBlendFunctionProperty(BlendFunction v) { alphaBlendFunction_ = v; }

    Blend BlendState::getAlphaDestinationBlendProperty() const { return alphaDestinationBlend_; }
    void BlendState::setAlphaDestinationBlendProperty(Blend v) { alphaDestinationBlend_ = v; }

    Blend BlendState::getAlphaSourceBlendProperty() const { return alphaSourceBlend_; }
    void BlendState::setAlphaSourceBlendProperty(Blend v) { alphaSourceBlend_ = v; }

    BlendFunction BlendState::getColorBlendFunctionProperty() const { return colorBlendFunction_; }
    void BlendState::setColorBlendFunctionProperty(BlendFunction v) { colorBlendFunction_ = v; }

    Blend BlendState::getColorDestinationBlendProperty() const { return colorDestinationBlend_; }
    void BlendState::setColorDestinationBlendProperty(Blend v) { colorDestinationBlend_ = v; }

    Blend BlendState::getColorSourceBlendProperty() const { return colorSourceBlend_; }
    void BlendState::setColorSourceBlendProperty(Blend v) { colorSourceBlend_ = v; }

    ColorWriteChannels BlendState::getColorWriteChannelsProperty() const { return colorWriteChannels_; }
    void BlendState::setColorWriteChannelsProperty(ColorWriteChannels v) { colorWriteChannels_ = v; }

    ColorWriteChannels BlendState::getColorWriteChannels1Property() const { return colorWriteChannels1_; }
    void BlendState::setColorWriteChannels1Property(ColorWriteChannels v) { colorWriteChannels1_ = v; }

    ColorWriteChannels BlendState::getColorWriteChannels2Property() const { return colorWriteChannels2_; }
    void BlendState::setColorWriteChannels2Property(ColorWriteChannels v) { colorWriteChannels2_ = v; }

    ColorWriteChannels BlendState::getColorWriteChannels3Property() const { return colorWriteChannels3_; }
    void BlendState::setColorWriteChannels3Property(ColorWriteChannels v) { colorWriteChannels3_ = v; }

    Color BlendState::getBlendFactorProperty() const { return blendFactor_; }
    void BlendState::setBlendFactorProperty(const Color& v) { blendFactor_ = v; }

    int BlendState::getMultiSampleMaskProperty() const { return multiSampleMask_; }
    void BlendState::setMultiSampleMaskProperty(int v) { multiSampleMask_ = v; }
}
