// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const RasterizerState RasterizerState::CullClockwise{CullMode::CullClockwiseFace};
    const RasterizerState RasterizerState::CullCounterClockwise{CullMode::CullCounterClockwiseFace};
    const RasterizerState RasterizerState::CullNone{CullMode::None};

    RasterizerState::RasterizerState()
        : cullMode_(CullMode::CullCounterClockwiseFace)
        , depthBias_(0.0f)
        , fillMode_(FillMode::Solid)
        , multiSampleAntiAlias_(true)
        , scissorTestEnable_(false)
        , slopeScaleDepthBias_(0.0f)
    {
    }

    RasterizerState::RasterizerState(CullMode cullMode)
        : RasterizerState()
    {
        cullMode_ = cullMode;
    }

    CullMode RasterizerState::getCullModeProperty() const { return cullMode_; }
    void RasterizerState::setCullModeProperty(CullMode v) { cullMode_ = v; }

    float RasterizerState::getDepthBiasProperty() const { return depthBias_; }
    void RasterizerState::setDepthBiasProperty(float v) { depthBias_ = v; }

    FillMode RasterizerState::getFillModeProperty() const { return fillMode_; }
    void RasterizerState::setFillModeProperty(FillMode v) { fillMode_ = v; }

    bool RasterizerState::getMultiSampleAntiAliasProperty() const { return multiSampleAntiAlias_; }
    void RasterizerState::setMultiSampleAntiAliasProperty(bool v) { multiSampleAntiAlias_ = v; }

    bool RasterizerState::getScissorTestEnableProperty() const { return scissorTestEnable_; }
    void RasterizerState::setScissorTestEnableProperty(bool v) { scissorTestEnable_ = v; }

    float RasterizerState::getSlopeScaleDepthBiasProperty() const { return slopeScaleDepthBias_; }
    void RasterizerState::setSlopeScaleDepthBiasProperty(float v) { slopeScaleDepthBias_ = v; }
}
