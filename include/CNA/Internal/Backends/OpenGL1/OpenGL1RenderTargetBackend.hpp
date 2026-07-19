#pragma once
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/OpenGL1/OpenGL1ContextRecovery.hpp"

namespace CNA::Internal::Backends::OpenGL1
{
    /**
     * @brief FBO-backed 2D render target for the OPENGL1 backend (plan_opengl1.md phase 2).
     *
     * Requires the ARB_framebuffer_object/core (>=3.0) entry points to be loadable via
     * SDL_GL_GetProcAddress -- see TryLoadOpenGL1FramebufferObjectFunctions() below and
     * OpenGL1GraphicsBackend::CreateRenderTarget2D(), which returns nullptr (the documented
     * IGraphicsBackend contract) when they cannot be loaded, preserving a strict capability
     * fallback rather than silently doing nothing. EXT_framebuffer_object (the older, narrower
     * extension -- different entry-point names, no combined depth+stencil attachment point) is
     * deliberately not supported: effectively unreachable on any GPU/driver from this decade,
     * and supporting it would meaningfully complicate this file for near-zero practical benefit.
     *
     * Does not support MSAA (GetMultiSampleCount() always 0, matching
     * SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing) == false) or mip-chain
     * auto-generation (mipMap is accepted but currently ignored -- level 0 only).
     */
    class OpenGL1RenderTargetBackend final : public IRenderTargetBackend, public IOpenGL1Recoverable
    {
    public:
        /**
         * @brief Creates the color texture, optional depth/stencil renderbuffer, and FBO.
         *
         * @param width       Render target width in pixels.
         * @param height      Render target height in pixels.
         * @param depthFormat Raw ordinal of Microsoft::Xna::Framework::Graphics::DepthFormat
         *                    (None=0, Depth16=1, Depth24=2, Depth24Stencil8=3).
         * @param registry    Context-loss recovery registry to register with, or nullptr when
         *                    context recovery is disabled (IGraphicsBackend::
         *                    SetContextRecoveryEnabled(false)).
         * @throws std::runtime_error if the resulting framebuffer is incomplete.
         */
        OpenGL1RenderTargetBackend(int width, int height, int depthFormat, OpenGL1ResourceRegistry* registry);
        ~OpenGL1RenderTargetBackend() override;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void BindGL() const override;
        void GetData(int level, int x, int y, int w, int h, void* data, int dataLength) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetColorGLHandle() const override { return colorTex_; }

        /**
         * @brief plan_opengl1.md phase 8: drops the FBO/texture/renderbuffer handles without
         * issuing any gl* calls. A render target's content is GPU-produced (no CPU shadow to
         * restore it from), so this only preserves the object's identity/dimensions/format --
         * RecreateGLResource() rebuilds an empty target, matching real XNA/FNA RenderTarget2D
         * semantics after a device reset (content is not implicitly preserved unless
         * RenderTargetUsage.PreserveContents, which this backend does not implement).
         */
        void ReleaseGLHandleOnly() override;
        /** @brief Rebuilds an empty FBO/color-texture/depth-renderbuffer of the same size/format. */
        void RecreateGLResource() override;

    private:
        void Build();
        unsigned int fbo_ = 0;
        unsigned int colorTex_ = 0;
        unsigned int depthRbo_ = 0;
        int width_ = 0;
        int height_ = 0;
        int depthFormat_ = 0;
        OpenGL1ResourceRegistry* registry_ = nullptr;
    };

    /**
     * @brief Loads the ARB_framebuffer_object/core-3.0 entry points OPENGL1's RenderTarget2D
     * support needs via SDL_GL_GetProcAddress.
     *
     * Must be called once after a GL context is current (OpenGL1GraphicsBackend's constructor,
     * gated on OpenGL1Capabilities::framebufferObject). Idempotent -- safe to call more than
     * once; the last call's result is what OpenGL1RenderTargetBackend's constructor checks.
     *
     * @return True only if every required entry point resolved to a non-null address.
     */
    bool TryLoadOpenGL1FramebufferObjectFunctions();
}
