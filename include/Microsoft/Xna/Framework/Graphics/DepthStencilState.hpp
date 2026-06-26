// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines depth-stencil state for the graphics pipeline. */
    class DepthStencilState : public GraphicsResource
    {
    public:
        /** @brief Preset: depth test and write both enabled with LessEqual comparison (XNA default). */
        static const DepthStencilState Default;
        /** @brief Preset: depth test enabled but writes disabled (read-only depth). */
        static const DepthStencilState DepthRead;
        /** @brief Preset: depth test and write both disabled. */
        static const DepthStencilState None;

        /** @brief Creates a DepthStencilState with XNA-compatible default values. */
        DepthStencilState();

        /** @brief Returns the fully-qualified .NET type name of this object. */
        NOXNA [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Gets whether depth buffering is enabled.
         * @return true if depth buffering is enabled.
         */
        [[nodiscard]] bool getDepthBufferEnableProperty() const;
        /**
         * @brief Sets whether depth buffering is enabled.
         * @param value true to enable depth buffering.
         */
        void setDepthBufferEnableProperty(bool value);

        /**
         * @brief Gets whether writing to the depth buffer is enabled.
         * @return true if depth writes are enabled.
         */
        [[nodiscard]] bool getDepthBufferWriteEnableProperty() const;
        /**
         * @brief Sets whether writing to the depth buffer is enabled.
         * @param value true to enable depth writes.
         */
        void setDepthBufferWriteEnableProperty(bool value);

        /**
         * @brief Gets the comparison function used for depth testing.
         * @return Current depth buffer comparison function.
         */
        [[nodiscard]] CompareFunction getDepthBufferFunctionProperty() const;
        /**
         * @brief Sets the comparison function used for depth testing.
         * @param value New depth buffer comparison function.
         */
        void setDepthBufferFunctionProperty(CompareFunction value);

        /**
         * @brief Gets whether stencil testing is enabled.
         * @return true if stencil testing is enabled.
         */
        [[nodiscard]] bool getStencilEnableProperty() const;
        /**
         * @brief Sets whether stencil testing is enabled.
         * @param value true to enable stencil testing.
         */
        void setStencilEnableProperty(bool value);

        /**
         * @brief Gets the comparison function used for stencil testing.
         * @return Current stencil comparison function.
         */
        [[nodiscard]] CompareFunction getStencilFunctionProperty() const;
        /**
         * @brief Sets the comparison function used for stencil testing.
         * @param value New stencil comparison function.
         */
        void setStencilFunctionProperty(CompareFunction value);

        /**
         * @brief Gets the mask applied when reading the stencil buffer.
         * @return Current stencil read mask.
         */
        [[nodiscard]] int getStencilMaskProperty() const;
        /**
         * @brief Sets the mask applied when reading the stencil buffer.
         * @param value New stencil read mask.
         */
        void setStencilMaskProperty(int value);

        /**
         * @brief Gets the mask applied when writing to the stencil buffer.
         * @return Current stencil write mask.
         */
        [[nodiscard]] int getStencilWriteMaskProperty() const;
        /**
         * @brief Sets the mask applied when writing to the stencil buffer.
         * @param value New stencil write mask.
         */
        void setStencilWriteMaskProperty(int value);

        /**
         * @brief Gets the reference value used for stencil comparisons.
         * @return Current stencil reference value.
         */
        [[nodiscard]] int getReferenceStencilProperty() const;
        /**
         * @brief Sets the reference value used for stencil comparisons.
         * @param value New stencil reference value.
         */
        void setReferenceStencilProperty(int value);

        /**
         * @brief Gets the stencil operation when the stencil test fails.
         * @return Current stencil fail operation.
         */
        [[nodiscard]] StencilOperation getStencilFailProperty() const;
        /**
         * @brief Sets the stencil operation when the stencil test fails.
         * @param value New stencil fail operation.
         */
        void setStencilFailProperty(StencilOperation value);

        /**
         * @brief Gets the stencil operation when the stencil test passes but depth test fails.
         * @return Current stencil depth-buffer fail operation.
         */
        [[nodiscard]] StencilOperation getStencilDepthBufferFailProperty() const;
        /**
         * @brief Sets the stencil operation when the stencil test passes but depth test fails.
         * @param value New stencil depth-buffer fail operation.
         */
        void setStencilDepthBufferFailProperty(StencilOperation value);

        /**
         * @brief Gets the stencil operation when both the stencil and depth tests pass.
         * @return Current stencil pass operation.
         */
        [[nodiscard]] StencilOperation getStencilPassProperty() const;
        /**
         * @brief Sets the stencil operation when both the stencil and depth tests pass.
         * @param value New stencil pass operation.
         */
        void setStencilPassProperty(StencilOperation value);

        /**
         * @brief Gets whether two-sided stencil mode is enabled.
         * @return true if two-sided stencil mode is enabled.
         */
        [[nodiscard]] bool getTwoSidedStencilModeProperty() const;
        /**
         * @brief Sets whether two-sided stencil mode is enabled.
         * @param value true to enable two-sided stencil mode.
         */
        void setTwoSidedStencilModeProperty(bool value);

        /**
         * @brief Gets the stencil comparison function for counter-clockwise faces.
         * @return Current counter-clockwise stencil comparison function.
         */
        [[nodiscard]] CompareFunction getCounterClockwiseStencilFunctionProperty() const;
        /**
         * @brief Sets the stencil comparison function for counter-clockwise faces.
         * @param value New counter-clockwise stencil comparison function.
         */
        void setCounterClockwiseStencilFunctionProperty(CompareFunction value);

        /**
         * @brief Gets the stencil fail operation for counter-clockwise faces.
         * @return Current counter-clockwise stencil fail operation.
         */
        [[nodiscard]] StencilOperation getCounterClockwiseStencilFailProperty() const;
        /**
         * @brief Sets the stencil fail operation for counter-clockwise faces.
         * @param value New counter-clockwise stencil fail operation.
         */
        void setCounterClockwiseStencilFailProperty(StencilOperation value);

        /**
         * @brief Gets the stencil depth-buffer fail operation for counter-clockwise faces.
         * @return Current counter-clockwise stencil depth-buffer fail operation.
         */
        [[nodiscard]] StencilOperation getCounterClockwiseStencilDepthBufferFailProperty() const;
        /**
         * @brief Sets the stencil depth-buffer fail operation for counter-clockwise faces.
         * @param value New counter-clockwise stencil depth-buffer fail operation.
         */
        void setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation value);

        /**
         * @brief Gets the stencil pass operation for counter-clockwise faces.
         * @return Current counter-clockwise stencil pass operation.
         */
        [[nodiscard]] StencilOperation getCounterClockwiseStencilPassProperty() const;
        /**
         * @brief Sets the stencil pass operation for counter-clockwise faces.
         * @param value New counter-clockwise stencil pass operation.
         */
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
