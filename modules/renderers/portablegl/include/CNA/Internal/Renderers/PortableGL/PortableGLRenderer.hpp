// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::PortableGL
{
    /**
     * Renderer handle for a vertex buffer, backed by a real PortableGL (`rswinkle/PortableGL`)
     * GPU-shaped buffer object created through `glGenBuffers`/`glBufferData`. Only the 16-byte
     * `VertexPositionColor` layout (float3 position + 4-byte packed color) is understood by the
     * colored 3D draw path this renderer implements; the caller's complete `VertexDeclaration` is
     * remembered so every draw can verify -- through the shared
     * `CNA::Internal::Graphics::RequireFaithfulVertexDeclaration()` boundary -- that the layout the
     * native fetch programs really is the layout the declaration describes. A stride alone cannot
     * establish that: two different declarations can share a stride.
     */
    class PortableGLVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Creates a real PortableGL buffer object owned by @p pglContext.
         *
         * @param pglContext Opaque pointer to the owning renderer's real PortableGL `glContext`
         *                   (kept `void*` here so this public header never needs to include
         *                   `portablegl.h`; cast back to `glContext*` only inside the .cpp).
         * @param vertexCapacity Initial vertex capacity, matching `IGraphicsRenderer::CreateVertexBuffer`.
         */
        PortableGLVertexBufferRenderer(void* pglContext, int vertexCapacity);
        /** @brief Deletes the underlying PortableGL buffer object. */
        ~PortableGLVertexBufferRenderer() override;

        /**
         * @brief Uploads @p vertex_count vertices of @p stride_in_bytes bytes each.
         *
         * @param data            Packed vertex data.
         * @param vertex_count    Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;

        /**
         * @brief Remembers the caller's complete vertex declaration for the draw-time fidelity check.
         *
         * @param vertexDeclaration Full declaration, including stride and elements in declaration order.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /** @brief Number of vertices the most recent upload (or the initial capacity) describes. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief CNAEXT. Real PortableGL buffer object handle (`glGenBuffers` id), 0 if never uploaded. */
        CNAEXT [[nodiscard]] unsigned int GLBufferHandle() const { return glBuffer_; }
        /** @brief CNAEXT. Stride in bytes of the most recent `SetData()` upload. */
        CNAEXT [[nodiscard]] std::size_t StrideInBytes() const { return stride_; }
        /** @brief CNAEXT. The declaration `SetVertexDeclaration()` last propagated, for the draw-time guard. */
        CNAEXT [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& DeclaredLayout() const
        {
            return declared_;
        }

    private:
        void* pglContext_;
        unsigned int glBuffer_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        CNA::Internal::Graphics::DeclaredVertexLayout declared_;
    };

    /** @brief Renderer handle for a 16- or 32-bit index buffer, backed by a real PortableGL buffer object. */
    class PortableGLIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Creates a real PortableGL buffer object owned by @p pglContext.
         *
         * @param pglContext See `PortableGLVertexBufferRenderer`'s identical parameter.
         * @param indexCapacity Initial index capacity.
         * @param thirtyTwoBit  Declared element width; uploads must use the same width.
         */
        PortableGLIndexBufferRenderer(void* pglContext, int indexCapacity, bool thirtyTwoBit);
        /** @brief Deletes the underlying PortableGL buffer object. */
        ~PortableGLIndexBufferRenderer() override;

        /**
         * @brief Uploads @p index_count 16-bit indices.
         * @param data        Packed `uint16_t` indices.
         * @param index_count Number of indices.
         */
        void SetData16(const void* data, int index_count) override;
        /**
         * @brief Uploads @p index_count 32-bit indices.
         * @param data        Packed `uint32_t` indices.
         * @param index_count Number of indices.
         */
        void SetData32(const void* data, int index_count) override;
        /** @brief Number of indices the most recent upload (or the initial capacity) describes. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        /** @brief Whether the most recent upload used 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief CNAEXT. Real PortableGL buffer object handle (`glGenBuffers` id), 0 if never uploaded. */
        CNAEXT [[nodiscard]] unsigned int GLBufferHandle() const { return glBuffer_; }

    private:
        void* pglContext_;
        unsigned int glBuffer_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
    };

    /**
     * Renderer handle for a texture, backed by a real PortableGL texture object created through
     * `glGenTextures`/`glTexImage2D`. There is no native SDL texture behind this handle; the
     * renderer is CPU-only and keeps its PortableGL object private.
     */
    class PortableGLTextureRenderer final : public ITextureRenderer
    {
    public:
        /**
         * @brief Creates and uploads a real PortableGL texture object.
         * @param pglContext See `PortableGLVertexBufferRenderer`'s identical parameter.
         * @param data       Source RGBA8 image, top row first.
         */
        PortableGLTextureRenderer(void* pglContext, const ImageData& data);
        /** @brief Deletes the underlying PortableGL texture object. */
        ~PortableGLTextureRenderer() override;

        /** @brief Texture width in texels. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Texture height in texels. */
        [[nodiscard]] int GetHeight() const override { return height_; }
        /**
         * @brief Replaces the whole level-0 image in place.
         * @param rgba   Source pixels, tightly packed RGBA8 rows, top row first.
         * @param stride Row pitch in bytes; must be `GetWidth() * 4`.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /**
         * @brief Uploads a mip level; only level 0 exists on a PortableGL texture.
         *
         * A PortableGL texture carries exactly one image (`glGenerateMipmap` is an upstream no-op
         * and this renderer allocates no chain), and `texture2D()` samples that one image whatever
         * the minification. A level above 0 is therefore not renderer state at all: `Texture2D`
         * keeps its own CPU copy of it, which is what answers `GetData`, and no draw on this
         * renderer could ever select it. Stated as an explicit override rather than inherited, so
         * the decision is visible where the data arrives.
         *
         * @param level  Mip level to write.
         * @param rgba   Source pixels, tightly packed RGBA8 rows, top row first.
         * @param levelW Width of this level in texels.
         * @param levelH Height of this level in texels.
         */
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /** @brief CNAEXT. Real PortableGL texture object handle (`glGenTextures` id). */
        CNAEXT [[nodiscard]] unsigned int GLTextureHandle() const { return glTexture_; }

        /**
         * @brief CNAEXT. Installs a CNA `SamplerState`'s filter/address mode on this texture object.
         *
         * PortableGL keeps filter and wrap state on the texture object itself (there is no separate
         * sampler object), so the SpriteBatch path applies the batch's resolved `SamplerState` here
         * immediately before each draw. Unsupported requests are rejected rather than silently
         * substituted -- see the implementation's own mapping notes.
         *
         * @param filter   Raw `TextureFilter` ordinal.
         * @param addressU Raw `TextureAddressMode` ordinal for U.
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         */
        CNAEXT void ApplySamplerState(int filter, int addressU, int addressV) const;

    private:
        void* pglContext_;
        unsigned int glTexture_ = 0;
        int width_ = 0;
        int height_ = 0;
    };

    class PortableGLRenderer;

    /**
     * SpriteBatch handle for the PortableGL renderer. Every `Draw()` call is submitted immediately
     * as a real textured-quad draw through PortableGL's own program/draw-call machinery (a
     * dedicated textured vertex/fragment shader pair sampling the bound texture via PortableGL's
     * `texture2D()`), not a CPU blit. The batch's resolved `SamplerState` really reaches the
     * sampled texture, and a custom `Effect` is refused rather than quietly drawn with the built-in
     * shader.
     */
    class PortableGLSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Binds this SpriteBatch handle to its owning renderer.
         * @param owner The renderer whose PortableGL context submits the quads.
         */
        explicit PortableGLSpriteBatchRenderer(PortableGLRenderer& owner);

        /** @brief Opens a batch; `Draw()` is only legal between `Begin()` and `End()`. */
        void Begin() override { begun_ = true; }
        /** @brief Closes a batch opened by `Begin()`. */
        void End() override { begun_ = false; }
        /**
         * @brief Sets the transform applied on top of the internal 2D ortho projection.
         * @param m Transform matrix, composed as `transform * ortho`.
         */
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }

        /**
         * @brief Rejects a custom `Effect`: PortableGL has no programmable shader stage a CNA
         *        `Effect` could be compiled into.
         *
         * @param effect The batch's custom effect; only `nullptr` (the built-in sprite shader) is
         *               accepted.
         * @throws System::NotSupportedException When @p effect is not null.
         */
        void SetCustomEffect(Effect* effect) override;

        /**
         * @brief Records the batch's texture filter, applied to the sampled texture at draw time.
         * @param textureFilter Raw `TextureFilter` ordinal.
         * @throws System::NotSupportedException When PortableGL cannot express the filter.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Records the batch's texture address modes, applied to the sampled texture at draw time.
         * @param addressU Raw `TextureAddressMode` ordinal for U.
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         * @throws System::NotSupportedException When PortableGL cannot express an address mode.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Draws @p texture at its own size with no tint.
         * @param texture Texture to sample.
         * @param x       Destination left edge, in pixels.
         * @param y       Destination top edge, in pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source rectangle of @p texture into a destination rectangle.
         * @param texture              Texture to sample.
         * @param destinationRectangle Destination rectangle, in pixels.
         * @param sourceRectangle      Source rectangle, in texels.
         * @param color                Per-vertex tint.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Rotated/mirrored counterpart of the rectangle overload.
         * @param texture              Texture to sample.
         * @param destinationRectangle Destination rectangle, in pixels.
         * @param sourceRectangle      Source rectangle, in texels.
         * @param color                Per-vertex tint.
         * @param rotation             Rotation about @p origin, in radians.
         * @param origin               Rotation origin, in source texels.
         * @param effects              Horizontal/vertical mirroring flags.
         * @param layerDepth           Ignored: every Draw() is submitted immediately.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        /** @brief CNAEXT. Whether a `Begin()` is currently active without a matching `End()`. */
        CNAEXT [[nodiscard]] bool IsBegunEXT() const { return begun_; }

    private:
        PortableGLRenderer& owner_;
        Matrix transform_ = Matrix::getIdentityProperty();
        bool begun_ = false;
        /// Resolved batch sampler. SpriteBatch::Begin() always (re-)applies one -- a null
        /// SamplerState resolves to SamplerState::LinearClamp there -- so these defaults only
        /// describe the state before the very first Begin().
        int samplerFilter_ = 0;    // TextureFilter::Linear
        int samplerAddressU_ = 1;  // TextureAddressMode::Clamp
        int samplerAddressV_ = 1;  // TextureAddressMode::Clamp
    };

    /**
     * PortableGL graphics renderer -- a genuine CPU software OpenGL 3.x-ish renderer built on
     * `rswinkle/PortableGL` (https://github.com/rswinkle/PortableGL, MIT licensed, single C99
     * header). Same architectural shape as the Headless/Software renderers -- no window, no GPU
     * library, `Present()` is a no-op, no native window or SDL renderer exists,
     * and pixel truth is exposed via `ReadBackbuffer()` -- but unlike Software's hand-rolled
     * rasterizer, the actual rasterization/shading pipeline is delegated to real PortableGL API
     * calls (`glGenBuffers`/`glBufferData`/`glVertexAttribPointer`, `pglCreateProgram` with real C
     * vertex+fragment shader callbacks, `glDrawArrays`/`glDrawElements`, `glClear`, `glViewport`,
     * `glTexImage2D`, `glBlendFuncSeparate`, `glStencilOpSeparate`, `glScissor`, `glCullFace`,
     * `glPolygonMode`).
     *
     * Scope, stated as capability truth rather than intent:
     *  - 3D: unlit `VertexPositionColor` (stride 16) and textured `VertexPositionTexture`
     *    (stride 20) BasicEffect routes. `VertexColorEnabled`, `DiffuseColor`, `Alpha`, texture 0,
     *    and sampler filter/address state are honoured. Lighting, fog, alpha test, skinning,
     *    dual-texture, environment mapping, PBR, instancing and multi-stream input are **refused
     *    deterministically**, never approximated.
     *  - 2D: a real textured SpriteBatch quad path with the batch's own `SamplerState`.
     *  - State: `BlendState` (separate RGB/alpha factors and equations, `BlendFactor`,
     *    target-0 `ColorWriteChannels`), `DepthStencilState` (depth **and** stencil, including
     *    two-sided stencil and standalone `ReferenceStencil`), `RasterizerState`
     *    (`CullMode`, `FillMode.WireFrame`, `ScissorTestEnable`, depth bias), `Viewport`
     *    (including `MinDepth`/`MaxDepth`) and `ScissorRectangle` all reach real PortableGL state.
     *  - Not implemented: render targets, cube/3D textures, occlusion queries, custom Effects,
     *    MSAA, anisotropic filtering, mip mapping. `SupportsCapability()` answers each of those
     *    truthfully, and the corresponding public entry points refuse rather than no-op.
     */
    class PortableGLRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the PortableGL context and its CPU framebuffer.
         * @param virtualWidth  Backbuffer width in pixels.
         * @param virtualHeight Backbuffer height in pixels.
         */
        PortableGLRenderer(int virtualWidth, int virtualHeight);
        /** @brief Destroys the PortableGL context and every renderer-owned native object. */
        ~PortableGLRenderer() override;

        /**
         * @brief Clears the color buffer.
         * @param r,g,b,a Clear color components in 0..1.
         */
        void Clear(float r, float g, float b, float a) override;
        /** @brief No-op: this renderer has no swap chain. */
        void Present() override {}
        /**
         * @brief Reports the logical backbuffer size.
         * @param width  Receives the width in pixels.
         * @param height Receives the height in pixels.
         */
        void GetViewportSize(int& width, int& height) override;
        /**
         * @brief Reallocates PortableGL's framebuffer at a new size.
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;
        /** @brief No-op: there is no window to scale into. */
        void SetPresentationMode(int /*mode*/) override {}

        /**
         * @brief Reads back a region of the backbuffer as RGBA8, top row first.
         * @param x,y    Top-left corner of the region, in pixels.
         * @param w,h    Region size in pixels.
         * @param pixels Destination for `w * h * 4` bytes.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Reports the format the back buffer really has, ignoring the request.
         *
         * A PortableGL context is created with one fixed pixel layout (the default 32-bit
         * `PGL_ABGR32`, i.e. RGBA8 in memory order), so a game that asks for anything else does
         * not get it. Reporting the applied format keeps `PresentationParameters.BackBufferFormat`
         * describing the surface that exists rather than echoing an unhonoured request.
         *
         * @param requestedFormat Requested `SurfaceFormat` ordinal.
         * @return `SurfaceFormat::Color`'s ordinal, always.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override;

        /**
         * @brief Reports the depth/stencil format the back buffer really has.
         *
         * PortableGL's default build allocates a combined 24-bit depth + 8-bit stencil buffer
         * unconditionally; there is no configuration in which the depth or stencil plane is absent.
         *
         * @param requestedFormat Requested `DepthFormat` ordinal.
         * @return `DepthFormat::Depth24Stencil8`'s ordinal, always.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int requestedFormat) const override;

        /**
         * @brief Creates a real PortableGL texture object from @p data.
         * @param data Source RGBA8 image.
         * @return The new texture handle.
         */
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        /**
         * @brief Creates a SpriteBatch handle bound to this renderer.
         * @return The new SpriteBatch handle.
         */
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /**
         * @brief Restores the default backbuffer, or refuses a real render-target binding.
         *
         * PortableGL owns exactly one framebuffer per context and creates no render targets
         * (`CreateRenderTarget2D()`/`CreateRenderTargetCube()` keep the interface's nullptr
         * defaults, which is why `GraphicsDevice::SetRenderTargets` already refuses a public
         * binding one step earlier). A non-empty set reaching this renderer is therefore refused
         * instead of being accepted and discarded.
         *
         * @param renderTargets Normalized binding descriptors, or null.
         * @param count         Number of bindings; only 0 is accepted.
         * @throws System::NotSupportedException When @p count is greater than zero.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /**
         * @brief Restores the default backbuffer, or refuses a real render-target binding.
         * @param rt Render target to bind; only `nullptr` is accepted.
         * @throws System::NotSupportedException When @p rt is not null.
         */
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Restores the default backbuffer, or refuses a cube-face binding.
         * @param rt   Cube render target to bind; only `nullptr` is accepted.
         * @param face Face index; unused when @p rt is null.
         * @throws System::NotSupportedException When @p rt is not null.
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;

        /**
         * @brief Clears color and depth in one call.
         * @param r,g,b,a Clear color components in 0..1.
         * @param depth   Depth value in 0..1.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /**
         * @brief Clears the depth buffer.
         * @param depth Depth value in 0..1.
         */
        void ClearDepth(float depth) override;
        /**
         * @brief Clears the stencil buffer.
         * @param stencil Stencil value.
         */
        void ClearStencil(int stencil) override;
        /**
         * @brief Clears depth and stencil in one call.
         * @param depth   Depth value in 0..1.
         * @param stencil Stencil value.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /**
         * @brief Clears color and stencil in one call.
         * @param r,g,b,a Clear color components in 0..1.
         * @param stencil Stencil value.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /**
         * @brief Clears color, depth and stencil in one call.
         * @param r,g,b,a Clear color components in 0..1.
         * @param depth   Depth value in 0..1.
         * @param stencil Stencil value.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        /**
         * @brief Enables or disables the depth test (`glEnable`/`glDisable(GL_DEPTH_TEST)`).
         * @param enabled Whether the depth test is performed.
         */
        void SetDepthTestEnabled(bool enabled) override;
        /**
         * @brief Enables or disables blending (`glEnable`/`glDisable(GL_BLEND)`).
         * @param enabled Whether the currently installed blend function is applied.
         */
        void SetBlendEnabled(bool enabled) override;
        /**
         * @brief Enables or disables depth writes (`glDepthMask`).
         * @param enabled Whether passing fragments write the depth buffer.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Installs a complete `BlendState` as real PortableGL output-merger state.
         *
         * Maps the CNA `Blend`/`BlendFunction` enums onto PortableGL's own factors and equations
         * with explicit switches (never by ordinal coincidence), through
         * `glBlendFuncSeparate`/`glBlendEquationSeparate`, and installs target-0's
         * `ColorWriteChannels` via `glColorMask`. Blending is disabled entirely only for the exact
         * identity `(One, Zero)` on both channels -- CNA's `BlendState::Opaque` -- matching every
         * other CNA renderer's own fast path.
         *
         * @param colorSrcBlend  Raw `Blend` ordinal for the RGB source factor.
         * @param alphaSrcBlend  Raw `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw `Blend` ordinal for the RGB destination factor.
         * @param alphaDstBlend  Raw `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw `BlendFunction` ordinal for the RGB equation.
         * @param alphaBlendFunc Raw `BlendFunction` ordinal for the alpha equation.
         * @param writeState     Per-target color write masks and the coverage sample mask.
         * @throws System::NotSupportedException When a factor, equation or MRT-slot mask cannot be
         *         expressed on PortableGL.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Installs `GraphicsDevice.BlendFactor` as PortableGL's constant blend color.
         * @param r,g,b,a Constant color components in 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Installs a complete `DepthStencilState` as real PortableGL depth **and** stencil state.
         *
         * Depth maps onto `glEnable`/`glDisable(GL_DEPTH_TEST)`, `glDepthMask` and `glDepthFunc`;
         * stencil onto `glEnable`/`glDisable(GL_STENCIL_TEST)` plus the separate-face
         * `glStencilFuncSeparate`/`glStencilOpSeparate`/`glStencilMaskSeparate` trio. CNA's
         * counter-clockwise stencil half is installed on PortableGL's FRONT face, because
         * PortableGL classifies a triangle whose window-space winding is counter-clockwise as front
         * facing (`glFrontFace(GL_CCW)`, which this renderer states explicitly).
         *
         * @param depthEnable       Whether the depth test is performed.
         * @param depthWriteEnable  Whether passing fragments write the depth buffer.
         * @param depthFunc         Raw `CompareFunction` ordinal for the depth comparison.
         * @param stencilEnable     Whether the stencil test is performed.
         * @param stencilFunc       Raw `CompareFunction` ordinal for the clockwise-face stencil test.
         * @param stencilPass       Raw `StencilOperation` ordinal applied when both tests pass.
         * @param stencilFail       Raw `StencilOperation` ordinal applied when the stencil test fails.
         * @param stencilDepthFail  Raw `StencilOperation` ordinal applied when the depth test fails.
         * @param stencilMask       Stencil read mask.
         * @param stencilWriteMask  Stencil write mask.
         * @param referenceStencil  Stencil reference value.
         * @param twoSidedStencilMode Whether the counter-clockwise half below is used at all.
         * @param ccwStencilFunc      Raw `CompareFunction` ordinal for counter-clockwise faces.
         * @param ccwStencilPass      Raw `StencilOperation` ordinal for counter-clockwise faces.
         * @param ccwStencilFail      Raw `StencilOperation` ordinal for counter-clockwise faces.
         * @param ccwStencilDepthFail Raw `StencilOperation` ordinal for counter-clockwise faces.
         * @throws System::NotSupportedException When a comparison or operation ordinal is unknown.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;

        /**
         * @brief Installs `GraphicsDevice.ReferenceStencil` on its own, without a full state re-apply.
         * @param value New stencil reference value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Installs a complete `RasterizerState` as real PortableGL rasterizer state.
         *
         * @param cullMode            Raw `CullMode` ordinal.
         * @param fillMode            Raw `FillMode` ordinal (`WireFrame` maps to `glPolygonMode(GL_LINE)`).
         * @param scissorTestEnable   Whether the scissor rectangle clips rasterization.
         * @param depthBias           Constant depth bias (`glPolygonOffset` units).
         * @param slopeScaleDepthBias Slope-scaled depth bias (`glPolygonOffset` factor).
         * @throws System::NotSupportedException When a cull or fill ordinal is unknown.
         */
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias,
                                  float slopeScaleDepthBias) override;

        /**
         * @brief Installs `GraphicsDevice.ScissorRectangle`, converting XNA's top-left origin to
         *        PortableGL's bottom-left window origin.
         * @param x,y Top-left corner in XNA coordinates.
         * @param w,h Rectangle size in pixels.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Installs `GraphicsDevice.Viewport`, converting XNA's top-left origin to
         *        PortableGL's bottom-left window origin, plus the depth range.
         * @param x,y      Top-left corner in XNA coordinates.
         * @param w,h      Viewport size in pixels.
         * @param minDepth Near end of the depth range.
         * @param maxDepth Far end of the depth range.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Stores a `GraphicsDevice.SamplerStates` slot for textured 3D draws.
         *
         * The textured `BasicEffect` route applies slot zero to its sampled texture. SpriteBatch
         * continues to carry its independently resolved sampler through
         * `ISpriteBatchRenderer::SetSamplerFilter`/`SetSamplerAddressMode`.
         *
         * @param slot          Texture unit index.
         * @param filter        Raw `TextureFilter` ordinal.
         * @param addressU      Raw `TextureAddressMode` ordinal for U.
         * @param addressV      Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy level.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Accepts the mip half of a sampler slot, which PortableGL has no storage for.
         *
         * PortableGL textures carry exactly one mip level (`glGenerateMipmap` is an upstream no-op
         * and this renderer never allocates a chain), so `MaxMipLevel`/`MipMapLevelOfDetailBias`
         * have nothing to select between.
         *
         * @param slot        Texture unit index.
         * @param maxMipLevel Highest mip level the sampler may use.
         * @param lodBias     Level-of-detail bias.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief Creates a real PortableGL buffer object for vertex data.
         * @param vertex_capacity Initial vertex capacity.
         * @return The new vertex-buffer handle.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        /**
         * @brief Creates a real PortableGL buffer object for 16-bit index data.
         * @param index_capacity Initial index capacity.
         * @return The new index-buffer handle.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        /** Creates a real PortableGL buffer object declared for 32-bit index data. */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        /**
         * @brief Draws vertex-colored primitives with the built-in unlit program.
         * @param vb            Vertex buffer to read from (stride 16).
         * @param world,view,projection Per-draw transform matrices.
         * @param primitive     Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world,
                                   const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive,
                                   int primitiveCount) override;

        /**
         * @brief Indexed counterpart of `DrawColoredPrimitives`.
         * @param vb            Vertex buffer to read from (stride 16).
         * @param ib            Index buffer to read from.
         * @param world,view,projection Per-draw transform matrices.
         * @param primitive     Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world,
                                          const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive,
                                          int primitiveCount) override;

        /**
         * @brief Effect-aware non-indexed draw honouring every offset CNA's draw contract carries.
         *
         * `params.vertexStart` selects the first vertex (`glDrawArrays`'s own `first`) and the bound
         * stream's `VertexOffset` shifts the attribute base, so a draw never silently starts at
         * vertex zero. The supported `BasicEffect` subset (`VertexColorEnabled`, `DiffuseColor`,
         * `Alpha`, `TextureEnabled`, `Texture`, and sampler slot zero) is applied; every unsupported
         * effect configuration is refused.
         *
         * @param vb            Vertex buffer named by `params.vertexStreams[0]`.
         * @param world,view,projection Per-draw transform matrices.
         * @param primitive     Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params        Per-draw effect and stream parameters.
         * @throws System::NotSupportedException When the draw asks for an unsupported effect,
         *         layout or stream combination.
         */
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world,
                              const Matrix& view,
                              const Matrix& projection,
                              PrimitiveType primitive,
                              int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Effect-aware indexed draw honouring `startIndex`, `baseVertex` and `VertexOffset`.
         *
         * PortableGL 0.100.0 exposes no `glDrawElementsBaseVertex`, so `baseVertex` is applied by
         * advancing the vertex attribute base by `baseVertex * stride` bytes -- exactly equivalent,
         * because PortableGL fetches attribute `i` from `offset + stride * i`. `startIndex` becomes
         * the byte offset handed to `glDrawElements`, for both 16- and 32-bit index buffers.
         *
         * @param vb            Vertex buffer named by `params.vertexStreams[0]`.
         * @param ib            Index buffer to read from.
         * @param world,view,projection Per-draw transform matrices.
         * @param primitive     Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params        Per-draw effect and stream parameters.
         * @throws System::NotSupportedException When the draw asks for an unsupported effect,
         *         layout or stream combination.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib,
                                     const Matrix& world,
                                     const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive,
                                     int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Reports whether this renderer genuinely implements @p capability.
         * @param capability Capability to query.
         * @return True only when a real implementation, a public path and a regression test exist.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * CNAEXT. Internal helper used by `PortableGLSpriteBatchRenderer` to issue one real
         * textured-quad draw through the same PGL program/draw-call machinery the 3D route uses --
         * not part of `IGraphicsRenderer`.
         *
         * @param glTexture       PortableGL texture handle to sample.
         * @param positionsPixels Four corner positions (TL, TR, BR, BL), in destination pixel space.
         * @param uvs             Four corresponding normalized texture coordinates.
         * @param colorsRgba01    Four corresponding per-vertex tint colors, components in [0,1].
         * @param spriteTransform `ISpriteBatchRenderer::SetTransformMatrix()`'s current value,
         *                        composed with the internal ortho projection exactly as
         *                        `EasyGLRenderer`'s own SpriteBatch does (`transform * ortho`).
         */
        CNAEXT void DrawTexturedQuadEXT(unsigned int glTexture,
                                        const float positionsPixels[4][2],
                                        const float uvs[4][2],
                                        const float colorsRgba01[4][4],
                                        const Matrix& spriteTransform);

    private:
        /// The subset of `GpuDrawParams` the colored 3D route can genuinely execute, plus the
        /// offsets that select which records a draw reads. Keeping the two routes on one code path
        /// through this small value means the legacy (`GpuDrawParams`-less) entry points need no
        /// synthetic 18 KB `GpuDrawParams`, and that no offset can be honoured on one route and
        /// forgotten on the other.
        struct ColoredDrawState
        {
            /// `GpuDrawParams::diffuseColor` -- BasicEffect's (DiffuseColor [+ Emissive]) * Alpha,
            /// with Alpha in [3]. White is the interface's own effect-less default.
            float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            /// `GpuDrawParams::vertexColorEnabled`. The legacy route is documented as "equivalent
            /// to BasicEffect with VertexColorEnabled = true", which is this default.
            bool vertexColorEnabled = true;
            /// Texture 0 for the unlit VertexPositionTexture route, or null for the colored route.
            const PortableGLTextureRenderer* texture = nullptr;
            /// The bound stream's own `VertexBufferBinding.VertexOffset`, in vertex elements.
            int streamVertexOffset = 0;
            /// First vertex for a non-indexed draw.
            int vertexStart = 0;
            /// First index element for an indexed draw.
            int startIndex = 0;
            /// Value added to every decoded index before the vertex fetch.
            int baseVertex = 0;
            /// Lowest decoded index the caller declares, relative to `baseVertex`.
            int minVertexIndex = 0;
            /// Size of the caller-declared decoded-index window; 0 on the legacy route.
            int numVertices = 0;
        };

        struct Impl;
        std::unique_ptr<Impl> impl_;
        int virtualWidth_;
        int virtualHeight_;

        /// Refuses every `GpuDrawParams` shape the colored route cannot execute, then extracts the
        /// part it can. Touches no PortableGL state, so a refused draw leaves the context exactly
        /// as the previous draw left it.
        static ColoredDrawState TranslateDrawParams(const GpuDrawParams& params, const char* route);

        /// Shared body of both non-indexed routes.
        void DrawColoredCommon(const IVertexBufferRenderer& vb,
                               const Matrix& world, const Matrix& view, const Matrix& projection,
                               PrimitiveType primitive, int primitiveCount,
                               const ColoredDrawState& state, const char* route);
        /// Indexed counterpart of DrawColoredCommon().
        void DrawIndexedColoredCommon(const IVertexBufferRenderer& vb,
                                      const IIndexBufferRenderer& ib,
                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                      PrimitiveType primitive, int primitiveCount,
                                      const ColoredDrawState& state, const char* route);
        /// Re-installs the cached scissor rectangle, or restores full-framebuffer bounds.
        void ApplyScissorState();
    };
}
