// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"

#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const DepthStencilState DepthStencilState::Default{"DepthStencilState.Default", true, true};
    const DepthStencilState DepthStencilState::DepthRead{"DepthStencilState.DepthRead", true, false};
    const DepthStencilState DepthStencilState::None{"DepthStencilState.None", false, false};

    DepthStencilState::DepthStencilState()
        : state_(std::make_shared<State>())
    {
    }

    DepthStencilState::DepthStencilState(const DepthStencilState& other)
        : GraphicsResource(other)
        , state_(std::make_shared<State>(*other.state_))
    {
        // Copy construction is CNA's mutable value-initialization spelling for an XNA state.
        state_->isBound = false;
        state_->isDisposed = false;
    }

    DepthStencilState& DepthStencilState::operator=(const DepthStencilState& other)
    {
        if (this != &other)
        {
            GraphicsResource::operator=(other);
            state_ = other.state_;
            ShareResourceIdentityWith(other);
        }
        return *this;
    }

    void DepthStencilState::Dispose()
    {
        state_->isDisposed = true;
        GraphicsResource::Dispose();
    }

    DepthStencilState::DepthStencilState(const std::string& name, bool depthEnable, bool depthWriteEnable)
        : DepthStencilState()
    {
        setNameProperty(name);
        state_->depthBufferEnable      = depthEnable;
        state_->depthBufferWriteEnable = depthWriteEnable;
        state_->isBound = true;
    }

    bool DepthStencilState::getDepthBufferEnableProperty() const { return state_->depthBufferEnable; }
    void DepthStencilState::setDepthBufferEnableProperty(bool v) { ThrowIfBound(); state_->depthBufferEnable = v; }

    bool DepthStencilState::getDepthBufferWriteEnableProperty() const { return state_->depthBufferWriteEnable; }
    void DepthStencilState::setDepthBufferWriteEnableProperty(bool v) { ThrowIfBound(); state_->depthBufferWriteEnable = v; }

    CompareFunction DepthStencilState::getDepthBufferFunctionProperty() const { return state_->depthBufferFunction; }
    void DepthStencilState::setDepthBufferFunctionProperty(CompareFunction v) { ThrowIfBound(); state_->depthBufferFunction = v; }

    bool DepthStencilState::getStencilEnableProperty() const { return state_->stencilEnable; }
    void DepthStencilState::setStencilEnableProperty(bool v) { ThrowIfBound(); state_->stencilEnable = v; }

    CompareFunction DepthStencilState::getStencilFunctionProperty() const { return state_->stencilFunction; }
    void DepthStencilState::setStencilFunctionProperty(CompareFunction v) { ThrowIfBound(); state_->stencilFunction = v; }

    int DepthStencilState::getStencilMaskProperty() const { return state_->stencilMask; }
    void DepthStencilState::setStencilMaskProperty(int v) { ThrowIfBound(); state_->stencilMask = v; }

    int DepthStencilState::getStencilWriteMaskProperty() const { return state_->stencilWriteMask; }
    void DepthStencilState::setStencilWriteMaskProperty(int v) { ThrowIfBound(); state_->stencilWriteMask = v; }

    int DepthStencilState::getReferenceStencilProperty() const { return state_->referenceStencil; }
    void DepthStencilState::setReferenceStencilProperty(int v) { ThrowIfBound(); state_->referenceStencil = v; }

    StencilOperation DepthStencilState::getStencilFailProperty() const { return state_->stencilFail; }
    void DepthStencilState::setStencilFailProperty(StencilOperation v) { ThrowIfBound(); state_->stencilFail = v; }

    StencilOperation DepthStencilState::getStencilDepthBufferFailProperty() const { return state_->stencilDepthBufferFail; }
    void DepthStencilState::setStencilDepthBufferFailProperty(StencilOperation v) { ThrowIfBound(); state_->stencilDepthBufferFail = v; }

    StencilOperation DepthStencilState::getStencilPassProperty() const { return state_->stencilPass; }
    void DepthStencilState::setStencilPassProperty(StencilOperation v) { ThrowIfBound(); state_->stencilPass = v; }

    bool DepthStencilState::getTwoSidedStencilModeProperty() const { return state_->twoSidedStencilMode; }
    void DepthStencilState::setTwoSidedStencilModeProperty(bool v) { ThrowIfBound(); state_->twoSidedStencilMode = v; }

    CompareFunction DepthStencilState::getCounterClockwiseStencilFunctionProperty() const { return state_->counterClockwiseStencilFunction; }
    void DepthStencilState::setCounterClockwiseStencilFunctionProperty(CompareFunction v) { ThrowIfBound(); state_->counterClockwiseStencilFunction = v; }

    StencilOperation DepthStencilState::getCounterClockwiseStencilFailProperty() const { return state_->counterClockwiseStencilFail; }
    void DepthStencilState::setCounterClockwiseStencilFailProperty(StencilOperation v) { ThrowIfBound(); state_->counterClockwiseStencilFail = v; }

    StencilOperation DepthStencilState::getCounterClockwiseStencilDepthBufferFailProperty() const { return state_->counterClockwiseStencilDepthBufferFail; }
    void DepthStencilState::setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation v) { ThrowIfBound(); state_->counterClockwiseStencilDepthBufferFail = v; }

    StencilOperation DepthStencilState::getCounterClockwiseStencilPassProperty() const { return state_->counterClockwiseStencilPass; }
    void DepthStencilState::setCounterClockwiseStencilPassProperty(StencilOperation v) { ThrowIfBound(); state_->counterClockwiseStencilPass = v; }

    void DepthStencilState::ThrowIfBound() const
    {
        if (state_->isBound)
        {
            throw System::InvalidOperationException(
                "Cannot modify a DepthStencilState after it has been bound to a GraphicsDevice.");
        }
    }

    void DepthStencilState::BindForUse(GraphicsDevice* device) const
    {
        if (state_->isDisposed || getIsDisposedProperty())
            throw System::ObjectDisposedException("DepthStencilState");
        state_->isBound = true;
        BindSharedResourceIdentityToDevice(device);
    }

    GetTypeNameCPP(DepthStencilState, "Microsoft.Xna.Framework.Graphics.DepthStencilState")
}
