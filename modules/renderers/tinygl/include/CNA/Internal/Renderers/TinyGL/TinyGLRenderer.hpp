// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::TinyGL
{
    /**
     * Renderer handle for a vertex buffer.
     *
     * TinyGL cannot be pointed at an interleaved XNA vertex record, for two independent reasons:
     * its buffer objects are only readable as attribute arrays through
     * `glBindBufferAsArray(target, buffer, type, size, stride)`, which carries no attribute
     * *offset*; and its client arrays are `GLfloat*` with a stride counted in **extra floats
     * between records**, not bytes (`glColorPointer` ignores its `type` argument entirely and
     * always reads floats). A `VertexPositionColor` record -- three floats then a packed 4-byte
     * colour -- is not expressible either way.
     *
     * This handle therefore keeps the record bytes CNA-side, and the draw path de-interleaves the
     * buffer into tightly packed float arrays TinyGL's API actually defines
     * (`TinyGLRenderer::BindVertexArrays`). Everything after that -- transform, clip, cull,
     * rasterize, texture, blend -- is TinyGL's own vertex-array path.
     */
    class TinyGLVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        /**
         * @brief Creates a vertex buffer with room for @p vertexCapacity vertices.
         *
         * @param vertexCapacity Initial vertex capacity, matching `IGraphicsRenderer::CreateVertexBuffer`.
         */
        explicit TinyGLVertexBufferRenderer(int vertexCapacity);

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

        /** @brief CNAEXT. Stride in bytes of the most recent `SetData()` upload. */
        CNAEXT [[nodiscard]] std::size_t StrideInBytes() const { return stride_; }
        /** @brief CNAEXT. Raw record bytes the draw path points TinyGL's array pointers at. */
        CNAEXT [[nodiscard]] const std::uint8_t* Bytes() const { return storage_.data(); }
        /** @brief CNAEXT. Size of the record storage in bytes. */
        CNAEXT [[nodiscard]] std::size_t ByteSize() const { return storage_.size(); }
        /** @brief CNAEXT. The declaration `SetVertexDeclaration()` last propagated, for the draw-time guard. */
        CNAEXT [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& DeclaredLayout() const
        {
            return declared_;
        }

    private:
        std::vector<std::uint8_t> storage_;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        CNA::Internal::Graphics::DeclaredVertexLayout declared_;
    };

    /**
     * Renderer handle for a 16- or 32-bit index buffer.
     *
     * TinyGL has no `glDrawElements` at all (`TINYGL-0`, `tinygl-spike/README.md`), so an indexed
     * draw is replayed through `glArrayElement()` inside `glBegin`/`glEnd` -- TinyGL's own indexed
     * route. The decoded indices therefore stay CNA-side, exactly like the vertex records.
     */
    class TinyGLIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Creates an index buffer with room for @p indexCapacity indices.
         * @param indexCapacity Initial index capacity.
         */
        explicit TinyGLIndexBufferRenderer(int indexCapacity);

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

        /**
         * @brief CNAEXT. Decoded index at @p position, widened to 32 bits.
         * @param position Zero-based element position.
         * @return The stored index value.
         */
        CNAEXT [[nodiscard]] std::uint32_t IndexAt(int position) const;

    private:
        std::vector<std::uint32_t> indices_;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
    };

    /**
     * Renderer handle for a texture, backed by two real TinyGL texture objects
     * (`glGenTextures`/`glTexImage2D`).
     *
     * Two upstream constraints shape this handle, both established by `TINYGL-0`:
     *
     *  - `glTexImage2D` accepts `GL_RGB`/`GL_UNSIGNED_BYTE` only. CNA's RGBA8 source is therefore
     *    converted on upload, and TinyGL never sees an alpha channel. The ordinary texture keeps
     *    every texel visible for `BlendState::Opaque`; a second cutout texture writes texels below
     *    `kAlphaCutoutThreshold` as TinyGL's `TGL_NO_DRAW_COLOR` key (0xFF00FF). The renderer selects
     *    that second object only for the two documented alpha-preset approximations.
     *  - Every texture is resampled to 256x256 inside `glTexImage2D` (`TGL_FEATURE_TEXTURE_DIM`),
     *    with nearest-neighbour and no interpolation. `GetWidth()`/`GetHeight()` keep reporting the
     *    size the game asked for, because that is what `Texture2D.Width`/`Height` must return; the
     *    resampling is a sampling-fidelity limitation, documented in `docs/tinygl-renderer.md`.
     *
     * The untouched RGBA8 source is kept as a CPU shadow so `GetData()` returns what was uploaded
     * rather than what TinyGL's lossy storage could give back.
     */
    class TinyGLTextureRenderer final : public ITextureRenderer
    {
    public:
        /** @brief Effective source alpha values strictly below this become the discard key. */
        static constexpr std::uint8_t kAlphaCutoutThreshold = 128;

        /**
         * @brief Creates and uploads a real TinyGL texture object.
         * @param data Source RGBA8 image, top row first.
         */
        explicit TinyGLTextureRenderer(const ImageData& data);
        /** @brief Deletes the underlying TinyGL texture object. */
        ~TinyGLTextureRenderer() override;

        /** @brief Texture width in texels, as requested by the game. */
        [[nodiscard]] int GetWidth() const override { return width_; }
        /** @brief Texture height in texels, as requested by the game. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Replaces the whole level-zero image and re-uploads it to TinyGL.
         * @param rgba   Source RGBA8 pixels, top row first.
         * @param stride Source row pitch in bytes.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Reads back a region of the CPU shadow as RGBA8.
         *
         * @param level      Mip level; only 0 exists on this renderer.
         * @param x,y        Top-left corner of the region.
         * @param w,h        Region size in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True when the region was copied, false when @p level or the region is invalid.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief CNAEXT. Selects the real TinyGL texture object for the installed blend mode.
         * @param cutout Whether low-alpha source texels must use TinyGL's discard key.
         */
        CNAEXT [[nodiscard]] unsigned int GLTextureHandle(bool cutout,
                                                          float alphaMultiplier = 1.0f) const;
        /** @brief CNAEXT. Whether any source texel was below the cutout threshold on the last upload. */
        CNAEXT [[nodiscard]] bool HasCutoutTexelsEXT() const { return hasCutoutTexels_; }

    private:
        void Upload();
        void UploadCutout(float alphaMultiplier) const;

        unsigned int glOpaqueTexture_ = 0;
        unsigned int glCutoutTexture_ = 0;
        int width_ = 0;
        int height_ = 0;
        mutable bool hasCutoutTexels_ = false;
        mutable float cutoutAlphaMultiplier_ = -1.0f;
        /// Untouched RGBA8 source, kept so GetData() is exact.
        std::vector<std::uint8_t> shadowRgba_;
    };

    class TinyGLRenderer;

    /**
     * SpriteBatch handle. Every `Draw()` overload resolves its four corner positions, texture
     * coordinates and per-vertex tint and hands them to `TinyGLRenderer::DrawTexturedQuadEXT()`,
     * which issues one real TinyGL textured draw.
     */
    class TinyGLSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /**
         * @brief Binds this batch to its owning renderer.
         * @param owner Renderer that executes the quads.
         */
        explicit TinyGLSpriteBatchRenderer(TinyGLRenderer& owner);

        /** @brief Marks the batch as begun. */
        void Begin() override { begun_ = true; }
        /** @brief Marks the batch as ended. */
        void End() override { begun_ = false; }
        /**
         * @brief Records the batch transform composed onto the sprite projection.
         * @param m Transform matrix from `SpriteBatch::Begin`.
         */
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }

        /**
         * @brief Refuses a custom Effect.
         * @param effect Effect from `SpriteBatch::Begin`; only `nullptr` is accepted.
         * @throws System::NotSupportedException When @p effect is not null.
         */
        void SetCustomEffect(Effect* effect) override;

        /**
         * @brief Records the batch texture filter.
         *
         * TinyGL's `glTexParameteri` is an upstream no-op and its rasterizer takes exactly one
         * nearest sample per fragment, so no filter selection reaches the hardware. `Anisotropic`
         * is refused because `SupportsCapability(AnisotropicFiltering)` reports false; every other
         * ordinal is accepted and sampled as `Point`, which is a documented approximation rather
         * than a silent one -- see `docs/tinygl-renderer.md`.
         *
         * @param textureFilter Raw `TextureFilter` ordinal.
         * @throws System::NotSupportedException When @p textureFilter is `Anisotropic` or unknown.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Records the batch texture address mode.
         *
         * TinyGL's texel fetch masks the fixed-point S/T coordinates against the 256-texel
         * dimension, which is `Wrap` and nothing else. Accepted and documented for the same reason
         * as `SetSamplerFilter()`.
         *
         * @param addressU Raw `TextureAddressMode` ordinal for U.
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         * @throws System::NotSupportedException When an ordinal is unknown.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Draws the whole texture at @p x, @p y with no tint.
         * @param texture Texture to draw.
         * @param x,y     Destination top-left corner in pixels.
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source region into a destination rectangle with a tint.
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination rectangle in pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint colour.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Rotated/flipped counterpart of the tinted overload.
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination rectangle in pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint colour.
         * @param rotation             Rotation in radians around @p origin.
         * @param origin               Rotation origin in source-rectangle texels, matching XNA.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Sort depth; this renderer draws in submission order.
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        /** @brief CNAEXT. Whether `Begin()` has been called without a matching `End()`. */
        CNAEXT [[nodiscard]] bool IsBegunEXT() const { return begun_; }

    private:
        TinyGLRenderer& owner_;
        Matrix transform_ = Matrix::getIdentityProperty();
        bool begun_ = false;
        /// Resolved batch sampler. Recorded for diagnostics only -- see SetSamplerFilter().
        int samplerFilter_ = 0;    // TextureFilter::Linear
        int samplerAddressU_ = 1;  // TextureAddressMode::Clamp
        int samplerAddressV_ = 1;  // TextureAddressMode::Clamp
    };

    /**
     * CNA graphics renderer implemented on TinyGL (C-Chads/tinygl, an archived fork of Fabrice
     * Bellard's TinyGL): a CPU implementation of a fixed-function OpenGL 1.x subset, with no
     * shaders anywhere in its design.
     *
     * Same architectural shape as the Headless/Software/PortableGL renderers -- no window, no GPU
     * library, no platform video subsystem, `Present()` is a no-op, no native window or native 2D
     * renderer exists, and pixel truth is exposed through `ReadBackbuffer()`.
     * Where `PORTABLEGL` is the shader-era CPU OpenGL, `TINYGL` is the fixed-function one: every
     * transform, clip, raster and texture-fetch decision below is made by real TinyGL API calls
     * (`glLoadMatrixf`, `glVertexPointer`, `glDrawArrays`, `glArrayElement`, `glTexImage2D`,
     * `glBlendFunc`, `glCullFace`, `glPolygonMode`, `glDepthMask`).
     *
     * Scope, stated as capability truth rather than intent:
     *  - 3D: the four built-in fixed-function vertex layouts: `VertexPositionColor` (stride 16),
     *    `VertexPositionTexture` (stride 20), `VertexPositionColorTexture` (stride 24) and
     *    `VertexPositionNormalTexture` (stride 32). `BasicEffect`'s `VertexColorEnabled`,
     *    `DiffuseColor`, `Alpha` and `TextureEnabled` are honoured; lighting, fog, alpha test,
     *    skinning, dual-texture,
     *    environment mapping, PBR, instancing and multi-stream input are **refused
     *    deterministically**, never approximated.
     *  - 2D: a real textured SpriteBatch quad path.
     *  - State: `BlendState` (only the factor/equation set TinyGL genuinely executes, plus the two
     *    XNA alpha presets mapped onto its colour-key cutout), the depth half of
     *    `DepthStencilState`, `RasterizerState`'s `CullMode` and `FillMode`, and `Viewport`.
     *  - Not implemented, and refused rather than no-opped: stencil, scissor, per-channel colour
     *    masks, a selectable depth comparison, depth bias, render targets, cube/3D textures,
     *    occlusion queries, custom Effects, MSAA, anisotropic filtering and mip mapping.
     *
     * **Every unsupported argument must be rejected before it reaches TinyGL.** TinyGL answers a
     * combination it cannot handle by calling `gl_fatal_error()`, which terminates the process
     * rather than setting an error flag (`TINYGL-0`). There is no recoverable native error path,
     * so validation here is a correctness requirement, not a courtesy.
     *
     * TinyGL keeps its context in one process-wide global (`glInit`/`glClose`), so exactly one
     * `TinyGLRenderer` may exist at a time; the constructor enforces that.
     */
    class TinyGLRenderer final : public IGraphicsRenderer
    {
    public:
        /**
         * @brief Creates the TinyGL context and its CPU framebuffer.
         * @param virtualWidth  Backbuffer width in pixels.
         * @param virtualHeight Backbuffer height in pixels.
         * @throws std::runtime_error When a TinyGLRenderer already exists, or the framebuffer
         *         cannot be allocated.
         */
        TinyGLRenderer(int virtualWidth, int virtualHeight);
        /** @brief Destroys the TinyGL context and every renderer-owned native object. */
        ~TinyGLRenderer() override;

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
         * @brief Reallocates TinyGL's framebuffer at a new size.
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;
        /** @brief No-op: there is no window to scale into. */
        void SetPresentationMode(int /*mode*/) override {}

        /**
         * @brief Reads back a region of the backbuffer as RGBA8, top row first.
         *
         * Reads TinyGL's `ZBuffer::pbuf` directly: upstream `glReadPixels` validates its arguments
         * and then returns without writing anything (`TINYGL-0` fact A).
         *
         * @param x,y    Top-left corner of the region, in pixels.
         * @param w,h    Region size in pixels.
         * @param pixels Destination for `w * h * 4` bytes.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        /**
         * @brief Reports the format the back buffer really has, ignoring the request.
         *
         * The context is opened in `ZB_MODE_RGBA`, whose 32-bit PIXEL is `0x00RRGGBB` -- the alpha
         * byte exists in memory but nothing in TinyGL ever writes it.
         *
         * @param requestedFormat Requested `SurfaceFormat` ordinal.
         * @return `SurfaceFormat::Color`'s ordinal, always.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override;

        /**
         * @brief Reports the depth/stencil format the back buffer really has.
         *
         * TinyGL's `ZBuffer` carries a 16-bit depth plane and no stencil plane at all, in every
         * configuration, so a request for any stencil-bearing format does not get one.
         *
         * @param requestedFormat Requested `DepthFormat` ordinal.
         * @return `DepthFormat::Depth16`'s ordinal, always.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int requestedFormat) const override;

        /** @brief TinyGL has a usable depth/stencil-state path backed by its real depth plane. */
        [[nodiscard]] bool SupportsDepthStencil() const override { return true; }
        /** @brief TinyGL's default ZBuffer has a real 16-bit depth plane. */
        [[nodiscard]] bool SupportsDepthBuffer() const override { return true; }
        /** @brief TinyGL's default ZBuffer has no stencil plane. */
        [[nodiscard]] bool SupportsStencilBuffer() const override { return false; }

        /**
         * @brief Creates a real TinyGL texture object from @p data.
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
         * @param renderTargets Normalized binding descriptors, or null.
         * @param count         Number of bindings; only 0 is accepted.
         * @throws System::NotSupportedException When @p count is greater than zero.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

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
         * @brief Accepts a stencil clear, which clears nothing: TinyGL has no stencil plane.
         *
         * Clearing an absent stencil plane is a no-op in real OpenGL too, so this is correct
         * behaviour rather than an approximation. What the renderer refuses is the promise that
         * would be false -- enabling the stencil test, see `ApplyDepthStencilState()`.
         *
         * @param stencil Stencil value; ignored.
         */
        void ClearStencil(int stencil) override;
        /**
         * @brief Clears depth; the stencil half clears nothing, as above.
         * @param depth   Depth value in 0..1.
         * @param stencil Stencil value; ignored.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /**
         * @brief Clears color; the stencil half clears nothing, as above.
         * @param r,g,b,a Clear color components in 0..1.
         * @param stencil Stencil value; ignored.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /**
         * @brief Clears color and depth; the stencil half clears nothing, as above.
         * @param r,g,b,a Clear color components in 0..1.
         * @param depth   Depth value in 0..1.
         * @param stencil Stencil value; ignored.
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
         * @brief Installs a `BlendState` as real TinyGL blend state, or refuses it.
         *
         * TinyGL's rasterizer implements exactly `{One, Zero, InverseSourceColor}` as source
         * factors, `{One, Zero, InverseDestinationColor}` as destination factors, and
         * `{Add, Subtract, ReverseSubtract}` as equations, on RGB with no alpha channel. Three
         * outcomes are possible, and no other:
         *
         *  1. A state whose factors and equations are all in that set is installed exactly.
         *  2. The two XNA alpha presets (`AlphaBlend` and `NonPremultiplied`, matched on their
         *     complete factor+function signature) are executed as TinyGL's own 1-bit colour-key
         *     cutout, the only transparency it has. This is a recorded approximation: alpha is
         *     thresholded at upload time, never interpolated.
         *  3. Everything else -- including `BlendState::Additive`, whose `SourceAlpha` source
         *     factor TinyGL's factor switch has no case for -- is refused.
         *
         * The colour write mask must select all four channels and `MultiSampleMask` must keep its
         * all-samples default: TinyGL has neither `glColorMask` nor sample-mask state.
         *
         * @param colorSrcBlend  Raw `Blend` ordinal for the RGB source factor.
         * @param alphaSrcBlend  Raw `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw `Blend` ordinal for the RGB destination factor.
         * @param alphaDstBlend  Raw `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw `BlendFunction` ordinal for the RGB equation.
         * @param alphaBlendFunc Raw `BlendFunction` ordinal for the alpha equation.
         * @param writeState     Per-target color write masks and the coverage sample mask.
         * @throws System::NotSupportedException When the state is not one of the three cases above.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Records `GraphicsDevice.BlendFactor`, which no TinyGL blend factor reads.
         *
         * `Blend::BlendFactor`/`InverseBlendFactor` are refused by `ApplyBlendState()`, so no
         * installed state can consult a constant blend colour. Recorded rather than dropped.
         *
         * @param r,g,b,a Constant color components in 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Installs the depth half of a `DepthStencilState`, and refuses the stencil half.
         *
         * TinyGL has no `glDepthFunc`. Its rasterizer's single comparison is `z >= stored` over a
         * z-buffer in which a larger stored value is nearer, which is `CompareFunction::LessEqual`
         * in XNA's convention -- and also XNA's own `DepthStencilState::Default`. Only that
         * ordinal is accepted, and @p stencilEnable must be false because there is no stencil plane.
         *
         * @param depthEnable       Whether the depth test is performed.
         * @param depthWriteEnable  Whether passing fragments write the depth buffer.
         * @param depthFunc         Raw `CompareFunction` ordinal; only `LessEqual` is accepted.
         * @param stencilEnable     Whether the stencil test is performed; only false is accepted.
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
         * @throws System::NotSupportedException When the depth comparison is not `LessEqual`, or
         *         the stencil test is enabled.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;

        /**
         * @brief Refuses a stencil reference change: TinyGL has no stencil plane.
         * @param value New stencil reference value.
         * @throws System::NotSupportedException When @p value is non-zero.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Installs a `RasterizerState` as real TinyGL rasterizer state.
         *
         * `CullMode` reaches `glEnable`/`glDisable(GL_CULL_FACE)` plus `glCullFace`, and
         * `FillMode::WireFrame` reaches `glPolygonMode(GL_FRONT_AND_BACK, GL_LINE)`. Scissor
         * testing and depth bias have no TinyGL implementation at all -- `glScissor` does not
         * exist and `glPolygonOffset` only stores its arguments -- so enabling either is refused.
         *
         * @param cullMode            Raw `CullMode` ordinal.
         * @param fillMode            Raw `FillMode` ordinal.
         * @param scissorTestEnable   Whether the scissor rectangle clips rasterization; only false is accepted.
         * @param depthBias           Constant depth bias; only 0 is accepted.
         * @param slopeScaleDepthBias Slope-scaled depth bias; only 0 is accepted.
         * @throws System::NotSupportedException When a cull or fill ordinal is unknown, scissor
         *         testing is enabled, or a depth bias is non-zero.
         */
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias,
                                  float slopeScaleDepthBias) override;

        /**
         * @brief Records `GraphicsDevice.ScissorRectangle`, which TinyGL cannot apply.
         *
         * `ApplyRasterizerState()` refuses `ScissorTestEnable`, so a recorded rectangle can never
         * become active. Recorded rather than dropped.
         *
         * @param x,y Top-left corner in XNA coordinates.
         * @param w,h Rectangle size in pixels.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Installs `GraphicsDevice.Viewport`; TinyGL's rasterizer already uses a top-left
         *        framebuffer origin, matching XNA.
         *
         * TinyGL has no depth-range control (`glDepthRange` does not exist), so @p minDepth and
         * @p maxDepth must be the full 0..1 range.
         *
         * @param x,y      Top-left corner in XNA coordinates.
         * @param w,h      Viewport size in pixels.
         * @param minDepth Near end of the depth range; only 0 is accepted.
         * @param maxDepth Far end of the depth range; only 1 is accepted.
         * @throws System::NotSupportedException When the depth range is not 0..1.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Records a `GraphicsDevice.SamplerStates` slot, which no TinyGL draw reads.
         *
         * `glTexParameteri` is an upstream no-op and the rasterizer's texel fetch masks the
         * fixed-point coordinates against the texture dimension, so sampling is always nearest
         * with wrap addressing regardless of the requested state. Inert, documented state -- not
         * dropped state.
         *
         * @param slot          Texture unit index.
         * @param filter        Raw `TextureFilter` ordinal.
         * @param addressU      Raw `TextureAddressMode` ordinal for U.
         * @param addressV      Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy level.
         * @throws System::NotSupportedException When @p filter is `Anisotropic` or unknown.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Accepts the inert defaults and refuses a non-default mip selection.
         * @param slot        Texture unit index.
         * @param maxMipLevel Highest mip level the sampler may use.
         * @param lodBias     Level-of-detail bias.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief Creates a vertex buffer handle.
         * @param vertex_capacity Initial vertex capacity.
         * @return The new vertex-buffer handle.
         */
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        /**
         * @brief Creates a 16-bit index buffer handle.
         * @param index_capacity Initial index capacity.
         * @return The new index-buffer handle.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Draws vertex-colored primitives through TinyGL's fixed-function pipeline.
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
         * CNAEXT. Internal helper used by `TinyGLSpriteBatchRenderer` to issue one real textured
         * quad through the same TinyGL draw machinery the 3D route uses -- not part of
         * `IGraphicsRenderer`.
         *
         * @param texture         TinyGL texture whose blend-appropriate object is sampled.
         * @param positionsPixels Four corner positions (TL, TR, BR, BL), in destination pixel space.
         * @param uvs             Four corresponding normalized texture coordinates.
         * @param colorsRgba01    Four corresponding per-vertex tint colors, components in [0,1].
         * @param spriteTransform `ISpriteBatchRenderer::SetTransformMatrix()`'s current value,
         *                        composed with the internal ortho projection.
         */
        CNAEXT void DrawTexturedQuadEXT(const TinyGLTextureRenderer& texture,
                                        const float positionsPixels[4][2],
                                        const float uvs[4][2],
                                        const float colorsRgba01[4][4],
                                        const Matrix& spriteTransform);

    private:
        /// The subset of `GpuDrawParams` the fixed-function routes can genuinely execute, plus the
        /// offsets that select which records a draw reads. Keeping both routes on one code path
        /// through this small value means no offset can be honoured on one route and forgotten on
        /// the other.
        struct FixedFunctionDrawState
        {
            /// `GpuDrawParams::diffuseColor` -- BasicEffect's DiffuseColor with Alpha in [3].
            float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            /// `GpuDrawParams::vertexColorEnabled`.
            bool vertexColorEnabled = true;
            /// Texture bound to unit 0, or null when this is the untextured colored route.
            const TinyGLTextureRenderer* texture = nullptr;
            /// The bound stream's own `VertexBufferBinding.VertexOffset`, in vertex elements.
            int streamVertexOffset = 0;
            /// First vertex for a non-indexed draw.
            int vertexStart = 0;
            /// First index element for an indexed draw.
            int startIndex = 0;
            /// Value added to every decoded index before the vertex fetch.
            int baseVertex = 0;
        };

        struct Impl;
        std::unique_ptr<Impl> impl_;
        int virtualWidth_;
        int virtualHeight_;

        /// Refuses every `GpuDrawParams` shape the fixed-function routes cannot execute, then
        /// extracts the part they can. Touches no TinyGL state, so a refused draw leaves the
        /// context exactly as the previous draw left it.
        FixedFunctionDrawState TranslateDrawParams(const GpuDrawParams& params,
                                                   const char* route);

        /// Validates the bound buffer and referenced vertex alpha, then installs TinyGL's arrays.
        /// Returns false when the complete draw is below the documented cutout threshold.
        bool BindVertexArrays(const TinyGLVertexBufferRenderer& vb,
                              const FixedFunctionDrawState& state,
                              const char* route,
                              const std::vector<std::uint32_t>& referencedVertices);
        /// Tears the array pointers down again, leaving no client state pointing at freed memory.
        void UnbindVertexArrays(bool textured, bool normal);
        /// Installs world/view/projection through TinyGL's own matrix stack.
        void LoadDrawMatrices(const Matrix& world, const Matrix& view, const Matrix& projection);
        /// Shared body of both non-indexed routes.
        void DrawCommon(const IVertexBufferRenderer& vb,
                        const Matrix& world, const Matrix& view, const Matrix& projection,
                        PrimitiveType primitive, int primitiveCount,
                        const FixedFunctionDrawState& state, const char* route);
        /// Indexed counterpart of DrawCommon().
        void DrawIndexedCommon(const IVertexBufferRenderer& vb,
                               const IIndexBufferRenderer& ib,
                               const Matrix& world, const Matrix& view, const Matrix& projection,
                               PrimitiveType primitive, int primitiveCount,
                               const FixedFunctionDrawState& state, const char* route);
    };
}
