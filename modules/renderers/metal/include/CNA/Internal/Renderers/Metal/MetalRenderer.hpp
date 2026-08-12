// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <memory>

namespace CNA::Internal::Renderers::Metal
{
    /**
     * @brief Direct Objective-C++ Metal implementation of CNA's graphics renderer contract.
     */
    class MetalRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates a Metal device, command queue, and SDL Metal view for a macOS window.
         *
         * @param args Renderer creation parameters, including the SDL window and presentation settings.
         */
        explicit MetalRenderer(const GraphicsRendererCreateArgs& args);
        /** @brief Releases all Metal and SDL resources owned by the renderer. */
        ~MetalRenderer() override;

        /**
         * @brief Metal renderers are device-bound and cannot be copied.
         *
         * @param other Renderer instance that would otherwise be copied.
         */
        MetalRenderer(const MetalRenderer& other) = delete;
        /**
         * @brief Metal renderers are device-bound and cannot be copy-assigned.
         *
         * @param other Renderer instance that would otherwise be assigned.
         * @return This renderer; the operation is deleted and therefore never returns.
         */
        MetalRenderer& operator=(const MetalRenderer& other) = delete;

        /**
         * @brief Clears the active color attachment.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         */
        void Clear(float r, float g, float b, float a) override;
        /** @brief Commits and presents the current backbuffer frame. */
        void Present() override;
        /**
         * @brief Returns the logical viewport size exposed to game code.
         *
         * @param width Receives the logical viewport width.
         * @param height Receives the logical viewport height.
         */
        void GetViewportSize(int& width, int& height) override;
        /**
         * @brief Returns the physical drawable rectangle used by the default viewport.
         *
         * @param x Receives the drawable-space horizontal origin.
         * @param y Receives the drawable-space vertical origin.
         * @param width Receives the drawable-space width.
         * @param height Receives the drawable-space height.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        /**
         * @brief Updates the logical presentation size.
         *
         * @param width Logical presentation width.
         * @param height Logical presentation height.
         */
        void SetVirtualResolution(int width, int height) override;
        /**
         * @brief Updates CNA's presentation scaling mode.
         *
         * @param mode Integer representation of the presentation scaling mode.
         */
        void SetPresentationMode(int mode) override;
        /**
         * @brief Maps immediate versus synchronized presentation onto CAMetalLayer.
         *
         * @param interval Requested presentation synchronization interval.
         */
        void SetSwapInterval(int interval) override;
        /**
         * @brief Rejects backbuffer MSAA by applying zero samples.
         *
         * @param requestedMultiSampleCount Requested XNA multisample count.
         * @return Zero because Metal MSAA is outside the supported contract.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        /**
         * @brief Reports zero because MSAA is not in the supported Metal contract.
         *
         * @return The applied XNA multisample count, always zero.
         */
        [[nodiscard]] int GetMultiSampleCount() const override;
        /**
         * @brief Reports the fixed BGRA8 backbuffer as XNA SurfaceFormat::Color.
         *
         * @param requestedFormat Requested XNA surface-format value.
         * @return The applied XNA SurfaceFormat::Color value.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override;
        /**
         * @brief Reports the fixed combined depth/stencil attachment as Depth24Stencil8.
         *
         * @param requestedFormat Requested XNA depth/stencil-format value.
         * @return The applied XNA DepthFormat::Depth24Stencil8 value.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int requestedFormat) const override;
        /**
         * @brief Reports that the Metal backbuffer owns a usable depth plane.
         *
         * @return True because the default Metal target includes depth storage.
         */
        [[nodiscard]] bool SupportsDepthBuffer() const override;
        /**
         * @brief Reports that the Metal backbuffer owns a usable stencil plane.
         *
         * @return True because the default Metal target includes stencil storage.
         */
        [[nodiscard]] bool SupportsStencilBuffer() const override;
        /**
         * @brief Converts SDL window coordinates to CNA logical coordinates.
         *
         * @param windowX Horizontal SDL window coordinate.
         * @param windowY Vertical SDL window coordinate.
         * @param logX Receives the horizontal CNA logical coordinate.
         * @param logY Receives the vertical CNA logical coordinate.
         * @return True when the coordinate lies within the active presentation area.
         */
        bool TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const override;
        /**
         * @brief Converts CNA logical coordinates to SDL window coordinates.
         *
         * @param logX Horizontal CNA logical coordinate.
         * @param logY Vertical CNA logical coordinate.
         * @param windowX Receives the horizontal SDL window coordinate.
         * @param windowY Receives the vertical SDL window coordinate.
         * @return True when the coordinate can be transformed using the active presentation area.
         */
        bool TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const override;
        /**
         * @brief Deterministically rejects backbuffer readback until it has adapted-Mac proof.
         *
         * @param x Horizontal origin of the requested source rectangle.
         * @param y Vertical origin of the requested source rectangle.
         * @param w Width of the requested source rectangle.
         * @param h Height of the requested source rectangle.
         * @param pixels Destination byte buffer, unused because the operation is unsupported.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        /**
         * @brief Creates an RGBA8 Metal Texture2D renderer resource.
         *
         * @param data Initial dimensions and RGBA8 pixel data.
         * @return The newly allocated texture renderer.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        /**
         * @brief Creates the built-in direct-Metal SpriteBatch implementation.
         *
         * @return The newly allocated SpriteBatch renderer.
         */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        /**
         * @brief Creates a Color-format Metal cube texture and rejects other formats.
         *
         * @param size Width and height of every cube-map face.
         * @param mipMap Whether mip storage is requested.
         * @param surfaceFormat Requested XNA surface-format value.
         * @return The newly allocated cube texture renderer.
         */
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        /**
         * @brief Rejects occlusion queries until command splitting and slot reuse are fixed.
         *
         * @return No value; the method always throws a not-supported exception.
         */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        /**
         * @brief Creates a Color RenderTarget2D with MSAA clamped to zero.
         *
         * @param w Render-target width in pixels.
         * @param h Render-target height in pixels.
         * @param depthFormat Requested XNA depth/stencil-format value.
         * @param preserveContents Whether target contents must survive later bindings.
         * @param mipMap Whether mip storage is requested.
         * @param multiSampleCount Requested XNA multisample count; Metal applies zero.
         * @return The newly allocated render-target renderer.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                     bool preserveContents = false,
                                                                     bool mipMap = false,
                                                                     int multiSampleCount = 0) override;
        /**
         * @brief Creates a Color RenderTarget2D and rejects non-Color surface formats.
         *
         * @param w Render-target width in pixels.
         * @param h Render-target height in pixels.
         * @param depthFormat Requested XNA depth/stencil-format value.
         * @param preserveContents Whether target contents must survive later bindings.
         * @param mipMap Whether mip storage is requested.
         * @param multiSampleCount Requested XNA multisample count; Metal applies zero.
         * @param surfaceFormat Requested XNA surface-format value.
         * @return The newly allocated render-target renderer.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        /**
         * @brief Binds one Metal RenderTarget2D or restores the backbuffer.
         *
         * @param rt Target to bind, or null to restore the backbuffer.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        /**
         * @brief Consumes one normalized target descriptor and rejects MRT atomically.
         *
         * @param renderTargets Normalized render-target descriptor array, or null when count is zero.
         * @param count Number of descriptors; only zero or one is supported.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        /**
         * @brief Creates a Color RenderTargetCube; cube MSAA is clamped to zero.
         *
         * @param size Width and height of every cube-map face.
         * @param depthFormat Requested XNA depth/stencil-format value.
         * @param preserveContents Whether target contents must survive later bindings.
         * @param mipMap Whether mip storage is requested.
         * @param multiSampleCount Requested XNA multisample count; Metal applies zero.
         * @return The newly allocated cube render-target renderer.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                           bool preserveContents = false,
                                                                           bool mipMap = false,
                                                                           int multiSampleCount = 0) override;
        /**
         * @brief Binds one validated Metal cube-map face or restores the backbuffer.
         *
         * @param rt Cube render target to bind, or null to restore the backbuffer.
         * @param face Cube-map face index in the inclusive range zero through five.
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        /**
         * @brief Creates a Color-format Metal volume texture and rejects other formats.
         *
         * @param w Texture width in texels.
         * @param h Texture height in texels.
         * @param depth Texture depth in texels.
         * @param mipMap Whether mip storage is requested.
         * @param surfaceFormat Requested XNA surface-format value.
         * @return The newly allocated volume-texture renderer.
         */
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        /**
         * @brief Rejects custom effects until the historical path has adapted-Mac proof.
         *
         * @param vertSrc Vertex-stage source text, unused because custom effects are unsupported.
         * @param fragSrc Fragment-stage source text, unused because custom effects are unsupported.
         * @return No value; the method always throws a not-supported exception.
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc, const std::string& fragSrc) override;

        /**
         * @brief Clears color and depth on the active target.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param depth Depth clear value.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /**
         * @brief Clears depth on the active target.
         *
         * @param depth Depth clear value.
         */
        void ClearDepth(float depth) override;
        /**
         * @brief Clears stencil on the active target.
         *
         * @param stencil Stencil clear value.
         */
        void ClearStencil(int stencil) override;
        /**
         * @brief Clears depth and stencil on the active target.
         *
         * @param depth Depth clear value.
         * @param stencil Stencil clear value.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /**
         * @brief Clears color and stencil on the active target.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param stencil Stencil clear value.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /**
         * @brief Clears color, depth, and stencil on the active target.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param depth Depth clear value.
         * @param stencil Stencil clear value.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        /**
         * @brief Enables or disables depth testing.
         *
         * @param enabled True to enable depth testing.
         */
        void SetDepthTestEnabled(bool enabled) override;
        /**
         * @brief Enables or disables blending.
         *
         * @param enabled True to enable blending.
         */
        void SetBlendEnabled(bool enabled) override;
        /**
         * @brief Enables or disables depth writes.
         *
         * @param enabled True to enable depth writes.
         */
        void SetDepthWriteEnabled(bool enabled) override;
        /**
         * @brief Applies blend factors/functions and rejects unsupported output-write state.
         *
         * @param colorSrcBlend Source blend factor for RGB components.
         * @param alphaSrcBlend Source blend factor for alpha.
         * @param colorDstBlend Destination blend factor for RGB components.
         * @param alphaDstBlend Destination blend factor for alpha.
         * @param colorBlendFunc Blend operation for RGB components.
         * @param alphaBlendFunc Blend operation for alpha.
         * @param writeState Per-target color write-mask state.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        /**
         * @brief Applies Metal depth/stencil comparison, operation, mask, and reference state.
         *
         * @param depthEnable True to enable depth testing.
         * @param depthWriteEnable True to enable depth-buffer writes.
         * @param depthFunc Depth comparison function.
         * @param stencilEnable True to enable stencil testing.
         * @param stencilFunc Clockwise-face stencil comparison function.
         * @param stencilPass Clockwise-face operation when stencil and depth pass.
         * @param stencilFail Clockwise-face operation when stencil fails.
         * @param stencilDepthFail Clockwise-face operation when depth fails.
         * @param stencilMask Stencil read mask.
         * @param stencilWriteMask Stencil write mask.
         * @param referenceStencil Stencil reference value.
         * @param twoSidedStencilMode True to use separate counter-clockwise state.
         * @param ccwStencilFunc Counter-clockwise-face stencil comparison function.
         * @param ccwStencilPass Counter-clockwise-face operation when stencil and depth pass.
         * @param ccwStencilFail Counter-clockwise-face operation when stencil fails.
         * @param ccwStencilDepthFail Counter-clockwise-face operation when depth fails.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass, int stencilFail,
                                    int stencilDepthFail, int stencilMask, int stencilWriteMask,
                                    int referenceStencil, bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail, int ccwStencilDepthFail) override;
        /**
         * @brief Applies culling, fill mode, scissor enablement, and depth bias.
         *
         * @param cullMode Requested culling mode.
         * @param fillMode Requested fill mode.
         * @param scissorTestEnable True to enable scissor testing.
         * @param depthBias Constant depth-bias value.
         * @param slopeScaleDepthBias Slope-scaled depth-bias value.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        /**
         * @brief Applies filter, address, and anisotropy state to one sampler slot.
         *
         * @param slot Sampler slot index.
         * @param filter Requested XNA texture filter.
         * @param addressU Requested horizontal address mode.
         * @param addressV Requested vertical address mode.
         * @param maxAnisotropy Requested maximum anisotropy.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        /**
         * @brief Accepts default mip controls and rejects unsupported non-default controls.
         *
         * @param slot Sampler slot index.
         * @param maxMipLevel Requested maximum mip level.
         * @param lodBias Requested level-of-detail bias.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;
        /**
         * @brief Updates the constant blend color.
         *
         * @param r Red blend component.
         * @param g Green blend component.
         * @param b Blue blend component.
         * @param a Alpha blend component.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;
        /**
         * @brief Updates the active stencil reference value.
         *
         * @param value New stencil reference value.
         */
        void SetReferenceStencil(int value) override;
        /**
         * @brief Updates the active scissor rectangle.
         *
         * @param x Horizontal rectangle origin.
         * @param y Vertical rectangle origin.
         * @param w Rectangle width.
         * @param h Rectangle height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;
        /**
         * @brief Updates the active viewport and depth range.
         *
         * @param x Horizontal viewport origin.
         * @param y Vertical viewport origin.
         * @param w Viewport width.
         * @param h Viewport height.
         * @param minDepth Minimum normalized depth value.
         * @param maxDepth Maximum normalized depth value.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Creates a native Metal vertex buffer.
         *
         * @param vertexCapacity Initial buffer capacity in bytes.
         * @return The newly allocated vertex-buffer renderer.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;
        /**
         * @brief Creates a native 16-bit Metal index buffer.
         *
         * @param indexCapacity Initial index capacity.
         * @return The newly allocated 16-bit index-buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;
        /**
         * @brief Creates a native 32-bit Metal index buffer.
         *
         * @param indexCapacity Initial index capacity.
         * @return The newly allocated 32-bit index-buffer renderer.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;
        /**
         * @brief Draws non-indexed colored primitives through a built-in Metal pipeline.
         *
         * @param vb Vertex-buffer renderer containing the draw data.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Requested primitive topology.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        /**
         * @brief Draws indexed colored primitives through a built-in Metal pipeline.
         *
         * @param vb Vertex-buffer renderer containing the draw vertices.
         * @param ib Index-buffer renderer containing the draw indices.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Requested primitive topology.
         * @param primitiveCount Number of primitives to draw.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        /**
         * @brief Draws one-stream non-indexed primitives and validates current draw parameters.
         *
         * @param vb Vertex-buffer renderer containing the draw data.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Requested primitive topology.
         * @param primitiveCount Number of primitives to draw.
         * @param params Normalized GPU draw parameters.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                              const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        /**
         * @brief Draws one-stream indexed primitives and validates current draw parameters.
         *
         * @param vb Vertex-buffer renderer containing the draw vertices.
         * @param ib Index-buffer renderer containing the draw indices.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Requested primitive topology.
         * @param primitiveCount Number of primitives to draw.
         * @param params Normalized GPU draw parameters.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        /**
         * @brief Inserts a Metal encoder debug signpost.
         *
         * @param marker Null-terminated marker text.
         */
        void SetStringMarkerEXT(const char* marker) override;

        /**
         * @brief Reports every current graphics capability explicitly and conservatively.
         *
         * @param capability Capability to query.
         * @return True only when that capability belongs to the supported Metal contract.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Opaque Objective-C++ implementation storage. */
        struct Impl;
        /**
         * @brief Returns mutable access to the implementation for renderer-owned helper objects.
         *
         * @return Mutable implementation storage.
         */
        Impl& impl();
        /**
         * @brief Returns immutable access to the implementation for renderer-owned helper objects.
         *
         * @return Immutable implementation storage.
         */
        const Impl& impl() const;

    private:
        std::shared_ptr<Impl> impl_;
    };
}
