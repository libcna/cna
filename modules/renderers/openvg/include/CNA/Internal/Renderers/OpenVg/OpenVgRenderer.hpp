// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <SDL3/SDL.h>

namespace CNA::Internal::Renderers::OpenVg
{
    /// Pure mapping from raw BlendState factors/BlendFunction (see IGraphicsRenderer::
    /// ApplyBlendState's own parameter doc) to a real ShivaVG VGBlendMode ordinal (VG_BLEND_SRC /
    /// VG_BLEND_SRC_OVER). Throws std::runtime_error for BlendState.Additive (ShivaVG declares
    /// VG_BLEND_ADDITIVE but never implements it) and for any other non-standard combination.
    /// Exposed standalone (contains no GL/OpenVG calls) so it can be unit tested directly -- see
    /// OpenVgRenderer.cpp for the full rationale.
    int BlendStateToVgBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                int colorDstBlend, int alphaDstBlend,
                                int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief OpenVG 1.1 vector-graphics renderer, implemented by ShivaVG on top of a real desktop
     * OpenGL context this renderer creates and owns itself (no EasyGL involved).
     *
     * See docs/openvg-renderer.md for the full capability boundary. In short: 2D-only (OpenVG has
     * no 3D pipeline at all -- every inherently-3D pure virtual throws by default, matching
     * Canvas/Skia's established pattern), real Clear/Present/textures/SpriteBatch through genuine
     * `vg*` OpenVG entry points, no render targets (ShivaVG has no EGL-VGImage-surface/FBO
     * equivalent to bind an off-screen VGImage as a draw target -- `CreateRenderTarget2D` keeps the
     * shared default `nullptr`, truthfully reporting "unsupported" rather than faking one).
     */
    class OpenVgRenderer final : public IGraphicsRenderer
    {
    public:
        OpenVgRenderer(SDL_Window* window, int virtualWidth, int virtualHeight, CnaPresentationMode mode);
        ~OpenVgRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        // OpenVG's VG_BLEND_MODE only expresses a handful of fixed Porter-Duff-style modes (see
        // docs/openvg-renderer.md's blend table) -- maps the 4 standard XNA BlendState presets and
        // throws for any other Blend/BlendFunction combination, same shape as Canvas's
        // BlendStateToCompositeOp (CanvasRenderer.hpp).
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        // OpenVG has no depth/stencil concept at all -- no render target, backbuffer included, has
        // one.
        [[nodiscard]] bool SupportsDepthStencil() const override { return false; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override
        {
            // OpenVG's VG_BLEND_ADDITIVE is declared by the spec but has no case in ShivaVG's own
            // updateBlendingStateGL (src/shPipeline.c) -- it silently falls through to normal
            // alpha blending. Reporting AdditiveBlending true would be exactly the "capability lie"
            // GraphicsCapability::AdditiveBlending's own doc comment warns against, so
            // ApplyBlendState throws for BlendState.Additive instead (see
            // BlendStateToVgBlendMode's own comment in OpenVgRenderer.cpp) and this reports false.
            //
            // Texture3D is reported false (honest: OpenVG/ShivaVG has no volume-texture concept at
            // all, and CreateTexture3D below only ever returns a discard-everything null object,
            // never real storage). This is what Texture3DTests.cpp's own dual fixture
            // (Texture3DTest, which self-skips when this is false, and
            // Texture3DUnsupportedRendererTest, which asserts the constructor throw when this is
            // false) is actually designed around -- see docs/openvg-renderer.md's capability table
            // for the one narrow, known consequence: modules/graphics/examples/
            // unsupported_3d_call_behavior_test.cpp's own Texture3D-specific expectation assumes a
            // renderer that reports this true, which is why that one shared fixture is not
            // registered for OPENVG (a renderer-owned equivalent is used instead).
            (void)capability;
            return false;
        }

        // ---- 3D: OpenVG is a 2D vector API with no 3D pipeline whatsoever. ----
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(
            int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(
            int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /// CNAEXT. The image-user-space Y size the renderer flips SpriteBatch draws against --
        /// OpenVgSpriteBatchRenderer needs this to compose its own per-draw matrices. Physical
        /// (framebuffer) pixels, not the logical/virtual size.
        [[nodiscard]] int GetPhysicalHeightEXT() const;

    private:
        void getLogicalSize(int& width, int& height) const;
        void applyDefaultViewportAndSurfaceSize();

        SDL_Window* window_ = nullptr;
        SDL_GLContext glContext_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        bool blendEnabled_ = true;
        int lastBlendMode_ = 0; // VG_BLEND_SRC_OVER
    };
}
