#pragma once

#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines depth-stencil state for the graphics pipeline.
    class DepthStencilState
    {
    public:
        static const DepthStencilState Default;
        static const DepthStencilState DepthRead;
        static const DepthStencilState None;

        DepthStencilState();

        [[nodiscard]] bool getDepthBufferEnableProperty() const;
        void setDepthBufferEnableProperty(bool value);

        [[nodiscard]] bool getDepthBufferWriteEnableProperty() const;
        void setDepthBufferWriteEnableProperty(bool value);

        [[nodiscard]] CompareFunction getDepthBufferFunctionProperty() const;
        void setDepthBufferFunctionProperty(CompareFunction value);

        [[nodiscard]] bool getStencilEnableProperty() const;
        void setStencilEnableProperty(bool value);

        [[nodiscard]] CompareFunction getStencilFunctionProperty() const;
        void setStencilFunctionProperty(CompareFunction value);

        [[nodiscard]] int getStencilMaskProperty() const;
        void setStencilMaskProperty(int value);

        [[nodiscard]] int getStencilWriteMaskProperty() const;
        void setStencilWriteMaskProperty(int value);

        [[nodiscard]] int getReferenceStencilProperty() const;
        void setReferenceStencilProperty(int value);

        [[nodiscard]] StencilOperation getStencilFailProperty() const;
        void setStencilFailProperty(StencilOperation value);

        [[nodiscard]] StencilOperation getStencilDepthBufferFailProperty() const;
        void setStencilDepthBufferFailProperty(StencilOperation value);

        [[nodiscard]] StencilOperation getStencilPassProperty() const;
        void setStencilPassProperty(StencilOperation value);

        [[nodiscard]] bool getTwoSidedStencilModeProperty() const;
        void setTwoSidedStencilModeProperty(bool value);

        [[nodiscard]] CompareFunction getCounterClockwiseStencilFunctionProperty() const;
        void setCounterClockwiseStencilFunctionProperty(CompareFunction value);

        [[nodiscard]] StencilOperation getCounterClockwiseStencilFailProperty() const;
        void setCounterClockwiseStencilFailProperty(StencilOperation value);

        [[nodiscard]] StencilOperation getCounterClockwiseStencilDepthBufferFailProperty() const;
        void setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation value);

        [[nodiscard]] StencilOperation getCounterClockwiseStencilPassProperty() const;
        void setCounterClockwiseStencilPassProperty(StencilOperation value);

    private:
        DepthStencilState(bool depthEnable, bool depthWriteEnable);

        bool depthBufferEnable_;
        bool depthBufferWriteEnable_;
        CompareFunction depthBufferFunction_;
        bool stencilEnable_;
        CompareFunction stencilFunction_;
        int stencilMask_;
        int stencilWriteMask_;
        int referenceStencil_;
        StencilOperation stencilFail_;
        StencilOperation stencilDepthBufferFail_;
        StencilOperation stencilPass_;
        bool twoSidedStencilMode_;
        CompareFunction counterClockwiseStencilFunction_;
        StencilOperation counterClockwiseStencilFail_;
        StencilOperation counterClockwiseStencilDepthBufferFail_;
        StencilOperation counterClockwiseStencilPass_;
    };
}
