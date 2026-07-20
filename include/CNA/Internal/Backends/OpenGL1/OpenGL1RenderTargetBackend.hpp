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
     * SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing) == false).
     *
     * plan_opengl1.md item 21 (EasyGL parity, found 2026-07-20): mipMap now genuinely regenerates
     * the color texture's mip chain from level 0 every time the target is unbound, following
     * FNA3D's own `OPENGL_ResolveTarget` mechanism (games never render into non-zero mip levels
     * directly -- the whole chain is always regenerated wholesale from level 0). Content is
     * GPU-produced, so unlike Texture2D there is no CPU-side pixel buffer to box-filter from as a
     * last-resort fallback: generation is gated purely on glGenerateMipmap being loadable, which
     * is expected always true in practice (see TryLoadOpenGL1FramebufferObjectFunctions()'s own
     * comment -- glGenerateMipmap is part of the same ARB_framebuffer_object/core-3.0 entry-point
     * family RenderTarget2D support already requires). HasMips() reports whether the last unbind
     * actually generated a complete chain (false before the first unbind, or if the driver
     * genuinely lacks glGenerateMipmap).
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
         * @param mipMap      Whether UnbindAsRenderTarget() should regenerate a full mip chain
         *                    from level 0 each time the target stops being active.
         * @param registry    Context-loss recovery registry to register with, or nullptr when
         *                    context recovery is disabled (IGraphicsBackend::
         *                    SetContextRecoveryEnabled(false)).
         * @throws std::runtime_error if the resulting framebuffer is incomplete.
         */
        OpenGL1RenderTargetBackend(int width, int height, int depthFormat, bool mipMap, OpenGL1ResourceRegistry* registry);
        ~OpenGL1RenderTargetBackend() override;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void BindGL() const override;
        void GetData(int level, int x, int y, int w, int h, void* data, int dataLength) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetColorGLHandle() const override { return colorTex_; }

        /** @brief True if the last UnbindAsRenderTarget() call generated a complete mip chain. */
        bool HasMips() const { return hasMips_; }

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
        bool mipMap_ = false;
        bool hasMips_ = false;
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

    /**
     * @brief FBO-backed cube-map render target for the OPENGL1 backend (plan_opengl1.md item 24,
     * EasyGL parity).
     *
     * Combines the two pieces of machinery this backend already has: an `ARB_texture_cube_map`
     * cube texture (same allocation shape `OpenGL1TextureCubeBackend` already uses) and a single
     * reusable FBO whose color attachment is re-pointed at the requested face
     * (`GL_TEXTURE_CUBE_MAP_POSITIVE_X+face`) on every `BindAsRenderTargetFace()` call -- the
     * standard "one FBO, six re-attachments" pattern, not six separate FBOs. One depth/stencil
     * renderbuffer is shared across all six faces (only one face is ever the active draw target
     * at a time, so a single depth buffer sized to match is sufficient, matching every real
     * engine's own env-map-capture convention).
     *
     * `GetData()` reads back via `glGetTexImage` directly on the cube texture object (not
     * `glReadPixels` against the bound FBO) -- unlike a 2D render target, a cube map face's
     * rendered content is already retrievable straight from the texture object regardless of
     * which FBO/attachment point last wrote it, the same mechanism `OpenGL1TextureCubeBackend::
     * GetData()` already uses for CPU-uploaded content. Still needs the same row-flip
     * `OpenGL1RenderTargetBackend::GetData()` applies, though: GPU-rasterized content is
     * bottom-up, unlike a CPU-uploaded texture's top-down convention.
     *
     * Same capability/fallback shape as `OpenGL1RenderTargetBackend`: requires BOTH
     * `OpenGL1Capabilities::framebufferObject` and `::textureCubeMap`;
     * `OpenGL1GraphicsBackend::CreateRenderTargetCube()` returns nullptr when either is absent.
     * Does not support MSAA (`multiSampleCount` argument is accepted but ignored) -- out of scope
     * for this item; plan_opengl1.md item 25 addresses `RenderTarget2D` MSAA specifically, and
     * does not attempt to extend to six separate cube-face resolve targets.
     */
    class OpenGL1RenderTargetCubeBackend final : public IRenderTargetCubeBackend, public IOpenGL1Recoverable
    {
    public:
        /**
         * @brief Creates the cube texture, optional depth/stencil renderbuffer, and one reusable FBO.
         *
         * @param size        Width/height of each of the 6 faces, in pixels.
         * @param depthFormat Raw ordinal of Microsoft::Xna::Framework::Graphics::DepthFormat
         *                    (None=0, Depth16=1, Depth24=2, Depth24Stencil8=3).
         * @param mipMap      Whether UnbindAsRenderTarget() should regenerate a full mip chain
         *                    (all 6 faces) from level 0 each time the target stops being active.
         * @param registry    Context-loss recovery registry to register with, or nullptr when
         *                    context recovery is disabled.
         * @throws std::runtime_error if the resulting framebuffer is incomplete.
         */
        OpenGL1RenderTargetCubeBackend(int size, int depthFormat, bool mipMap, OpenGL1ResourceRegistry* registry);
        ~OpenGL1RenderTargetCubeBackend() override;

        int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetGLHandle() const override { return id_; }

        void GetData(int face, int level, int x, int y, int w, int h, void* data, int dataLength) const override;
        void BindGL() const override;

        /** @brief True if the last UnbindAsRenderTarget() call generated a complete mip chain. */
        bool HasMips() const { return hasMips_; }

        /** @brief Same rationale as OpenGL1RenderTargetBackend::ReleaseGLHandleOnly(). */
        void ReleaseGLHandleOnly() override;
        /** @brief Rebuilds an empty FBO/cube-texture/depth-renderbuffer of the same size/format. */
        void RecreateGLResource() override;

    private:
        void Build();
        unsigned int fbo_ = 0;
        unsigned int id_ = 0;
        unsigned int depthRbo_ = 0;
        int size_ = 0;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        bool hasMips_ = false;
        OpenGL1ResourceRegistry* registry_ = nullptr;
    };
}
