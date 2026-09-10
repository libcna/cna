#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareFramebufferAllocation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    class SoftwareRenderer;

    /**
     * @brief Real CPU-owned RGBA8 colour, depth and optional resolved 4x MSAA storage.
     *
     * This is the Software renderer's actual state, not a bookkeeping fiction
     * (plans/plan_software.md design decision 3) -- every pixel written here is a genuinely correct
     * pixel a test can read back and assert on, with no GPU involved at all.
     */
    struct SoftwareFramebuffer
    {
        explicit SoftwareFramebuffer(bool allocateDepth = true,
                                     bool allocateStencil = true,
                                     int requestedSurfaceFormat = 0)
            : allocateDepthStorage(allocateDepth),
              allocateStencilStorage(allocateStencil),
              surfaceFormat(requestedSurfaceFormat)
        {
        }

        int width = 0;
        int height = 0;
        /// RGBA8, width*height*4 bytes. With MSAA this is the logically mutable resolved cache:
        /// a const readback may refresh it from `multiSampleColor` without changing the rendered
        /// image or the active render-pass state.
        mutable std::vector<std::uint8_t> color;
        /// Canonical shader-visible RGBA values for non-Color render-target formats.
        mutable std::vector<float> wideColor;
        std::vector<float> depthBuffer;   ///< Single-sample/resolved width*height depths, 0..1.
        /// Single-sample/resolved 8-bit stencil value per pixel.
        std::vector<std::uint8_t> stencilBuffer;
        /// Four RGBA8 samples per pixel when 4x CPU MSAA is enabled; empty otherwise. `color`
        /// remains the resolved presentation/readback image, so existing consumers never see an
        /// unresolved sample plane.
        mutable std::vector<std::uint8_t> multiSampleColor;
        /// Four canonical float RGBA samples per pixel for non-Color 4x targets.
        mutable std::vector<float> multiSampleWideColor;
        /// Four independent float depth samples per pixel when 4x CPU MSAA is enabled.
        std::vector<float> multiSampleDepthBuffer;
        /// Four independent 8-bit stencil samples per pixel when 4x CPU MSAA is enabled.
        std::vector<std::uint8_t> multiSampleStencilBuffer;
        int multiSampleCount = 0; ///< 0 = single sampled; the CPU implementation supports 4 only.
        bool allocateDepthStorage = true;
        bool allocateStencilStorage = true;
        int surfaceFormat = 0; ///< Raw XNA SurfaceFormat ordinal; zero is Color/RGBA8.

        void Resize(int w, int h);
        void SetMultiSampleCount(int sampleCount);
        /** @brief Returns whether four-sample color storage is active. */
        [[nodiscard]] bool HasMultiSampleColor() const { return multiSampleCount == 4; }
        /** @brief Returns whether four-sample depth storage is active. */
        [[nodiscard]] bool HasMultiSampleDepth() const
        { return HasMultiSampleColor() && allocateDepthStorage; }
        /** @brief Returns whether four-sample stencil storage is active. */
        [[nodiscard]] bool HasMultiSampleStencil() const
        { return HasMultiSampleColor() && allocateStencilStorage; }
        /** @brief Returns the number of raster samples stored per pixel. */
        [[nodiscard]] int EffectiveSampleCount() const { return HasMultiSampleColor() ? 4 : 1; }
        /** @brief Returns whether the target owns format-preserving float-domain colour storage. */
        [[nodiscard]] bool HasWideColor() const { return surfaceFormat != 0; }
        /**
         * @brief Reads one stored destination colour in shader arithmetic range.
         * @param pixel Pixel index in row-major order.
         * @param sample Sample index 0-3 for MSAA, or -1 for resolved/single-sample storage.
         * @return Canonical RGBA components after target-format quantization.
         */
        [[nodiscard]] std::array<float, 4> ReadColor(std::size_t pixel, int sample = -1) const;
        /**
         * @brief Applies the declared target format's channel expansion and precision.
         * @param value Shader-domain RGBA value.
         * @return The value a store followed by a load exposes.
         */
        [[nodiscard]] std::array<float, 4> QuantizeColor(
            const std::array<float, 4>& value) const;
        /**
         * @brief Returns the exact byte width of one declared-format texel.
         * @return Bytes per pixel for this framebuffer's SurfaceFormat.
         */
        [[nodiscard]] int DeclaredColorTexelSize() const;
        /**
         * @brief Decodes one exact declared-format texel.
         * @param data Source texel bytes.
         * @return Canonical shader-visible RGBA value.
         */
        [[nodiscard]] std::array<float, 4> DecodeDeclaredColor(const void* data) const;
        /**
         * @brief Encodes one canonical colour in the exact declared target format.
         * @param value Canonical shader-visible RGBA value.
         * @param data Destination texel bytes.
         */
        void EncodeDeclaredColor(const std::array<float, 4>& value, void* data) const;
        /**
         * @brief Writes selected channels and quantizes them to this target's declared format.
         * @param pixel Pixel index in row-major order.
         * @param sample Sample index 0-3 for MSAA, or -1 for resolved/single-sample storage.
         * @param value Incoming RGBA components.
         * @param colorWriteMask XNA ColorWriteChannels bits.
         */
        void WriteColor(std::size_t pixel, int sample,
                        const std::array<float, 4>& value, int colorWriteMask) const;
        /**
         * @brief Replaces resolved level-zero colour from exact declared-format rows.
         * @param data Source bytes.
         * @param stride Source row pitch in bytes.
         */
        void LoadDeclaredColor(const std::uint8_t* data, int stride);
        /**
         * @brief Encodes a level-zero rectangle to exact declared-format bytes.
         * @param x Left source coordinate.
         * @param y Top source coordinate.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Destination bytes.
         */
        void StoreDeclaredColor(int x, int y, int w, int h, void* data) const;
        /// Copies every resolved RGBA pixel into all active samples. A no-op when MSAA is off.
        void CopyResolvedColorToMultiSample();
        /// Resolves the per-sample colour plane into `color`. A no-op for single-sample targets.
        void ResolveColor() const;
        void ClearColor(float r, float g, float b, float a);
        void ClearDepthValue(float depthValue);
        void ClearStencilValue(int stencilValue);
    };

    class SoftwareVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit SoftwareVertexBufferRenderer(int vertexCapacity);

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        // SOFTWARE-108: the CPU vertex reader resolves elements by declaration semantic/index,
        // format and stream-local offset; retaining the declaration here avoids widening the
        // renderer-neutral draw contract with Software-only convenience data.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        [[nodiscard]] int Capacity() const { return capacity_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }
        /// The declaration this buffer carries for Software's semantic vertex decoder.
        [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& Declaration() const
        {
            return declaration_;
        }
        /// Raw vertex bytes from the most recent SetData() call. Declared buffers are interpreted
        /// by their elements; only the legacy empty-declaration CNAEXT buffer uses Stride().
        [[nodiscard]] const std::vector<std::uint8_t>& Data() const { return data_; }

    private:
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> data_;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    class SoftwareIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        SoftwareIndexBufferRenderer(int indexCapacity, bool thirtyTwoBit);

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
     * concrete renderer class. Before this, the sampler `dynamic_cast`ed straight to
     * `SoftwareTextureRenderer`, which a `SoftwareRenderTargetRenderer` is not -- so a finished render
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
         * filled. The default of 1 keeps every resource that has no chain -- including a
         * non-mipmapped render target and any future colour surface -- behaving exactly as it did
         * before mip selection existed.
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

        /**
         * @brief Fetches one texel in the value range exposed to an XNA pixel shader.
         *
         * The default converts the RGBA8 colour surface. Texture formats with signed-normalized,
         * floating-point, or wider normalized storage override this so sampling does not lose
         * range or precision merely because the CPU framebuffer itself is RGBA8.
         *
         * @param level Mip level beginning at zero.
         * @param x Texel x coordinate within the level.
         * @param y Texel y coordinate within the level.
         * @param r Receives the red component.
         * @param g Receives the green component.
         * @param b Receives the blue component.
         * @param a Receives the alpha component.
         */
        virtual void FetchColorTexel(int level, int x, int y,
                                     float& r, float& g, float& b, float& a) const
        {
            const int width = ColorWidth(level);
            const auto& pixels = ColorPixels(level);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            r = pixels[offset + 0] / 255.0f;
            g = pixels[offset + 1] / 255.0f;
            b = pixels[offset + 2] / 255.0f;
            a = pixels[offset + 3] / 255.0f;
        }
    };

    /**
     * @brief Read-only access to one Software cube resource's face and mip storage.
     *
     * Plain TextureCube and RenderTargetCube both implement this renderer-local capability, so
     * EnvironmentMapEffect samples the same CPU cube contract regardless of how the pixels were
     * produced.
     */
    class SoftwareCubeSurface
    {
    public:
        /** @brief Destroys the renderer-local cube storage view. */
        virtual ~SoftwareCubeSurface() = default;
        /**
         * @brief Returns the level-zero edge length.
         *
         * @return The width and height of every level-zero face in pixels.
         */
        [[nodiscard]] virtual int CubeSize() const = 0;
        /**
         * @brief Returns the contiguous mip count available for one face.
         *
         * @param face Raw CubeMapFace ordinal in the range 0 through 5.
         * @return The number of sampleable levels beginning at level zero.
         */
        [[nodiscard]] virtual int CubeFaceLevelCount(int face) const = 0;
        /**
         * @brief Returns one mip level's edge length.
         *
         * @param level Mip level beginning at zero.
         * @return The width and height of the requested square face.
         */
        [[nodiscard]] virtual int CubeFaceDimension(int level) const = 0;
        /**
         * @brief Returns one face and mip level's tightly packed RGBA8 pixels.
         *
         * @param face Raw CubeMapFace ordinal in the range 0 through 5.
         * @param level Mip level beginning at zero.
         * @return The immutable pixel storage used by the CPU cube sampler.
         */
        [[nodiscard]] virtual const std::vector<std::uint8_t>& CubeFacePixels(
            int face, int level) const = 0;
        /**
         * @brief Fetches one cube texel in shader-visible range without RGBA8 narrowing.
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Texel x coordinate.
         * @param y Texel y coordinate.
         * @param r Receives red.
         * @param g Receives green.
         * @param b Receives blue.
         * @param a Receives alpha.
         */
        virtual void FetchCubeColorTexel(int face, int level, int x, int y,
                                         float& r, float& g, float& b, float& a) const
        {
            const int dimension = CubeFaceDimension(level);
            const std::vector<std::uint8_t>& pixels = CubeFacePixels(face, level);
            const std::size_t offset =
                (static_cast<std::size_t>(y) * dimension + x) * 4u;
            r = pixels[offset + 0] / 255.0f;
            g = pixels[offset + 1] / 255.0f;
            b = pixels[offset + 2] / 255.0f;
            a = pixels[offset + 3] / 255.0f;
        }
    };

    /**
     * @brief REMED-GFX-150: one resolved XNA SamplerState for one texture slot.
     *
     * Holds the raw XNA enum ordinals `IGraphicsRenderer::ApplySamplerState` already carries, so the
     * rasterizer's sampler reads exactly what the public `SamplerState` said and nothing is lost in
     * translation. Pre-fix `SoftwareRenderer::ApplySamplerState` named none of its parameters
     * and `SoftwareSpriteBatchRenderer`'s equivalents were dead stores, so every textured fragment
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
        /** @brief Requested maximum anisotropy; Software clamps work to its deterministic CPU cap. */
        int maxAnisotropy = 4;
        /** @brief Most detailed mip level the sampler may select. */
        int maxMipLevel = 0;
        /** @brief Bias added to the computed mip level of detail before resource clamping. */
        float lodBias = 0.0f;
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

        /**
         * @brief Whether XNA/FNA disables blending for this factor combination.
         *
         * FNA3D derives BlendEnable from the four factors only. Consequently One/Zero on both
         * channels bypasses even a non-Add blend function; Software preserves that observable
         * fixed-function rule instead of evaluating an equation the reference never enables.
         */
        [[nodiscard]] bool IsOpaqueIdentity() const
        {
            return colorSource == 0 && alphaSource == 0 &&
                   colorDestination == 1 && alphaDestination == 1;
        }
    };

    class SoftwareTextureRenderer : public ITextureRenderer, public SoftwareColorSurface
    {
    public:
        explicit SoftwareTextureRenderer(const ImageData& data);
        SoftwareTextureRenderer(int width, int height);

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        /** @brief Returns the exact SurfaceFormat ordinal retained by this texture. */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        /** @brief Reports whether exact caller-visible bytes exist for a mip level. */
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;
        /** @brief Copies exact declared-format bytes from a texture rectangle. */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

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
        /**
         * @brief Fetches one decoded texel without narrowing signed or floating-point formats.
         *
         * @param level Mip level beginning at zero.
         * @param x Texel x coordinate within the level.
         * @param y Texel y coordinate within the level.
         * @param r Receives the red component.
         * @param g Receives the green component.
         * @param b Receives the blue component.
         * @param a Receives the alpha component.
         */
        void FetchColorTexel(int level, int x, int y,
                             float& r, float& g, float& b, float& a) const override;

    protected:
        int width_ = 0;
        int height_ = 0;
        std::vector<std::uint8_t> pixels_;
        /// Shader-visible RGBA values; unlike pixels_, this preserves signed and HDR ranges.
        std::vector<float> samplePixels_;

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
            /** @brief Exact caller-visible bytes in the declared SurfaceFormat. */
            std::vector<std::uint8_t> rawPixels;
            /** @brief Shader-visible RGBA values without RGBA8 narrowing. */
            std::vector<float> samplePixels;
        };

        int surfaceFormat_ = 0;
        std::vector<std::uint8_t> rawPixels_;
        /// Levels 1..N as supplied; index i holds level i+1.
        std::vector<MipLevel> mipLevels_;
        /// How many levels are stored CONTIGUOUSLY from 0. Always at least 1.
        int storedLevels_ = 1;
        /// The level count the public resource declared, which bounds what may be stored.
        int declaredLevels_ = 1;
    };

    class SoftwareRenderTargetRenderer final : public IRenderTargetRenderer, public SoftwareColorSurface
    {
    public:
        SoftwareRenderTargetRenderer(int w, int h, int depthFormat, bool mipMap, int multiSampleCount,
                                    bool hasRealDepthBuffer = true,
                                    bool hasStandaloneStencilBuffer = false,
                                    int surfaceFormat = 0);

        [[nodiscard]] int GetWidth() const override { return framebuffer_.width; }
        [[nodiscard]] int GetHeight() const override { return framebuffer_.height; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /**
         * @brief Replaces one exact declared-format mip level.
         * @param level Destination mip level.
         * @param data Source pixels.
         * @param levelW Expected mip width.
         * @param levelH Expected mip height.
         */
        void UpdatePixelsLevel(int level, const uint8_t* data, int levelW, int levelH) override;
        /**
         * @brief Reports readable level-zero or allocated/generated mip storage.
         * @param level Mip level to query.
         * @return True when the level is currently readable.
         */
        [[nodiscard]] bool HasDefinedMipLevel(int level) const noexcept override;
        /**
         * @brief Returns the actual SurfaceFormat ordinal backing this target.
         * @return Raw SurfaceFormat ordinal.
         */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override { return surfaceFormat_; }
        /**
         * @brief Copies a rendered sub-rectangle in the target's declared SurfaceFormat.
         *
         * REMED-GFX-124: without this override the call reached `ITextureRenderer::GetData`'s default
         * no-op. That was not merely a missing feature -- `Texture2D::GetData`'s render-target
         * fallback hands the renderer a scratch buffer it zero-initialized itself and then converts
         * those bytes for the caller, so a no-op renderer produced a fabricated, fully written,
         * uniformly transparent-black frame rather than an obviously empty result.
         *
         * Reads the colour attachment only; the depth buffer is never consulted. Rows are returned
         * top-first, matching this renderer's framebuffer layout and `ReadBackbuffer`, so no flip is
         * applied. Level-zero reads of an active multisampled target first refresh the resolved
         * colour cache from its live samples; this is a snapshot only and neither unbinds the target
         * nor generates mipmaps. An unsupported or out-of-range request throws instead of leaving
         * caller memory silently unchanged.
         *
         * @param level      Mip level. Levels above zero are available only after a mipmapped
         *                   target was unbound, which completes its CPU box-filter resolve.
         * @param x          Left edge of the requested rectangle, in pixels.
         * @param y          Top edge of the requested rectangle, in pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed declared-format pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @throws System::ArgumentNullException if @p data is null.
         * @throws System::NotSupportedException if @p level is outside this target's chain, or
         *         if a requested generated level is not ready because its render pass is active.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle is
         *         empty or leaves the target, or @p dataLength is too small for the rectangle.
         * @return Always true -- REMED-GFX-127's "the whole region was written" report. Software's
         *         colour storage is CPU memory this object owns, so once the request validates
         *         there is no failure mode left between the copy starting and finishing; every
         *         rejected request throws instead.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        /**
         * @brief Returns how many readback requests reached this renderer object.
         *
         * REMED-GFX-198 uses this diagnostic to distinguish shared argument rejection from a
         * Software capability refusal. Invalid public mip levels must leave this count unchanged;
         * a valid public read increments it exactly once.
         *
         * @return The number of calls to GetData on this renderer object.
         */
        [[nodiscard]] std::size_t GetReadbackCallCountEXT() const { return readbackCallCount_; }
        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int) const override
        {
            return hasRealDepthBuffer_ ? depthFormat_ : 0;
        }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        { return hasRealDepthBuffer_ && depthFormatWasRequested; }
        [[nodiscard]] bool HasRealStencilBuffer(bool stencilFormatWasRequested) const override
        {
            return hasStandaloneStencilBuffer_ ||
                   (hasRealDepthBuffer_ && stencilFormatWasRequested);
        }

        // SoftwareColorSurface -- level 0 is the SAME storage the rasterizer writes into. A
        // finished mipmapped target additionally exposes its generated box-filter levels, with no
        // shadow copy of level 0 that could drift out of sync with what was rendered.
        [[nodiscard]] int ColorWidth() const override { return framebuffer_.width; }
        [[nodiscard]] int ColorHeight() const override { return framebuffer_.height; }
        [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels() const override
        { return framebuffer_.color; }
        [[nodiscard]] int ColorLevelCount() const override
        { return mipLevelsReady_ ? levelCount_ : 1; }
        [[nodiscard]] int ColorWidth(int level) const override;
        [[nodiscard]] int ColorHeight(int level) const override;
        [[nodiscard]] const std::vector<std::uint8_t>& ColorPixels(int level) const override;
        /**
         * @brief Fetches a target texel without narrowing float-domain storage to RGBA8.
         * @param level Mip level beginning at zero.
         * @param x Texel x coordinate.
         * @param y Texel y coordinate.
         * @param r Receives red.
         * @param g Receives green.
         * @param b Receives blue.
         * @param a Receives alpha.
         */
        void FetchColorTexel(int level, int x, int y,
                             float& r, float& g, float& b, float& a) const override;

        [[nodiscard]] bool IsBound() const { return bound_; }
        [[nodiscard]] SoftwareFramebuffer& Framebuffer() { return framebuffer_; }
        [[nodiscard]] const SoftwareFramebuffer& Framebuffer() const { return framebuffer_; }

    private:
        /// Regenerates levels 1..N from framebuffer level 0 with a clamped 2x2 RGBA8 box filter.
        /// Called only as a render target leaves the active render pass.
        void GenerateMipMaps();
        [[nodiscard]] int MipWidth(int level) const;
        [[nodiscard]] int MipHeight(int level) const;

        SoftwareFramebuffer framebuffer_;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        std::vector<std::vector<std::uint8_t>> mipLevels_; ///< levels 1..levelCount_-1
        std::vector<std::vector<float>> mipWideLevels_; ///< canonical RGBA float levels 1..N
        bool mipLevelsReady_ = false;
        int surfaceFormat_ = 0;
        int multiSampleCount_ = 0;
        bool hasRealDepthBuffer_ = true;
        bool hasStandaloneStencilBuffer_ = false;
        bool bound_ = false;
        mutable std::size_t readbackCallCount_ = 0;
    };

    /// SOFTWARE-82: real 6-face RGBA8 cube map storage, for EnvironmentMapEffect.
    ///
    /// REMED-GFX-135 extended it to EVERY mip level TextureCube declares. Previously only level 0
    /// was allocated, so a mipmapped cube reported a LevelCount its storage could not back and
    /// `SetData(level > 0, ...)` returned early without storing anything -- one of the three silent
    /// write-loss routes that finding removed. Storage is now allocated per level, so `LevelCount`
    /// and `SetData`/`GetData` agree at every level.
    class SoftwareTextureCubeRenderer final : public ITextureCubeRenderer,
                                              public SoftwareCubeSurface
    {
    public:
        /**
         * @brief Allocates zeroed RGBA8 storage for all six faces.
         *
         * @param size   Edge length of one cube face at mip 0, in texels.
         * @param mipMap Allocate the full mip chain down to 1x1 as well as level 0.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal retained by the cube.
         */
        SoftwareTextureCubeRenderer(int size, bool mipMap, int surfaceFormat);

        /**
         * @brief Copies one face's RGBA8 sub-rectangle into this renderer's CPU storage.
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
         * @brief Stores exact DXT blocks and refreshes the decoded CPU sampling face.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Block-aligned destination x coordinate in texels.
         * @param y Block-aligned destination y coordinate in texels.
         * @param w Region width in texels, block-aligned or reaching the mip edge.
         * @param h Region height in texels, block-aligned or reaching the mip edge.
         * @param data Exact DXT block payload.
         * @param dataLength Available payload bytes.
         * @return True when the complete region was retained and decoded.
         */
        [[nodiscard]] bool SetCompressedDataEXT(
            int face, int level, int x, int y, int w, int h,
            const void* data, int dataLength) override;
        /**
         * @brief Stores exact uncompressed declared-format cube texels.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Destination x coordinate in texels.
         * @param y Destination y coordinate in texels.
         * @param w Region width in texels.
         * @param h Region height in texels.
         * @param data Tightly packed declared-format source texels.
         * @param dataLength Available source bytes.
         * @return True when the complete region and its decoded sampling plane were updated.
         */
        [[nodiscard]] bool SetDataBytesEXT(
            int face, int level, int x, int y, int w, int h,
            const void* data, int dataLength) override;
        /**
         * @brief Reads one face's stored RGBA8 sub-rectangle back from this renderer's CPU shadow.
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
        /**
         * @brief Reads exact uncompressed declared-format cube texels.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Source x coordinate in texels.
         * @param y Source y coordinate in texels.
         * @param w Region width in texels.
         * @param h Region height in texels.
         * @param data Destination for tightly packed declared-format texels.
         * @param dataLength Available destination bytes.
         * @return True when the complete requested raw region was copied.
         */
        [[nodiscard]] bool GetDataBytesEXT(
            int face, int level, int x, int y, int w, int h,
            void* data, int dataLength) const override;

        [[nodiscard]] int GetSize() const { return size_; }
        /**
         * @brief Returns the declared SurfaceFormat ordinal retained by this cube.
         * @return The raw SurfaceFormat ordinal supplied at construction.
         */
        [[nodiscard]] int GetSurfaceFormatEXT() const noexcept override
        { return surfaceFormat_; }
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

        /**
         * @brief Returns the level-zero edge length through the shared CPU cube sampler contract.
         *
         * @return The cube size in pixels.
         */
        [[nodiscard]] int CubeSize() const override { return GetSize(); }
        /**
         * @brief Returns the contiguous supplied mip count for one face.
         *
         * @param face Raw CubeMapFace ordinal.
         * @return The number of sampleable levels beginning at zero.
         */
        [[nodiscard]] int CubeFaceLevelCount(int face) const override
        { return FaceLevelCount(face); }
        /**
         * @brief Returns one mip level's edge length.
         *
         * @param level Mip level beginning at zero.
         * @return The face dimension in pixels.
         */
        [[nodiscard]] int CubeFaceDimension(int level) const override { return FaceDim(level); }
        /**
         * @brief Returns immutable sample storage for one face and mip level.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @return The tightly packed RGBA8 face pixels.
         */
        [[nodiscard]] const std::vector<std::uint8_t>& CubeFacePixels(
            int face, int level) const override
        { return FacePixels(face, level); }
        /**
         * @brief Fetches one format-decoded cube texel without precision narrowing.
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Texel x coordinate.
         * @param y Texel y coordinate.
         * @param r Receives red.
         * @param g Receives green.
         * @param b Receives blue.
         * @param a Receives alpha.
         */
        void FetchCubeColorTexel(int face, int level, int x, int y,
                                 float& r, float& g, float& b, float& a) const override;

    private:
        /// Face edge length at @p level, never below 1 -- mirrors TextureCube.cpp's own mipDim().
        [[nodiscard]] int LevelDim(int level) const;

        int size_ = 0;
        int surfaceFormat_ = 0;
        int levelCount_ = 1;
        /// levels_[level][face] -- one tightly packed RGBA8 buffer per face per allocated mip level.
        std::vector<std::array<std::vector<std::uint8_t>, 6>> levels_;
        /// Exact uncompressed declared-format bytes, retained independently from sampled colour.
        std::vector<std::array<std::vector<std::uint8_t>, 6>> rawLevels_;
        /// Canonical shader-visible RGBA values preserving wider-than-8-bit precision.
        std::vector<std::array<std::vector<float>, 6>> sampleLevels_;
        /// Exact face/mip DXT blocks; empty for an uncompressed cube.
        std::vector<std::array<std::vector<std::uint8_t>, 6>> compressedLevels_;
        /// REMED-GFX-182: supplied_[level][face] -- whether the FULL face rectangle at that level
        /// has been written. Level 0 of every face counts as supplied from construction, matching
        /// SoftwareTextureRenderer's own `storedLevels_ = 1` starting point.
        std::vector<std::array<bool, 6>> supplied_;
        /// Contiguously supplied levels from 0, per face. Always at least 1.
        std::array<int, 6> faceLevels_{1, 1, 1, 1, 1, 1};
    };

    /**
     * @brief SOFTWARE-119 CPU renderable cube with isolated face colour and shared depth/stencil.
     */
    class SoftwareRenderTargetCubeRenderer final : public IRenderTargetCubeRenderer,
                                                   public SoftwareCubeSurface
    {
    public:
        /**
         * @brief Allocates six renderable RGBA8 faces and the requested classic target storage.
         *
         * @param size Edge length of every face.
         * @param depthFormat Raw XNA DepthFormat ordinal.
         * @param preserveContents Whether public usage requests preservation.
         * @param mipMap Whether to allocate and generate the full mip chain.
         * @param multiSampleCount Requested sample count; Software applies either 0 or 4.
         * @param surfaceFormat Raw SurfaceFormat ordinal for the colour attachment.
         */
        SoftwareRenderTargetCubeRenderer(int size, int depthFormat, bool preserveContents,
                                         bool mipMap, int multiSampleCount,
                                         int surfaceFormat = 0);

        /**
         * @brief Returns the level-zero edge length.
         *
         * @return The cube size in pixels.
         */
        [[nodiscard]] int GetSize() const override { return size_; }
        /**
         * @brief Selects one face as the active render surface.
         *
         * @param face Raw CubeMapFace ordinal in the range 0 through 5.
         */
        void BindAsRenderTargetFace(int face) override;
        /** @brief Resolves and releases the active face, generating its mip chain when requested. */
        void UnbindAsRenderTarget() override;
        /**
         * @brief Returns the applied sample count.
         *
         * @return Either zero or four.
         */
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /**
         * @brief Reports whether requested depth storage is real.
         *
         * @param requested Whether the public layer requested a depth buffer.
         * @return True when the target owns compatible CPU depth storage.
         */
        [[nodiscard]] bool HasRealDepthBuffer(bool requested) const override
        { return requested && depthFormat_ != 0; }
        /**
         * @brief Reports whether requested stencil storage is real.
         *
         * @param requested Whether the public layer requested a stencil buffer.
         * @return True only for Depth24Stencil8 storage.
         */
        [[nodiscard]] bool HasRealStencilBuffer(bool requested) const override
        { return requested && depthFormat_ == 3; }
        /**
         * @brief Uploads a tightly packed declared-format rectangle to one face and mip level.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Left edge of the destination rectangle.
         * @param y Top edge of the destination rectangle.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Source texels in the target's declared format.
         * @param dataLength Available source bytes.
         * @return True when the complete validated upload was performed.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /**
         * @brief Uploads exact declared-format texels to one cube-target face.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Left edge of the destination rectangle.
         * @param y Top edge of the destination rectangle.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Source texels in the target's declared format.
         * @param dataLength Available source bytes.
         * @return True when the complete validated region was stored.
         */
        [[nodiscard]] bool SetDataBytesEXT(
            int face, int level, int x, int y, int w, int h,
            const void* data, int dataLength) override
        {
            return SetData(face, level, x, y, w, h, data, dataLength);
        }
        /**
         * @brief Reads a tightly packed declared-format rectangle from one face and mip level.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Left edge of the source rectangle.
         * @param y Top edge of the source rectangle.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Destination texels in the target's declared format.
         * @param dataLength Available destination bytes.
         * @return True when the complete validated readback was performed.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        /**
         * @brief Reads exact declared-format texels from one cube-target face.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Left edge of the source rectangle.
         * @param y Top edge of the source rectangle.
         * @param w Rectangle width.
         * @param h Rectangle height.
         * @param data Destination texels in the target's declared format.
         * @param dataLength Available destination bytes.
         * @return True when the complete validated region was returned.
         */
        [[nodiscard]] bool GetDataBytesEXT(
            int face, int level, int x, int y, int w, int h,
            void* data, int dataLength) const override
        {
            return GetData(face, level, x, y, w, h, data, dataLength);
        }

        /**
         * @brief Returns the level-zero edge length.
         *
         * @return The cube size in pixels.
         */
        [[nodiscard]] int CubeSize() const override { return size_; }
        /**
         * @brief Returns the contiguous generated or supplied mip count for one face.
         * @param face Raw CubeMapFace ordinal.
         * @return The number of sampleable levels beginning at zero.
         */
        [[nodiscard]] int CubeFaceLevelCount(int face) const override;
        /**
         * @brief Returns one mip level's edge length.
         * @param level Mip level beginning at zero.
         * @return The face dimension in pixels.
         */
        [[nodiscard]] int CubeFaceDimension(int level) const override;
        /**
         * @brief Returns immutable sample storage for one face and mip level.
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @return The tightly packed RGBA8 face pixels.
         */
        [[nodiscard]] const std::vector<std::uint8_t>& CubeFacePixels(
            int face, int level) const override;
        /**
         * @brief Fetches a rendered cube texel from its format-preserving CPU plane.
         * @param face Raw CubeMapFace ordinal.
         * @param level Mip level beginning at zero.
         * @param x Texel x coordinate.
         * @param y Texel y coordinate.
         * @param r Receives red.
         * @param g Receives green.
         * @param b Receives blue.
         * @param a Receives alpha.
         */
        void FetchCubeColorTexel(int face, int level, int x, int y,
                                 float& r, float& g, float& b, float& a) const override;

        /**
         * @brief Returns the face currently selected for rendering.
         *
         * @return The raw CubeMapFace ordinal, or -1 when unbound.
         */
        [[nodiscard]] int ActiveFace() const { return activeFace_; }
        /**
         * @brief Returns the active face's framebuffer.
         *
         * @return Mutable CPU color/depth/stencil storage for the bound face.
         */
        [[nodiscard]] SoftwareFramebuffer& Framebuffer();
        /**
         * @brief Returns the active face's framebuffer.
         *
         * @return Immutable CPU color/depth/stencil storage for the bound face.
         */
        [[nodiscard]] const SoftwareFramebuffer& Framebuffer() const;
        /**
         * @brief Prepares one face as a member of a simultaneous render-target set.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param ownsDepthStencil Whether this is slot zero and therefore owns the set's shared
         *                         depth/stencil attachment.
         * @return The selected face's CPU framebuffer.
         */
        [[nodiscard]] SoftwareFramebuffer& BindForMrt(int face, bool ownsDepthStencil);
        /**
         * @brief Resolves one face leaving a simultaneous render-target set.
         *
         * @param face Raw CubeMapFace ordinal.
         * @param ownsDepthStencil Whether the face owns the set's shared depth/stencil attachment.
         */
        void UnbindForMrt(int face, bool ownsDepthStencil);

    private:
        void GenerateMipMaps(int face);
        void LoadSharedDepthStencil(SoftwareFramebuffer& framebuffer);
        void StoreSharedDepthStencil(const SoftwareFramebuffer& framebuffer);
        [[nodiscard]] std::vector<std::uint8_t>& MutableFacePixels(int face, int level);

        int size_ = 0;
        int depthFormat_ = 0;
        bool mipMap_ = false;
        int levelCount_ = 1;
        int multiSampleCount_ = 0;
        int surfaceFormat_ = 0;
        int activeFace_ = -1;
        bool bound_ = false;
        std::array<bool, 6> boundFaces_{};
        std::array<SoftwareFramebuffer, 6> framebuffers_;
        std::vector<std::array<std::vector<std::uint8_t>, 6>> mipLevels_;
        std::vector<std::array<std::vector<float>, 6>> mipWideLevels_;
        std::vector<std::array<bool, 6>> supplied_;
        std::array<int, 6> faceLevelCounts_{1, 1, 1, 1, 1, 1};
        std::vector<float> sharedDepth_;
        std::vector<std::uint8_t> sharedStencil_;
        std::vector<float> sharedMultiSampleDepth_;
        std::vector<std::uint8_t> sharedMultiSampleStencil_;
    };

    /** @brief CPU-owned declared-format storage for an XNA volume texture and all mip levels. */
    class SoftwareTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        /**
         * @brief Allocates zeroed storage for the requested volume and optional full mip chain.
         *
         * XNA requests the complete chain through the largest of width, height and depth.
         *
         * @param width Volume width at level zero.
         * @param height Volume height at level zero.
         * @param depth Volume depth at level zero.
         * @param mipMap Whether to allocate the full XNA mip chain.
         * @param surfaceFormat Raw XNA SurfaceFormat ordinal retained by the volume.
         */
        SoftwareTexture3DRenderer(int width, int height, int depth, bool mipMap,
                                  int surfaceFormat);

        /**
         * @brief Copies a tightly packed RGBA8 box into one mip level.
         *
         * @return True only when the complete box was stored.
         */
        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   const void* data, int dataLength) override;

        /**
         * @brief Copies a tightly packed RGBA8 box out of one mip level.
         *
         * @return True only when the complete box was returned.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   void* data, int dataLength) const override;

        /**
         * @brief Stores exact declared-format voxels for the requested box.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param z Front edge.
         * @param w Box width.
         * @param h Box height.
         * @param depth Box depth.
         * @param data Source bytes.
         * @param dataLength Available source bytes.
         * @return True when the complete box was stored.
         */
        [[nodiscard]] bool SetDataBytesEXT(
            int level, int x, int y, int z, int w, int h, int depth,
            const void* data, int dataLength) override;

        /**
         * @brief Returns exact declared-format voxels for the requested box.
         * @param level Mip level.
         * @param x Left edge.
         * @param y Top edge.
         * @param z Front edge.
         * @param w Box width.
         * @param h Box height.
         * @param depth Box depth.
         * @param data Destination bytes.
         * @param dataLength Available destination bytes.
         * @return True when the complete box was returned.
         */
        [[nodiscard]] bool GetDataBytesEXT(
            int level, int x, int y, int z, int w, int h, int depth,
            void* data, int dataLength) const override;

        /**
         * @brief Reports the level-zero volume dimensions to renderer-side consumers.
         *
         * @param width Receives the width.
         * @param height Receives the height.
         * @param depth Receives the depth.
         */
        void GetDimensionsEXT(int& width, int& height, int& depth) const noexcept override;

    private:
        [[nodiscard]] int LevelWidth(int level) const;
        [[nodiscard]] int LevelHeight(int level) const;
        [[nodiscard]] int LevelDepth(int level) const;

        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int levelCount_ = 1;
        int surfaceFormat_ = 0;
        /// levels_[level] is slice-major, row-major, tightly packed declared-format storage.
        std::vector<std::vector<std::uint8_t>> levels_;
    };

    // Cube-map render targets remain a tracked parity gap. SOFTWARE-118 gives Texture3D an exact
    // CPU storage/transfer implementation; shader sampling is a separate CNAEXT-only surface in
    // the current repository and is not advertised by the storage capability.

    /** @brief Deterministic CPU implementation of an XNA occlusion query. */
    class SoftwareOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        /**
         * @brief Creates a query owned by the specified Software renderer.
         *
         * @param owner Renderer whose passing raster samples are measured.
         */
        explicit SoftwareOcclusionQueryRenderer(SoftwareRenderer& owner);

        /** @brief Releases an active query slot before destruction. */
        ~SoftwareOcclusionQueryRenderer() override;

        /** @brief Starts a fresh synchronous CPU sample measurement. */
        void Begin() override;

        /** @brief Ends the current measurement and makes its result complete. */
        void End() override;

        /**
         * @brief Gets whether the most recently ended measurement is complete.
         *
         * @return true after a matching End call.
         */
        [[nodiscard]] bool IsComplete() const override { return complete_ && !active_; }

        /**
         * @brief Gets the exact number of samples that passed the raster tests.
         *
         * @return Saturating exact sample count from the latest measurement.
         */
        [[nodiscard]] int PixelCount() const override { return pixelCount_; }

        /**
         * @brief Adds the set bits in a passing single-sample or 4x MSAA mask.
         *
         * @param passingSamples Mask returned by the shared fragment-test state machine.
         */
        void RecordPassingSamples(unsigned int passingSamples) noexcept
        {
            const int increment =
                static_cast<int>((passingSamples >> 0u) & 1u) +
                static_cast<int>((passingSamples >> 1u) & 1u) +
                static_cast<int>((passingSamples >> 2u) & 1u) +
                static_cast<int>((passingSamples >> 3u) & 1u);
            if (pixelCount_ > std::numeric_limits<int>::max() - increment)
                pixelCount_ = std::numeric_limits<int>::max();
            else
                pixelCount_ += increment;
        }

    private:
        SoftwareRenderer* owner_ = nullptr;
        int pixelCount_ = 0;
        bool active_ = false;
        bool complete_ = false;
    };

    class SoftwareEffectRenderer final : public IEffectRenderer
    {
    public:
        // Mirrors HEADLESS-16: accepts any GLSL/HLSL/WGSL source string without compiling it --
        // this renderer's own fixed pixel-shading path (Phase S5/S6) is what actually renders,
        // not the supplied shader source (plans/plan_software.md design decision 8).
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
        void BindTexture(int, ITextureRenderer*) override {}

        [[nodiscard]] bool IsBound() const { return bound_; }

    private:
        bool compiled_ = false;
        bool bound_ = false;
    };

    /// Draws are wired to the shared rasterizer core in Phase S6 (SOFTWARE-51) -- a
    /// SpriteBatch::Draw() call is just a textured quad (2 triangles), reusing the same code
    /// path DrawPrimitivesEx uses (plans/plan_software.md design decision 5).
    class SoftwareSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit SoftwareSpriteBatchRenderer(class SoftwareRenderer& owner);

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transformMatrix_ = m; }
        void SetCustomEffect(Effect* effect) override { customEffect_ = effect; }
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerMaxAnisotropy(int maxAnisotropy) override
        { maxAnisotropy_ = maxAnisotropy; }
        void SetSamplerMipState(int maxMipLevel, float lodBias) override
        { maxMipLevel_ = maxMipLevel; lodBias_ = lodBias; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        { addressU_ = addressU; addressV_ = addressV; }
        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;
        /**
         * @brief Rasterizes one sprite without quantizing its destination geometry.
         *
         * @param texture Source texture.
         * @param destinationX Destination left edge in viewport-local pixels.
         * @param destinationY Destination top edge in viewport-local pixels.
         * @param destinationWidth Destination width in pixels.
         * @param destinationHeight Destination height in pixels.
         * @param sourceRectangle Source texel rectangle.
         * @param color Per-channel tint.
         * @param rotation Rotation about @p origin in radians.
         * @param origin Rotation and scale origin in source-texel units.
         * @param effects Horizontal and vertical reflection flags.
         * @param layerDepth Sprite sort depth forwarded to the fragment pipeline.
         */
        void Draw(const ITextureRenderer& texture,
                  float destinationX,
                  float destinationY,
                  float destinationWidth,
                  float destinationHeight,
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
        { return SoftwareSamplerState{textureFilter_, addressU_, addressV_, maxAnisotropy_,
                                      maxMipLevel_, lodBias_}; }

    private:
        SoftwareRenderer& owner_;
        bool begun_ = false;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        Effect* customEffect_ = nullptr;
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        int maxAnisotropy_ = 4;
        int maxMipLevel_ = 0;
        float lodBias_ = 0.0f;
    };

    /**
     * @brief Software (CPU) rasterizer graphics renderer.
     *
     * See plans/plan_software.md for the full task breakdown and design rationale. Unlike HEADLESS
     * (which only does bookkeeping and never produces a real pixel), this renderer actually
     * rasterizes real triangles into a CPU-owned RGBA8 framebuffer -- GetBackBufferData()/
     * ReadBackbuffer() return genuinely correct pixels, with no GPU, display server, or driver
     * involved at all.
     */
    class SoftwareRenderer : public IGraphicsRenderer
    {
    public:
        SoftwareRenderer(int virtualWidth, int virtualHeight,
                                bool allocateDepthBuffer = true,
                                bool allocateStencilBuffer = true);
        ~SoftwareRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        /**
         * @brief Returns the sample count actually stored by the CPU backbuffer.
         * @return Zero for single-sample storage or four for the CPU 4x mode.
         */
        [[nodiscard]] int GetMultiSampleCount() const override
        { return backbuffer_.multiSampleCount; }
        /**
         * @brief Reports the construction-time sample count applied by the CPU backbuffer.
         * @param requestedMultiSampleCount Requested count before renderer clamping.
         * @return Zero or four according to the storage actually allocated.
         */
        [[nodiscard]] int GetAppliedMultiSampleCountEXT(
            int requestedMultiSampleCount) const override;
        /**
         * @brief Reports the fixed RGBA8 Color format used by the CPU backbuffer.
         * @param requestedFormat Requested SurfaceFormat ordinal.
         * @return SurfaceFormat::Color's ordinal.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override;
        /**
         * @brief Reconfigures CPU backbuffer sample storage at device reset time.
         * @param requestedMultiSampleCount Requested XNA presentation sample count.
         * @return Zero or four according to the storage actually allocated.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        /**
         * @brief Reconfigures meaningful CPU backbuffer depth and stencil storage.
         * @param backBufferFormat Requested backbuffer SurfaceFormat ordinal.
         * @param depthStencilFormat Requested DepthFormat ordinal.
         * @param isFullScreen Requested fullscreen state, which has no CPU-window effect.
         */
        void UpdatePresentationFormatEXT(
            int backBufferFormat, int depthStencilFormat, bool isFullScreen) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        /**
         * @brief Creates a format-preserving CPU RenderTarget2D.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param depthFormat Raw DepthFormat ordinal.
         * @param preserveContents Whether target transitions preserve prior contents.
         * @param mipMap Whether to allocate a mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat Raw SurfaceFormat ordinal.
         * @return A CPU render target with matching declared-format storage.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;
        /**
         * @brief Creates a CPU-backed renderable cube resource.
         *
         * @param size Edge length of every face.
         * @param depthFormat Raw XNA DepthFormat ordinal.
         * @param preserveContents Whether target transitions preserve prior contents.
         * @param mipMap Whether the target owns a generated mip chain.
         * @param multiSampleCount Requested multisample count.
         * @return A six-face Software render target.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;
        /**
         * @brief Creates a format-preserving CPU RenderTargetCube.
         * @param size Edge length of every face.
         * @param depthFormat Raw DepthFormat ordinal.
         * @param preserveContents Whether target transitions preserve prior contents.
         * @param mipMap Whether to allocate a mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat Raw SurfaceFormat ordinal.
         * @return A six-face CPU render target with matching declared-format storage.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        /**
         * @brief Selects one cube face as the active CPU framebuffer.
         *
         * @param rt Render target to bind, or null to restore the backbuffer.
         * @param face Raw CubeMapFace ordinal.
         */
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        /**
         * @brief Reports support for linearly filtering half-float texture data.
         *
         * @return Always true because the CPU sampler filters decoded floating-point planes.
         */
        [[nodiscard]] bool SupportsHalfFloatTextureLinearFilteringEXT() const override;
        /** @brief Classifies formats whose complete CPU texture path is implemented. */
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(
            int surfaceFormat) const override;
        /**
         * @brief Classifies formats whose complete six-face CPU cube path is implemented.
         * @param surfaceFormat Raw SurfaceFormat ordinal.
         * @return Supported for Color and DXT1/3/5, Unsupported for known unfinished formats.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyTextureCubeFormatEXT(
            int surfaceFormat) const override;
        /**
         * @brief Classifies formats whose complete CPU volume-transfer path is implemented.
         * @param surfaceFormat Raw SurfaceFormat ordinal.
         * @return Supported for the fifteen XNA HiDef volume formats.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyTexture3DFormatEXT(
            int surfaceFormat) const override;
        /**
         * @brief Reports exactly the classic colour target formats backed by CPU storage.
         * @param surfaceFormat Raw SurfaceFormat ordinal.
         * @return Supported for implemented classic target formats, Unsupported for known gaps.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetFormatEXT(
            int surfaceFormat) const override;
        /** @brief Prevents packed texture bytes from entering the Color transfer overload. */
        [[nodiscard]] RendererFormatVerdict ClassifyColorTransferFormatEXT(
            int surfaceFormat) const override;
        /** @brief Reports the DXT formats whose exact 4x4 blocks Software preserves and decodes. */
        [[nodiscard]] bool IsCompressedTransferFormatEXT(int surfaceFormat) const override;
        /** @brief Reports DXT cube formats whose exact blocks Software retains and decodes. */
        [[nodiscard]] bool IsCompressedCubeTransferFormatEXT(
            int surfaceFormat) const override;
        /** @brief Keeps loader-provided DXT blocks so the Software texture owns exact bytes. */
        [[nodiscard]] bool LoadsCompressedContentNativelyEXT() const override { return true; }

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
        void ApplyRasterizerMultiSampleState(bool enabled) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
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

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

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
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                                   const Matrix& projection, PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
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
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
                              const Matrix& projection, PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        /**
         * @brief Draws indexed geometry once for each per-instance stream record.
         *
         * @param vb Primary per-vertex buffer.
         * @param ib Bound index buffer.
         * @param world Effect world matrix.
         * @param view Effect view matrix.
         * @param projection Effect projection matrix.
         * @param primitive Primitive topology.
         * @param primitiveCount Number of primitives per instance.
         * @param instanceCount Number of instances to rasterize.
         * @param params Complete per-draw effect and vertex-stream state.
         */
        void DrawInstancedPrimitivesEx(
            const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            PrimitiveType primitive, int primitiveCount, int instanceCount,
            const GpuDrawParams& params) override;

        // ---- Software-specific, CNAEXT-equivalent debug/testing API ----

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
        /**
         * @brief Returns the raw XNA ColorWriteChannels mask for an MRT slot.
         *
         * Classic stock effects emit COLOR0 only, so the rasterizer consumes slot zero; retaining
         * all four values keeps state application exact for multi-output effect paths.
         *
         * @param slot Render-target slot in the inclusive range zero through three.
         * @return The raw ColorWriteChannels mask applied to the selected slot.
         */
        [[nodiscard]] int GetColorWriteMask(int slot = 0) const
        {
            return colorWriteMasks_[static_cast<std::size_t>(slot)];
        }
        /// REMED-GFX-077/GDI-073: the current BlendState.MultiSampleMask. Bit 0 controls a
        /// single-sample surface; when the optional four-sample colour plane is active, bits 0..3
        /// independently gate its 2x2 coverage samples. 0xFFFFFFFF = all samples.
        [[nodiscard]] unsigned int GetMultiSampleMask() const { return multiSampleMask_; }
        /// The raw CullMode ordinal from the most recent ApplyRasterizerState() call (SOFTWARE-81).
        /// Used by SoftwareSpriteBatchRenderer so its quads are culled the same way real FNA's
        /// SpriteBatch is: FNA's own SpriteBatch defaults its RasterizerState to
        /// CullCounterClockwise (not CullNone), and its quad winding is authored to survive that.
        [[nodiscard]] int GetCullMode() const { return cullMode_; }

        /// REMED-GFX-082: the raw FillMode ordinal from the most recent ApplyRasterizerState() call
        /// (0=Solid, 1=WireFrame). Used by SoftwareSpriteBatchRenderer so a wireframe RasterizerState
        /// supplied through SpriteBatch.Begin (REMED-GFX-081) outlines the sprite's quad triangles the
        /// same way it outlines 3D geometry.
        [[nodiscard]] int GetFillMode() const { return fillMode_; }

        /** @brief Returns whether triangles use independent multisample coverage locations. */
        [[nodiscard]] bool IsMultiSampleAntiAliasEnabled() const
        { return multiSampleAntiAlias_; }

        /// REMED-GFX-083: the RasterizerState.DepthBias / SlopeScaleDepthBias floats from the most recent
        /// ApplyRasterizerState() call (both previously discarded). Consumed by SoftwareSpriteBatchRenderer
        /// via owner_ so a bias supplied through SpriteBatch.Begin's RasterizerState (REMED-GFX-081)
        /// offsets the sprite quad's depth exactly as it offsets 3D geometry. See the members below for
        /// the unit convention.
        [[nodiscard]] float GetDepthBias() const { return depthBias_; }
        [[nodiscard]] float GetSlopeScaleDepthBias() const { return slopeScaleDepthBias_; }
        [[nodiscard]] bool IsStencilTestEnabled() const { return stencilTestEnabled_; }
        [[nodiscard]] int GetStencilCompareFunction() const { return stencilCompareFunction_; }
        [[nodiscard]] int GetStencilPassOperation() const { return stencilPassOperation_; }
        [[nodiscard]] int GetStencilFailOperation() const { return stencilFailOperation_; }
        [[nodiscard]] int GetStencilDepthFailOperation() const { return stencilDepthFailOperation_; }
        [[nodiscard]] int GetStencilReadMask() const { return stencilReadMask_; }
        [[nodiscard]] int GetStencilWriteMask() const { return stencilWriteMask_; }
        [[nodiscard]] int GetReferenceStencil() const { return referenceStencil_; }
        /** @brief Returns whether counter-clockwise faces use their separate stencil tuple. */
        [[nodiscard]] bool IsTwoSidedStencilEnabled() const { return twoSidedStencilMode_; }
        /** @brief Returns the raw counter-clockwise-face stencil comparison ordinal. */
        [[nodiscard]] int GetCounterClockwiseStencilCompareFunction() const
        { return counterClockwiseStencilCompareFunction_; }
        /** @brief Returns the raw counter-clockwise-face stencil-pass operation ordinal. */
        [[nodiscard]] int GetCounterClockwiseStencilPassOperation() const
        { return counterClockwiseStencilPassOperation_; }
        /** @brief Returns the raw counter-clockwise-face stencil-fail operation ordinal. */
        [[nodiscard]] int GetCounterClockwiseStencilFailOperation() const
        { return counterClockwiseStencilFailOperation_; }
        /** @brief Returns the raw counter-clockwise-face depth-fail operation ordinal. */
        [[nodiscard]] int GetCounterClockwiseStencilDepthFailOperation() const
        { return counterClockwiseStencilDepthFailOperation_; }

        /// REMED-GFX-073: the active GraphicsDevice.Viewport rectangle in pixels of the currently
        /// bound target. When no custom viewport has been set (SetViewport never called), the full
        /// current framebuffer is returned. SoftwareSpriteBatchRenderer places its viewport-local
        /// quads at (x,y) and clips them to (x,y,w,h), matching real XNA/FNA's viewport-local
        /// SpriteBatch semantics (the GPU renderers' GFX-072 contract).
        void GetActiveViewport(int& x, int& y, int& w, int& h) const;

        /// REMED-GFX-080: whether RasterizerState.ScissorTestEnable is currently on (the flag from
        /// the most recent ApplyRasterizerState() call). ScissorRectangle only clips rasterization
        /// when this is true; when false the stored rectangle has no effect. Consumed by the 2D and
        /// 3D raster paths (and by SoftwareSpriteBatchRenderer via owner_).
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

    protected:
        /**
         * @brief Reports a SpriteBatch quad's clipped inclusive candidate-pixel bounds.
         *
         * The Software SpriteBatch invokes this only after applying origin, rotation, its
         * transform matrix, viewport origin, viewport clipping, and optional scissor clipping.
         * Presentation renderers layered on this rasterizer can therefore track conservative
         * display damage without maintaining a second copy of the quad geometry.
         */
        virtual void OnSpriteRasterBounds(int /*minX*/, int /*minY*/,
                                          int /*maxX*/, int /*maxY*/) {}

        /// The real backbuffer, independently of any currently bound render target. Presentation
        /// renderers layered on this CPU rasterizer use it rather than accidentally displaying an
        /// off-screen target that happens to be active when Present() is called.
        [[nodiscard]] SoftwareFramebuffer& BackbufferFramebuffer() { return backbuffer_; }
        [[nodiscard]] const SoftwareFramebuffer& BackbufferFramebuffer() const { return backbuffer_; }

    private:
        friend class SoftwareSpriteBatchRenderer;
        friend class SoftwareOcclusionQueryRenderer;

        /** @brief Claims the renderer-wide active query slot for @p query. */
        [[nodiscard]] bool TryActivateOcclusionQuery(SoftwareOcclusionQueryRenderer* query);
        /** @brief Releases the renderer-wide active query slot when owned by @p query. */
        void ReleaseOcclusionQuery(SoftwareOcclusionQueryRenderer* query);

        void DrawIndexedPrimitivesInternal(
            const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
            const Matrix& world, const Matrix& view, const Matrix& projection,
            PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
            bool applyInstanceStreams);

        /// Submits one already-transformed SpriteBatch quad to the shared CPU triangle rasterizer.
        /// Keeping this narrow bridge private lets SoftwareSpriteBatchRenderer own the public draw
        /// geometry/transform path in its own translation unit while all CPU 2D and 3D triangles
        /// continue to share one fragment implementation.
        void RasterizeSpriteQuad(const ITextureRenderer& texture,
                                 const Vector2& c0, const Vector2& c1,
                                 const Vector2& c2, const Vector2& c3,
                                 float layerDepth, float r, float g, float b, float a,
                                 float u1, float v1, float u2, float v2,
                                 Effect* customEffect,
                                 const SoftwareSamplerState& spriteSampler);

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
        /** @brief Resolves every currently bound single or simultaneous render target. */
        void UnbindCurrentTargets();
        /**
         * @brief Invokes a colour-only operation on every active MRT attachment.
         *
         * @param operation Operation applied to each active colour framebuffer.
         */
        void ForEachActiveColorTarget(
            const std::function<void(SoftwareFramebuffer&)>& operation);

        SoftwareFramebuffer backbuffer_;
        SoftwareRenderTargetRenderer* currentRenderTarget_ = nullptr;
        SoftwareRenderTargetCubeRenderer* currentCubeRenderTarget_ = nullptr;
        std::array<SoftwareFramebuffer*, 4> currentMrtFramebuffers_{};
        std::array<SoftwareRenderTargetRenderer*, 4> currentMrt2DTargets_{};
        std::array<SoftwareRenderTargetCubeRenderer*, 4> currentMrtCubeTargets_{};
        std::array<int, 4> currentMrtCubeFaces_{};
        int currentMrtCount_ = 0;
        /// The one query whose Begin/End interval currently receives passing raster samples.
        SoftwareOcclusionQueryRenderer* activeOcclusionQuery_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        bool depthTestEnabled_ = true;
        /// REMED-GFX-030: DepthStencilState.DepthBufferWriteEnable. Defaults to true with the
        /// Software renderer's existing default test-enable and LessEqual function, matching
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
        /// REMED-GFX-077/SOFTWARE-120: raw XNA ColorWriteChannels for MRT slots 0..3 (bit0=R,
        /// bit1=G, bit2=B, bit3=A). Defaults to 15 (All), matching XNA's default BlendState.
        std::array<int, 4> colorWriteMasks_{15, 15, 15, 15};
        /// REMED-GFX-077/GDI-073: current BlendState.MultiSampleMask. Single-sample surfaces use
        /// bit 0; the optional four-sample colour plane uses bits 0..3. Defaults to 0xFFFFFFFF (all
        /// samples), matching XNA's default (-1).
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

        /** @brief XNA rasterizer multisample coverage toggle; enabled by default. */
        bool multiSampleAntiAlias_ = true;

        /// REMED-GFX-083: RasterizerState.DepthBias / SlopeScaleDepthBias, stored by ApplyRasterizerState()
        /// (its 4th/5th float args, previously discarded). Both default to 0 (XNA/FNA default), for which
        /// the rasterizer adds no depth offset and the output is byte-identical to pre-GFX-083. Expressed
        /// in XNA's public units: DepthBias is a normalized post-viewport depth offset, while
        /// SlopeScaleDepthBias multiplies the triangle's maximum screen-space depth slope. GPU
        /// backends convert the constant to their native depth-buffer units; the CPU rasterizer
        /// can add the normalized value directly in ComputeDepthBiasOffset (see the .cpp).
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;

        // 8-bit stencil state, captured by the GDI 2D masking path and stored here beside the
        // shared CPU depth state. The Software renderer keeps its established default of disabled.
        bool stencilTestEnabled_ = false;
        int stencilCompareFunction_ = 0; // CompareFunction::Always
        int stencilPassOperation_ = 0; // StencilOperation::Keep
        int stencilFailOperation_ = 0;
        int stencilDepthFailOperation_ = 0;
        int stencilReadMask_ = 0xFF;
        int stencilWriteMask_ = 0xFF;
        int referenceStencil_ = 0;
        /// SOFTWARE-121: XNA's counter-clockwise/back-face stencil tuple. Read/write masks and
        /// ReferenceStencil remain shared across faces, exactly as on the public state object.
        bool twoSidedStencilMode_ = false;
        int counterClockwiseStencilCompareFunction_ = 0;
        int counterClockwiseStencilPassOperation_ = 0;
        int counterClockwiseStencilFailOperation_ = 0;
        int counterClockwiseStencilDepthFailOperation_ = 0;

        /// REMED-GFX-073: current GraphicsDevice.Viewport, stored by SetViewport() and consumed by
        /// the SpriteBatch path (GetActiveViewport()). GraphicsDevice pushes this on every viewport
        /// change and resets it to the full target on each RenderTarget transition, so a single
        /// current-viewport field is always relative to the active target. `viewportSet_` starts
        /// false so that -- before GraphicsDevice sets any viewport, or for direct-renderer use --
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
        /// any scissor rectangle, or for direct-renderer use -- GetActiveScissor() falls back to the
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
