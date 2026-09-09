// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    const RasterizerState RasterizerState::CullClockwise{"RasterizerState.CullClockwise", CullMode::CullClockwiseFace};
    const RasterizerState RasterizerState::CullCounterClockwise{"RasterizerState.CullCounterClockwise", CullMode::CullCounterClockwiseFace};
    const RasterizerState RasterizerState::CullNone{"RasterizerState.CullNone", CullMode::None};

    RasterizerState::RasterizerState()
        : state_(std::make_shared<State>())
    {
    }

    RasterizerState::RasterizerState(const RasterizerState& other)
        : GraphicsResource(other)
        , state_(std::make_shared<State>(*other.state_))
    {
        // Copy construction is CNA's mutable value-initialization spelling for an XNA state.
        state_->isBound = false;
    }

    RasterizerState& RasterizerState::operator=(const RasterizerState& other)
    {
        if (this != &other)
        {
            GraphicsResource::operator=(other);
            state_ = other.state_;
        }
        return *this;
    }

    RasterizerState::RasterizerState(const std::string& name, CullMode cullMode)
        : RasterizerState()
    {
        setNameProperty(name);
        state_->cullMode = cullMode;
        state_->isBound = true;
    }

    CullMode RasterizerState::getCullModeProperty() const { return state_->cullMode; }
    void RasterizerState::setCullModeProperty(CullMode v) { ThrowIfBound(); state_->cullMode = v; }

    float RasterizerState::getDepthBiasProperty() const { return state_->depthBias; }
    void RasterizerState::setDepthBiasProperty(float v) { ThrowIfBound(); state_->depthBias = v; }

    FillMode RasterizerState::getFillModeProperty() const { return state_->fillMode; }
    void RasterizerState::setFillModeProperty(FillMode v) { ThrowIfBound(); state_->fillMode = v; }

    bool RasterizerState::getMultiSampleAntiAliasProperty() const { return state_->multiSampleAntiAlias; }
    void RasterizerState::setMultiSampleAntiAliasProperty(bool v) { ThrowIfBound(); state_->multiSampleAntiAlias = v; }

    bool RasterizerState::getScissorTestEnableProperty() const { return state_->scissorTestEnable; }
    void RasterizerState::setScissorTestEnableProperty(bool v) { ThrowIfBound(); state_->scissorTestEnable = v; }

    float RasterizerState::getSlopeScaleDepthBiasProperty() const { return state_->slopeScaleDepthBias; }
    void RasterizerState::setSlopeScaleDepthBiasProperty(float v) { ThrowIfBound(); state_->slopeScaleDepthBias = v; }

    void RasterizerState::ThrowIfBound() const
    {
        if (state_->isBound)
        {
            throw System::InvalidOperationException(
                "Cannot modify a RasterizerState after it has been bound to a GraphicsDevice.");
        }
    }

    void RasterizerState::BindForUse() const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("RasterizerState");
        state_->isBound = true;
    }

    GetTypeNameCPP(RasterizerState, "Microsoft.Xna.Framework.Graphics.RasterizerState")
}
