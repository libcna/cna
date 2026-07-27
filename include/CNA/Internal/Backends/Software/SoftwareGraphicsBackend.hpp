#pragma once

#include "../Common/IGraphicsBackend.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Software
{
    /**
     * @brief Real CPU-owned color (RGBA8) + depth (float32) buffer pair.
     *
     * This is the Software backend's actual state, not a bookkeeping fiction
     * (plan_software.md design decision 3) -- every pixel written here is a genuinely correct
     * pixel a test can read back and assert on, with no GPU involved at all.
     */
    struct SoftwareFramebuffer
    {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> color;  ///< RGBA8, width*height*4 bytes.
        std::vector<float> depthBuffer;   ///< width*height floats, 0..1.

        void Resize(int w, int h);
        void ClearColor(float r, float g, float b, float a);
        void ClearDepthValue(float depthValue);
    };

    class SoftwareVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        explicit SoftwareVertexBufferBackend(int vertexCapacity);

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }
        /// Raw vertex bytes from the most recent SetData() call -- the rasterizer (Phase S4)
        /// reads vertex attributes directly from here, keyed by Stride() (plan_software.md
        /// design decision 2: stride-based format inference).
        [[nodiscard]] const std::vector<std::uint8_t>& Data() const { return data_; }

    private:
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> data_;
    };

    class SoftwareIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        SoftwareIndexBufferBackend(int indexCapacity, bool thirtyTwoBit);

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] const std::vector<std::uint8_t>& Data() const { return data_; }

    private:
        void Upload(const void* data, int index_count, bool dataIsThirtyTwoBit);

        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<std::uint8_t> data_;
    };

    /**
     * @brief Read-only access to one Software resource's RGBA8 colour storage.
     *
     * REMED-GFX-124: every Software colour CONSUMER (the rasterizer's texture sampler, the
     * SpriteBatch quad path, render-target readback) binds to this capability instead of to a
     * concrete backend class. Before this, the sampler `dynamic_cast`ed straight to
     * `SoftwareTextureBackend`, which a `SoftwareRenderTargetBackend` is not -- so a finished render
     * target resolved as a null texture and shaded untextured white, even though its pixels were
     * sitting in CPU memory the whole time.
     *
     * There is exactly ONE colour buffer per resource and this interface only exposes it: a texture
     * reports its own `pixels_`, a render target reports its framebuffer's `color`. Nothing is
     * copied, mirrored or resolved to make a target sampleable, and the const reference keeps
     * consumers from writing through it (a target's colour storage stays writable only through the
     * raster/Clear paths, so active-target validation cannot be bypassed by holding this).
     */
    class SoftwareColorSurface
    {
    public:
        virtual ~SoftwareColorSurface() = default;
        /** @brief Width in pixels of the colour storage. */
        [[nodiscard]] virtual int ColorWidth() const = 0;
        /** @brief Height in pixels of the colour storage. */
        [[nodiscard]] virtual int ColorHeight() const = 0;
        /**
         * @brief The resource's RGBA8 pixels, row-major, top row first, ColorWidth()*4 bytes per row.
         * @return A reference to the resource's own storage -- never a copy.
         */
        [[nodiscard]] virtual const std::vector<std::uint8_t>& ColorPixels() const = 0;
    };

    class SoftwareTextureBackend : public ITextureBackend, public SoftwareColorSurface
    {
    public:
        explicit SoftwareTextureBackend(const ImageData& data);
        SoftwareTextureBackend(int width, int height);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        // SoftwareColorSurface -- real RGBA8 pixel storage, which the rasterizer's texture sampler
        // (Phase S5) reads directly from.
        [[nodiscard]] int ColorWidth() const override { return width_; }
        [[nodiscard]] int ColorHeight() const override { return height_; }
        [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels() const override { return pixels_; }

    protected:
        int width_ = 0;
        int height_ = 0;
        std::vector<std::uint8_t> pixels_;
    };

    class SoftwareRenderTargetBackend final : public IRenderTargetBackend, public SoftwareColorSurface
    {
    public:
        SoftwareRenderTargetBackend(int w, int h, int depthFormat, bool mipMap, int multiSampleCount);

        [[nodiscard]] int GetWidth() const override { return framebuffer_.width; }
        [[nodiscard]] int GetHeight() const override { return framebuffer_.height; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /**
         * @brief Copies a sub-rectangle of the rendered colour attachment into @p data as RGBA8.
         *
         * REMED-GFX-124: without this override the call reached `ITextureBackend::GetData`'s default
         * no-op. That was not merely a missing feature -- `Texture2D::GetData`'s render-target
         * fallback hands the backend a scratch buffer it zero-initialized itself and then converts
         * those bytes for the caller, so a no-op backend produced a fabricated, fully written,
         * uniformly transparent-black frame rather than an obviously empty result.
         *
         * Reads the colour attachment only; the depth buffer is never consulted. Rows are returned
         * top-first, matching this backend's framebuffer layout and `ReadBackbuffer`, so no flip is
         * applied. An unsupported or out-of-range request throws instead of leaving caller memory
         * silently unchanged.
         *
         * @param level      Mip level; Software stores level 0 only.
         * @param x          Left edge of the requested rectangle, in pixels.
         * @param y          Top edge of the requested rectangle, in pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @throws System::ArgumentNullException if @p data is null.
         * @throws System::NotSupportedException if @p level is above 0.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle is
         *         empty or leaves the target, or @p dataLength is too small for the rectangle.
         */
        void GetData(int level, int x, int y, int w, int h,
                     void* data, int dataLength) const override;
        void BindAsRenderTarget() override { bound_ = true; }
        void UnbindAsRenderTarget() override { bound_ = false; }
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        { return depthFormatWasRequested; }

        // SoftwareColorSurface -- the SAME storage the rasterizer writes into. A finished target is
        // sampleable with no resolve, no shadow copy and no extra allocation; there is no second
        // colour buffer that could drift out of sync with what was rendered.
        [[nodiscard]] int ColorWidth() const override { return framebuffer_.width; }
        [[nodiscard]] int ColorHeight() const override { return framebuffer_.height; }
        [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels() const override
        { return framebuffer_.color; }

        [[nodiscard]] bool IsBound() const { return bound_; }
        [[nodiscard]] SoftwareFramebuffer& Framebuffer() { return framebuffer_; }
        [[nodiscard]] const SoftwareFramebuffer& Framebuffer() const { return framebuffer_; }

    private:
        SoftwareFramebuffer framebuffer_;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        int multiSampleCount_ = 0;
        bool bound_ = false;
    };

    /// SOFTWARE-82: real 6-face RGBA8 cube map storage, for EnvironmentMapEffect. Mip levels
    /// beyond 0 aren't stored (mirrors SoftwareTextureBackend::UpdatePixelsLevel's own no-op
    /// precedent -- no mipmapping support in v1).
    class SoftwareTextureCubeBackend final : public ITextureCubeBackend
    {
    public:
        explicit SoftwareTextureCubeBackend(int size);

        void SetData(int face, int level, int x, int y, int w, int h,
                    const void* data, int dataLength) override;
        void GetData(int face, int level, int x, int y, int w, int h,
                    void* data, int dataLength) const override;

        [[nodiscard]] int GetSize() const { return size_; }
        /// Real RGBA8 pixel storage for one of the 6 faces (CubeMapFace ordinal 0-5), size*size*4
        /// bytes -- the rasterizer's cube sampler (SOFTWARE-82) reads directly from here.
        [[nodiscard]] const std::vector<std::uint8_t>& FacePixels(int face) const { return faces_[face]; }

    private:
        int size_ = 0;
        std::array<std::vector<std::uint8_t>, 6> faces_;
    };

    // Cube-map render targets, Texture3D, and hardware occlusion queries remain out of scope for
    // v1 (plan_software.md Boundaries) -- CreateRenderTargetCube/CreateTexture3D/
    // CreateOcclusionQuery all keep IGraphicsBackend's own shared default (returns nullptr).
    // REMED-CONTENT-004: Texture3D's own absence is now reported via
    // SupportsCapability(GraphicsCapability::Texture3D) => false, so Texture3D's constructor fails
    // cleanly instead of a caller's SetData()/GetData() calls silently discarding data.

    class SoftwareEffectBackend final : public IEffectBackend
    {
    public:
        // Mirrors HEADLESS-16: accepts any GLSL/HLSL/WGSL source string without compiling it --
        // this backend's own fixed pixel-shading path (Phase S5/S6) is what actually renders,
        // not the supplied shader source (plan_software.md design decision 8).
        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override { bound_ = true; }
        void Unbind() override { bound_ = false; }
        [[nodiscard]] bool IsValid() const override { return compiled_; }
        [[nodiscard]] std::string GetCompileError() const override { return {}; }
        void SetUniformFloat(const char*, float) override {}
        void SetUniformInt(const char*, int) override {}
        void SetUniformVec2(const char*, float, float) override {}
        void SetUniformVec3(const char*, float, float, float) override {}
        void SetUniformVec4(const char*, float, float, float, float) override {}
        void SetUniformMat4(const char*, const float*) override {}
        void SetUniformFloatArray(const char*, const float*, int) override {}
        void SetUniformVec2Array(const char*, const float*, int) override {}
        void BindTexture(int, ITextureBackend*) override {}

        [[nodiscard]] bool IsBound() const { return bound_; }

    private:
        bool compiled_ = false;
        bool bound_ = false;
    };

    /// Draws are wired to the shared rasterizer core in Phase S6 (SOFTWARE-51) -- a
    /// SpriteBatch::Draw() call is just a textured quad (2 triangles), reusing the same code
    /// path DrawPrimitivesEx uses (plan_software.md design decision 5).
    class SoftwareSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit SoftwareSpriteBatchBackend(class SoftwareGraphicsBackend& owner);

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transformMatrix_ = m; }
        void SetCustomEffect(Effect* effect) override { customEffect_ = effect; }
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        { addressU_ = addressU; addressV_ = addressV; }
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        [[nodiscard]] bool IsBegun() const { return begun_; }

    private:
        SoftwareGraphicsBackend& owner_;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        Effect* customEffect_ = nullptr;
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
    };

    /**
     * @brief Software (CPU) rasterizer graphics backend.
     *
     * See plan_software.md for the full task breakdown and design rationale. Unlike HEADLESS
     * (which only does bookkeeping and never produces a real pixel), this backend actually
     * rasterizes real triangles into a CPU-owned RGBA8 framebuffer -- GetBackBufferData()/
     * ReadBackbuffer() return genuinely correct pixels, with no GPU, display server, or driver
     * involved at all.
     */
    class SoftwareGraphicsBackend final : public IGraphicsBackend
    {
    public:
        SoftwareGraphicsBackend(int virtualWidth, int virtualHeight);
        ~SoftwareGraphicsBackend() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        SDL_Window* GetWindowInternal() const override { return nullptr; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc, int stencilPass, int stencilFail,
                                    int stencilDepthFail, int stencilMask, int stencilWriteMask,
                                    int referenceStencil, bool twoSidedStencilMode, int ccwStencilFunc,
                                    int ccwStencilPass, int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

        /**
         * @brief Renders `primitiveCount` primitives from the whole bound vertex buffer.
         *
         * REMED-GFX-119: this overload carries no `GpuDrawParams` and therefore no `vertexStart`,
         * so its contract is a complete-buffer draw beginning at element zero. Its only caller has
         * already copied exactly the requested source range into the temporary buffer it binds.
         * The exact topology-derived vertex count is still validated against the bound buffer in
         * 64-bit, and a range that leaves the buffer throws `System::ArgumentOutOfRangeException`
         * before any vertex byte is read rather than being clamped.
         */
        void DrawColoredPrimitives(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        /**
         * @brief Effect-aware non-indexed draw over an exact vertex range.
         *
         * REMED-GFX-119: `params.vertexStart` is a vertex-**element** offset, never a byte offset,
         * and `primitiveCount` fixes the exact topology-derived vertex count, so vertex
         * `vertexStart + local` is read for `local` in `[0, consumed)` and nothing before or after
         * that range is consumed. The whole range is validated against the bound buffer in 64-bit
         * before the stride multiply, so a request that leaves the buffer throws
         * `System::ArgumentOutOfRangeException` instead of forming an invalid pointer into the CPU
         * vertex storage.
         */
        void DrawPrimitivesEx(const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view,
                              const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        // ---- Software-specific, NOXNA-equivalent debug/testing API ----

        /// The currently-active framebuffer (the bound RenderTarget2D's, or the backbuffer's when
        /// none is bound) -- real, CPU-owned pixel/depth storage.
        [[nodiscard]] SoftwareFramebuffer& CurrentFramebuffer();
        [[nodiscard]] const SoftwareFramebuffer& CurrentFramebuffer() const;
        /// Whether the current BlendState is anything other than the Opaque preset (design
        /// decision 7: only Opaque/AlphaBlend are distinguished in v1). Used by
        /// SoftwareSpriteBatchBackend to decide whether to alpha-blend its own quads, since
        /// SpriteBatch::Begin() applies its BlendState the same way any other draw does.
        [[nodiscard]] bool IsBlendEnabled() const { return blendEnabled_; }
        [[nodiscard]] bool IsDepthTestEnabled() const { return depthTestEnabled_; }
        /// REMED-GFX-030: whether passing fragments may update the active target's depth buffer.
        /// Kept independent from depth testing so DepthRead can compare against existing depth
        /// without modifying it. A disabled depth test suppresses both comparison and storage.
        [[nodiscard]] bool IsDepthWriteEnabled() const { return depthWriteEnabled_; }
        /// REMED-GFX-030: raw XNA CompareFunction ordinal used for the depth comparison
        /// (Always=0 through NotEqual=7). Captured with the other two depth fields for each draw.
        [[nodiscard]] int GetDepthCompareFunction() const { return depthCompareFunction_; }
        /// REMED-GFX-077: raw XNA ColorWriteChannels of the current BlendState (slot 0; Software has
        /// one active colour buffer). Used by SoftwareSpriteBatchBackend so its quads honour the
        /// per-channel write mask the same way any other draw does. 15 (All) = every channel.
        [[nodiscard]] int GetColorWriteMask() const { return colorWriteMask_; }
        /// REMED-GFX-077: the current BlendState.MultiSampleMask. Software is single-sample, so only
        /// bit 0 is meaningful (bit 0 clear discards the fragment). 0xFFFFFFFF = all samples.
        [[nodiscard]] unsigned int GetMultiSampleMask() const { return multiSampleMask_; }
        /// The raw CullMode ordinal from the most recent ApplyRasterizerState() call (SOFTWARE-81).
        /// Used by SoftwareSpriteBatchBackend so its quads are culled the same way real FNA's
        /// SpriteBatch is: FNA's own SpriteBatch defaults its RasterizerState to
        /// CullCounterClockwise (not CullNone), and its quad winding is authored to survive that.
        [[nodiscard]] int GetCullMode() const { return cullMode_; }

        /// REMED-GFX-082: the raw FillMode ordinal from the most recent ApplyRasterizerState() call
        /// (0=Solid, 1=WireFrame). Used by SoftwareSpriteBatchBackend so a wireframe RasterizerState
        /// supplied through SpriteBatch.Begin (REMED-GFX-081) outlines the sprite's quad triangles the
        /// same way it outlines 3D geometry.
        [[nodiscard]] int GetFillMode() const { return fillMode_; }

        /// REMED-GFX-083: the RasterizerState.DepthBias / SlopeScaleDepthBias floats from the most recent
        /// ApplyRasterizerState() call (both previously discarded). Consumed by SoftwareSpriteBatchBackend
        /// via owner_ so a bias supplied through SpriteBatch.Begin's RasterizerState (REMED-GFX-081)
        /// offsets the sprite quad's depth exactly as it offsets 3D geometry. See the members below for
        /// the unit convention.
        [[nodiscard]] float GetDepthBias() const { return depthBias_; }
        [[nodiscard]] float GetSlopeScaleDepthBias() const { return slopeScaleDepthBias_; }

        /// REMED-GFX-073: the active GraphicsDevice.Viewport rectangle in pixels of the currently
        /// bound target. When no custom viewport has been set (SetViewport never called), the full
        /// current framebuffer is returned. SoftwareSpriteBatchBackend places its viewport-local
        /// quads at (x,y) and clips them to (x,y,w,h), matching real XNA/FNA's viewport-local
        /// SpriteBatch semantics (the GPU backends' GFX-072 contract).
        void GetActiveViewport(int& x, int& y, int& w, int& h) const;

        /// REMED-GFX-080: whether RasterizerState.ScissorTestEnable is currently on (the flag from
        /// the most recent ApplyRasterizerState() call). ScissorRectangle only clips rasterization
        /// when this is true; when false the stored rectangle has no effect. Consumed by the 2D and
        /// 3D raster paths (and by SoftwareSpriteBatchBackend via owner_).
        [[nodiscard]] bool IsScissorTestEnabled() const { return scissorTestEnable_; }

        /// REMED-GFX-080: the active GraphicsDevice.ScissorRectangle in pixels of the currently
        /// bound target (framebuffer/target space -- NOT viewport-local). When no scissor rectangle
        /// has been set (SetScissorRect never called), the full current framebuffer is returned, so
        /// enabling scissor testing without an explicit rectangle is a no-op clip, matching XNA's
        /// default full-target ScissorRectangle. GraphicsDevice pushes this on every
        /// setScissorRectangleProperty() and resets it to the full target on each RenderTarget
        /// transition, so this single field is always relative to the active target. The rectangle
        /// is stored independently of IsScissorTestEnabled(): a later RasterizerState change can
        /// enable it without re-setting the rectangle.
        void GetActiveScissor(int& x, int& y, int& w, int& h) const;

    private:
        /// REMED-GFX-079: the active viewport as raster parameters for the 3D draw path --
        /// (x,y,w,h) from GetActiveViewport() plus the MinDepth/MaxDepth depth range (defaulting to
        /// [0,1] before any custom viewport is set, matching GetActiveViewport's full-framebuffer
        /// x/y/w/h fallback). One source of truth shared by all four 3D draw entry points, so a
        /// custom GraphicsDevice.Viewport positions, sub-scales, and depth-range-remaps 3D geometry.
        void GetActiveViewportRaster(int& x, int& y, int& w, int& h,
                                     float& minDepth, float& maxDepth) const;

        SoftwareFramebuffer backbuffer_;
        SoftwareRenderTargetBackend* currentRenderTarget_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        bool depthTestEnabled_ = true;
        /// REMED-GFX-030: DepthStencilState.DepthBufferWriteEnable. Defaults to true with the
        /// Software backend's existing default test-enable and LessEqual function, matching
        /// DepthStencilState::Default. A passing fragment writes only when test AND write are enabled;
        /// disabled depth testing performs neither comparison nor storage (the GL/D3D/XNA contract).
        bool depthWriteEnabled_ = true;
        /// REMED-GFX-030: raw CompareFunction ordinal. 3 = LessEqual, the public Default/DepthRead/
        /// None preset function. ApplyDepthStencilState validates all public values 0..7 rather than
        /// silently approximating an unknown value.
        int depthCompareFunction_ = 3;
        /// Opaque (false) vs. simplified AlphaBlend (true) -- design decision 7. Defaults to
        /// false, matching real XNA/FNA's own default GraphicsDevice.BlendState (Opaque).
        bool blendEnabled_ = false;
        /// REMED-GFX-077: raw XNA ColorWriteChannels of the current BlendState, slot 0 (bit0=R,
        /// bit1=G, bit2=B, bit3=A). Defaults to 15 (All), matching XNA's default BlendState.
        int colorWriteMask_ = 15;
        /// REMED-GFX-077: current BlendState.MultiSampleMask. Single-sample ⇒ only bit 0 matters.
        /// Defaults to 0xFFFFFFFF (all samples), matching XNA's default (-1).
        unsigned int multiSampleMask_ = 0xFFFFFFFFu;
        /// Raw CullMode ordinal (0=None, 1=CullClockwiseFace, 2=CullCounterClockwiseFace) from the
        /// most recent ApplyRasterizerState() call (SOFTWARE-81). Defaults to 2
        /// (CullCounterClockwiseFace), matching real XNA/FNA's own default
        /// RasterizerState.CullCounterClockwise -- GraphicsDevice's constructor applies this for
        /// real via ApplyRasterizerState() before any game code runs, so this default rarely
        /// matters in practice, but is kept consistent with the real default for clarity.
        int cullMode_ = 2;

        /// REMED-GFX-082: raw FillMode ordinal (0=Solid, 1=WireFrame) from the most recent
        /// ApplyRasterizerState() call. Defaults to 0 (Solid), matching real XNA/FNA's default
        /// RasterizerState.FillMode -- the rasterizer fills triangle interiors unless a WireFrame
        /// RasterizerState is applied, in which case it outlines only the triangle edges. Independent
        /// of CullMode/ScissorTestEnable.
        int fillMode_ = 0;

        /// REMED-GFX-083: RasterizerState.DepthBias / SlopeScaleDepthBias, stored by ApplyRasterizerState()
        /// (its 4th/5th float args, previously discarded). Both default to 0 (XNA/FNA default), for which
        /// the rasterizer adds no depth offset and the output is byte-identical to pre-GFX-083. Expressed
        /// in the same UNSCALED units GraphicsDevice forwards to every GPU backend: DepthBias in units of
        /// the depth buffer's minimum resolvable difference (fed into vkCmdSetDepthBias's constantFactor /
        /// glPolygonOffset's units / rounded into D3D11_RASTERIZER_DESC::DepthBias), SlopeScaleDepthBias as
        /// a multiplier on the triangle's max screen-space depth slope (vkCmdSetDepthBias's slopeFactor /
        /// glPolygonOffset's factor). The rasterizer folds both into the post-viewport per-fragment depth
        /// via ComputeDepthBiasOffset (see the .cpp), matching the GPU backends' polygon-offset contract.
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;

        /// REMED-GFX-073: current GraphicsDevice.Viewport, stored by SetViewport() and consumed by
        /// the SpriteBatch path (GetActiveViewport()). GraphicsDevice pushes this on every viewport
        /// change and resets it to the full target on each RenderTarget transition, so a single
        /// current-viewport field is always relative to the active target. `viewportSet_` starts
        /// false so that -- before GraphicsDevice sets any viewport, or for direct-backend use --
        /// GetActiveViewport() falls back to the full current framebuffer. MinDepth/MaxDepth are
        /// stored for completeness but not consumed by the 2D sprite path (SpriteBatch uses
        /// layerDepth directly), matching the pre-existing behavior.
        bool viewportSet_ = false;
        int viewportX_ = 0;
        int viewportY_ = 0;
        int viewportWidth_ = 0;
        int viewportHeight_ = 0;
        float viewportMinDepth_ = 0.0f;
        float viewportMaxDepth_ = 1.0f;

        /// REMED-GFX-080: RasterizerState.ScissorTestEnable, stored by ApplyRasterizerState() (its
        /// third argument, previously discarded). Independent of the stored ScissorRectangle below
        /// -- the rectangle is meaningful even while this is false, because a later RasterizerState
        /// change can enable scissor testing without re-setting the rectangle. Defaults to false,
        /// matching real XNA/FNA's default RasterizerState.ScissorTestEnable (off).
        bool scissorTestEnable_ = false;

        /// REMED-GFX-080: current GraphicsDevice.ScissorRectangle, stored by SetScissorRect() (a
        /// no-op before this task). `scissorSet_` starts false so that -- before GraphicsDevice sets
        /// any scissor rectangle, or for direct-backend use -- GetActiveScissor() falls back to the
        /// full current framebuffer (an inert clip, matching XNA's default full-target
        /// ScissorRectangle). GraphicsDevice pushes this on every setScissorRectangleProperty() and
        /// resets it to the full target on each RenderTarget transition, so a single stored
        /// rectangle is always relative to the active target. Stored in framebuffer/target space,
        /// NOT viewport-local: it is intersected directly with the framebuffer∩Viewport clip.
        bool scissorSet_ = false;
        int scissorX_ = 0;
        int scissorY_ = 0;
        int scissorWidth_ = 0;
        int scissorHeight_ = 0;
    };
}
