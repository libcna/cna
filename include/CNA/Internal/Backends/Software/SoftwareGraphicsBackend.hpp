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

        /**
         * @brief How many mip levels this resource really HOLDS, counting from level 0.
         *
         * REMED-GFX-175. This is a count of levels whose pixels a caller actually supplied, not the
         * level count the public resource declares: a `Texture2D` created with `mipMap=true` whose
         * game only ever wrote level 0 reports 1 here, so the sampler cannot select a level nobody
         * filled. The default of 1 keeps every resource that has no chain -- render targets, and any
         * future colour surface -- behaving exactly as it did before mip selection existed.
         *
         * @return The number of contiguously stored levels, always at least 1.
         */
        [[nodiscard]] virtual int ColorLevelCount() const { return 1; }
        /**
         * @brief Width in pixels of mip level @p level.
         * @param level Mip level, 0 .. ColorLevelCount()-1.
         * @return The level's width; level 0's width for any resource with a single level.
         */
        [[nodiscard]] virtual int ColorWidth(int level) const { (void)level; return ColorWidth(); }
        /**
         * @brief Height in pixels of mip level @p level.
         * @param level Mip level, 0 .. ColorLevelCount()-1.
         * @return The level's height; level 0's height for any resource with a single level.
         */
        [[nodiscard]] virtual int ColorHeight(int level) const { (void)level; return ColorHeight(); }
        /**
         * @brief The RGBA8 pixels of mip level @p level, in the same layout as ColorPixels().
         * @param level Mip level, 0 .. ColorLevelCount()-1.
         * @return A reference to that level's own storage -- never a copy.
         */
        [[nodiscard]] virtual const std::vector<std::uint8_t>& ColorPixels(int level) const
        { (void)level; return ColorPixels(); }
    };

    /**
     * @brief REMED-GFX-150: one resolved XNA SamplerState for one texture slot.
     *
     * Holds the raw XNA enum ordinals `IGraphicsBackend::ApplySamplerState` already carries, so the
     * rasterizer's sampler reads exactly what the public `SamplerState` said and nothing is lost in
     * translation. Pre-fix `SoftwareGraphicsBackend::ApplySamplerState` named none of its parameters
     * and `SoftwareSpriteBatchBackend`'s equivalents were dead stores, so every textured fragment
     * was filtered LinearClamp whatever the game selected.
     *
     * The defaults are Linear/Clamp/Clamp, which is what an unconfigured slot behaved as before this
     * struct existed -- a slot that no `ApplySamplerState` call has reached is therefore unchanged.
     */
    struct SoftwareSamplerState
    {
        /** @brief Raw Microsoft::Xna::Framework::Graphics::TextureFilter ordinal (0 = Linear). */
        int filter = 0;
        /** @brief Raw TextureAddressMode ordinal for U (0 = Wrap, 1 = Clamp, 2 = Mirror). */
        int addressU = 1;
        /** @brief Raw TextureAddressMode ordinal for V (0 = Wrap, 1 = Clamp, 2 = Mirror). */
        int addressV = 1;
    };

    /**
     * @brief REMED-GFX-148: one complete XNA BlendState captured for a Software draw.
     *
     * The six raw ordinals are kept together so the CPU fragment stage cannot collapse distinct
     * presets to one boolean or resolve a later live device state. Defaults are the complete
     * Opaque identity: One/Zero with Add, independently for colour and alpha.
     */
    struct SoftwareBlendState
    {
        int colorSource = 0;
        int alphaSource = 0;
        int colorDestination = 1;
        int alphaDestination = 1;
        int colorFunction = 0;
        int alphaFunction = 0;

        /** @brief Whether this is the exact Opaque identity, including both functions. */
        [[nodiscard]] bool IsOpaqueIdentity() const
        {
            return colorSource == 0 && alphaSource == 0 &&
                   colorDestination == 1 && alphaDestination == 1 &&
                   colorFunction == 0 && alphaFunction == 0;
        }
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

        // REMED-GFX-175: the mip chain, as far as it was actually SUPPLIED. levels_[0] is level 0's
        // dimensions only -- level 0's pixels stay in pixels_, which UpdatePixels and every existing
        // consumer already own.
        [[nodiscard]] int ColorLevelCount() const override { return storedLevels_; }
        [[nodiscard]] int ColorWidth(int level) const override;
        [[nodiscard]] int ColorHeight(int level) const override;
        [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels(int level) const override;

    protected:
        int width_ = 0;
        int height_ = 0;
        std::vector<std::uint8_t> pixels_;

        /**
         * @brief One supplied mip level above level 0.
         *
         * REMED-GFX-175. Levels are held only once a caller writes them; nothing here is derived
         * from level 0, so a texture whose chain was declared but never filled keeps a stored level
         * count of 1 and samples exactly as it did before mip selection existed.
         */
        struct MipLevel
        {
            /** @brief Level width in pixels. */
            int width = 0;
            /** @brief Level height in pixels. */
            int height = 0;
            /** @brief Tightly packed RGBA8 pixels, row-major, top row first. */
            std::vector<std::uint8_t> pixels;
        };

        /// Levels 1..N as supplied; index i holds level i+1.
        std::vector<MipLevel> mipLevels_;
        /// How many levels are stored CONTIGUOUSLY from 0. Always at least 1.
        int storedLevels_ = 1;
        /// The level count the public resource declared, which bounds what may be stored.
        int declaredLevels_ = 1;
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
         * @return Always true -- REMED-GFX-127's "the whole region was written" report. Software's
         *         colour storage is CPU memory this object owns, so once the request validates
         *         there is no failure mode left between the copy starting and finishing; every
         *         rejected request throws instead.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
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

    /// SOFTWARE-82: real 6-face RGBA8 cube map storage, for EnvironmentMapEffect.
    ///
    /// REMED-GFX-135 extended it to EVERY mip level TextureCube declares. Previously only level 0
    /// was allocated, so a mipmapped cube reported a LevelCount its storage could not back and
    /// `SetData(level > 0, ...)` returned early without storing anything -- one of the three silent
    /// write-loss routes that finding removed. Storage is now allocated per level, so `LevelCount`
    /// and `SetData`/`GetData` agree at every level.
    class SoftwareTextureCubeBackend final : public ITextureCubeBackend
    {
    public:
        /**
         * @brief Allocates zeroed RGBA8 storage for all six faces.
         *
         * @param size   Edge length of one cube face at mip 0, in texels.
         * @param mipMap Allocate the full mip chain down to 1x1 as well as level 0.
         */
        SoftwareTextureCubeBackend(int size, bool mipMap);

        /**
         * @brief Copies one face's RGBA8 sub-rectangle into this backend's CPU storage.
         *
         * REMED-GFX-135. `levels_` IS this resource's authoritative content, so a completed copy is
         * a completed write -- there is no asynchronous stage to wait on and the caller's memory is
         * never retained past the call.
         *
         * @return True when the whole requested region was stored; false for an out-of-range level
         *         or face, an out-of-bounds rectangle or a source buffer too small for the region.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads one face's stored RGBA8 sub-rectangle back from this backend's CPU shadow.
         *
         * REMED-GFX-130. `levels_` IS this resource's authoritative content here -- SetData writes
         * straight into it and the rasterizer's cube sampler reads straight out of it -- so every
         * allocated level returns exact data. Reporting false for anything outside the allocated
         * chain is the point of the finding: the shared layer used to convert its own zeroed scratch
         * buffer into a complete transparent-black face instead.
         *
         * @return True when the whole requested region was copied out of the shadow; false for an
         *         out-of-range level or face, or an out-of-bounds rectangle.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] int GetSize() const { return size_; }
        /**
         * @brief How many mip levels this cube ALLOCATED, counting from level 0.
         *
         * REMED-GFX-182. This is the chain `TextureCube` declared and REMED-GFX-135 gave storage to,
         * not the part of it a caller filled -- see FaceLevelCount() for that. Reported by the cube
         * trace so "six levels are stored and only level 0 is reachable" is a measured statement.
         *
         * @return The allocated level count, always at least 1.
         */
        [[nodiscard]] int LevelCount() const { return levelCount_; }
        /**
         * @brief How many mip levels of one face a caller actually SUPPLIED, counting from level 0.
         *
         * REMED-GFX-182, and exactly REMED-GFX-175's `ColorLevelCount()` semantics applied per cube
         * face: storage for the whole declared chain exists from construction (REMED-GFX-135), but
         * it starts ZEROED, so a chain the game never wrote must not be selectable or a minified
         * draw would fade into transparent black nobody uploaded. A level counts only once the full
         * face rectangle at that level has been written and every level below it has too.
         *
         * @param face CubeMapFace ordinal 0-5.
         * @return The number of contiguously supplied levels for that face, always at least 1.
         */
        [[nodiscard]] int FaceLevelCount(int face) const;
        /**
         * @brief Face edge length in texels at mip level @p level.
         * @param level Mip level, 0 .. LevelCount()-1.
         * @return The level's edge length, never below 1.
         */
        [[nodiscard]] int FaceDim(int level) const { return LevelDim(level); }
        /// Real RGBA8 pixel storage for one of the 6 faces (CubeMapFace ordinal 0-5) at mip 0,
        /// size*size*4 bytes -- the rasterizer's cube sampler (SOFTWARE-82) reads directly from
        /// here.
        [[nodiscard]] const std::vector<std::uint8_t>& FacePixels(int face) const { return levels_[0][face]; }
        /**
         * @brief One face's RGBA8 storage at mip level @p level, in the same layout as FacePixels().
         *
         * REMED-GFX-182: the cube sampler reads a SELECTED level through this, so the MIPMAP
         * component of `SamplerStates[1]` reaches the reflection cube. An out-of-range level
         * resolves to level 0 rather than indexing outside the chain.
         *
         * @param face  CubeMapFace ordinal 0-5.
         * @param level Mip level, 0 .. LevelCount()-1.
         * @return A reference to that face's own storage at that level -- never a copy.
         */
        [[nodiscard]] const std::vector<std::uint8_t>& FacePixels(int face, int level) const;

    private:
        /// Face edge length at @p level, never below 1 -- mirrors TextureCube.cpp's own mipDim().
        [[nodiscard]] int LevelDim(int level) const;

        int size_ = 0;
        int levelCount_ = 1;
        /// levels_[level][face] -- one tightly packed RGBA8 buffer per face per allocated mip level.
        std::vector<std::array<std::vector<std::uint8_t>, 6>> levels_;
        /// REMED-GFX-182: supplied_[level][face] -- whether the FULL face rectangle at that level
        /// has been written. Level 0 of every face counts as supplied from construction, matching
        /// SoftwareTextureBackend's own `storedLevels_ = 1` starting point.
        std::vector<std::array<bool, 6>> supplied_;
        /// Contiguously supplied levels from 0, per face. Always at least 1.
        std::array<int, 6> faceLevels_{1, 1, 1, 1, 1, 1};
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

        /// REMED-GFX-150: the sampler `SpriteBatch::Begin` resolved for this batch (FNA defaults a
        /// null argument to SamplerState.LinearClamp and always re-applies the result, so this is
        /// re-established on every Begin and cannot leak from a previous batch). Pre-fix these three
        /// fields were written here and read nowhere, so the whole SamplerState argument was inert.
        [[nodiscard]] SoftwareSamplerState GetSamplerState() const
        { return SoftwareSamplerState{textureFilter_, addressU_, addressV_}; }

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
        void SetBlendFactor(float r, float g, float b, float a) override;
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
        /// REMED-GFX-148: complete BlendState and constant factor captured by a SpriteBatch draw.
        [[nodiscard]] const SoftwareBlendState& GetBlendState() const { return blendState_; }
        [[nodiscard]] const std::array<float, 4>& GetBlendFactor() const { return blendFactor_; }
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

        /// REMED-GFX-150: the SamplerState most recently applied to @p slot. `GraphicsDevice`
        /// re-applies every slot before each 3D draw, so slot 0 (texture0) and slot 1
        /// (DualTextureEffect's overlay) always describe the draw being issued. An out-of-range slot
        /// returns the default Linear/Clamp state rather than throwing, because this is read on the
        /// raster path rather than at the public API boundary where the slot is already validated.
        [[nodiscard]] SoftwareSamplerState GetSamplerState(int slot) const
        {
            if (slot < 0 || slot >= kMaxSamplerSlots) return SoftwareSamplerState{};
            return samplerSlots_[static_cast<std::size_t>(slot)];
        }

    private:
        /// XNA exposes 16 texture sampler slots; ApplySamplerState validates against this.
        static constexpr int kMaxSamplerSlots = 16;
        /// REMED-GFX-150: the per-slot SamplerState the rasterizer's sampler consults. Previously
        /// ApplySamplerState stored nothing at all.
        SoftwareSamplerState samplerSlots_[kMaxSamplerSlots]{};

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
        /// REMED-GFX-148: all four blend factors and both functions. The former boolean discarded
        /// every distinction except exact Opaque versus "anything else", forcing Additive,
        /// AlphaBlend and NonPremultiplied through one hard-coded straight-alpha equation.
        SoftwareBlendState blendState_{};
        /// GraphicsDevice.BlendFactor, snapshotted beside blendState_ for each Software draw.
        std::array<float, 4> blendFactor_{1.0f, 1.0f, 1.0f, 1.0f};
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
