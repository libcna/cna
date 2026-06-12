// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const DepthStencilState DepthStencilState::Default{true, true};
    const DepthStencilState DepthStencilState::DepthRead{true, false};
    const DepthStencilState DepthStencilState::None{false, false};

    DepthStencilState::DepthStencilState()
        : depthBufferEnable_(true)
        , depthBufferWriteEnable_(true)
        , depthBufferFunction_(CompareFunction::LessEqual)
        , stencilEnable_(false)
        , stencilFunction_(CompareFunction::Always)
        , stencilMask_(0x7FFFFFFF)
        , stencilWriteMask_(0x7FFFFFFF)
        , referenceStencil_(0)
        , stencilFail_(StencilOperation::Keep)
        , stencilDepthBufferFail_(StencilOperation::Keep)
        , stencilPass_(StencilOperation::Keep)
        , twoSidedStencilMode_(false)
        , counterClockwiseStencilFunction_(CompareFunction::Always)
        , counterClockwiseStencilFail_(StencilOperation::Keep)
        , counterClockwiseStencilDepthBufferFail_(StencilOperation::Keep)
        , counterClockwiseStencilPass_(StencilOperation::Keep)
    {
    }

    DepthStencilState::DepthStencilState(bool depthEnable, bool depthWriteEnable)
        : DepthStencilState()
    {
        depthBufferEnable_      = depthEnable;
        depthBufferWriteEnable_ = depthWriteEnable;
    }

    bool DepthStencilState::getDepthBufferEnableProperty() const { return depthBufferEnable_; }
    void DepthStencilState::setDepthBufferEnableProperty(bool v) { depthBufferEnable_ = v; }

    bool DepthStencilState::getDepthBufferWriteEnableProperty() const { return depthBufferWriteEnable_; }
    void DepthStencilState::setDepthBufferWriteEnableProperty(bool v) { depthBufferWriteEnable_ = v; }

    CompareFunction DepthStencilState::getDepthBufferFunctionProperty() const { return depthBufferFunction_; }
    void DepthStencilState::setDepthBufferFunctionProperty(CompareFunction v) { depthBufferFunction_ = v; }

    bool DepthStencilState::getStencilEnableProperty() const { return stencilEnable_; }
    void DepthStencilState::setStencilEnableProperty(bool v) { stencilEnable_ = v; }

    CompareFunction DepthStencilState::getStencilFunctionProperty() const { return stencilFunction_; }
    void DepthStencilState::setStencilFunctionProperty(CompareFunction v) { stencilFunction_ = v; }

    int DepthStencilState::getStencilMaskProperty() const { return stencilMask_; }
    void DepthStencilState::setStencilMaskProperty(int v) { stencilMask_ = v; }

    int DepthStencilState::getStencilWriteMaskProperty() const { return stencilWriteMask_; }
    void DepthStencilState::setStencilWriteMaskProperty(int v) { stencilWriteMask_ = v; }

    int DepthStencilState::getReferenceStencilProperty() const { return referenceStencil_; }
    void DepthStencilState::setReferenceStencilProperty(int v) { referenceStencil_ = v; }

    StencilOperation DepthStencilState::getStencilFailProperty() const { return stencilFail_; }
    void DepthStencilState::setStencilFailProperty(StencilOperation v) { stencilFail_ = v; }

    StencilOperation DepthStencilState::getStencilDepthBufferFailProperty() const { return stencilDepthBufferFail_; }
    void DepthStencilState::setStencilDepthBufferFailProperty(StencilOperation v) { stencilDepthBufferFail_ = v; }

    StencilOperation DepthStencilState::getStencilPassProperty() const { return stencilPass_; }
    void DepthStencilState::setStencilPassProperty(StencilOperation v) { stencilPass_ = v; }

    bool DepthStencilState::getTwoSidedStencilModeProperty() const { return twoSidedStencilMode_; }
    void DepthStencilState::setTwoSidedStencilModeProperty(bool v) { twoSidedStencilMode_ = v; }

    CompareFunction DepthStencilState::getCounterClockwiseStencilFunctionProperty() const { return counterClockwiseStencilFunction_; }
    void DepthStencilState::setCounterClockwiseStencilFunctionProperty(CompareFunction v) { counterClockwiseStencilFunction_ = v; }

    StencilOperation DepthStencilState::getCounterClockwiseStencilFailProperty() const { return counterClockwiseStencilFail_; }
    void DepthStencilState::setCounterClockwiseStencilFailProperty(StencilOperation v) { counterClockwiseStencilFail_ = v; }

    StencilOperation DepthStencilState::getCounterClockwiseStencilDepthBufferFailProperty() const { return counterClockwiseStencilDepthBufferFail_; }
    void DepthStencilState::setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation v) { counterClockwiseStencilDepthBufferFail_ = v; }

    StencilOperation DepthStencilState::getCounterClockwiseStencilPassProperty() const { return counterClockwiseStencilPass_; }
    void DepthStencilState::setCounterClockwiseStencilPassProperty(StencilOperation v) { counterClockwiseStencilPass_ = v; }
}
