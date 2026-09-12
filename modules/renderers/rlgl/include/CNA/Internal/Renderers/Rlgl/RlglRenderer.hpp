// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

#include <array>
#include <memory>

namespace CNA::Internal::Renderers::Rlgl
{
    /**
     * @brief Standalone-rlgl renderer device using a CNA-owned OpenGL 3.3 core context.
     *
     * This class owns no windowing or raylib framework state. The platform creates and swaps the
     * context, while the renderer initializes exactly one process-global rlgl instance inside it.
     */
    class RlglRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates an RLGL device on the supplied CNA presentation surface.
         *
         * @param args Platform surface, GL service, presentation, and multisample request.
         */
        explicit RlglRenderer(const GraphicsRendererCreateArgs& args);

        /** @brief Shuts down rlgl before releasing the CNA-owned OpenGL context. */
        ~RlglRenderer() override;

        /** @brief RLGL devices cannot be copied because each owns a GL context lifecycle. */
        RlglRenderer(const RlglRenderer&) = delete;

        /** @brief RLGL devices cannot be copy-assigned. */
        RlglRenderer& operator=(const RlglRenderer&) = delete;

        /**
         * @brief Clears the active color buffer.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         */
        void Clear(float r, float g, float b, float a) override;

        /** @brief Presents the current back buffer through CNA's platform context service. */
        void Present() override;

        /**
         * @brief Returns the logical viewport extent exposed to game code.
         *
         * @param width Receives the logical width.
         * @param height Receives the logical height.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Returns the physical drawable rectangle for the current presentation mode.
         *
         * @param x Receives the physical left edge.
         * @param y Receives the physical top edge.
         * @param width Receives the physical width.
         * @param height Receives the physical height.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;

        /**
         * @brief Refreshes the CNA-owned drawable size and scale snapshot.
         *
         * @param surface Updated surface information for the same stable window.
         */
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;

        /**
         * @brief Updates the logical resolution used by presentation scaling.
         *
         * @param width Logical width, or zero to use the physical width.
         * @param height Logical height, or zero to use the physical height.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Updates the presentation scaling policy.
         *
         * @param mode A CnaPresentationMode ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Applies a requested swap interval through the platform GL service.
         *
         * @param interval Number of retraces, with zero selecting immediate presentation.
         */
        void SetSwapInterval(int interval) override;

        /**
         * @brief Returns the swap interval most recently requested by CNA.
         *
         * @return The requested interval, whether or not the driver accepted it.
         */
        CNAEXT [[nodiscard]] int GetSwapIntervalEXT() const override { return swapInterval_; }

        /**
         * @brief Returns the multisample count actually granted for the back buffer.
         *
         * @return The granted sample count, or zero when multisampling is disabled.
         */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        /**
         * @brief Maps a presentation multisample request to the driver-granted count.
         *
         * @param requestedMultiSampleCount The requested count; retained only for interface parity.
         * @return The count actually granted for this device.
         */
        [[nodiscard]] int GetAppliedMultiSampleCountEXT(
            int requestedMultiSampleCount) const override;

        /**
         * @brief Reports whether the created back buffer has both depth and stencil planes.
         *
         * @return True only when both planes were granted.
         */
        [[nodiscard]] bool SupportsDepthStencil() const override;

        /**
         * @brief Reports whether the created back buffer has a depth plane.
         *
         * @return True when the driver granted at least one depth bit.
         */
        [[nodiscard]] bool SupportsDepthBuffer() const override { return depthBits_ > 0; }

        /**
         * @brief Reports whether the created back buffer has a stencil plane.
         *
         * @return True when the driver granted at least one stencil bit.
         */
        [[nodiscard]] bool SupportsStencilBuffer() const override { return stencilBits_ > 0; }

        /**
         * @brief Answers implemented renderer capabilities without inferring them from GL support.
         *
         * @param capability Capability to query.
         * @return True only for a corresponding complete CNA path with focused validation.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief Reads an RGBA8 region of the current back buffer in top-left row order.
         *
         * @param x Left edge in game/backbuffer coordinates.
         * @param y Top edge in game/backbuffer coordinates.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param pixels Destination holding at least w * h * 4 bytes.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Creates a sampled two-dimensional texture.
         *
         * @param data Texture dimensions and initial pixels.
         * @return Renderer-owned texture record.
         * @throws std::runtime_error When the requested format has not passed its RLGL gate.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;

        /**
         * @brief Returns the current OpenGL context's maximum two-dimensional texture edge.
         * @return The `GL_MAX_TEXTURE_SIZE` value measured after rlgl initialization.
         */
        [[nodiscard]] int GetMaxTextureDimension() const override { return maxTextureSize_; }

        /**
         * @brief Classifies Texture2D formats whose native storage path has passed validation.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return Supported only for implemented exact layouts; Unsupported otherwise.
         */
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Prevents Color transfers from reinterpreting signed-normalized formats.
         * @param surfaceFormat Raw `SurfaceFormat` ordinal.
         * @return Unsupported for signed-normalized layouts and Defer otherwise.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyColorTransferFormatEXT(
            int surfaceFormat) const override;

        /**
         * @brief Creates the renderer-owned SpriteBatch implementation.
         *
         * @return Never returns until RLGL-010 implements the batcher.
         * @throws System::NotSupportedException Until RLGL-010 is complete.
         */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /**
         * @brief Applies XNA filter, U/V addressing, and anisotropy to a texture slot.
         * @param slot Texture unit index.
         * @param filter Raw `TextureFilter` ordinal.
         * @param addressU Raw U `TextureAddressMode` ordinal.
         * @param addressV Raw V `TextureAddressMode` ordinal.
         * @param maxAnisotropy Requested maximum anisotropy.
         */
        void ApplySamplerState(
            int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;

        /**
         * @brief Applies XNA mip-level selection and bias to a texture slot.
         * @param slot Texture unit index.
         * @param maxMipLevel Most detailed selectable mip level.
         * @param lodBias Level-of-detail bias.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief Applies XNA W addressing to a texture slot.
         * @param slot Texture unit index.
         * @param addressW Raw W `TextureAddressMode` ordinal.
         */
        void ApplySamplerAddressW(int slot, int addressW) override;

        /**
         * @brief Restores the default framebuffer or rejects an unsupported render target set.
         *
         * @param renderTargets Ordered target bindings, or nullptr for the back buffer.
         * @param count Number of bindings; zero restores the back buffer.
         * @throws System::NotSupportedException When count is greater than zero.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /**
         * @brief Clears color and depth planes in one operation.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param depth Depth clear value.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Clears the depth plane.
         *
         * @param depth Depth clear value.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Clears the stencil plane.
         *
         * @param stencil Stencil clear value.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Clears depth and stencil planes in one operation.
         *
         * @param depth Depth clear value.
         * @param stencil Stencil clear value.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Clears color and stencil planes in one operation.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param stencil Stencil clear value.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Clears color, depth, and stencil planes in one operation.
         *
         * @param r Red clear component.
         * @param g Green clear component.
         * @param b Blue clear component.
         * @param a Alpha clear component.
         * @param depth Depth clear value.
         * @param stencil Stencil clear value.
         */
        void ClearColorDepthAndStencil(
            float r, float g, float b, float a, float depth, int stencil) override;

        /**
         * @brief Enables or disables depth testing through rlgl.
         *
         * @param enabled True to enable depth testing.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Enables or disables color blending through rlgl.
         *
         * @param enabled True to enable blending.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Enables or disables depth writes through rlgl.
         *
         * @param enabled True to enable depth writes.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Creates a vertex buffer.
         *
         * @param vertexCapacity Maximum vertex count.
         * @return Never returns until RLGL-011 implements the resource.
         * @throws System::NotSupportedException Until RLGL-011 is complete.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;

        /**
         * @brief Creates a 16-bit index buffer.
         *
         * @param indexCapacity Maximum index count.
         * @return Never returns until RLGL-011 implements the resource.
         * @throws System::NotSupportedException Until RLGL-011 is complete.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;

        /**
         * @brief Creates a 32-bit index buffer.
         *
         * @param indexCapacity Maximum index count.
         * @return Never returns until RLGL-011 implements the resource.
         * @throws System::NotSupportedException Until RLGL-011 is complete.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;

        /**
         * @brief Draws non-indexed colored primitives.
         *
         * @param vb Vertex buffer.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives.
         * @throws System::NotSupportedException Until RLGL-011 is complete.
         */
        void DrawColoredPrimitives(
            const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
            const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Draws indexed colored primitives.
         *
         * @param vb Vertex buffer.
         * @param ib Index buffer.
         * @param world World transform.
         * @param view View transform.
         * @param projection Projection transform.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives.
         * @throws System::NotSupportedException Until RLGL-011 is complete.
         */
        void DrawIndexedColoredPrimitives(
            const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Applies a physical GL viewport and depth range.
         *
         * @param x Left edge in top-left-origin drawable coordinates.
         * @param y Top edge in top-left-origin drawable coordinates.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param minDepth Minimum depth value.
         * @param maxDepth Maximum depth value.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Applies the scissor rectangle in top-left-origin drawable coordinates.
         *
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

    private:
        struct SamplerRecord
        {
            unsigned int id = 0;
            int filter = 0;
            int addressU = 0;
            int addressV = 0;
            int addressW = 0;
            int maxAnisotropy = 4;
            int maxMipLevel = 0;
            float lodBias = 0.0f;
        };

        void CreateContext(int requestedMultiSampleCount);
        void GetPhysicalSize(int& width, int& height) const;
        void GetLogicalSize(int& width, int& height) const;
        SamplerRecord& GetSamplerRecord(int slot);
        void ApplySamplerRecord(int slot, SamplerRecord& sampler);

        PlatformGlSurfaceState surface_;
        CNA::Platform::IPlatformGlContext* platformGlService_ = nullptr;
        std::unique_ptr<PlatformGlContextOwner> platformContext_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::Letterbox;
        int swapInterval_ = 1;
        int multiSampleCount_ = 0;
        int depthBits_ = 0;
        int stencilBits_ = 0;
        int maxTextureSize_ = 0;
        int maxSamplerSlots_ = 0;
        float maxSamplerAnisotropy_ = 1.0f;
        std::array<SamplerRecord, 16> samplers_{};
        bool lifecycleClaimed_ = false;
        bool rlglInitialized_ = false;
        bool registered_ = false;
    };
}
