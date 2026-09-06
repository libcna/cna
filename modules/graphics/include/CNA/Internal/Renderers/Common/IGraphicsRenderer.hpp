#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/DisplayColorSpace.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/ICompiledEffectRuntime.hpp"

namespace Microsoft::Xna::Framework::Graphics { class Effect; }
namespace CNA::Platform
{
    class IPlatformGlContext;
    class IPlatformSurfacePresenter;
    class IPlatformVulkanSurface;
}

namespace CNA::Internal::Renderers
{
    struct RendererSurfaceInfo;
    using Color = Microsoft::Xna::Framework::Color;
    using Rectangle = Microsoft::Xna::Framework::Rectangle;
    using Vector2 = Microsoft::Xna::Framework::Vector2;
    using SpriteEffects = Microsoft::Xna::Framework::Graphics::SpriteEffects;
    using PrimitiveType = Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Matrix = Microsoft::Xna::Framework::Matrix;
    using Effect = Microsoft::Xna::Framework::Graphics::Effect;
    using ImageData = CNA::Internal::Graphics::ImageData;
    using SetDataOptions = Microsoft::Xna::Framework::Graphics::SetDataOptions;
    using VertexDeclaration = Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using VertexElement = Microsoft::Xna::Framework::Graphics::VertexElement;
    using VertexElementFormat = Microsoft::Xna::Framework::Graphics::VertexElementFormat;

    /**
     * @brief Renderer-neutral BlendState output-merger write state (REMED-GFX-077).
     *
     * Carries the two BlendState fields that are NOT expressible through the six blend
     * factor/function ordinals of ApplyBlendState: the four per-render-target colour write
     * masks (`BlendState.ColorWriteChannels` / `ColorWriteChannels1` / `ColorWriteChannels2` /
     * `ColorWriteChannels3`, MRT slots 0..3) and the coverage sample mask
     * (`BlendState.MultiSampleMask`). Kept as a small POD appended to ApplyBlendState so every
     * renderer's existing factor/function→native mapping is untouched; only this genuinely-new
     * output state is added. `colorWriteChannels[i]` holds the raw XNA `ColorWriteChannels` int
     * (bit0=R, bit1=G, bit2=B, bit3=A; 15 = All) — a bit layout identical to the native colour
     * masks of Vulkan/D3D9/D3D11/D3D12/WebGPU/SDL_GPU/bgfx, so the value is usable directly on
     * mask-capable renderers and via the ColorWriteHas* helpers on boolean renderers.
     */
    struct BlendWriteState
    {
        /** @brief Raw XNA ColorWriteChannels int per MRT slot 0..3 (bit0=R,1=G,2=B,3=A; 15 = All). */
        int          colorWriteChannels[4] = { 15, 15, 15, 15 };
        /** @brief XNA BlendState.MultiSampleMask coverage bitmask (bit i enables sample i; 0xFFFFFFFF = all). */
        unsigned int multiSampleMask       = 0xFFFFFFFFu;
    };

    /** @brief True if the XNA ColorWriteChannels int enables the red channel (bit 0). */
    [[nodiscard]] constexpr bool ColorWriteHasRed  (int cwc) { return (cwc & 1) != 0; }
    /** @brief True if the XNA ColorWriteChannels int enables the green channel (bit 1). */
    [[nodiscard]] constexpr bool ColorWriteHasGreen(int cwc) { return (cwc & 2) != 0; }
    /** @brief True if the XNA ColorWriteChannels int enables the blue channel (bit 2). */
    [[nodiscard]] constexpr bool ColorWriteHasBlue (int cwc) { return (cwc & 4) != 0; }
    /** @brief True if the XNA ColorWriteChannels int enables the alpha channel (bit 3). */
    [[nodiscard]] constexpr bool ColorWriteHasAlpha(int cwc) { return (cwc & 8) != 0; }

    /**
     * @brief Renderer handle for a vertex buffer.
     *
     * Owned by `Microsoft::Xna::Framework::Graphics::VertexBuffer`. The
     * concrete type is renderer-specific (e.g. an OpenGL VBO+VAO pair) and
     * intentionally hidden from the public XNA-like API.
     *
     * @note Status: IMPLEMENTED for VertexPositionColor (stride 16),
     *       VertexPositionTexture (20), VertexPositionColorTexture (24),
     *       and VertexPositionNormalTexture (32) on the EasyGL renderer.
     */
    class IVertexBufferRenderer
    {
    public:
        virtual ~IVertexBufferRenderer() = default;
        /**
         * @brief Uploads `vertex_count` `VertexPositionColor` vertices.
         *
         * @param data         Pointer to a contiguous array of vertices,
         *                     each of size `stride_in_bytes`.
         * @param vertex_count Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         */
        virtual void SetData(const void* data,
                             int vertex_count,
                             std::size_t stride_in_bytes) = 0;

        /**
         * @brief Uploads vertex data with an explicit streaming hint.
         *
         * Renderers may use @p options to select a more efficient upload path
         * (e.g. buffer orphaning for `Discard`, `glBufferSubData` for
         * `NoOverwrite`). The default implementation ignores @p options.
         *
         * @param data            Packed vertex data.
         * @param vertex_count    Number of vertices.
         * @param stride_in_bytes Size of one vertex in bytes.
         * @param options         Streaming hint.
         */
        virtual void SetDataWithOptions(const void* data,
                                        int vertex_count,
                                        std::size_t stride_in_bytes,
                                        SetDataOptions options)
        {
            SetData(data, vertex_count, stride_in_bytes);
        }

        /**
         * @brief Supplies the caller's own complete vertex declaration so a renderer can bind
         * genuinely custom vertex layouts generically instead of only the fixed set of
         * byte-strides its 3D draw path otherwise recognizes.
         *
         * Called immediately before `SetData()`/`SetDataWithOptions()`.  This is deliberately a
         * required renderer operation: each implementation must make an explicit decision to use
         * or ignore a declaration, so a newly added renderer cannot silently discard one.
         *
         * @param vertexDeclaration Full declaration, including stride and elements in declaration
         *                          order.
         */
        virtual void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) = 0;

        [[nodiscard]] virtual int GetVertexCount() const = 0;
    };

    /**
     * @brief Renderer handle for a 16- or 32-bit index buffer.
     */
    class IIndexBufferRenderer
    {
    public:
        virtual ~IIndexBufferRenderer() = default;
        virtual void SetData16(const void* data, int index_count) = 0;
        virtual void SetData32(const void* data, int index_count)
        {
            throw std::runtime_error("SetData32 not supported by this renderer");
        }

        /** @brief Uploads 16-bit index data with a streaming hint. Default ignores @p options. */
        virtual void SetData16WithOptions(const void* data, int index_count, SetDataOptions options)
        {
            SetData16(data, index_count);
        }

        /** @brief Uploads 32-bit index data with a streaming hint. Default ignores @p options. */
        virtual void SetData32WithOptions(const void* data, int index_count, SetDataOptions options)
        {
            SetData32(data, index_count);
        }

        [[nodiscard]] virtual int  GetIndexCount()   const = 0;
        [[nodiscard]] virtual bool IsThirtyTwoBit()  const { return false; }
    };

    /**
     * @brief Renderer handle for a GPU occlusion query.
     *
     * On OpenGL ES 3.0 (EasyGL), uses GL_ANY_SAMPLES_PASSED — so PixelCount()
     * returns 0 (no visible samples) or 1 (at least one visible sample), not an
     * exact pixel count. This matches FNA's behaviour on GLES3.
     */
    /// plans/plan_modern.md MOD-2163: a GPU-side elapsed-time query around a range of commands.
    ///
    /// Deliberately not an occlusion query with a different name. The two look alike and are not:
    /// an occlusion query counts samples and is answered by the rasteriser, while this measures
    /// wall-clock time *on the GPU* and needs `GL_EXT_disjoint_timer_query` on ES or GL 3.3 on the
    /// desktop. A renderer without it must report false rather than substitute a CPU clock, because
    /// a CPU number wearing a GPU name is worse than no number -- it is a measurement of when the
    /// driver returned, which is exactly the thing GPU timing exists to see past.
    class IGpuTimerRenderer
    {
    public:
        virtual ~IGpuTimerRenderer() = default;

        /// Starts the timed range. Only one may be open at a time on most implementations.
        virtual void Begin() = 0;

        /// Ends the timed range. The result becomes available asynchronously, usually a frame later.
        virtual void End() = 0;

        /// Whether the GPU has finished and the result can be read without stalling.
        [[nodiscard]] virtual bool IsResultAvailable() const = 0;

        /// The elapsed GPU time in nanoseconds, or zero when no result is available.
        [[nodiscard]] virtual std::uint64_t ElapsedNanoseconds() const = 0;
    };

    class IOcclusionQueryRenderer
    {
    public:
        virtual ~IOcclusionQueryRenderer() = default;
        virtual void Begin() = 0;
        virtual void End()   = 0;
        [[nodiscard]] virtual bool IsComplete() const = 0;
        [[nodiscard]] virtual int  PixelCount() const = 0;

        /// True when PixelCount() is a real tally of the fragments that passed, as XNA's own
        /// Direct3D 9 query is. False when the backend can only answer "any" or "none" -- which
        /// is all OpenGL ES 3.0 and WebGL 2 offer, their core query target being the boolean
        /// GL_ANY_SAMPLES_PASSED. A game that divides PixelCount() by an area to get a coverage
        /// ratio -- the lensflare idiom -- gets 1/area rather than a fraction there, so it needs
        /// to be able to ask. Backends that genuinely count leave this alone.
        [[nodiscard]] virtual bool PixelCountIsPreciseEXT() const noexcept { return true; }
    };

    class ITextureRenderer;

    /**
     * @brief A GPU buffer a compute shader reads and writes (an SSBO, in GL terms).
     *
     * plans/plan_modern.md `MOD-1501`. Deliberately byte-oriented and free of every XNA type: a storage
     * buffer holds whatever a shader says it holds, and the public `CNA::Graphics::StorageBuffer`
     * wrapper is where a typed view over it belongs.
     */
    class IStorageBufferRenderer
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IStorageBufferRenderer() = default;

        /**
         * @brief Uploads bytes into the buffer, starting at its beginning.
         *
         * @param data     The source bytes; must hold at least @p byteSize readable bytes.
         * @param byteSize How many bytes to upload; must not exceed @ref GetByteSize.
         */
        virtual void SetData(const void* data, std::size_t byteSize) = 0;

        /**
         * @brief Reads bytes back out of the buffer, starting at its beginning.
         *
         * @param out      Receives the bytes; must have room for at least @p byteSize.
         * @param byteSize How many bytes to read; must not exceed @ref GetByteSize.
         */
        virtual void GetData(void* out, std::size_t byteSize) const = 0;

        /** @brief Returns the buffer's size in bytes. */
        [[nodiscard]] virtual std::size_t GetByteSize() const = 0;
    };

    /**
     * @brief One compiled compute program, and the bindings a dispatch of it reads.
     *
     * plans/plan_modern.md `MOD-1500`. Mirrors `IEffectRenderer`'s shape: the renderer owns the compiled
     * object, the interface exposes only what a caller must be able to say about it, and no XNA
     * type appears in a signature -- an image binding arrives as an `ITextureRenderer`, and the
     * access mode as an ordinal (`CNA::GraphicsImageAccess`) rather than as an enumeration this
     * header would have to include.
     */
    class IComputeShaderRenderer
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IComputeShaderRenderer() = default;

        /**
         * @brief Compiles and links the program.
         *
         * @param computeSrc The compute-shader source, in whatever language the renderer takes.
         * @return True when the program linked; @ref GetCompileError says why when it did not.
         */
        virtual bool CompileProgram(const std::string& computeSrc) = 0;

        /** @brief Makes this the program a following dispatch runs. */
        virtual void Bind() = 0;

        /**
         * @brief Sets a scalar integer uniform.
         *
         * @param name  The uniform's name in the source.
         * @param value The value.
         */
        virtual void SetUniformInt(const char* /*name*/, int /*value*/) {}

        /**
         * @brief Sets a scalar float uniform.
         *
         * @param name  The uniform's name in the source.
         * @param value The value.
         */
        virtual void SetUniformFloat(const char* /*name*/, float /*value*/) {}

        /**
         * @brief Binds a storage buffer to one of the program's binding points.
         *
         * @param binding The binding index the shader declares.
         * @param buffer  The buffer, or null to unbind.
         */
        virtual void BindStorageBuffer(int /*binding*/, IStorageBufferRenderer* /*buffer*/) {}

        /**
         * @brief Binds a texture as a readable/writable image.
         *
         * @param unit       The image unit the shader declares.
         * @param texture    The texture, or null to unbind.
         * @param accessMode A `CNA::GraphicsImageAccess` ordinal.
         */
        virtual void BindImageTexture(int /*unit*/, ITextureRenderer* /*texture*/,
                                      int /*accessMode*/) {}

        /**
         * @brief Binds a texture to a sampler unit the program can sample.
         *
         * plans/plan_modern.md `MOD-1552`. Distinct from @ref BindImageTexture in what it needs of the
         * texture: sampling has no immutability requirement, so this works where an image binding
         * does not. The caller still sets the sampler uniform itself, with @ref SetUniformInt, for
         * the same reason `IEffectRenderer` does -- the unit is data, not a name this layer knows.
         *
         * @param unit    The texture unit to bind to.
         * @param texture The texture, or null to unbind.
         */
        virtual void BindTexture(int /*unit*/, ITextureRenderer* /*texture*/) {}

        /** @brief Returns whether a program is currently linked and usable. */
        [[nodiscard]] virtual bool IsValid() const = 0;

        /** @brief Returns the compiler/linker log from the last failed @ref CompileProgram. */
        [[nodiscard]] virtual std::string GetCompileError() const = 0;
    };

    /**
     * @brief Renderer interface for a cube map texture.
     *
     * SKIA-149: inherits `enable_shared_from_this` (matching `ITextureRenderer`) so a
     * `SkiaEffectRenderer` can hold a `weak_ptr` for cube-sampling lifetime tracking, identical to
     * the existing `ITextureRenderer`/`SetTexture(unit, Texture2D)` pattern. Requires
     * `TextureCube`/`RenderTargetCube` to own their renderer via `shared_ptr`, not `unique_ptr`.
     */
    class ITextureCubeRenderer : public std::enable_shared_from_this<ITextureCubeRenderer>
    {
    public:
        virtual ~ITextureCubeRenderer() = default;
        /**
         * @brief Uploads raw RGBA8 pixels into a sub-rectangle of a single cube face.
         *
         * REMED-GFX-135, the write-side counterpart of `GetData`'s contract below. Returns **true
         * only when the COMPLETE requested region was stored**, and false when this renderer stored
         * nothing. There is no third state: an implementation that cannot store the region, or that
         * fails part-way through, must return false rather than reporting a partial write as
         * success.
         *
         * There is deliberately no default body. `void` was the whole defect: it left an
         * implementation no way to say "I stored nothing", so `TextureCube::SetData`'s
         * `if (renderer_) renderer_->SetData(...)` returned normally after an upload that had been
         * validated, traced and discarded (Headless), dropped for an unstored mip level (Software),
         * or never attempted at all. The shared layer now raises `System::NotSupportedException` on
         * false, so the one thing a renderer can never do is accept data it does not keep.
         *
         * `data` holds the region as tightly packed RGBA8 rows, top row first, so its row pitch is
         * `w * 4`. The caller's memory is valid only for the duration of the call: an implementation
         * that hands it to an asynchronous native upload must copy or stage it before returning
         * true.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to write.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Source pixels, tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True if the whole region was stored; false if this renderer stored nothing.
         */
        [[nodiscard]] virtual bool SetData(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength) = 0;
        /**
         * @brief Uploads exact block-compressed bytes into a cube face.
         *
         * Coordinates remain in texel space; the source holds the complete padded block payload
         * for the requested region. The default refuses because most cube renderers currently
         * accept only RGBA8 pixels.
         *
         * @param face       Cube face index.
         * @param level      Mip level.
         * @param x          Left edge in texels, block aligned.
         * @param y          Top edge in texels, block aligned.
         * @param w          Width in texels, block aligned or reaching the mip edge.
         * @param h          Height in texels, block aligned or reaching the mip edge.
         * @param data       Exact compressed block payload.
         * @param dataLength Payload size in bytes.
         * @return True only when the complete payload was stored.
         */
        [[nodiscard]] virtual bool SetCompressedDataEXT(
            int face, int level, int x, int y, int w, int h,
            const void* data, int dataLength)
        {
            (void)face; (void)level; (void)x; (void)y; (void)w; (void)h;
            (void)data; (void)dataLength;
            return false;
        }
        /**
         * @brief Reads back raw RGBA8 pixels from a sub-rectangle of a single cube face.
         *
         * REMED-GFX-130, extending REMED-GFX-127's contract to this interface. Returns **true only
         * when the complete requested region was written into @p data**, and false when this
         * renderer performed no readback at all. There is no third state: an implementation that
         * fails part-way through, or that cannot complete the transfer, must return false rather
         * than reporting a partially written buffer as success.
         *
         * The default is `false` -- "this renderer has no cube-map readback" -- because a silent
         * no-op default was worse than useless here. `TextureCube::GetData` hands this method a
         * scratch buffer it zero-initialized itself and converts the result for the caller, so a
         * no-op default did not leave the caller's destination untouched: it fabricated a complete,
         * uniformly transparent-black cube face that passed both "did GetData write anything?" and
         * any expectation whose content happened to be transparent black. The shared layer now
         * converts only on `true` and raises `System::NotSupportedException` on `false`, so the one
         * thing an unimplemented renderer can never do is answer with content it never read.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; exactly w * h * 4.
         * @return True if the whole region was written; false if this renderer read nothing back.
         */
        [[nodiscard]] virtual bool GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
        {
            (void)face; (void)level; (void)x; (void)y; (void)w; (void)h;
            (void)data; (void)dataLength;
            return false;
        }
        /// Binds this cube map to the requested GL texture unit. No-op on non-GL renderers.
        virtual void BindGL(int unit = 0) const {}
        /// Shares a reference to the CPU pixel buffer owned by TextureCube::cpuPixels_[face] for
        /// one cube face's level 0. Mirrors ITextureRenderer::ShareCpuPixels()'s own purpose
        /// exactly (OpenGL-style renderer context-loss restoration) -- default no-op; only OPENGL1
        /// currently implements it.
        virtual void ShareCpuPixels(int /*face*/, std::shared_ptr<std::vector<uint8_t>> /*pixels*/) {}
        /// SKIA-149: face width/height in texels. `BindTextureCube(unit, ITextureCubeRenderer*)`
        /// receives only this raw renderer pointer (see `ShaderEffect::SetTexture(unit,
        /// TextureCube&)`), with no separate size parameter, so a renderer that needs its own size
        /// to build a sampling representation must be able to ask the renderer directly rather than
        /// requiring a shared, cross-renderer `BindTextureCube` signature change. Defaults to 0
        /// ("unknown/unsupported"), harmless for every renderer that does not implement sampling.
        [[nodiscard]] virtual int GetSizeEXT() const noexcept { return 0; }
    };

    /**
     * @brief Renderer interface for a 3D (volume) texture.
     *
     * SKIA-149: inherits `enable_shared_from_this` (matching `ITextureRenderer`) so a
     * `SkiaEffectRenderer` can hold a `weak_ptr` for volume-sampling lifetime tracking, identical to
     * the existing `ITextureRenderer`/`SetTexture(unit, Texture2D)` pattern. Requires `Texture3D` to
     * own its renderer via `shared_ptr`, not `unique_ptr`.
     */
    class ITexture3DRenderer : public std::enable_shared_from_this<ITexture3DRenderer>
    {
    public:
        virtual ~ITexture3DRenderer() = default;
        /**
         * @brief Uploads raw RGBA8 voxels into a sub-volume of the given mip level.
         *
         * REMED-GFX-135. Identical contract to `ITextureCubeRenderer::SetData` -- see its
         * documentation for why there is no default body. `data` holds the requested box slice by
         * slice (front to back), each slice as tightly packed RGBA8 rows with the top row first, so
         * the row pitch is `w * 4` and the slice pitch is `w * h * 4`.
         *
         * @param level      Mip level to write.
         * @param x          Left edge of the requested box, in voxels.
         * @param y          Top edge of the requested box, in voxels.
         * @param z          Front edge of the requested box, in voxels.
         * @param w          Width of the requested box, in voxels.
         * @param h          Height of the requested box, in voxels.
         * @param depth      Depth of the requested box, in voxels.
         * @param data       Source voxels, tightly packed RGBA8.
         * @param dataLength Size of @p data in bytes; at least w * h * depth * 4.
         * @return True if the whole box was stored; false if this renderer stored nothing.
         */
        [[nodiscard]] virtual bool SetData(int level, int x, int y, int z,
                                           int w, int h, int depth,
                                           const void* data, int dataLength) = 0;
        /**
         * @brief Reads back raw RGBA8 voxels from a sub-volume of the given mip level.
         *
         * REMED-GFX-130. Identical contract to `ITextureCubeRenderer::GetData` above -- see its
         * documentation for why the default is `false` rather than a silent no-op. `data` receives
         * the requested box slice by slice (front to back), each slice as tightly packed RGBA8 rows
         * with the top row first, so the row pitch is `w * 4` and the slice pitch is `w * h * 4`.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the requested box, in voxels.
         * @param y          Top edge of the requested box, in voxels.
         * @param z          Front edge of the requested box, in voxels.
         * @param w          Width of the requested box, in voxels.
         * @param h          Height of the requested box, in voxels.
         * @param depth      Depth of the requested box, in voxels.
         * @param data       Destination for the tightly packed RGBA8 box.
         * @param dataLength Size of @p data in bytes; exactly w * h * depth * 4.
         * @return True if the whole box was written; false if this renderer read nothing back.
         */
        [[nodiscard]] virtual bool GetData(int level, int x, int y, int z,
                                           int w, int h, int depth,
                                           void* data, int dataLength) const
        {
            (void)level; (void)x; (void)y; (void)z; (void)w; (void)h; (void)depth;
            (void)data; (void)dataLength;
            return false;
        }
        /// Binds this volume texture to the requested GL texture unit. No-op on non-GL renderers.
        virtual void BindGL(int unit = 0) const {}
        /// SKIA-149: width/height/depth in voxels, mirroring `ITextureCubeRenderer::GetSizeEXT`'s
        /// rationale exactly -- `BindTexture3D` receives only this raw renderer pointer. Defaults to
        /// all zero ("unknown/unsupported"), harmless for every renderer that does not implement
        /// sampling.
        virtual void GetDimensionsEXT(int& width, int& height, int& depth) const noexcept
        {
            width = height = depth = 0;
        }
    };

    /**
     * Renderer texture handle. Shared lifetime identity lets bounded consumers retain weak
     * bindings without keeping disposed public Texture2D resources alive or storing raw pointers.
     */
    class ITextureRenderer : public std::enable_shared_from_this<ITextureRenderer>
    {
    public:
        virtual ~ITextureRenderer() = default;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        /// Replaces full level-0 texture pixels in-place. stride = row bytes (width * 4 for RGBA).
        virtual void UpdatePixels(const uint8_t* rgba, int stride) {}
        /// Uploads a specific mip level. levelW/levelH are the dimensions at that level.
        virtual void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) {}
        /**
         * Reports whether the renderer owns deterministic readable bytes for a mip level even when
         * Texture2D has no caller-authored CPU shadow for it. The default is false: allocated GPU
         * mip storage is not necessarily initialized or readable. Renderers returning true must
         * complete GetData for every valid rectangle of that level without fabricating bytes.
         */
        [[nodiscard]] virtual bool HasDefinedMipLevel(int /*level*/) const noexcept { return false; }
        /**
         * The raw `SurfaceFormat` ordinal this texture's storage was created with.
         *
         * A renderer that samples one- and two-channel formats through GL's own expansion rule
         * needs this to restore Direct3D 9's: D3D9 hands a shader `(R, 1, 1, 1)` for a
         * single-channel format and `(R, G, 1, 1)` for a two-channel one, where GL yields
         * `(R, 0, 0, 1)` and `(R, G, 0, 1)`. The default is `SurfaceFormat::Color` (0), which
         * expands identically under both, so a renderer that has no such rule needs no override.
         */
        [[nodiscard]] virtual int GetSurfaceFormatEXT() const noexcept { return 0; }
        /// Binds the underlying GL texture handle to the requested unit (no-op on non-GL renderers).
        virtual void BindGL(int unit = 0) const {}
        /// Shares a reference to the CPU pixel buffer owned by Texture2D::cpuPixels_.
        /// The renderer stores this reference for OpenGL context-loss restoration instead
        /// of keeping its own duplicate copy of the pixel data.
        virtual void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> /*pixels*/) {}
        /**
         * @brief Reads back raw RGBA8 pixels from a sub-rectangle of the given mip level.
         *
         * REMED-GFX-127. Returns **true only when the complete requested region was written into
         * @p data**, and false when this renderer performed no readback at all. There is no third
         * state: an implementation that fails part-way through, or that cannot complete the
         * transfer, must return false rather than reporting a partially written buffer as success.
         *
         * The default is `false` — "this renderer has no render-target/texture readback" — because a
         * silent no-op default was worse than useless here. `Texture2D::GetData` hands this method a
         * scratch buffer it zero-initialized itself and converts the result for the caller, so a
         * no-op default did not leave the caller's destination untouched: it fabricated a complete,
         * uniformly transparent-black frame that passed both "did GetData write anything?" and any
         * expectation whose content happened to be transparent black. The shared layer now converts
         * only on `true` and raises `System::NotSupportedException` on `false`, so the one thing an
         * unimplemented renderer can never do is answer with content it never read.
         *
         * `Texture2D::GetData` only calls this when its own CPU-side pixel shadow is unavailable
         * (i.e. for a RenderTarget2D, whose content comes from GPU rendering rather than SetData())
         * -- plain, SetData()-populated textures never reach this path.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in pixels.
         * @param y          Top edge of the requested region, in pixels.
         * @param w          Width of the requested region, in pixels.
         * @param h          Height of the requested region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was written; false if this renderer read nothing back.
         */
        [[nodiscard]] virtual bool GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
        {
            (void)level; (void)x; (void)y; (void)w; (void)h; (void)data; (void)dataLength;
            return false;
        }
    };

    /// Renderer handle for a 2D render target (off-screen FBO on EasyGL).
    class IRenderTargetRenderer : public ITextureRenderer
    {
    public:
        /// Bind the FBO so subsequent draws go to this render target.
        virtual void BindAsRenderTarget() = 0;
        /// Unbind and restore the default framebuffer (back buffer).
        virtual void UnbindAsRenderTarget() = 0;
        /// Returns the native GL color texture handle; returns 0 on non-GL renderers.
        [[nodiscard]] virtual unsigned int GetColorGLHandle() const { return 0; }
        /// Returns the actual (device-clamped) multisample count this target was created
        /// with; 0 if none/not supported by this renderer. Matches FNA's semantics where
        /// RenderTarget2D.MultiSampleCount reflects the real clamped value, not the raw
        /// constructor request (FNA3D_GetMaxMultiSampleCount).
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /**
         * @brief Returns the depth/stencil format actually backing this target.
         *
         * The default preserves the requested ordinal because most render-target renderers create
         * exactly that attachment. A renderer that normalizes or rejects attachment types can
         * override this so RenderTarget2D.DepthStencilFormat never reports storage that does not
         * exist. The integer convention avoids coupling this renderer interface to the XNA enum.
         *
         * @param requestedDepthStencilFormat Requested DepthFormat ordinal.
         * @return Applied DepthFormat ordinal.
         */
        [[nodiscard]] virtual int GetAppliedDepthStencilFormatEXT(
            int requestedDepthStencilFormat) const
        {
            return requestedDepthStencilFormat;
        }
        /// Returns whether this specific target instance actually has a real depth
        /// buffer backing it, as opposed to merely being requested via DepthFormat at
        /// construction time. Most renderers honor whatever DepthFormat was requested, so the
        /// default mirrors that (via @p depthFormatWasRequested, computed by the caller from
        /// RenderTarget2D::getDepthStencilFormatProperty() != DepthFormat::None). The native 2D renderer's
        /// 2D-only render targets never allocate real depth-buffer storage regardless of what
        /// format was requested, and overrides this to always return false (Task 708).
        [[nodiscard]] virtual bool HasRealDepthBuffer(bool depthFormatWasRequested) const { return depthFormatWasRequested; }
        /// Returns whether this target has a real stencil plane. The caller passes true only for
        /// Depth24Stencil8. Most renderers allocate depth and stencil together, so the compatibility
        /// default delegates to HasRealDepthBuffer(); a renderer with standalone stencil storage
        /// (GDI's CPU 2D extension) overrides this independently.
        [[nodiscard]] virtual bool HasRealStencilBuffer(bool stencilFormatWasRequested) const
        {
            return HasRealDepthBuffer(stencilFormatWasRequested);
        }
    };

    /// Renderer handle for a cube-map render target.
    /// Each face can be activated independently for rendering.
    /// Inherits ITextureCubeRenderer so RenderTargetCube can share a single GPU image
    /// with its TextureCube base class (same pattern as IRenderTargetRenderer : ITextureRenderer).
    class IRenderTargetCubeRenderer : public ITextureCubeRenderer
    {
    public:
        virtual ~IRenderTargetCubeRenderer() = default;
        /// Returns the width/height of each cube face in pixels.
        [[nodiscard]] virtual int GetSize() const = 0;
        /// Activates face @p face (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z) as the draw target.
        virtual void BindAsRenderTargetFace(int face) = 0;
        /// Unbind and restore the default framebuffer.
        virtual void UnbindAsRenderTarget() = 0;
        /// Returns the underlying GL texture handle so the cube map can be sampled.
        [[nodiscard]] virtual unsigned int GetGLHandle() const { return 0; }
        /// See IRenderTargetRenderer::GetMultiSampleCount.
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /**
         * @brief Cube equivalent of IRenderTargetRenderer::GetAppliedDepthStencilFormatEXT.
         *
         * plans/plan_vulkan.md `VULKAN-215`. Its 2D twin has had this since renderers began
         * substituting depth formats; the cube did not, so `RenderTargetCube.DepthStencilFormat`
         * could only ever echo the request. The identity default keeps every renderer that does
         * not substitute exactly as it was.
         *
         * @param requestedDepthStencilFormat Requested DepthFormat ordinal.
         * @return Applied DepthFormat ordinal.
         */
        [[nodiscard]] virtual int GetAppliedDepthStencilFormatEXT(
            int requestedDepthStencilFormat) const
        {
            return requestedDepthStencilFormat;
        }
        /// Cube equivalent of IRenderTargetRenderer::HasRealDepthBuffer.
        [[nodiscard]] virtual bool HasRealDepthBuffer(bool depthFormatWasRequested) const
        {
            return depthFormatWasRequested;
        }
        /// Cube equivalent of IRenderTargetRenderer::HasRealStencilBuffer.
        [[nodiscard]] virtual bool HasRealStencilBuffer(bool stencilFormatWasRequested) const
        {
            return HasRealDepthBuffer(stencilFormatWasRequested);
        }

        /**
         * @brief Reports that this renderer cannot upload CPU pixels into a rendered cube face.
         *
         * REMED-GFX-135. This body used to be `{}` -- an empty override that made
         * `RenderTargetCube::SetData` (inherited from `TextureCube`) return normally on every
         * renderer while storing nothing, which is exactly the accept-and-discard this finding
         * removes, reached through inheritance instead of a null renderer. `false` makes the shared
         * layer raise `System::NotSupportedException` instead.
         *
         * EasyGL overrides this with a real upload into the shared GL cube texture; the other
         * render-target cube renderers inherit this refusal, matching `GetData`'s own default
         * immediately below.
         *
         * @return Always false.
         */
        [[nodiscard]] bool SetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/, int /*w*/,
                                   int /*h*/, const void* /*data*/, int /*dataLength*/) override
        {
            return false;
        }

        // ITextureCubeRenderer::GetData is deliberately NOT re-declared here: a render-target cube
        // inherits the same `return false` default, which means "this renderer cannot read a
        // rendered cube face back to the CPU" and makes `TextureCube::GetData` raise
        // System::NotSupportedException (REMED-GFX-130).
        //
        // REMED-GFX-134 implemented it on every renderer that owns a rendered cube resource:
        // EasyGL, Vulkan, Bgfx, D3D9, D3D11 and D3D12 joined SdlGpu and WebGPU, each reusing the
        // mechanism its plain-TextureCube sibling already uses in the same file plus that
        // renderer's own rendered-face specifics -- REMED-GFX-067's `originBottomLeft` row
        // normalization on GL and bgfx, the MSAA resolve, and the deferred-draw flush a
        // still-bound or not-yet-presented target needs (REMED-GFX-074/075). The public row order
        // is the one `RenderTarget2D::GetData` already established: top row first.
        //
        // Headless keeps the inherited refusal because it rasterizes nothing, and the renderers
        // that create no cube render target at all (Software, native 2D, ASCII, Canvas, DIRECTX3, GDI)
        // never reach this class -- `GraphicsDevice::SetRenderTargets` refuses to bind one and
        // `TextureCube::GetData` refuses a null renderer one step earlier. Every remaining boundary
        // (a multisampled or mipped cube target on bgfx, a mip level D3D9 never allocated, WebGPU's
        // mipMap=true refusal) is likewise a `false`, never invented content.
    };

    /**
     * @brief Renderer-neutral description of one normalized render-target attachment.
     *
     * REMED-GFX-096 replaces the former plural handoff (`IRenderTargetRenderer*[]`), which could
     * express only RenderTarget2D and therefore discarded both RenderTargetCube type and face.
     * This value keeps one slot's resource kind, concrete renderer, selected subresource, extent,
     * and applied sample count together so slot alignment cannot be lost through parallel arrays.
     * `arraySlice` is retained for CNA's public binding shape; current CNA render targets expose
     * no texture arrays, so GraphicsDevice accepts only slice 0.
     */
    class RenderTargetBindingDescriptor
    {
    public:
        enum class Type
        {
            RenderTarget2D,
            RenderTargetCubeFace,
        };

        static RenderTargetBindingDescriptor ForRenderTarget2D(
            IRenderTargetRenderer* target, int arraySlice, int width, int height,
            int appliedMultiSampleCount)
        {
            return RenderTargetBindingDescriptor(
                Type::RenderTarget2D, target, nullptr, arraySlice, 0,
                width, height, appliedMultiSampleCount);
        }

        static RenderTargetBindingDescriptor ForRenderTargetCubeFace(
            IRenderTargetCubeRenderer* target, int face, int size,
            int appliedMultiSampleCount)
        {
            return RenderTargetBindingDescriptor(
                Type::RenderTargetCubeFace, nullptr, target, 0, face,
                size, size, appliedMultiSampleCount);
        }

        [[nodiscard]] Type GetType() const { return type_; }
        [[nodiscard]] bool IsRenderTarget2D() const
        {
            return type_ == Type::RenderTarget2D;
        }
        [[nodiscard]] bool IsRenderTargetCubeFace() const
        {
            return type_ == Type::RenderTargetCubeFace;
        }
        [[nodiscard]] IRenderTargetRenderer* GetRenderTarget2D() const
        {
            return renderTarget2D_;
        }
        [[nodiscard]] IRenderTargetCubeRenderer* GetRenderTargetCube() const
        {
            return renderTargetCube_;
        }
        [[nodiscard]] int GetArraySlice() const { return arraySlice_; }
        [[nodiscard]] int GetCubeFace() const { return cubeFace_; }
        [[nodiscard]] int GetWidth() const { return width_; }
        [[nodiscard]] int GetHeight() const { return height_; }
        [[nodiscard]] int GetAppliedMultiSampleCount() const
        {
            return appliedMultiSampleCount_;
        }

        [[nodiscard]] bool IsSameSubresource(
            const RenderTargetBindingDescriptor& other) const
        {
            if (type_ != other.type_) return false;
            if (IsRenderTarget2D())
                return renderTarget2D_ == other.renderTarget2D_
                    && arraySlice_ == other.arraySlice_;
            return renderTargetCube_ == other.renderTargetCube_
                && cubeFace_ == other.cubeFace_;
        }

    private:
        RenderTargetBindingDescriptor(
            Type type, IRenderTargetRenderer* renderTarget2D,
            IRenderTargetCubeRenderer* renderTargetCube,
            int arraySlice, int cubeFace, int width, int height,
            int appliedMultiSampleCount)
            : type_(type)
            , renderTarget2D_(renderTarget2D)
            , renderTargetCube_(renderTargetCube)
            , arraySlice_(arraySlice)
            , cubeFace_(cubeFace)
            , width_(width)
            , height_(height)
            , appliedMultiSampleCount_(appliedMultiSampleCount)
        {
        }

        Type type_;
        IRenderTargetRenderer* renderTarget2D_;
        IRenderTargetCubeRenderer* renderTargetCube_;
        int arraySlice_;
        int cubeFace_;
        int width_;
        int height_;
        int appliedMultiSampleCount_;
    };

    /// Renderer handle for a compiled shader program (vertex + fragment).
    /// Created via IGraphicsRenderer::CreateEffectRenderer().
    /**
     * @brief The shading dialect a renderer's custom `ShaderEffect` sources must be written in.
     * CNAEXT.
     *
     * A `ShaderEffect` has always been renderer-specific source text -- the framework hands the
     * string to the renderer and the renderer's own compiler decides. What was missing was any
     * SUPPORTED way for an application to ask which dialect it should supply, so the only way to
     * know was to infer it from the build's renderer identity. That is wrong twice over: in a
     * multi-renderer build the identity is not the active renderer, and a renderer that is itself
     * an abstraction over several native APIs (IGL, LLGL, Diligent) does not have one answer per
     * build at all -- IGL's is chosen per process by `CNA_IGL_BACKEND`.
     *
     * `Unknown` is the honest default and what every renderer that has not declared one answers.
     * It does not mean "no shaders"; it means this renderer has not stated a dialect, and an
     * application should not guess.
     */
    enum class ShaderDialectEXT : int
    {
        /** @brief Not declared by this renderer. */
        Unknown,
        /** @brief Desktop OpenGL GLSL (`#version 3xx core` / `4xx core`). */
        GlslDesktop,
        /** @brief OpenGL ES / WebGL GLSL (`#version 100` / `300 es`). */
        GlslEs,
        /** @brief GLSL compiled to SPIR-V: explicit `location`/`set`/`binding` are mandatory. */
        GlslVulkan,
        /** @brief Direct3D High Level Shader Language. */
        Hlsl,
        /** @brief Metal Shading Language. */
        Msl,
        /** @brief WebGPU Shading Language. */
        Wgsl
    };

    class IEffectRenderer
    {
    public:
        virtual ~IEffectRenderer() = default;
        /// Compiles the program from GLSL/HLSL/SPIR-V sources.
        /// Returns true on success; false if compilation fails (see GetCompileError()).
        virtual bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) = 0;
        /// Binds the program for subsequent draw calls.
        virtual void Bind() = 0;
        /// Unbinds the program (restores default shader).
        virtual void Unbind() = 0;
        /// Returns true if the program has been compiled successfully.
        [[nodiscard]] virtual bool IsValid() const = 0;
        /// Returns the last compilation error string, or empty if no error.
        [[nodiscard]] virtual std::string GetCompileError() const = 0;
        /**
         * @brief Declares the std140 uniform block this effect's parameters live in. CNAEXT.
         *
         * Needed only where loose (non-block) uniforms do not exist -- which is every SPIR-V
         * target, including IGL's Vulkan backend. There a `SetUniformFloat("tint", ...)` has
         * nowhere to go: the shader's parameters are members of a block, and reaching them means
         * knowing each one's byte offset.
         *
         * That mapping is DECLARED rather than discovered, and that is a finding rather than a
         * preference: IGL `v1.1.1` returns an empty reflection on Vulkan
         * (`vulkan::RenderPipelineState` constructs a default `RenderPipelineReflection` and its
         * `getIndexByName` is `IGL_DEBUG_ASSERT_NOT_IMPLEMENTED`), so there is no name-to-offset
         * information to be had from the API. An application already has to supply a separate
         * Vulkan shader source (see @ref ShaderDialectEXT); declaring the block it wrote is a
         * smaller, explicit step than having CNA parse that source to guess at it.
         *
         * Ignored by a renderer whose uniforms are loose, which is why it is a no-op by default:
         * the same application code then runs unchanged on both.
         *
         * @param blockSizeBytes Size of the whole block, std140-padded.
         * @param names          Member names, `count` of them; must outlive this call only.
         * @param offsets        Each member's byte offset from the start of the block.
         * @param count          Number of members.
         */
        virtual void DeclareUniformBlockEXT(int blockSizeBytes, const char* const* names,
                                            const int* offsets, int count) {}

        /// Sets a float uniform by name.
        virtual void SetUniformFloat(const char* name, float value) {}
        /// Sets an int uniform by name.
        virtual void SetUniformInt(const char* name, int value) {}
        /// Sets a vec2 uniform by name.
        virtual void SetUniformVec2(const char* name, float x, float y) {}
        /// Sets a vec3 uniform by name.
        virtual void SetUniformVec3(const char* name, float x, float y, float z) {}
        /// Sets a vec4 uniform by name.
        virtual void SetUniformVec4(const char* name, float x, float y, float z, float w) {}
        /// Sets a column-major 4×4 matrix uniform by name.
        virtual void SetUniformMat4(const char* name, const float* matrix) {}
        /// Sets a float array uniform by name. `count` is the number of scalar elements.
        virtual void SetUniformFloatArray(const char* name, const float* values, int count) {}
        /// Sets a vec2 array uniform by name. `count` is the number of vec2 elements
        /// (`values` holds `count * 2` floats).
        virtual void SetUniformVec2Array(const char* name, const float* values, int count) {}

        /// plans/plan_modern.md MOD-217 (reopened): uploads @p count vec3 values. A vec3 array cannot be
        /// filled through SetUniformFloatArray -- GL rejects the type mismatch and leaves the
        /// uniform at zero, silently, which is how an SSAO kernel of 64 vec3s turns into 64 samples
        /// at the origin and an image with no occlusion in it at all.
        virtual void SetUniformVec3Array(const char* name, const float* values, int count) {}

        /// plans/plan_modern.md MOD-810: uploads @p count column-major 4x4 matrices, for a shader that
        /// declares `mat4 name[N]` -- a skinning palette, most of the time. Separate from
        /// SetUniformMat4 for the same reason the array forms above are separate from their scalar
        /// ones: the single-matrix call uploads exactly one matrix whatever the uniform's declared
        /// size, so filling a palette with it leaves every bone past the first at its default.
        virtual void SetUniformMat4Array(const char* name, const float* matrices, int count) {}
        /// Binds a texture to the given sampler unit (0-based) for subsequent draw calls.
        /// Unit 0 is normally driven by the caller (e.g. SpriteBatch); this is for additional
        /// units a custom shader samples directly (e.g. a second blend-source texture).
        virtual void BindTexture(int unit, ITextureRenderer* texture) {}
        /// Task 1081: binds a cube texture to the given sampler unit (0-based), for a custom
        /// shader that declares a `samplerCube` uniform (e.g. a reflection map). Separate from
        /// `BindTexture()` since `ITextureCubeRenderer` is its own interface, not a subtype of
        /// `ITextureRenderer` -- GL itself allows a 2D and a cube texture bound to the same unit
        /// simultaneously, since they occupy distinct binding targets; the shader's own sampler
        /// type (`sampler2D` vs `samplerCube`) determines which one is actually sampled.
        virtual void BindTextureCube(int unit, ITextureCubeRenderer* texture) {}
        /// plans/plan_graphics.md Task 863: binds a volume texture to the given sampler unit (0-based),
        /// for a custom shader that declares a `sampler3D` uniform. Same reasoning as
        /// `BindTextureCube()` above -- `ITexture3DRenderer` is its own interface, not a subtype of
        /// `ITextureRenderer`, and GL allows a 2D/cube/3D texture bound to the same unit
        /// simultaneously since each occupies a distinct binding target; the shader's own sampler
        /// type (`sampler2D`/`samplerCube`/`sampler3D`) determines which one is actually sampled.
        virtual void BindTexture3D(int unit, ITexture3DRenderer* texture) {}
    };

    class ISpriteBatchRenderer
    {
    public:
        virtual ~ISpriteBatchRenderer() = default;
        virtual void Begin() = 0;
        virtual void End() = 0;
        /// Sets the transform matrix applied on top of the 2D ortho projection.
        /// Must be called before the first Draw of each Begin/End block.
        virtual void SetTransformMatrix(const Matrix& m) {}
        /// Sets a custom Effect to use for sprite rendering instead of the built-in sprite shader.
        /// Pass nullptr to restore the built-in shader. Must be called before Begin().
        virtual void SetCustomEffect(Effect* effect) {}
        /// Sets the texture filter mode applied to each Draw call.
        /// Passes the raw TextureFilter int value; 0=Linear, 1=Point/Nearest, others map to nearest.
        virtual void SetSamplerFilter(int /*textureFilter*/) {}
        /**
         * @brief Sets the texture address (wrap/clamp/mirror) mode applied to each Draw call.
         *
         * Default: no-op (renderer keeps whatever wrap mode the texture was created with, i.e.
         * Clamp on EasyGL).
         *
         * @param addressU Raw TextureAddressMode int value for U (0=Wrap, 1=Clamp, 2=Mirror).
         * @param addressV Raw TextureAddressMode int value for V (0=Wrap, 1=Clamp, 2=Mirror).
         */
        virtual void SetSamplerAddressMode(int /*addressU*/, int /*addressV*/) {}
        /**
         * @brief CNAEXT. Tells the renderer whether the batch SpriteBatch::Begin() just started is
         *        SpriteSortMode::Immediate.
         *
         * Called once per Begin(), before Begin() itself, with the sort mode actually requested for
         * that batch -- SpriteBatch.cpp's own shared layer already forwards each Immediate Draw()
         * straight to the renderer the instant it is called (bypassing its sprite queue entirely), so
         * a renderer that internally batches/defers its own JS/GPU work across Draw() calls (unlike
         * one that is already synchronous per Draw(), which needs no signal at all) can use this to
         * flush per-draw instead, so that device state changed BETWEEN two Draw() calls in the same
         * Begin/End block is honestly reflected per sprite rather than only as of whichever state was
         * current when the whole batch was eventually flushed. Default: no-op, preserving every
         * existing renderer's current (already-established) behavior unless it opts in.
         *
         * @param immediate True when SpriteSortMode::Immediate is active for this batch.
         */
        virtual void SetImmediateMode(bool /*immediate*/) {}
        virtual void Draw(const ITextureRenderer& texture, float x, float y) = 0;
        virtual void Draw(const ITextureRenderer& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color) = 0;
        virtual void Draw(const ITextureRenderer& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth) = 0;

        /**
         * @brief Draws one sprite whose destination keeps its sub-pixel position and size.
         *
         * XNA 4.0 and FNA carry a sprite's destination through the whole batch as floats -- FNA's
         * `SpriteBatch.PushSprite` takes `float destinationX/Y/W/H` and writes them straight into
         * the quad -- so a sprite drawn at a fractional position lands between pixels and its
         * edges are filtered by the active SamplerState (LinearClamp by default). Only this
         * overload can reproduce that; the integer-`Rectangle` overload above quantises the
         * destination to whole pixels before the renderer ever sees it.
         *
         * The default implementation truncates and forwards to that integer overload, which is
         * exactly the behaviour every renderer had before this overload existed. A renderer
         * becomes sub-pixel accurate by overriding this method; one that does not is unchanged.
         *
         * @param texture             Texture to sample.
         * @param destinationX        Destination left edge, in pixels, unrounded.
         * @param destinationY        Destination top edge, in pixels, unrounded.
         * @param destinationWidth    Destination width, in pixels, unrounded.
         * @param destinationHeight   Destination height, in pixels, unrounded.
         * @param sourceRectangle     Source region of @p texture, in texels.
         * @param color               Tint colour.
         * @param rotation            Rotation about @p origin, in radians.
         * @param origin              Rotation/scale origin, in source-texel space.
         * @param effects             Horizontal/vertical flip flags.
         * @param layerDepth          Sort depth.
         */
        virtual void Draw(const ITextureRenderer& texture,
                          float destinationX,
                          float destinationY,
                          float destinationWidth,
                          float destinationHeight,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth)
        {
            Draw(texture,
                 Rectangle(static_cast<int>(destinationX), static_cast<int>(destinationY),
                           static_cast<int>(destinationWidth), static_cast<int>(destinationHeight)),
                 sourceRectangle, color, rotation, origin, effects, layerDepth);
        }

        /**
         * SKIA-157: draws a triangle-list 2D mesh through @p effect's own bound custom shader
         * (SKIA-144-156's bounded SkVertices/SkSL mesh ABI) -- an entirely different draw
         * primitive from every `Draw()` overload above, which always submits exactly one
         * quad through the built-in or `cnaTexture0`-shaped sprite shader. Composes with the
         * active `SetTransformMatrix()` the same way ordinary sprite draws do; @p colors/@p uvs
         * may be null if @p effect's compiled program declares no vertex-colour combine / no
         * texture children respectively. Default: throws, since only a renderer with a real mesh
         * ABI (Skia) can implement this -- every other renderer's `ISpriteBatchRenderer` correctly
         * keeps rejecting it rather than silently drawing nothing or falling back to sprite mode.
         */
        virtual void DrawMeshEXT(
            Effect& /*effect*/,
            const Vector2* /*positions*/, const Color* /*colors*/, const Vector2* /*uvs*/,
            int /*vertexCount*/, const std::uint16_t* /*indices*/, int /*indexCount*/)
        {
            throw std::runtime_error(
                "This renderer does not support DrawMeshEXT (SKIA-144-157's bounded SkVertices "
                "mesh ABI is Skia-specific).");
        }
    };

    /**
     * @brief REMED-GFX-201: one bound vertex stream, captured by value for the duration of a draw.
     *
     * XNA's `GraphicsDevice.SetVertexBuffers` binds up to 16 `VertexBufferBinding`s, and a
     * `VertexDeclaration` may split a single vertex's elements across several of them. FNA hands
     * its driver exactly this per-slot tuple (`FNA3D_VertexBufferBinding`: buffer, declaration,
     * stride, element offset, instance frequency) for *every* draw route -- ordinary and
     * instanced alike -- and each driver converts `vertexOffset * stride` with **that stream's own
     * stride**, binds it at **its own input slot**, and lets the native draw's start-vertex
     * (`glDrawArrays`'s `first`, `vkCmdDraw`'s `firstVertex`, `Draw`'s `StartVertexLocation`,
     * `BaseVertexLocation`) advance every stream by that same element count, each again by its own
     * stride. This struct is CNA's equivalent, sized and shaped so an ordinary draw needs no heap
     * allocation.
     *
     * A renderer must read these fields, never the public binding state: `GraphicsDevice`'s
     * `currentVertexBuffers_` is mutable between a deferred enqueue and its replay.
     */
    struct GpuVertexStreamBinding
    {
        /// Public binding slot -- the stream's index in the `SetVertexBuffers` array. Also the
        /// native input slot on every API that has one (D3D11/D3D12 `InputSlot`, Vulkan
        /// `binding`, WebGPU vertex-buffer index, bgfx stream index, SDL_GPU `slot`).
        int slot = 0;

        /// Renderer resource for this stream. Never null for `slot < vertexStreamCount`. Only
        /// valid for the duration of the `Draw*PrimitivesEx` call that carries it -- a deferred
        /// renderer must copy the concrete handle, exactly as it already does for `instanceVb`.
        const IVertexBufferRenderer* buffer = nullptr;

        /// This stream's own `VertexDeclaration` stride, in bytes. Never stream 0's.
        int strideInBytes = 0;

        /// Byte offset at which this stream's declaration begins inside the *combined* layout --
        /// the running sum of the strides of the per-vertex streams before it. CNA's renderers
        /// derive their native input elements from a byte stride (`InputElementsForStride` and
        /// the equivalent per-renderer tables), whose element offsets are combined-layout offsets;
        /// subtracting this value converts one to the stream-local `AlignedByteOffset` that FNA3D
        /// takes straight from the per-stream declaration.
        int combinedByteBase = 0;

        /// This stream's `VertexBufferBinding.VertexOffset`, in vertex elements, **less the shared
        /// base already folded into `vertexStart`/`baseVertex`** (see `GpuDrawParams::vertexStart`).
        /// Always >= 0, and always 0 for a single-stream draw, which is what keeps the
        /// single-stream native binding byte-identical to its pre-REMED-GFX-201 form.
        int vertexOffset = 0;

        /// `VertexBufferBinding.InstanceFrequency`; 0 means a per-vertex stream.
        int instanceFrequency = 0;

        /// Vertex elements the stream's buffer holds, for renderers that must bound a native range.
        int vertexCount = 0;
    };

    /// XNA 4.0 HiDef's `SetVertexBuffers` limit, and therefore the fixed capacity of a draw's
    /// stream list. Matches `GraphicsDevice::kMaxVertexBufferBindings`.
    inline constexpr int kMaxVertexStreams = 16;

    /**
     * @brief Per-draw effect parameters forwarded from the XNA effect layer
     *        to the graphics renderer.
     *
     * Populated via Effect::FillGpuDrawParams() before each draw call so the
     * renderer can select and configure the appropriate shader variant.
     */
    struct GpuDrawParams
    {
        const ITextureRenderer*     texture0 = nullptr;      ///< Texture unit 0 (diffuse), or null
        const ITextureRenderer*     texture1 = nullptr;      ///< Texture unit 1 (DualTextureEffect second layer), or null
        const ITextureCubeRenderer* envMap   = nullptr;      ///< Cube map for EnvironmentMapEffect, or null
        float diffuseColor[4]  = {1,1,1,1};                ///< RGBA 0..1
        /// CNA's fixed ColorMatrixEffect, used only by the shared CPU SpriteBatch path. The
        /// matrix is row-major and transforms the post-texture/post-tint source before blending:
        /// out[row] = dot(colorMatrix[row], sourceRGBA) + colorOffset[row], clamped to [0,1].
        /// Other renderers deliberately leave this false rather than pretending to execute it.
        bool cpu2DColorMatrixEnabled = false;
        float cpu2DColorMatrix[16] = {
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        float cpu2DColorOffset[4] = {0,0,0,0};
        float ambientColor[3]  = {0,0,0};                   ///< RGB 0..1 (BasicEffect path)
        float light0Dir[3]     = {0,-1,0};                  ///< World-space, pre-normalized
        float light0Diffuse[3] = {1,1,1};                   ///< RGB 0..1
        /// BasicEffect: DirectionalLight1's direction/diffuse, zeroed when disabled. World-space, pre-normalized.
        float light1Dir[3]     = {0,-1,0};
        float light1Diffuse[3] = {0,0,0};
        /// BasicEffect: DirectionalLight2's direction/diffuse, zeroed when disabled. World-space, pre-normalized.
        float light2Dir[3]     = {0,-1,0};
        float light2Diffuse[3] = {0,0,0};
        float worldColMajor[16] = {                         ///< Column-major world matrix
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        /// Alpha-test parameters (x=refVal, y=tolerance, z=passWeight, w=failWeight).
        /// Shader evaluates: if ((y>0) ? (|a-x|<y) : (a<x)) ? z : w < 0 → discard.
        /// Default {0,0,1,1} = Always pass (never discard).
        float alphaTest[4]      = {0.0f, 0.0f, 1.0f, 1.0f};
        /// True when the active stock effect is AlphaTestEffect, including its `Always` and
        /// `Never` variants whose identity cannot be recovered from alphaTest.x/y alone.
        bool alphaTestEffect = false;
        /// EnvironmentMapEffect: emissive+ambient combined, RGB 0..1.
        /// BasicEffect (lit path only): raw EmissiveColor*Alpha, added after the ambient/light
        /// sum is multiplied by DiffuseColor (CNA folds ambient into that multiply instead of
        /// FNA's pre-baked "ambient+emissive" shader uniform — numerically equivalent net result,
        /// see BasicEffect::FillGpuDrawParams()).
        float emissiveColor[3]  = {0,0,0};
        /// EnvironmentMapEffect: specular tint from env map, RGB 0..1.
        float envMapSpecular[3] = {0,0,0};
        /// EnvironmentMapEffect: camera world-space position for reflection vector.
        /// BasicEffect (lit path only): camera world-space position for specular half-vector.
        float eyePositionWorld[3] = {0,0,0};
        /// plans/plan_modern.md MOD-821: shadow reception. `shadowMap` holds light-space distance (not a
        /// depth buffer -- CNA cannot sample a depth attachment; see CNA::Graphics::ShadowMap), and
        /// `lightViewProjColMajor` takes a world position into that map's space. Defaults mean "no
        /// shadows", and a renderer with no shadow-sampling variant accepts and ignores them, the
        /// same convention the PBR fields use.
        const ITextureRenderer* shadowMap = nullptr;
        float lightViewProjColMajor[16] = {
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        bool  shadowsEnabled = false;
        float shadowDepthBias = 0.0015f;
        /// plans/plan_modern.md MOD-840: PCF kernel radius in shadow-map texels. 0 is a single tap, 1 a
        /// 3x3 neighbourhood, 2 a 5x5 one. Expressed as a radius rather than a kernel size because
        /// that is what a shader loop bound needs, and clamped by the renderer rather than trusted.
        int   shadowPcfRadius = 1;
        /// plans/plan_modern.md MOD-908: cascaded shadows. Zero means "one map", and everything below is
        /// ignored -- which is what keeps every existing draw, and every renderer with no cascade
        /// shader, exactly as it was. When it is non-zero `shadowMap` is the cascade atlas rather
        /// than a single map, `cascadeMatricesColMajor` holds one world-to-atlas matrix per cascade
        /// (the atlas sub-rectangle already baked in) and `lightViewProjColMajor` is unused.
        int   cascadeCount = 0;
        float cascadeMatricesColMajor[4 * 16] = {};
        /// View-space depth at which each cascade stops being used, ascending.
        float cascadeSplits[4] = {0, 0, 0, 0};
        /// The view matrix's third column, so the shader can compute a fragment's view depth
        /// without being given the whole matrix a second time.
        float cascadeViewZRow[4] = {0, 0, -1, 0};
        /// Width of the cross-fade between neighbouring cascades, in view-depth units. Zero is a
        /// hard switch, which is visible as a line across the ground wherever the two cascades
        /// disagree about an edge.
        float cascadeBlendBand = 0.0f;
        /// Tints each cascade a different colour. A debugging aid, off by default.
        bool  cascadeDebugTint = false;
        /// plans/plan_modern.md MOD-1005: one punctual light and its shadow. 0 means none -- and every
        /// draw that has never heard of punctual lights leaves it there, which is what keeps this
        /// free. 1 is a point light (shadowed by `punctualShadowCube`), 2 a spot light (shadowed
        /// by `punctualShadowMap` through `punctualViewProjColMajor`). Both maps store *distance
        /// from the light over its range*, not projected depth; see CNA::Graphics::CubeShadowMap.
        int   punctualKind = 0;
        float punctualPosition[3]  = {0, 0, 0};
        float punctualDirection[3] = {0, -1, 0};
        float punctualDiffuse[3]   = {1, 1, 1};
        float punctualRange        = 20.0f;
        /// Cosine of the inner and outer cone half-angles, precomputed: the shader needs the
        /// cosines and computing them per fragment would be six transcendental calls per pixel.
        float punctualCosInner     = 1.0f;
        float punctualCosOuter     = 1.0f;
        float punctualShadowBias   = 0.004f;
        const ITextureCubeRenderer* punctualShadowCube = nullptr;
        const ITextureRenderer*     punctualShadowMap  = nullptr;
        float punctualViewProjColMajor[16] = {
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        /// BasicEffect: DirectionalLight0/1/2's SpecularColor, zeroed when that light is disabled
        /// (mirrors the light*Diffuse zeroing — FNA's DirectionalLight.Enabled setter zeroes both).
        float light0Specular[3] = {0,0,0};
        float light1Specular[3] = {0,0,0};
        float light2Specular[3] = {0,0,0};
        /// BasicEffect: material SpecularColor, applied once to the summed per-light specular
        /// contribution (not per-light, unlike DiffuseColor which multiplies the light-sum then
        /// is applied once too — both are single material-level multiplies, matching FNA).
        float specularColor[3] = {1,1,1};
        /// BasicEffect: Blinn-Phong specular exponent.
        float specularPower = 16.0f;
        /// EnvironmentMapEffect: blend amount for the env map contribution [0,1].
        float envMapAmount = 0.0f;
        /// EnvironmentMapEffect: when true, the env-map blend factor is Fresnel-weighted
        /// (`pow(max(1-|dot(eye,normal)|,0),fresnelFactor)*envMapAmount`) instead of flat `envMapAmount`.
        bool fresnelEnabled = false;
        /// EnvironmentMapEffect: exponent for the Fresnel edge-weighting term above.
        float fresnelFactor = 1.0f;
        /// BasicEffect/SkinnedEffect: real XNA `PreferPerPixelLighting` value (plans/plan_dx9.md
        /// Divergence 1 / D9-81 item 1). When true, XNA selects a per-pixel-lit shader
        /// (`VSBasicPixelLighting*`/`PSBasicPixelLighting*`); when false (XNA's own default),
        /// it selects a per-vertex-lit shader instead. Renderers that generate both lighting
        /// families honour this (D3D9, D3D11, D3D12, WebGPU, Vulkan, bgfx, EasyGL, OpenGL4,
        /// Magnum, Diligent); fixed-function renderers evaluate lighting per vertex by construction; a
        /// renderer with neither renders per-pixel regardless of its value -- a known, tracked
        /// divergence from XNA's default, not fixed by adding this field alone. Only meaningful
        /// when `lightingEnabled` is true.
        bool preferPerPixelLighting = false;
        /// EnvironmentMapEffect: real XNA `specularEnabled` value (plans/plan_dx9.md Divergence 1 /
        /// D9-81 item 4) -- true when `SpecularColor` is non-black, selecting a distinct
        /// compiled shader in real XNA rather than a uniform toggle. `envMapSpecular` above
        /// already carries the specular color itself; this field additionally carries whether
        /// XNA would have compiled the specular-enabled shader variant, since a specular color
        /// that is legitimately black-but-enabled is not losslessly recoverable from the RGB
        /// value alone (unlike BasicEffect's `oneLight`/AlphaTestEffect's `isEqNe`, D9-81's
        /// other two findings). No renderer currently reads this field.
        bool specularEnabled = false;
        /// SkinnedEffect: column-major mat4 per bone (72 × 16 floats), zero-initialised.
        float boneTransforms[72 * 16] = {};
        /// SkinnedEffect: number of valid entries in boneTransforms (0 = none).
        int boneCount = 0;
        /// SkinnedEffect: number of bone weight/index pairs to evaluate per vertex (1, 2, or 4,
        /// matching FNA's real Skin(vin, boneCount) shader behavior of only summing the first
        /// N weight/index pairs -- Task 895).
        int weightsPerVertex = 4;
        /// BasicEffect fog: when true the fog uniforms below are used.
        bool  fogEnabled      = false;
        /// BasicEffect fog: RGB blend colour.
        float fogColor[3]     = {0, 0, 0};
        /// REMED-GFX-010: FNA stock-effect fog vector (EffectHelpers.SetFogVector). Dotting this
        /// with the object-space (or, for SkinnedEffect, the post-skin) vertex position `float4(p,1)`
        /// yields FNA's `fogFactor = saturate(dot(pos, fogVector))` — a true *view-space* Z fog term,
        /// because the vector bakes in the third column of `World*View`
        /// (`{M13,M23,M33} * scale`, `w = (M43 + fogStart) * scale`, `scale = 1/(fogStart-fogEnd)`).
        /// Renderers compute `keep = 1 - saturate(dot(pos, fogVector))` (their "keep" convention).
        /// All-zero when fog is disabled (dot→0→keep=1, a true no-op) and `{0,0,0,1}` for the
        /// degenerate `fogStart==fogEnd` case (dot→1→keep=0, fully fogged), matching FNA exactly.
        /// Populated by every fog-capable stock effect's FillGpuDrawParams(). This is the
        /// authoritative representation for every fog-capable stock-effect renderer.
        float fogVector[4]    = {0, 0, 0, 0};
        bool textureEnabled      = false;
        bool vertexColorEnabled  = true;
        bool lightingEnabled     = false;
        /// When true the renderer selects a two-sampler DualTexture shader variant.
        bool dualTexture         = false;
        /// When true the renderer selects a cube-map env-mapping shader variant.
        bool envMapping          = false;
        /// When true the renderer selects the skinning shader variant.
        bool skinned             = false;
        /// When true the renderer selects the PbrEffect (metallic-roughness BRDF) shader variant
        /// (plans/plan_cnj.md CNB-58, Phase 13A).
        bool pbr                 = false;
        /// Number of instances to draw (1 = non-instanced).
        int instanceCount = 1;
        /// REMED-GFX-201/202: every active declared `VertexBufferBinding`, in public slot order,
        /// captured by value -- per-vertex and per-instance alike, on every draw route.
        /// `vertexStreams[0]` is always the stream `Draw*PrimitivesEx`'s own `vb` argument refers
        /// to, so a renderer that reads only `vb` still sees exactly what it saw before this field
        /// existed. REMED-GFX-233's one legacy empty-declaration buffer is the sole public-binding
        /// exception and deliberately leaves this array empty (see `vertexStreamCount`). Entries
        /// at or past `vertexStreamCount` are unset and must not be read.
        ///
        /// For declared layouts this is CNA's `FNA3D_VertexBufferBinding` array, and like FNA's
        /// own `PrepareVertexBindingArray` it is prepared identically for `DrawPrimitives`,
        /// `DrawIndexedPrimitives` and `DrawInstancedPrimitives`. An instance stream is simply an
        /// entry whose `instanceFrequency` is greater than zero; there is no second representation
        /// of "the instance buffer" anywhere.
        std::array<GpuVertexStreamBinding, kMaxVertexStreams> vertexStreams{};
        /// Active entries in `vertexStreams`. 0 on internal routes that bind no public buffer
        /// (SpriteBatch, `DrawUser*`) and on REMED-GFX-233's legacy ordinary single-buffer route
        /// whose intentionally empty VertexDeclaration has no stride to describe; that route uses
        /// the Draw*PrimitivesEx `vb` argument and its renderer upload stride. 1 for every declared
        /// single-stream draw. Nonzero declared streams, instanced submission, and multi-stream
        /// draws are never folded into this compatibility representation.
        int vertexStreamCount = 0;
        /// Sum of the per-vertex (`instanceFrequency == 0`) streams' strides -- the byte stride of
        /// the *combined* vertex the shader sees, and therefore the key a stride-dispatched
        /// renderer must select its input layout and shader variant with. Equals the single
        /// stream's own stride whenever `vertexStreamCount == 1`, so single-stream dispatch is
        /// unchanged. 0 when `vertexStreamCount == 0`.
        int combinedVertexStride = 0;
        /// First vertex index for non-indexed draws (maps to glDrawArrays `first` / vkCmdDraw `firstVertex`).
        /// REMED-GFX-201: on the ORDINARY routes this already includes the smallest per-vertex
        /// binding offset of the draw (`GraphicsDevice::FoldedVertexStreamOffset`), which is why
        /// every `vertexStreams[k].vertexOffset` is a non-negative remainder there and a
        /// single-stream draw carries its whole public offset here exactly as REMED-GFX-200
        /// established. The instanced route folds nothing (see `baseVertex`).
        int vertexStart = 0;
        /// First index in the IBO for indexed draws (maps to glDrawElements byte offset / vkCmdDrawIndexed `firstIndex`).
        int startIndex  = 0;
        /// Value added to each index before vertex fetch (maps to glDrawElementsBaseVertex / vkCmdDrawIndexed `vertexOffset`).
        /// REMED-GFX-201: on the ordinary routes, like `vertexStart` above, this already includes
        /// the draw's folded per-vertex binding offset, and it advances **every** per-vertex stream
        /// by that many of that stream's own elements.
        ///
        /// REMED-GFX-202: the INSTANCED route folds nothing into it -- every stream there carries
        /// its whole public `VertexOffset` in `vertexStreams[k].vertexOffset`, which is exactly
        /// FNA3D's own D3D11 driver (`offset = binding.vertexOffset * stride` per binding, with
        /// `BaseVertexLocation = baseVertex` passed separately to `DrawIndexedInstanced`). This
        /// value therefore advances only the PER-VERTEX streams: a per-instance slot is addressed
        /// by instance index, which a base-vertex term does not touch.
        int baseVertex  = 0;
        /// Lowest decoded index expected by the caller, relative to `baseVertex`.
        int minVertexIndex = 0;
        /// Size of the caller-declared decoded-index range beginning at `minVertexIndex`.
        int numVertices = 0;
        /// Task 1079: when non-null, a `ShaderEffect`-compiled custom program is currently bound
        /// (`Effect::GetEffectRendererPtr()`) and the renderer should bind/draw with it directly
        /// instead of selecting one of its own built-in stride-dispatched shaders. Null for every
        /// stock effect (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
        /// `SkinnedEffect`) and for no-effect draws. Renderers that don't implement this (non-EasyGL)
        /// safely ignore it, matching the established accepted-and-ignored pattern for other
        /// not-yet-renderer-supported `GpuDrawParams` fields.
        IEffectRenderer* customEffectRenderer = nullptr;
        /// Runtime for a compiled XNA/FNA Effect Framework pass. This is deliberately separate
        /// from customEffectRenderer: ShaderEffect is a source pair, while a compiled effect owns
        /// reflection, techniques, passes, samplers, and state assignments.
        ICompiledEffectRuntime* compiledEffectRuntime = nullptr;
        /// True whenever the active effect is ShaderEffect, even when this renderer returned no
        /// IEffectRenderer. Backends without custom shaders use this to refuse the draw instead of
        /// mistaking a null renderer for an ordinary fixed-function stock effect.
        bool customEffectRequested = false;
        /// plans/plan_cnj.md CNB-58 (Phase 13A): PbrEffect's normal map (tangent-space, RGB), or null.
        /// When null the surface normal from the vertex stream is used unperturbed.
        const ITextureRenderer* pbrNormalMap = nullptr;
        /// PbrEffect: metallic-roughness map, glTF's own packing convention (G=roughness,
        /// B=metallic; R/A unused), or null (Metallic/RoughnessFactor alone are then the
        /// per-material constant values).
        const ITextureRenderer* pbrMetallicRoughnessMap = nullptr;
        /// PbrEffect: emissive map (RGB), or null (EmissiveFactor alone is then constant).
        const ITextureRenderer* pbrEmissiveMap = nullptr;
        /// PbrEffect: occlusion map (R channel, 1=fully lit .. 0=fully occluded), or null
        /// (no occlusion darkening applied).
        const ITextureRenderer* pbrOcclusionMap = nullptr;
        /// KHR_materials_specular scalar strength map; only alpha is meaningful and is linear.
        const ITextureRenderer* pbrSpecularMap = nullptr;
        /// KHR_materials_specular colour map; RGB is sRGB-encoded by default.
        const ITextureRenderer* pbrSpecularColorMap = nullptr;
        /// plans/plan_gltf.md GLTF-182/GLTF-183: bit i selects packed TextureCoordinate1 for PBR
        /// texture slot i (base colour, normal, metallic-roughness, emissive, occlusion,
        /// specular strength, specular colour); a clear
        /// bit selects TextureCoordinate0. The importer maps arbitrary glTF source TEXCOORD_n
        /// indices onto these two collision-free renderer channels before filling the effect.
        std::uint32_t pbrTextureCoordinateSetMask = 0;
        /// plans/plan_gltf.md GLTF-184: two affine rows per core PBR map, in base-colour, normal,
        /// metallic-roughness, emissive, occlusion order. For row vectors `r0` and `r1`, the
        /// transformed coordinate is `{dot(float3(uv,1),r0.xyz),
        /// dot(float3(uv,1),r1.xyz)}`. The fourth component is deterministic padding for native
        /// constant-buffer alignment. Identity defaults preserve old callers and old content.
        float pbrTextureTransformRows[10][4] = {
            {1,0,0,0}, {0,1,0,0}, {1,0,0,0}, {0,1,0,0},
            {1,0,0,0}, {0,1,0,0}, {1,0,0,0}, {0,1,0,0},
            {1,0,0,0}, {0,1,0,0}};
        /// GLTF-344: the same representation for specular strength then specular colour. Kept in
        /// a separate additive block so existing native renderer structs that copy the ten core
        /// rows by `sizeof(pbrTextureTransformRows)` retain their exact size and cannot overflow.
        float pbrSpecularTextureTransformRows[4][4] = {
            {1,0,0,0}, {0,1,0,0}, {1,0,0,0}, {0,1,0,0}};
        /// PbrEffect: metallic factor [0,1], multiplied with pbrMetallicRoughnessMap's B channel
        /// when bound (or used alone as a constant when it isn't).
        float pbrMetallicFactor = 1.0f;
        /// PbrEffect: roughness factor [0,1], multiplied with pbrMetallicRoughnessMap's G channel
        /// when bound (or used alone as a constant when it isn't).
        float pbrRoughnessFactor = 1.0f;
        /// plans/plan_gltf.md GLTF-343/GLTF-344: dielectric normal-incidence reflectance after applying
        /// KHR_materials_ior and the factor-only part of KHR_materials_specular. Core glTF's
        /// default is 0.04 in every channel. Kept separate from the metallic F0, which remains the
        /// material's base colour, and from F90 below because specularFactor can reduce grazing
        /// reflectance independently. Every PBR-capable renderer consumes both endpoints -- as of
        /// plans/plan_gltf.md GLTF-476, which found that claim had been written while `IGL` consumed
        /// neither and hard-coded 0.04 in its shader. It is now machine-checked by
        /// `GltfRendererPbrFallbackPolicy.EveryPbrRendererConsumesEveryUniversalPbrDrawParameter`
        /// rather than asserted here.
        float pbrDielectricF0[3] = {0.04f, 0.04f, 0.04f};
        /// Dielectric grazing reflectance after KHR_materials_specular's scalar strength (default 1).
        float pbrDielectricF90 = 1.0f;
        /// GLTF-344: IOR F0 times specularColorFactor before clamping/weighting. A colour-map
        /// sample must multiply this value before the specification's per-channel clamp.
        float pbrDielectricF0Unclamped[3] = {0.04f, 0.04f, 0.04f};
        /// GLTF-344: authored scalar strength, retained separately for texture-driven evaluation.
        float pbrSpecularFactor = 1.0f;
        /// plans/plan_gltf.md GLTF-224: glTF `normalTexture.scale`. Scales the sampled tangent-space
        /// normal's x and y before the tangent basis is applied -- 0 flattens the map to the
        /// geometric normal, 1 is the map as authored, and glTF puts no upper bound on it. Only
        /// meaningful when `pbrNormalMap` is bound.
        float pbrNormalScale = 1.0f;
        /// plans/plan_gltf.md GLTF-225: glTF `occlusionTexture.strength`, applied as the specification's
        /// own `1 + strength * (sampled - 1)`. At 0 the result is 1 -- no occlusion at all,
        /// whatever the map holds -- and at 1 it is the map unchanged. Only meaningful when
        /// `pbrOcclusionMap` is bound.
        float pbrOcclusionStrength = 1.0f;
        /// plans/plan_gltf.md GLTF-210: the base-colour texture's samples are sRGB-ENCODED and must be
        /// decoded to linear before lighting (glTF §3.9.2). The `DiffuseColor` FACTOR is already
        /// linear and must NOT be decoded -- the two multiply, and decoding both would apply the
        /// transfer twice to one of them. Renderers that do not implement colour management ignore
        /// this, the established accepted-and-ignored pattern for a field they do not yet honour.
        bool pbrBaseColorTextureIsSrgb = true;
        /// plans/plan_gltf.md GLTF-210: the emissive texture is sRGB-encoded, on the same terms as
        /// `pbrBaseColorTextureIsSrgb`. `emissiveColor` (the factor, possibly scaled above 1 by
        /// KHR_materials_emissive_strength) is linear and is not decoded.
        bool pbrEmissiveTextureIsSrgb = true;
        /// KHR_materials_specular colour-map RGB follows glTF's sRGB encoding rule.
        bool pbrSpecularColorTextureIsSrgb = true;
        /// plans/plan_gltf.md GLTF-212: encode the fragment's RGB from linear back to sRGB before it
        /// reaches the framebuffer. Alpha is never encoded -- glTF §3.9.4 makes it coverage, not
        /// colour. Normal, occlusion and metallic-roughness maps carry no flag at all because
        /// §3.9.2 declares them linear unconditionally; a flag would imply a choice that does not
        /// exist.
        bool pbrEncodeOutputToSrgb = true;
        /// plans/plan_modern.md MOD-1224: image-based lighting, the three split-sum products at once.
        /// `iblEnabled` false -- which is every draw that has never heard of IBL -- leaves the
        /// flat `ambientColor` term exactly as it was, and that is deliberate: the two are the
        /// same term computed two ways, so a renderer applies one or the other, never their sum
        /// (MOD-1226). Renderers with no IBL shader variant accept and ignore all six fields, the
        /// same convention the PBR and shadow groups above use.
        bool iblEnabled = false;
        /// Diffuse irradiance, indexed by the surface normal.
        const ITextureCubeRenderer* iblIrradiance = nullptr;
        /// GGX-prefiltered specular, indexed by the reflection vector; roughness selects the mip.
        const ITextureCubeRenderer* iblPrefilteredSpecular = nullptr;
        /// The (N.V across, roughness down) scale/bias table. Row 0 is roughness 0.
        const ITextureRenderer* iblBrdfLut = nullptr;
        /// How many mips the prefiltered cube was generated with. The shader's mip for a given
        /// roughness is `roughness * (count - 1)`, which is CNA::Graphics::EnvironmentProcessor::
        /// mipForRoughness -- the same formula on both sides, by construction rather than by
        /// agreement.
        int   iblPrefilteredMipCount = 1;
        /// Multiplies the whole environment contribution. The products are 8-bit, so an
        /// environment brighter than 1.0 carries its brightness here instead of in its texels.
        float iblIntensity = 1.0f;
    };

    /**
     * @brief REMED-GFX-201: where one element of the combined vertex layout actually lives.
     *
     * CNA's renderers describe a vertex layout by its byte stride, so their element tables use
     * offsets measured inside the *combined* vertex. A multi-stream draw stores those same bytes
     * in several buffers, so each element must be re-slotted before it reaches a native input
     * descriptor.
     */
    struct GpuVertexStreamSlot
    {
        /// Index into `GpuDrawParams::vertexStreams` -- and the native input slot.
        int streamIndex = 0;
        /// The element's byte offset inside that stream's own vertex, i.e. what FNA3D reads
        /// straight from the per-stream `VertexElement.Offset`.
        int byteOffsetInStream = 0;
    };

    /**
     * @brief The byte stride a renderer must select its input layout and shader variant with.
     *
     * REMED-GFX-201: for a bound-buffer draw this is the sum of the per-vertex streams' strides,
     * which equals the one stream's own stride whenever a single buffer is bound. The internal
     * routes that stage their own temporary buffer -- `DrawUser*`, SpriteBatch,
     * `DrawColoredPrimitives` -- bind no public `VertexBufferBinding` at all and leave
     * `vertexStreamCount` at 0; they keep dispatching on @p fallbackStride, the stride of the one
     * buffer they built, so their behaviour is untouched by this feature. REMED-GFX-233's legacy
     * empty-declaration single-buffer route likewise leaves the count at 0 because only the
     * renderer resource knows its typed-upload stride; @p fallbackStride is authoritative there.
     *
     * @param params         The draw being dispatched.
     * @param fallbackStride The named `vb`'s own stride.
     */
    [[nodiscard]] inline std::size_t CombinedVertexStrideOr(
        const GpuDrawParams& params, std::size_t fallbackStride)
    {
        return params.vertexStreamCount > 0 && params.combinedVertexStride > 0
                   ? static_cast<std::size_t>(params.combinedVertexStride)
                   : fallbackStride;
    }

    /**
     * @brief Maps a combined-layout byte offset to the stream that holds it.
     *
     * @param params            The draw whose stream list is being bound.
     * @param combinedByteOffset Byte offset of the element inside the combined vertex.
     * @return The owning stream and the element's offset within it. Degenerates to
     *         `{0, combinedByteOffset}` for a single-stream draw, so a caller that routes every
     *         element through this helper keeps its single-stream native binding unchanged.
     */
    [[nodiscard]] inline GpuVertexStreamSlot MapCombinedOffsetToStream(
        const GpuDrawParams& params, int combinedByteOffset)
    {
        GpuVertexStreamSlot result{};
        for (int i = 0; i < params.vertexStreamCount; ++i)
        {
            const GpuVertexStreamBinding& stream = params.vertexStreams[i];
            if (stream.instanceFrequency != 0)
                continue;
            if (combinedByteOffset >= stream.combinedByteBase &&
                combinedByteOffset < stream.combinedByteBase + stream.strideInBytes)
            {
                result.streamIndex = i;
                result.byteOffsetInStream = combinedByteOffset - stream.combinedByteBase;
                return result;
            }
        }
        result.byteOffsetInStream = combinedByteOffset;
        return result;
    }

    /**
     * @brief How many bound streams advance per vertex (`instanceFrequency == 0`).
     */
    [[nodiscard]] inline int PerVertexStreamCount(const GpuDrawParams& params)
    {
        int count = 0;
        for (int i = 0; i < params.vertexStreamCount; ++i)
            if (params.vertexStreams[i].instanceFrequency == 0)
                ++count;
        return count;
    }

    /**
     * @brief How many bound streams advance per instance (`instanceFrequency > 0`).
     *
     * REMED-GFX-202: XNA places no limit on this -- `InstanceFrequency` is a property of each
     * binding, so any subset of the 16 slots may be per-instance, at any frequencies. Every CNA
     * renderer that implements instancing at all currently binds exactly one, so more than one is
     * gated by `GraphicsCapability::MultiStreamVertexInput` and rejected before submission rather
     * than silently reduced to the first.
     */
    [[nodiscard]] inline int InstanceStreamCount(const GpuDrawParams& params)
    {
        int count = 0;
        for (int i = 0; i < params.vertexStreamCount; ++i)
            if (params.vertexStreams[i].instanceFrequency > 0)
                ++count;
        return count;
    }

    /**
     * @brief The @p ordinal-th per-instance stream in public slot order, or null.
     *
     * @param params  The draw being dispatched.
     * @param ordinal Zero-based position among the per-instance streams only.
     */
    [[nodiscard]] inline const GpuVertexStreamBinding* NthInstanceStream(
        const GpuDrawParams& params, int ordinal)
    {
        int seen = 0;
        for (int i = 0; i < params.vertexStreamCount; ++i)
        {
            const GpuVertexStreamBinding& stream = params.vertexStreams[i];
            if (stream.instanceFrequency <= 0)
                continue;
            if (seen == ordinal)
                return &stream;
            ++seen;
        }
        return nullptr;
    }

    /**
     * @brief The lowest-slot per-instance stream, or null when this is not an instanced draw.
     *
     * REMED-GFX-202: the direct replacement for the removed `GpuDrawParams::instanceVb` /
     * `instanceVertexOffset` / `instanceFrequency` trio. `stream->vertexOffset` is the public
     * `VertexBufferBinding.VertexOffset` in vertex ELEMENTS (never bytes) and
     * `stream->instanceFrequency` is the step rate / divisor.
     */
    [[nodiscard]] inline const GpuVertexStreamBinding* FirstInstanceStream(
        const GpuDrawParams& params)
    {
        return NthInstanceStream(params, 0);
    }

    /**
     * @brief The @p ordinal-th per-vertex stream in public slot order, or null.
     *
     * @param params  The draw being dispatched.
     * @param ordinal Zero-based position among the per-vertex streams only.
     */
    [[nodiscard]] inline const GpuVertexStreamBinding* NthPerVertexStream(
        const GpuDrawParams& params, int ordinal)
    {
        int seen = 0;
        for (int i = 0; i < params.vertexStreamCount; ++i)
        {
            const GpuVertexStreamBinding& stream = params.vertexStreams[i];
            if (stream.instanceFrequency != 0)
                continue;
            if (seen == ordinal)
                return &stream;
            ++seen;
        }
        return nullptr;
    }

    /**
     * @brief The stream `Draw*PrimitivesEx`'s own `vb` argument names, or null on the internal
     *        routes that bind no public buffer at all.
     *
     * REMED-GFX-202: the direct replacement for the removed `GpuDrawParams::vertexBufferOffset` --
     * read `FirstPerVertexStream(params)->vertexOffset`.
     */
    [[nodiscard]] inline const GpuVertexStreamBinding* FirstPerVertexStream(
        const GpuDrawParams& params)
    {
        return NthPerVertexStream(params, 0);
    }

    /**
     * @brief Whether this draw needs more than the one stream `Draw*PrimitivesEx` names directly.
     *
     * The single-stream fast path every renderer already implements stays selected by this being
     * false, which it is for every draw CNA issued before REMED-GFX-201.
     */
    [[nodiscard]] inline bool HasMultipleVertexStreams(const GpuDrawParams& params)
    {
        return PerVertexStreamCount(params) > 1;
    }

    /**
     * @brief Whether this draw binds more per-instance streams than the single one every
     *        instancing renderer already handles.
     *
     * REMED-GFX-202: the instanced fast path stays selected by this being false, which it is for
     * every instanced draw CNA issued before this task.
     */
    [[nodiscard]] inline bool HasMultipleInstanceStreams(const GpuDrawParams& params)
    {
        return InstanceStreamCount(params) > 1;
    }

    /**
     * @brief REMED-GFX-202: fills the classic one-per-vertex + one-per-instance stream pair.
     *
     * `GraphicsDevice` builds `vertexStreams` from the public `VertexBufferBinding` array; this is
     * for the renderer harnesses and examples that drive `DrawInstancedPrimitivesEx` directly with a
     * hand-built `GpuDrawParams` and would otherwise each hand-roll the same two entries.
     *
     * @param params                 Draw parameters to populate.
     * @param perVertexBuffer        Slot 0's buffer -- the same object passed as `vb`.
     * @param instanceBuffer         Slot 1's buffer.
     * @param instanceFrequency      Slot 1's `InstanceFrequency`; must be > 0.
     * @param perVertexStrideInBytes Slot 0's declaration stride, or 0 when the renderer resolves it.
     * @param perVertexOffset        Slot 0's `VertexOffset`, in vertex elements.
     * @param instanceStrideInBytes  Slot 1's declaration stride, or 0 when the renderer resolves it.
     * @param instanceOffset         Slot 1's `VertexOffset`, in vertex elements.
     */
    inline void SetInstancedVertexStreamsEXT(
        GpuDrawParams& params,
        const IVertexBufferRenderer& perVertexBuffer,
        const IVertexBufferRenderer& instanceBuffer,
        int instanceFrequency = 1,
        int perVertexStrideInBytes = 0,
        int perVertexOffset = 0,
        int instanceStrideInBytes = 0,
        int instanceOffset = 0)
    {
        GpuVertexStreamBinding& geometry = params.vertexStreams[0];
        geometry = GpuVertexStreamBinding{};
        geometry.slot = 0;
        geometry.buffer = &perVertexBuffer;
        geometry.strideInBytes = perVertexStrideInBytes;
        geometry.vertexOffset = perVertexOffset;
        geometry.vertexCount = perVertexBuffer.GetVertexCount();

        GpuVertexStreamBinding& instances = params.vertexStreams[1];
        instances = GpuVertexStreamBinding{};
        instances.slot = 1;
        instances.buffer = &instanceBuffer;
        instances.strideInBytes = instanceStrideInBytes;
        instances.vertexOffset = instanceOffset;
        instances.instanceFrequency = instanceFrequency;
        instances.vertexCount = instanceBuffer.GetVertexCount();

        params.vertexStreamCount = 2;
        params.combinedVertexStride = perVertexStrideInBytes;
    }

    /**
     * @brief REMED-GFX-202: the deterministic rejection a renderer that binds exactly one stream of
     *        each input rate performs instead of silently rendering from a subset of the array.
     *
     * `GraphicsDevice` already rejects both shapes for a renderer that does not report
     * `GraphicsCapability::MultiStreamVertexInput`, so this never fires through the public API. It
     * exists because `Draw*PrimitivesEx` is a public interface method a harness may call with a
     * hand-built `GpuDrawParams`, and because a truncated binding array looks exactly like a
     * correct draw of the wrong data.
     *
     * @param params      The draw being dispatched.
     * @param rendererName Name used in the message, e.g. "Vulkan".
     */
    inline void RejectUnsupportedStreamCombination(
        const GpuDrawParams& params, const char* rendererName)
    {
        if (!HasMultipleVertexStreams(params) && !HasMultipleInstanceStreams(params))
            return;
        throw std::runtime_error(
            std::string(rendererName) +
            " cannot bind more than one VertexBufferBinding of the same input rate (" +
            std::to_string(PerVertexStreamCount(params)) + " per-vertex and " +
            std::to_string(InstanceStreamCount(params)) +
            " per-instance streams were bound). The binding list is never silently truncated.");
    }

    /**
     * @brief The byte offset at which stream @p streamIndex's first fetched record begins.
     *
     * This is FNA3D's `vertexStride * (vertexOffset + baseVertex)` with the start-vertex term
     * left to the native draw call, which applies it to every stream with that stream's own
     * stride. Pass the start-vertex term in @p startElement only on APIs whose draw call cannot
     * carry it.
     */
    [[nodiscard]] inline std::size_t VertexStreamByteOffset(
        const GpuVertexStreamBinding& stream, int startElement = 0)
    {
        return static_cast<std::size_t>(stream.vertexOffset + startElement) *
               static_cast<std::size_t>(stream.strideInBytes);
    }

    /**
     * @brief A renderer's answer about a surface format, or a deferral to the framework's own rule.
     *
     * plans/plan_runtimerenderer.md design decision 9. Renderer-specific format behaviour used to live as
     * #ifdef blocks inside the XNA layer, where the #else branch carried the renderer-agnostic
     * rule. Moving those behind a virtual has to preserve that structure exactly: a renderer either
     * has a real, renderer-specific answer, or it defers -- it must never be forced to restate the
     * framework's rule, because a wrong restatement silently narrows a public API.
     */
    enum class RendererFormatVerdict
    {
        /** @brief This renderer has no renderer-specific answer; the framework's own rule applies. */
        Defer,

        /** @brief This renderer genuinely supports the format. */
        Supported,

        /** @brief This renderer genuinely does not support the format. */
        Unsupported
    };

    /**
     * @brief Owns a renderer context on the calling thread for one bounded operation.
     *
     * Native GL renderers use this internal lease to serialize a complete content decode against
     * frame rendering while moving their context between threads. Other renderer families return
     * no lease because their APIs either support concurrent resource creation directly or own a
     * different synchronization boundary.
     */
    class IRendererThreadContextLease
    {
    public:
        /** @brief Releases the calling thread's renderer context ownership. */
        virtual ~IRendererThreadContextLease() = default;
    };

    /** @brief Selects how an outer renderer-thread context lease handles its own prior binding. */
    enum class RendererThreadContextLeaseRelease : int
    {
        /** @brief Restore the binding that was current before the bounded operation. */
        RestorePreviousBinding,
        /** @brief Release this renderer's own prior binding so another thread can acquire it. */
        ReleaseRendererBinding
    };

    class IGraphicsRenderer
    {
    public:
        virtual ~IGraphicsRenderer() = default;
        /**
         * @brief Acquires this renderer's context for a complete caller-owned operation.
         * @param release Selects whether this renderer's own prior binding is restored or released.
         * @return A lifetime token, or null when this renderer needs no explicit context lease.
         */
        [[nodiscard]] virtual std::unique_ptr<IRendererThreadContextLease>
            AcquireThreadContextLeaseEXT(
                RendererThreadContextLeaseRelease release =
                    RendererThreadContextLeaseRelease::RestorePreviousBinding)
        {
            (void)release;
            return nullptr;
        }
        virtual void Clear(float r, float g, float b, float a) = 0;
        virtual void Present() = 0;
        virtual void GetViewportSize(int& width, int& height) = 0;
        /// Refreshes the platform-owned presentation surface snapshot after a resize or density
        /// change. The default is inert for renderers whose native swap chain handles this itself.
        virtual void OnSurfaceChanged(const RendererSurfaceInfo& /*surface*/) {}
        /**
         * @brief Notifies retained presentation backends that a native client must be repainted.
         * @param window Affected stable window id, or zero for a process-wide invalidation.
         */
        virtual void OnSurfaceInvalidated(CNA::Platform::WindowId /*window*/) {}
        /// Returns the PHYSICAL viewport rectangle (window/framebuffer pixels)
        /// GraphicsDevice::UpdateViewportFromWindow() should apply as the default GL/GPU viewport
        /// after a window resize or presentation-mode change -- separate from GetViewportSize(),
        /// which returns the LOGICAL size exposed to game code via GraphicsDevice.Viewport.Width/
        /// Height (those must stay the game's own virtual resolution even when the physical
        /// rectangle differs, e.g. under CnaPresentationMode::Letterbox/Overscan).
        /// Default: (0, 0, GetViewportSize()) -- the physical rectangle equals the logical
        /// size at the window origin, matching every renderer's behavior before this method
        /// existed. Only a renderer implementing REAL Letterbox/Overscan/Stretch (a physical
        /// sub-rectangle that differs in size and/or is not at the window origin) needs to
        /// override this -- see OpenGL2Renderer's override for the reference
        /// implementation in this codebase.
        virtual void GetDefaultViewportRect(int& x, int& y, int& width, int& height)
        {
            x = 0;
            y = 0;
            GetViewportSize(width, height);
        }
        /// Updates the renderer logical presentation size at runtime.
        /// Called by GraphicsDevice::SetVirtualResolution() when
        /// GraphicsDeviceManager::ApplyChanges() propagates a new
        /// PreferredBackBufferWidth/Height from the game.
        virtual void SetVirtualResolution(int width, int height) = 0;
        /// Updates the renderer presentation/scaling mode at runtime.
        /// Called by GraphicsDevice when GraphicsDeviceManager::ApplyChanges() is used.
        virtual void SetPresentationMode(int mode) = 0;
        /// Updates the swap interval at runtime (0=immediate, 1=VSync, 2=half-rate).
        /// A renderer that cannot change VSync after construction ignores this; one that can is
        /// expected to apply it, not merely record it. The Vulkan renderer used to be named here
        /// as an example of the former and is no longer one -- it rebuilds its swapchain with the
        /// matching VkPresentModeKHR (plans/plan_vulkan.md VULKAN-332), which is the same
        /// mechanism it already ran on every resize.
        virtual void SetSwapInterval(int /*interval*/) {}
        /**
         * @brief The swap interval this renderer was last asked for, honoured by the driver or not.
         *
         * A setter with no getter is why `EasyGL_GraphicsDeviceManager_Vsync` could not tell "CNA
         * never forwarded the request" from "the driver declined it", and so skipped wherever vsync
         * is unavailable while defending nothing (REMED-GFX-243). Reporting the request separates
         * the two: it is CNA's own state, so it is checkable in a headless context.
         *
         * @return The interval last passed to SetSwapInterval, or -1 from a renderer that does not
         *         record it -- which is the honest answer for one that ignores the setter too.
         */
        CNAEXT [[nodiscard]] virtual int GetSwapIntervalEXT() const { return -1; }
        /// Task 902: reconfigures the backbuffer's MSAA sample count in place, called from
        /// GraphicsDevice::Reset() so GraphicsDeviceManager.PreferMultiSampling (and any other
        /// preference-driven MultiSampleCount change) actually reaches the renderer instead of
        /// being silently ignored after construction. Returns the actual, device-clamped sample
        /// count applied (0 = no MSAA). Default: unsupported -- renderers that cannot change this
        /// post-construction report back whatever GetMultiSampleCount() already is.
        virtual int ApplyMultiSampleCount(int /*requestedMultiSampleCount*/) { return GetMultiSampleCount(); }
        /// CNAEXT (plans/plan_dx9.md D9-30/D9-33, found empirically). Reconfigures the renderer's tracked
        /// back-buffer format/depth-stencil format/fullscreen state in place, called from
        /// GraphicsDevice::Reset() alongside SetVirtualResolution()/ApplyMultiSampleCount() above --
        /// same "actually reach the renderer instead of being silently ignored after construction"
        /// rationale as ApplyMultiSampleCount, for the GraphicsRendererCreateArgs::backBufferFormat/
        /// depthStencilFormat/isFullScreen fields specifically. Without this, a GraphicsDeviceManager
        /// that sets PreferredDepthStencilFormat AFTER the renderer's initial construction (the
        /// common case: Game lazily constructs a GraphicsDevice with default PresentationParameters
        /// before GraphicsDeviceManager.ApplyChanges() ever runs) would silently keep the renderer on
        /// its original construction-time format forever. Default: no-op -- every existing renderer
        /// (parity, not authenticity, goals) may continue to ignore this exactly as before it
        /// existed; only a renderer that honors real requested formats (D3D9) needs to act on it, and
        /// even then only needs to actually apply it on its own next natural resize/reset point
        /// (immediately reconstructing a device from inside this call is not required).
        virtual void UpdatePresentationFormatEXT(int /*backBufferFormat*/, int /*depthStencilFormat*/,
                                                  bool /*isFullScreen*/) {}
        /**
         * @brief Maps a requested backbuffer format ordinal to the renderer's applied format.
         *
         * Renderers that honor the request use the identity default. Fixed-format renderers override
         * this so GraphicsDevice can expose the real format after construction/reset without
         * coupling this interface to SurfaceFormat.
         */
        [[nodiscard]] virtual int GetAppliedBackBufferFormatEXT(int requestedFormat) const
        {
            return requestedFormat;
        }
        /**
         * @brief Maps a requested MSAA sample count to the count this renderer actually applied.
         *
         * plans/plan_runtimerenderer.md design decision 9. The identity default is deliberate and is
         * exactly what every renderer except GDI did when this was an #ifdef CNA_RENDERER_GDI block
         * in the XNA layer: PresentationParameters keeps echoing back what the game asked for.
         * Only a renderer that clamps the request at construction time (GDI, which supports one
         * real optional mode) needs to correct that, and it must not be generalized to "always
         * report GetMultiSampleCount()" -- most renderers leave that at its 0 default, so doing so
         * would report "no MSAA" for every renderer that in fact honoured the request.
         *
         * @param requestedMultiSampleCount The count the game asked for.
         * @return The count actually in effect.
         */
        [[nodiscard]] virtual int GetAppliedMultiSampleCountEXT(int requestedMultiSampleCount) const
        {
            return requestedMultiSampleCount;
        }

        /** @brief Depth/stencil counterpart of GetAppliedBackBufferFormatEXT(). */
        [[nodiscard]] virtual int GetAppliedDepthStencilFormatEXT(int requestedFormat) const
        {
            return requestedFormat;
        }
        /// Returns the backbuffer's actual (device-clamped) MSAA sample count; 0 if none/unsupported.
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }

        // --- GraphicsProfile ceilings (plans/plan_runtimerenderer.md design decision 9) -------------
        //
        // XNA's GraphicsProfile.Reach/HiDef carry real, enforced-at-creation-time ceilings. Only a
        // renderer with a genuine capability structure to consult (D3D9's D3DCAPS9) can answer them
        // honestly; the other 45 have no such structure, and this project refuses to substitute a
        // hardcoded table pretending to be a capability query. The defaults below are therefore
        // "no ceiling" -- exactly the behaviour every non-D3D9 renderer had when these lived as
        // #ifdef CNA_RENDERER_DIRECTX9 blocks inside the XNA layer.

        /**
         * @brief Largest texture edge length the given GraphicsProfile permits.
         *
         * A profile CEILING, not a hardware query: even where the device could allocate more, a
         * Reach-profile game is restricted to the profile's limit, which is what XNA's portability
         * guarantee means.
         *
         * @param graphicsProfile GraphicsProfile ordinal (Reach = 0, HiDef = 1).
         * @return The maximum edge length, or std::numeric_limits<int>::max() for no ceiling.
         */
        [[nodiscard]] virtual int GetMaxTextureSizeForProfileEXT(int graphicsProfile) const
        {
            (void)graphicsProfile;
            return (std::numeric_limits<int>::max)();
        }

        /**
         * @brief Largest cube-map edge length the given GraphicsProfile permits.
         *
         * @param graphicsProfile GraphicsProfile ordinal (Reach = 0, HiDef = 1).
         * @return The maximum edge length, or std::numeric_limits<int>::max() for no ceiling.
         */
        [[nodiscard]] virtual int GetMaxCubeSizeForProfileEXT(int graphicsProfile) const
        {
            (void)graphicsProfile;
            return (std::numeric_limits<int>::max)();
        }

        /**
         * @brief Largest volume-texture extent the given GraphicsProfile permits.
         *
         * Zero has a distinct meaning here: the profile does not support volume (3D) textures at
         * all, which is genuinely the case for GraphicsProfile.Reach on D3D9 -- not merely a small
         * size ceiling.
         *
         * @param graphicsProfile GraphicsProfile ordinal (Reach = 0, HiDef = 1).
         * @return The maximum extent, 0 for "no volume textures at all", or
         *         std::numeric_limits<int>::max() for no ceiling.
         */
        [[nodiscard]] virtual int GetMaxVolumeExtentForProfileEXT(int graphicsProfile) const
        {
            (void)graphicsProfile;
            return (std::numeric_limits<int>::max)();
        }

        /**
         * @brief Largest number of simultaneous render targets the given GraphicsProfile permits.
         *
         * Distinct from XNA's own general 4-target ceiling (which GraphicsDevice enforces for every
         * renderer) and from any hardware cap the renderer enforces separately.
         *
         * @param graphicsProfile GraphicsProfile ordinal (Reach = 0, HiDef = 1).
         * @return The maximum count, or std::numeric_limits<int>::max() for no ceiling.
         */
        [[nodiscard]] virtual int GetMaxRenderTargetsForProfileEXT(int graphicsProfile) const
        {
            (void)graphicsProfile;
            return (std::numeric_limits<int>::max)();
        }

        // --- Surface-format boundaries (plans/plan_runtimerenderer.md design decision 9) -------------

        /**
         * @brief Whether a Texture2D may be created with the given surface format.
         *
         * Tri-state on purpose. The framework already has a renderer-agnostic rule for this
         * (Texture::ValidateFormat), and 45 of the 46 renderers are content with it -- so the
         * default is Defer, meaning "the framework's own rule applies". Only a renderer that
         * genuinely stores each format in its own native layout (SKIA) answers Supported /
         * Unsupported. A plain bool would have forced every other renderer to restate the
         * framework rule, and getting that restatement wrong would silently narrow a public API.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return This renderer's verdict, or Defer to accept the framework's rule.
         */
        /**
         * @brief The dialect a custom `ShaderEffect`'s sources must be written in. CNAEXT.
         *
         * @return This renderer's dialect, or `ShaderDialectEXT::Unknown` if it has not declared
         *         one. Answered at RUNTIME, so a renderer that picks its native API per process
         *         reports what it actually picked rather than what the build defaulted to.
         */
        [[nodiscard]] virtual ShaderDialectEXT GetShaderDialectEXT() const
        {
            return ShaderDialectEXT::Unknown;
        }

        [[nodiscard]] virtual RendererFormatVerdict ClassifySurfaceFormatEXT(int surfaceFormat) const
        {
            (void)surfaceFormat;
            return RendererFormatVerdict::Defer;
        }

        /**
         * @brief Whether a RenderTarget2D may be created with the given surface format.
         *
         * Deliberately separate from ClassifySurfaceFormatEXT: renderability is a strictly narrower
         * question than storability. SKIA's raster surface has no hardware format restriction at
         * all, yet it still refuses the formats real XNA/FNA hardware cannot render into, because
         * parity with XNA is the goal rather than "whatever the backing library happens to allow".
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return This renderer's verdict, or Defer to accept the framework's rule.
         */
        [[nodiscard]] virtual RendererFormatVerdict ClassifyRenderTargetFormatEXT(int surfaceFormat) const
        {
            (void)surfaceFormat;
            return RendererFormatVerdict::Defer;
        }

        /**
         * @brief Whether GetData/SetData with a Color-shaped element is meaningful for a format.
         *
         * Same tri-state reasoning as ClassifySurfaceFormatEXT. The framework rule here is "any
         * format whose texel is a multiple of four bytes", which is a route real code depends on
         * (MouseCursor::FromTexture2D reads a ColorSrgbEXT texture through it). SKIA narrows it to
         * the formats that are genuinely 32-bit RGBA-shaped, because it stores the others in their
         * own layouts and a Color* transfer would read the wrong bits.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return This renderer's verdict, or Defer to accept the framework's rule.
         */
        [[nodiscard]] virtual RendererFormatVerdict ClassifyColorTransferFormatEXT(int surfaceFormat) const
        {
            (void)surfaceFormat;
            return RendererFormatVerdict::Defer;
        }

        /**
         * @brief Whether a format is a block-compressed format this renderer transfers as blocks.
         *
         * The default is false for every format: renderers that do not store compressed textures
         * natively never take the compressed transfer path.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return true when the format transfers as compressed blocks rather than pixels.
         */
        [[nodiscard]] virtual bool IsCompressedTransferFormatEXT(int surfaceFormat) const
        {
            (void)surfaceFormat;
            return false;
        }

        /**
         * @brief Whether a cube texture accepts a format's exact block-compressed payload.
         *
         * This is separate from @ref IsCompressedTransferFormatEXT because a backend may have
         * implemented compressed 2D textures without implementing the corresponding cube-map
         * allocation and upload path.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return true when compressed cube faces transfer as blocks rather than RGBA pixels.
         */
        [[nodiscard]] virtual bool IsCompressedCubeTransferFormatEXT(int surfaceFormat) const
        {
            (void)surfaceFormat;
            return false;
        }

        /**
         * @brief Whether the content loaders should keep block-compressed content compressed.
         *
         * WEBGPU-144 Phase 2 / XNB-24: `Texture2D::FromStream` (DDS) and the `.xnb` Texture2D reader
         * force-decode DXT/BC to `Color` by default. A renderer that both stores compressed textures
         * natively and prefers to receive loaded content that way returns true here; the loaders
         * then keep the raw blocks and upload them through the compressed `SetData` path (guarded, in
         * addition, by @ref IsCompressedTransferFormatEXT for the specific format) instead of
         * CPU-decompressing. This is a loader-policy flag distinct from
         * @ref IsCompressedTransferFormatEXT: a renderer may transfer blocks via `SetData` yet still
         * deliberately prefer decoded content (the default), so this stays false for every renderer
         * that has not opted in.
         *
         * @return true to keep loaded block-compressed content compressed; default false.
         */
        [[nodiscard]] virtual bool LoadsCompressedContentNativelyEXT() const
        {
            return false;
        }

        /// Converts a point from SDL window-coordinate space to logical (virtual) game
        /// coordinates. A renderer whose drawable pixel size differs from the window's logical size
        /// must account for that density internally. Returns true on success. Default: no-op.
        /// Converts a point from the platform window's logical client-coordinate space to the
        /// renderer's virtual game-coordinate space. Presentation scale, crop/letterbox offsets
        /// and any difference between logical client units and drawable pixels are entirely the
        /// renderer's responsibility. Returns true when a logical counterpart exists. Default:
        /// no transform (returns false and leaves the outputs untouched).
        virtual bool TransformWindowToLogical(float windowX, float windowY,
                                              float& logX, float& logY) const { return false; }
        /// Converts a point from logical game coordinates to the platform window's logical client
        /// coordinates -- the inverse of TransformWindowToLogical for points in the presentation
        /// viewport. Returns true on success. Default: no transform (returns false and leaves the
        /// outputs untouched); callers may then use a 1:1 fallback.
        virtual bool TransformLogicalToWindow(float logX, float logY,
                                              float& windowX, float& windowY) const { return false; }
        virtual std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) = 0;
        virtual std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() = 0;

        /// Reads the rendered backbuffer pixels for the given region into @p pixels (RGBA8).
        /// @p x, @p y are top-left in game coordinates; @p pixels must hold w*h*4 bytes.
        /// Default implementation throws — override in renderers that support readback.
        virtual void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
        {
            throw std::runtime_error("ReadBackbuffer: not implemented in this renderer");
        }

        /// Creates a renderer occlusion query object. Returns nullptr on
        /// renderers that do not support hardware occlusion queries.
        virtual std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() { return nullptr; }
        virtual std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) { return nullptr; }
        virtual std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) { return nullptr; }

        /// Creates an off-screen FBO-backed render target. Returns nullptr on
        /// renderers that do not support render targets. `depthFormat` is the raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::DepthFormat (None=0, Depth16=1, Depth24=2,
        /// Depth24Stencil8=3), passed as `int` to avoid coupling this renderer-agnostic header
        /// to the XNA namespace — mirrors CreateTexture3D/CreateTextureCube's `surfaceFormat`
        /// convention. EasyGL and Bgfx honor the exact requested format (None omits the
        /// depth/stencil attachment entirely); Vulkan always allocates a combined depth+stencil
        /// buffer using its device-wide format regardless of the exact value requested, since
        /// varying it per render target would require a depth-format-keyed render pass/pipeline
        /// cache (Vulkan render-pass-compatibility rules require matching attachment formats
        /// for the pipelines this renderer currently shares across the backbuffer and every
        /// render target) — a real architectural change, tracked as Task 911 (Task 877).
        /// `mipMap` requests a full mip chain, auto-generated from level 0 when the target is
        /// unbound (matching FNA3D's OPENGL_ResolveTarget behavior) — all 3 renderers implement
        /// this (Task 336/878/906). `multiSampleCount` requests a multisampled color (and depth,
        /// where honored) attachment, resolved into the sampleable texture when the target is
        /// unbound (same FNA3D resolve mechanism; all 3 renderers implement this for
        /// RenderTarget2D — Task 337/878/879).
        virtual std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) { return nullptr; }

        /// SKIA-142: same contract as CreateRenderTarget2D, plus an explicit
        /// Microsoft::Xna::Framework::Graphics::SurfaceFormat (passed as `int` so this header
        /// does not need to include SurfaceFormat.hpp, matching CreateTexture's ImageData
        /// convention). The default forwards to CreateRenderTarget2D and ignores the format,
        /// preserving every existing renderer's Color-only render-target behavior unchanged; only
        /// a renderer that actually supports additional render-target formats needs to override
        /// this instead.
        virtual std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int /*surfaceFormat*/)
        {
            return CreateRenderTarget2D(w, h, depthFormat, preserveContents, mipMap, multiSampleCount);
        }

        /// plans/plan_modern.md MOD-123: whether sampling a half-float colour texture with a linear
        /// (or mip) filter is supported by this context, as distinct from being able to render into
        /// one. Default false: a renderer that has not checked cannot promise it, and a pass that
        /// wrongly assumes hardware filtering gets undefined sampling rather than a slower path.
        [[nodiscard]] virtual bool SupportsHalfFloatTextureLinearFilteringEXT() const
        {
            return false;
        }

        /// Activates the given render target (binds its FBO). Pass nullptr to
        /// restore the default back buffer.
        virtual void SetRenderTarget2D(IRenderTargetRenderer* rt) {}

        /// Creates a cube-map render target. Returns nullptr on renderers that
        /// do not support cube map render targets. See CreateRenderTarget2D for `depthFormat`/
        /// `mipMap`/`multiSampleCount`.
        /// `preserveContents` (REMED-GFX-136) carries the public RenderTargetCube's own
        /// RenderTargetUsage down here, exactly as CreateRenderTarget2D's identically-placed
        /// parameter does — true means "when a face of this target is bound, load what is already
        /// in it", false means "the previous colour need not survive". Both public targets derive
        /// it from the single Microsoft::Xna::Framework::Graphics::
        /// RenderTargetUsagePreservesContentsEXT() mapping, so PlatformContents cannot mean one
        /// thing to the shared layer and another to a renderer. Before this parameter existed a
        /// cube target's real usage stopped at RenderTargetCube's constructor and every renderer
        /// had to invent an answer: Vulkan and WebGPU both invented "always discard", so a
        /// PreserveContents cube face was wiped on every bind cycle.
        virtual std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) { return nullptr; }

        /// plans/plan_modern.md MOD-107: CreateRenderTargetCube plus an explicit
        /// Microsoft::Xna::Framework::Graphics::SurfaceFormat ordinal, exactly as
        /// CreateRenderTarget2DEXT is to CreateRenderTarget2D. The default forwards and ignores the
        /// format, so every renderer keeps its Color-only cube behaviour until it implements more;
        /// the CNAEXT engine layer needs this for image-based lighting, whose irradiance and
        /// prefiltered-specular products are float cube maps rendered face by face.
        virtual std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int /*surfaceFormat*/)
        {
            return CreateRenderTargetCube(size, depthFormat, preserveContents, mipMap, multiSampleCount);
        }

        /// Compiles a shader program from GLSL/HLSL source strings.
        /// Returns nullptr on renderers that do not support programmable shaders.
        virtual std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                                      const std::string& fragSrc)
        { return nullptr; }

        /// Parses and compiles a Direct3D 9 Effect Framework binary for this renderer/device.
        /// The default is an explicit unsupported result; callers must pair this with the
        /// CompiledEffects capability and never silently substitute a stock shader.
        virtual std::unique_ptr<ICompiledEffectRuntime> CreateCompiledEffect(
            const std::uint8_t* /*effectCode*/, std::size_t /*effectCodeBytes*/)
        {
            return nullptr;
        }

        /// Dedicated opt-in for compiled XNA effects. This separate false-by-default gate is
        /// intentional: legacy renderer SupportsCapability switches often return true for enum
        /// values added after they were written. GraphicsDevice consults this method for
        /// CompiledEffects so an old catch-all cannot accidentally advertise a native runtime.
        [[nodiscard]] virtual bool SupportsCompiledEffects() const { return false; }

        /// plans/plan_modern.md MOD-1502: compute shaders and storage buffers. Every default is the
        /// honest "this renderer cannot", so all renderer families compile unchanged and none of
        /// them accidentally claims a feature it has never heard of -- the same opt-in shape
        /// SupportsCompiledEffects uses, and for the same reason.
        virtual std::unique_ptr<IComputeShaderRenderer> CreateComputeShader(
            const std::string& /*computeSrc*/)
        {
            return nullptr;
        }

        /// Creates a storage buffer of @p byteSize bytes, or null where unsupported.
        virtual std::unique_ptr<IStorageBufferRenderer> CreateStorageBuffer(
            std::size_t /*byteSize*/)
        {
            return nullptr;
        }

        /// Runs the bound compute program over a grid of work groups. A no-op where unsupported.
        virtual void DispatchCompute(IComputeShaderRenderer* /*shader*/, int /*groupsX*/,
                                     int /*groupsY*/, int /*groupsZ*/) {}

        /// Orders memory access after a dispatch. `barrierBits` is a `CNA::GraphicsMemoryBarrier`
        /// bitmask, which each renderer translates into its own native bits.
        virtual void MemoryBarrierEXT(int /*barrierBits*/) {}

        /// plans/plan_modern.md MOD-1699: whether the source text handed to `CreateEffectRenderer`
        /// actually determines the pixels. Three renderers answer this differently from
        /// `GraphicsCapability::CustomEffects`, and each way is legitimate: SOFTWARE and HEADLESS
        /// *accept* any shader source and render with their own fixed path instead (a documented
        /// decision, not a bug), and Vulkan compiles SPIR-V bytecode rather than the GLSL text the
        /// engine layer writes. A post-process pass that believes its shader ran on those
        /// renderers produces a copy of its input and reports success, which is the one outcome
        /// worse than refusing. False by default, for the same reason every other promise here is.
        [[nodiscard]] virtual bool ExecutesShaderEffectSourceEXT() const { return false; }

        /// plans/plan_modern.md MOD-1699: whether this renderer's lit shaders actually SAMPLE the
        /// shadow state an effect carries (`IShadowReceiverEXT`: single map, cascades, punctual).
        /// The state itself is accepted and ignored everywhere -- that convention is what keeps a
        /// draw working on a renderer with no shadow shader -- but "accepted and ignored" and
        /// "honoured" are different promises, and a caller that wants to know which it is getting
        /// has no way to ask otherwise. False by default: a renderer that has not implemented the
        /// sampling must not claim it.
        [[nodiscard]] virtual bool SupportsShadowSamplingEXT() const { return false; }

        /// plans/plan_modern.md MOD-1699: whether this renderer's PBR shader honours the image-based
        /// lighting group of `GpuDrawParams` (`iblIrradiance`, `iblPrefilteredSpecular`,
        /// `iblBrdfLut`). Same reasoning as `SupportsShadowSamplingEXT`, and the same default.
        [[nodiscard]] virtual bool SupportsImageBasedLightingEXT() const { return false; }

        /// plans/plan_modern.md MOD-1500: whether this renderer really implements compute. False by
        /// default, and consulted by GraphicsDevice for GraphicsCapability::ComputeShaders rather
        /// than that capability being answered by a renderer's own switch -- many of those end in
        /// `default: return true`.
        [[nodiscard]] virtual bool SupportsComputeShadersEXT() const { return false; }

        /// plans/plan_modern.md MOD-2090: whether this renderer really issues indirect draws --
        /// `DrawPrimitivesIndirectEXT` and `DrawIndexedPrimitivesIndirectEXT` below. False by
        /// default, and consulted by GraphicsDevice for GraphicsCapability::IndirectDraw rather
        /// than that capability being answered by a renderer's own switch, for the reason
        /// SupportsComputeShadersEXT states.
        [[nodiscard]] virtual bool SupportsIndirectDrawEXT() const { return false; }

        /// plans/plan_modern.md MOD-1514: whether a `Texture2D` can be bound to a compute shader as an
        /// image. Separate from `SupportsComputeShadersEXT` because the two genuinely differ: GL ES
        /// 3.1 requires an *immutable* texture (`glTexStorage2D`) for `glBindImageTexture`, and
        /// CNA's textures are allocated mutably (`glTexImage2D`), so an ES context that fully
        /// supports compute still cannot bind one. Desktop GL accepts a mutable texture. False by
        /// default, like every other promise here.
        [[nodiscard]] virtual bool SupportsComputeImageBindingEXT() const { return false; }

        /// plans/plan_modern.md MOD-2092: the colour space the swap chain is currently presenting in.
        /// `Srgb` by default and for every CNA renderer today -- an HDR swap chain is a property of
        /// the *presentation* path (DXGI, Vulkan surface formats, a platform's own HDR opt-in), not
        /// of a drawing API, and no CNA platform back end offers one yet. A renderer must not claim
        /// otherwise: PQ-encoded pixels handed to an sRGB swap chain are washed out and grey, which
        /// is a worse outcome than SDR output that is simply correct.
        [[nodiscard]] virtual CNA::DisplayColorSpace GetDisplayColorSpaceEXT() const
        {
            return CNA::DisplayColorSpace::Srgb;
        }

        /// plans/plan_modern.md MOD-2092: asks the swap chain to present in @p space.
        /// @return True when the swap chain now presents in that space. The default accepts only
        ///         `Srgb`, which is the truth rather than a stub: a renderer that returned true
        ///         without reconfiguring anything would have every caller encode for a display that
        ///         is not there.
        virtual bool SetDisplayColorSpaceEXT(const CNA::DisplayColorSpace space)
        {
            return space == CNA::DisplayColorSpace::Srgb;
        }

        /// plans/plan_modern.md MOD-2091: how many storage buffers a VERTEX shader on this context may
        /// read. GL ES 3.1 permits this to be zero -- a context can support compute completely and
        /// still refuse an SSBO in a vertex stage -- so it is a separate number rather than
        /// something derivable from `SupportsComputeShadersEXT()`. Zero means "not there", which is
        /// what every renderer without compute returns.
        [[nodiscard]] virtual int GetMaxVertexShaderStorageBlocksEXT() const { return 0; }

        /// plans/plan_modern.md MOD-2091: binds a storage buffer to a binding point a following DRAW's
        /// shaders read, rather than a following dispatch's. The binding points are the same set
        /// `IComputeShaderRenderer::BindStorageBuffer` uses; what differs is only which stage reads
        /// them, which is why this is on the renderer rather than on a program.
        virtual void BindStorageBufferForDrawEXT(int /*binding*/,
                                                 const IStorageBufferRenderer& /*buffer*/) {}

        /// plans/plan_modern.md MOD-2163: whether this renderer can measure GPU time around a range of
        /// commands. False by default, and false is the honest answer wherever the underlying API
        /// has no timer query -- see IGpuTimerRenderer for why a CPU fallback is not offered.
        [[nodiscard]] virtual bool SupportsGpuTimerEXT() const { return false; }

        /// plans/plan_modern.md MOD-2163: creates a GPU timer, or null where SupportsGpuTimerEXT() is
        /// false. Null rather than a stub that returns zeroes, so a caller cannot mistake "no
        /// timer here" for "this pass took no time".
        virtual std::unique_ptr<IGpuTimerRenderer> CreateGpuTimerEXT() { return nullptr; }

        /// plans/plan_modern.md MOD-1505: the dispatch limits this context guarantees, per axis
        /// (0 = x, 1 = y, 2 = z). Zero means "unknown or unsupported", which is what every
        /// renderer without compute returns.
        [[nodiscard]] virtual int GetMaxComputeWorkGroupCountEXT(int /*axis*/) const { return 0; }

        /// The largest local size a compute shader may declare, per axis.
        [[nodiscard]] virtual int GetMaxComputeWorkGroupSizeEXT(int /*axis*/) const { return 0; }

        /// The largest product of a compute shader's local sizes.
        [[nodiscard]] virtual int GetMaxComputeWorkGroupInvocationsEXT() const { return 0; }

        /// Activates a specific face of a cube-map render target for rendering.
        /// Pass nullptr to restore the default back buffer.
        virtual void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
        {
            if (rt) rt->BindAsRenderTargetFace(face);
            else SetRenderTarget2D(nullptr);
        }

        /// Activates a normalized ordered render-target set. Every renderer must explicitly
        /// consume or reject cube-face descriptors; there is deliberately no compatibility
        /// default that could flatten a cube to RenderTarget2D or face +X.
        /// Pass nullptr / count=0 to restore the default back buffer.
        virtual void SetRenderTargets(
            const RenderTargetBindingDescriptor* renderTargets, int count) = 0;

        // ---- Graphics state ----

        /// Applies a BlendState to the renderer. Default: no-op.
        /// @param writeState REMED-GFX-077: the four per-MRT-slot colour write masks
        ///                   (BlendState.ColorWriteChannels/1/2/3) and the coverage sample mask
        ///                   (BlendState.MultiSampleMask). No default argument: the migration
        ///                   deliberately forces every override to make an explicit decision
        ///                   (honor exactly, honor with a documented capability restriction, or
        ///                   intentionally not support with a documented reason) — never a silent
        ///                   no-op.
        virtual void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                     int colorDstBlend, int alphaDstBlend,
                                     int colorBlendFunc, int alphaBlendFunc,
                                     const BlendWriteState& writeState) {}

        /// Applies a DepthStencilState to the renderer. Default: no-op.
        virtual void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                            int depthFunc,
                                            bool stencilEnable, int stencilFunc,
                                            int stencilPass, int stencilFail, int stencilDepthFail,
                                            int stencilMask, int stencilWriteMask, int referenceStencil,
                                            bool twoSidedStencilMode,
                                            int ccwStencilFunc, int ccwStencilPass,
                                            int ccwStencilFail, int ccwStencilDepthFail) {}

        /// Applies a RasterizerState to the renderer. Default: no-op.
        /// @param cullMode            Raw CullMode int value.
        /// @param fillMode            Raw FillMode int value.
        /// @param scissorTestEnable   Whether the scissor test is enabled.
        /// @param depthBias           Constant depth bias (XNA DepthBias).
        /// @param slopeScaleDepthBias Slope-scaled depth bias (XNA SlopeScaleDepthBias).
        virtual void ApplyRasterizerState(int cullMode, int fillMode,
                                          bool scissorTestEnable,
                                          float depthBias = 0.0f,
                                          float slopeScaleDepthBias = 0.0f) {}

        /// Applies a SamplerState to the given texture slot. Default: no-op.
        /// @param slot         Texture unit index (0–15).
        /// @param filter       Raw TextureFilter int value.
        /// @param addressU     Raw TextureAddressMode int value for U.
        /// @param addressV     Raw TextureAddressMode int value for V.
        /// @param maxAnisotropy Maximum anisotropy level (1–16).
        virtual void ApplySamplerState(int slot, int filter,
                                       int addressU, int addressV,
                                       int maxAnisotropy) {}

        /// Applies sampler controls not represented by filter/address mode. Defaults to no-op
        /// so existing renderers can adopt each field independently and explicitly.
        virtual void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) {}

        /// Applies the third addressing axis of a SamplerState. Separate from ApplySamplerState
        /// for the same reason as the mip controls above: it is observable only where a renderer
        /// samples a volume texture, so each renderer adopts it explicitly. A renderer that does
        /// not override this must not invent a W mode of its own -- an effect's assigned ADDRESSW
        /// is then the one that stands. Default: no-op.
        /// @param slot     Texture unit index (0-15).
        /// @param addressW Raw TextureAddressMode int value for W.
        virtual void ApplySamplerAddressW(int slot, int addressW) {}

        /// Sets the constant blend color used with the BlendFactor blend mode.
        /// Maps to glBlendColor on GL renderers. Default: no-op.
        virtual void SetBlendFactor(float r, float g, float b, float a) {}

        /// Task 870/319: GraphicsDevice.ReferenceStencil is a real, independent device property
        /// (FNA3D_Get/SetReferenceStencil), analogous to BlendFactor above -- it must take effect
        /// immediately, standalone from a full DepthStencilState re-application. Default: no-op
        /// (renderers that only apply ReferenceStencil as part of ApplyDepthStencilState's full
        /// state and don't yet cache it for standalone re-application silently ignore this).
        virtual void SetReferenceStencil(int /*value*/) {}

        /// Sets the scissor clip rectangle. Default: no-op.
        virtual void SetScissorRect(int x, int y, int w, int h) {}

        /// Sets the GPU viewport rectangle and depth range (Task 880). Default: no-op.
        virtual void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) {}

        // ---- 3D pipeline ----

        /**
         * @brief Whether this renderer can maintain a complete depth/stencil buffer at all, for the
         * default back buffer (as opposed to an explicit RenderTarget2D, which has its own
         * per-instance IRenderTargetRenderer::HasRealDepthBuffer() query).
         *
         * Most renderers are 3D-capable and honor whatever depth/stencil format the presentation
         * parameters request, so the default is `true`. A renderer that is entirely 2D-only
         * (the native 2D renderer) never has a depth/stencil buffer regardless of what was requested and
         * overrides this to `false` — used by GraphicsDevice::Clear(ClearOptions, ...) to mask
         * DepthBuffer/Stencil out of a clear request instead of forwarding it to a
         * ClearColorDepthAndStencil()-style method this renderer cannot honor.
         */
        [[nodiscard]] virtual bool SupportsDepthStencil() const { return true; }

        /// Returns whether the default back buffer has a usable depth plane. Renderers with a
        /// depth-only surface override this independently of SupportsDepthStencil().
        [[nodiscard]] virtual bool SupportsDepthBuffer() const { return SupportsDepthStencil(); }

        /// Returns whether the default back buffer has a usable stencil plane. Kept separate
        /// from SupportsDepthBuffer() because historical APIs such as Glide expose Z but no
        /// stencil; GraphicsDevice::Clear must then retain a requested depth clear and discard
        /// only the impossible stencil portion.
        [[nodiscard]] virtual bool SupportsStencilBuffer() const { return SupportsDepthStencil(); }

        /**
         * Renderer boundary used by common public draw/model entry points before they inspect,
         * pack, allocate, bind, or otherwise consume 3D input. Fully 3D-capable renderers retain
         * the no-op default; a deliberately 2D-only renderer overrides this with its stable
         * unsupported-operation diagnostic.
         */
        virtual void Ensure3DSupported(const char* /*operation*/) const {}

        /**
         * @brief Clears color and depth buffers in a single call.
         *
         * @param r,g,b,a    Clear color in range 0..1.
         * @param depth      Depth value to clear with (0..1).
         */
        virtual void ClearColorAndDepth(float r, float g, float b, float a, float depth) = 0;
        virtual void ClearDepth(float depth) = 0;

        /**
         * @brief Clears the stencil buffer only, to a given reference value.
         * @param stencil Stencil value to clear with.
         */
        virtual void ClearStencil(int stencil) = 0;

        /**
         * @brief Clears the depth and stencil buffers in a single call.
         * @param depth   Depth value to clear with (0..1).
         * @param stencil Stencil value to clear with.
         */
        virtual void ClearDepthAndStencil(float depth, int stencil) = 0;

        /**
         * @brief Clears the color and stencil buffers in a single call.
         * @param r,g,b,a Clear color in range 0..1.
         * @param stencil Stencil value to clear with.
         */
        virtual void ClearColorAndStencil(float r, float g, float b, float a, int stencil) = 0;

        /**
         * @brief Clears the color, depth, and stencil buffers in a single call.
         * @param r,g,b,a Clear color in range 0..1.
         * @param depth   Depth value to clear with (0..1).
         * @param stencil Stencil value to clear with.
         */
        virtual void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) = 0;

        /**
         * @brief Enables or disables depth testing.
         *
         * @note Status: PARTIAL. Only the EasyGL renderer honors this; other
         *       renderers throw on first 3D usage.
         */
        virtual void SetDepthTestEnabled(bool enabled) = 0;
        virtual void SetBlendEnabled(bool enabled) = 0;
        virtual void SetDepthWriteEnabled(bool enabled) = 0;

        /**
         * @brief Creates a renderer-specific vertex buffer for
         *        `VertexPositionColor` data.
         */
        virtual std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) = 0;

        /**
         * @brief Creates a renderer-specific 16-bit index buffer.
         */
        virtual std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) = 0;
        /// Creates a 32-bit index buffer. A renderer must opt in explicitly: delegating to the
        /// 16-bit factory makes a valid uint32 upload look successful until draw-time truncation.
        virtual std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int /*index_capacity*/)
        {
            throw std::runtime_error(
                "IGraphicsRenderer::CreateIndexBuffer32: 32-bit index buffers are not supported by this renderer");
        }

        /**
         * @brief Draws colored primitives from `vb` using the supplied transform.
         *
         * The renderer internally applies a basic colored-vertex shader
         * (equivalent to `BasicEffect` with `VertexColorEnabled = true`).
         *
         * @param vb            Vertex buffer to read from.
         * @param world,view,projection Per-draw transform matrices (XNA
         *                              row-major). The combined matrix
         *                              uploaded to the GPU is
         *                              `projection * view * world`.
         * @param primitive     Primitive topology.
         * @param primitiveCount Number of primitives (NOT vertices).
         */
        virtual void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                           const Matrix& world,
                                           const Matrix& view,
                                           const Matrix& projection,
                                           PrimitiveType primitive,
                                           int primitiveCount) = 0;

        /**
         * @brief Indexed counterpart of `DrawColoredPrimitives`.
         */
        virtual void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                  const IIndexBufferRenderer& ib,
                                                  const Matrix& world,
                                                  const Matrix& view,
                                                  const Matrix& projection,
                                                  PrimitiveType primitive,
                                                  int primitiveCount) = 0;

        /**
         * @brief Effect-aware draw — selects the shader variant based on
         *        vertex layout (derived from stride) and @p params.
         *
         * REMED-GFX-201: @p vb is `params.vertexStreams[0].buffer`, kept as a named argument so
         * the single-stream path is unchanged. The complete set of bound per-vertex streams is
         * `params.vertexStreams[0 .. params.vertexStreamCount)`, and the layout the shader sees
         * spans all of them: select the input layout with `params.combinedVertexStride`, re-slot
         * each of its elements with `MapCombinedOffsetToStream()`, and bind every stream at its
         * own slot with its own stride and `VertexStreamByteOffset()`. A renderer that cannot
         * express the combined layout must throw before native submission rather than render from
         * stream 0 alone; `HasMultipleVertexStreams()` selects that branch.
         *
         * Default implementation falls back to DrawColoredPrimitives so
         * renderers that have not yet implemented this path still work.
         */
        virtual void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                      const Matrix& world,
                                      const Matrix& view,
                                      const Matrix& projection,
                                      PrimitiveType primitive,
                                      int primitiveCount,
                                      const GpuDrawParams& params)
        {
            DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
        }

        /**
         * @brief Indexed counterpart of `DrawPrimitivesEx`.
         */
        virtual void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                             const IIndexBufferRenderer& ib,
                                             const Matrix& world,
                                             const Matrix& view,
                                             const Matrix& projection,
                                             PrimitiveType primitive,
                                             int primitiveCount,
                                             const GpuDrawParams& params)
        {
            DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
        }

        /**
         * @brief Instanced indexed draw — default throws on renderers that don't support it. A
         * permanently 2D-only renderer may opt into WarnAndStub, in which case this is a
         * warning-producing no-op.
         *
         * REMED-GFX-202: this route takes the SAME complete stream description the two ordinary
         * routes take. @p vb is `params.vertexStreams[0].buffer`, kept as a named argument so the
         * classic single-per-vertex-stream path is unchanged; the per-instance streams are the
         * entries whose `instanceFrequency` is greater than zero, reached through
         * `FirstInstanceStream()` / `NthInstanceStream()`. Each stream carries its own slot,
         * declaration, stride, `VertexOffset` (in vertex ELEMENTS) and `InstanceFrequency`, and the
         * whole array is captured by value, so a deferred renderer must copy what it needs from it
         * and must never re-read `GraphicsDevice`'s public binding state at replay.
         *
         * A renderer that cannot express the bound combination must throw before native submission
         * rather than render from a subset of the streams; `HasMultipleVertexStreams()` and
         * `HasMultipleInstanceStreams()` select that branch, and `GraphicsDevice` already rejects
         * both shapes for a renderer that does not report
         * `GraphicsCapability::MultiStreamVertexInput`.
         */
        virtual void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                               const IIndexBufferRenderer& ib,
                                               const Matrix& world,
                                               const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive,
                                               int primitiveCount,
                                               int instanceCount,
                                               const GpuDrawParams& params)
        {
            (void)vb; (void)ib; (void)world; (void)view; (void)projection;
            (void)primitive; (void)primitiveCount; (void)instanceCount; (void)params;
            if (!SupportsCapability(CNA::GraphicsCapability::ThreeD) &&
                unsupported3DGraphicsCallBehavior_ ==
                    CNA::Unsupported3DGraphicsCallBehavior::WarnAndStub)
            {
                HandleUnsupported3DCall(
                    CNA::getCurrentGraphicsRendererName(), "DrawInstancedPrimitives");
                return;
            }
            throw std::runtime_error(
                "DrawInstancedPrimitives is not supported on this graphics renderer.");
        }

        /**
         * @brief Draws with the count and offsets read out of a GPU buffer instead of passed in.
         *
         * plans/plan_modern.md `MOD-2090`. @p argumentBuffer holds a `CNA::IndirectDrawArguments` at
         * @p argumentByteOffset, and the renderer never looks at it: the numbers are fetched by
         * the GPU as the command is issued, which is the whole point -- a compute shader can
         * decide how much to draw without the answer travelling back through the CPU.
         *
         * The consequence is that **none of the range validation the ordinary routes perform is
         * possible here.** `GraphicsDevice` checks that a requested primitive range fits the bound
         * buffers before every other draw; that check cannot exist for this one, because the range
         * is in GPU memory. A wrong count is undefined behaviour rather than an exception, and the
         * shader that wrote it owns the obligation. Everything the CPU *can* still check --
         * that a buffer is bound, an effect applied, and the argument offset inside the buffer --
         * is checked in `GraphicsDevice` exactly as it is for the other routes.
         *
         * @param vb                 The bound vertex buffer, as for `DrawPrimitivesEx`.
         * @param world              The world matrix.
         * @param view               The view matrix.
         * @param projection         The projection matrix.
         * @param primitive          The topology; the argument buffer supplies only counts, never this.
         * @param argumentBuffer     The buffer holding the arguments. Never null.
         * @param argumentByteOffset Where in it they start, in bytes.
         * @param params             The same complete draw description the ordinary routes take.
         */
        virtual void DrawPrimitivesIndirectEXT(const IVertexBufferRenderer& vb,
                                               const Matrix& world,
                                               const Matrix& view,
                                               const Matrix& projection,
                                               PrimitiveType primitive,
                                               const IStorageBufferRenderer& argumentBuffer,
                                               int argumentByteOffset,
                                               const GpuDrawParams& params)
        {
            (void)vb; (void)world; (void)view; (void)projection; (void)primitive;
            (void)argumentBuffer; (void)argumentByteOffset; (void)params;
            throw std::runtime_error(
                "Indirect drawing is not supported on this graphics renderer.");
        }

        /**
         * @brief Indexed counterpart of @ref DrawPrimitivesIndirectEXT.
         *
         * @param vb                 The bound vertex buffer.
         * @param ib                 The bound index buffer.
         * @param world              The world matrix.
         * @param view               The view matrix.
         * @param projection         The projection matrix.
         * @param primitive          The topology.
         * @param argumentBuffer     The buffer holding a `CNA::IndirectDrawIndexedArguments`.
         * @param argumentByteOffset Where in it they start, in bytes.
         * @param params             The same complete draw description the ordinary routes take.
         */
        virtual void DrawIndexedPrimitivesIndirectEXT(const IVertexBufferRenderer& vb,
                                                      const IIndexBufferRenderer& ib,
                                                      const Matrix& world,
                                                      const Matrix& view,
                                                      const Matrix& projection,
                                                      PrimitiveType primitive,
                                                      const IStorageBufferRenderer& argumentBuffer,
                                                      int argumentByteOffset,
                                                      const GpuDrawParams& params)
        {
            (void)vb; (void)ib; (void)world; (void)view; (void)projection; (void)primitive;
            (void)argumentBuffer; (void)argumentByteOffset; (void)params;
            throw std::runtime_error(
                "Indirect drawing is not supported on this graphics renderer.");
        }

        /// Disables context-loss recovery on the running renderer.
        /// Safe to call after renderer creation when no resources have been
        /// loaded yet (e.g. from Game1 constructor). Future Create* calls
        /// will skip registry registration and CPU shadow copies.
        virtual void SetContextRecoveryEnabled(bool /*enabled*/) {}

        /**
         * @brief Selects how permanently unsupported 3D calls are handled by 2D-only renderers.
         *
         * The default is Throw, preserving each renderer's established behavior. WarnAndStub is
         * device-local and takes effect immediately; changing the policy clears the warn-once
         * history so the first subsequent stubbed operation is visible in the log.
         */
        virtual void SetUnsupported3DGraphicsCallBehavior(
            CNA::Unsupported3DGraphicsCallBehavior behavior)
        {
            if (unsupported3DGraphicsCallBehavior_ == behavior)
                return;

            unsupported3DGraphicsCallBehavior_ = behavior;
            warnedUnsupported3DCalls_.clear();
        }

        /** @brief Returns the active unsupported-3D-call policy. */
        [[nodiscard]] virtual CNA::Unsupported3DGraphicsCallBehavior
        GetUnsupported3DGraphicsCallBehavior() const
        {
            return unsupported3DGraphicsCallBehavior_;
        }

        /// Returns whether this renderer (and, for device-dependent entries, the current runtime
        /// device/driver) supports the given CNA::GraphicsCapability. Default implementation
        /// returns true except for multi-stream input, compiled effects and the float
        /// render-target entries, each of which requires explicit renderer opt-in. Every renderer
        /// with a narrower contract -- including no-renderer, 2D-only, fixed-function,
        /// experimental, or device-dependent capability gaps -- must override the applicable
        /// entries truthfully.
        [[nodiscard]] virtual bool SupportsCapability(CNA::GraphicsCapability capability) const
        {
            if (capability == CNA::GraphicsCapability::StencilBuffer)
                return SupportsStencilBuffer();
            // REMED-GFX-201: MultiStreamVertexInput is the one entry whose default is FALSE. A
            // renderer derives its native input elements from a single byte stride, so binding a
            // second per-vertex stream is real work it must opt into by name; defaulting to true
            // would make a renderer that silently renders from stream 0 alone claim otherwise.
            if (capability == CNA::GraphicsCapability::MultiStreamVertexInput)
                return false;
            // A bytecode parser alone is insufficient: the backend must own the native shaders,
            // reflection/value lifecycle, exact passes and state/sampler application before it
            // can opt in. The corresponding creation default above returns nullptr.
            if (capability == CNA::GraphicsCapability::CompiledEffects)
                return SupportsCompiledEffects();
            // plans/plan_modern.md MOD-100/MOD-101: float colour render targets are opt-in for the same
            // reason. CreateRenderTarget2DEXT's own shared default drops the requested
            // SurfaceFormat and produces an 8-bit Color target, so a renderer that has not been
            // taught float formats would, under a true default, promise that values above 1.0
            // survive a render-to-target -- the exact promise the HDR pipeline is built on.
            if (capability == CNA::GraphicsCapability::FloatRenderTargets ||
                capability == CNA::GraphicsCapability::HalfFloatRenderTargets)
                return false;
            return true;
        }

        /**
         * @brief Returns renderer-specific English limitations that are intentionally not a
         *        machine-readable capability or numeric limit.
         *
         * This is for qualitative, combination, performance, validation-environment and known-
         * issue notes. Any fact an application can safely branch on belongs in the structured
         * capability profile instead. The shared default contributes no renderer-specific text;
         * `GraphicsDevice` still appends the common interpretation rules to every generated
         * report.
         *
         * @return Stable UTF-8 text, or an empty view when the renderer has no extra note.
         */
        [[nodiscard]] virtual std::string_view GetAdditionalLimitationsTextEXT() const
        {
            return {};
        }

        /**
         * @brief REMED-GFX-201: how many per-vertex `VertexBufferBinding`s this renderer can bind
         *        to one ordinary draw.
         *
         * XNA's public ceiling is `kMaxVertexStreams` (16). A renderer that reports
         * `GraphicsCapability::MultiStreamVertexInput` but has a lower native input-slot limit
         * returns it here so `GraphicsDevice` rejects an over-wide binding set deterministically
         * rather than truncating it, collapsing streams, or letting the native API abort. The
         * default is the public maximum; a renderer that does not support multi-stream input at
         * all is already rejected by the capability itself and never reaches this.
         */
        [[nodiscard]] virtual int GetMaxVertexStreams() const
        {
            return kMaxVertexStreams;
        }

        /// REMED-CONTENT-001: returns this renderer's real maximum single-axis texture dimension
        /// (width or height), used by shared content-reading code to reject an XNB-declared size
        /// before any renderer-specific texture creation is attempted. Default of 16384 matches the
        /// guaranteed ceiling on every native API this project targets (D3D11/D3D12 feature level
        /// 11_0's REQ_TEXTURE2D_U_OR_V_DIMENSION, and the value real-world Vulkan/Metal/GL
        /// implementations report) -- no renderer currently needs a tighter or looser override.
        /// D3D9's own narrower Reach/HiDef profile ceiling (2048/4096) is enforced separately by
        /// Texture2D.cpp's existing ValidateTextureSizeForProfileEXT and is unaffected by this.
        [[nodiscard]] virtual int GetMaxTextureDimension() const
        {
            return 16384;
        }

        // ---- Debug / testing ----

        /**
         * @brief Reports whether the renderer can safely accept a game Draw call now.
         *
         * The default is true. Context-restoring renderers override this while their native
         * device is unavailable so the framework can continue Update ticks without letting game
         * rendering call invalid entry points.
         *
         * @return True when a Draw/Present pair may begin.
         */
        [[nodiscard]] virtual bool CanBeginDrawEXT() const { return true; }

        /// Inserts a named GPU debug label into the command stream.
        /// Default implementation is a no-op; Vulkan renderer overrides with
        /// vkCmdInsertDebugUtilsLabelEXT when VK_EXT_debug_utils is available.
        virtual void SetStringMarkerEXT(const char* /*marker*/) {}

        /// Simulates an OpenGL context loss.
        /// On Web (Emscripten): triggers WEBGL_lose_context.loseContext().
        /// On desktop: destroys the SDL GL context and immediately recreates it,
        /// forcing all GPU resources to be re-initialised.
        virtual void DebugSimulateContextLoss() {}

        /// Simulates an OpenGL context restore after a previous DebugSimulateContextLoss().
        /// On Web: triggers WEBGL_lose_context.restoreContext().
        /// On desktop: equivalent to DebugSimulateContextLoss() (destroy + recreate).
        virtual void DebugRestoreContext() {}

        // ---- Window id → renderer registry ----
        // Renderers that implement coordinate conversion register themselves here so input can
        // map platform window-client coordinates without knowing or resolving a native window.
        // WindowId is the sole identity crossing this common boundary.

        static void RegisterForWindow(CNA::Platform::WindowId window, IGraphicsRenderer* renderer)
        {
            windowRegistry()[window] = renderer;
        }
        static void UnregisterForWindow(CNA::Platform::WindowId window)
        {
            windowRegistry().erase(window);
        }
        static IGraphicsRenderer* GetForWindow(CNA::Platform::WindowId window)
        {
            auto& reg = windowRegistry();
            auto it = reg.find(window);
            return it != reg.end() ? it->second : nullptr;
        }

    protected:
        /**
         * @brief Throws or emits a warn-once message according to the active policy.
         *
         * Callers must invoke this only for operations permanently unsupported by a 2D-only
         * renderer. If it returns, WarnAndStub is active and the caller must perform a safe no-op
         * or return a valid null-object resource.
         *
         * const so a renderer's Ensure3DSupported() override (itself const, since it runs ahead
         * of any state mutation) can call this directly as its earliest-possible guard, instead of
         * only the later factory/draw methods that already call it. warnedUnsupported3DCalls_ is
         * mutable for exactly this reason.
         */
        void HandleUnsupported3DCall(std::string_view rendererName,
                                     std::string_view methodName) const
        {
            const std::string failureMessage =
                std::string(rendererName) + " does not support 3D: " + std::string(methodName);

            if (unsupported3DGraphicsCallBehavior_ !=
                CNA::Unsupported3DGraphicsCallBehavior::WarnAndStub)
            {
                throw std::runtime_error(failureMessage);
            }

            if (warnedUnsupported3DCalls_.insert(std::string(methodName)).second)
            {
                CNA::Logger::Warn(
                    failureMessage +
                        " was ignored because Unsupported3DGraphicsCallBehavior::WarnAndStub "
                        "is active.",
                    CNA::LogCategory::RENDER);
            }
        }

        /** @brief True when safe null-object resources should replace unsupported factories. */
        [[nodiscard]] bool ShouldStubUnsupported3DResource() const
        {
            return unsupported3DGraphicsCallBehavior_ ==
                   CNA::Unsupported3DGraphicsCallBehavior::WarnAndStub;
        }

    private:
        CNA::Unsupported3DGraphicsCallBehavior unsupported3DGraphicsCallBehavior_ =
            CNA::Unsupported3DGraphicsCallBehavior::Throw;
        mutable std::unordered_set<std::string> warnedUnsupported3DCalls_;

        static std::unordered_map<CNA::Platform::WindowId, IGraphicsRenderer*>& windowRegistry()
        {
            static std::unordered_map<CNA::Platform::WindowId, IGraphicsRenderer*> reg;
            return reg;
        }
    };

    /**
     * @brief Presentation/scaling policy used when the virtual (game-logic)
     *        resolution differs from the physical surface size.
     *
     * Matches XNA/Windows Phone semantics:
     * - Letterbox            – scale = min(surfW/virtW, surfH/virtH); adds bars.
     * - Overscan             – scale = max(surfW/virtW, surfH/virtH); crops edges.
     * - Stretch              – stretches to fill without preserving aspect ratio.
     * - NativeBackBuffer     – no scaling; game draws at its requested size.
     * - FixedHeightDynamicWidth – keeps the game's preferred height as the
     *                            logical height and computes logical width from
     *                            the actual surface aspect ratio:
     *                              logicalW = round(outputW * preferredH / outputH)
     *                            Then applies LETTERBOX so the computed canvas
     *                            fills the surface perfectly (no bars, no crop).
     *                            This matches XNA/Windows Phone behaviour where
     *                            height=480 is fixed and wider devices simply
     *                            show more horizontal content.
     */
    enum class CnaPresentationMode
    {
        Letterbox = 0,
        Overscan = 1,
        Stretch = 2,
        NativeBackBuffer = 3,
        FixedHeightDynamicWidth = 4
    };

    /**
     * @brief CNAEXT (plans/plan_dx9.md D9-34). Real, driver-triggered device lifecycle events a renderer
     * can report back to GraphicsDevice via GraphicsRendererCreateArgs::deviceEventCallback.
     *
     * Distinct from the pre-existing IGraphicsRenderer::SetContextRecoveryEnabled()/
     * DebugSimulateContextLoss()/DebugRestoreContext() channel, which is one-directional and
     * test-only (GraphicsDevice commands a renderer to simulate loss). This is the opposite
     * direction -- renderer reports a real, async, driver-detected event up to GraphicsDevice --
     * and it is what actually fires GraphicsDevice::DeviceLost/DeviceResetting/DeviceReset for a
     * genuine device loss (as opposed to GraphicsDevice::Reset()'s own direct Raise() calls for an
     * app-initiated reset with new PresentationParameters -- a different, pre-existing path that
     * this enum does not replace).
     */
    enum class RendererDeviceEvent
    {
        /// The device has been lost (e.g. D3D9's D3DERR_DEVICELOST) and cannot be used until it
        /// is reset. Fires GraphicsDevice::DeviceLost.
        Lost,
        /// About to release all pool-default-equivalent resources and reset the device. Fires
        /// GraphicsDevice::DeviceResetting.
        Resetting,
        /// The device was successfully reset and resources recreated. Fires
        /// GraphicsDevice::DeviceReset.
        Reset
    };

    /**
     * @brief Immutable platform-window snapshot supplied when a renderer is created.
     *
     * The value owns nothing. Its native handle remains valid only while the platform window
     * identified by @ref windowId is alive. Renderers use the physical @ref drawableSize for
     * swap-chain sizing; @ref displayScale relates that size to logical window coordinates.
     */
    struct RendererSurfaceInfo
    {
        /** @brief Stable identity used by platform events and renderer lookup. */
        CNA::Platform::WindowId windowId = 0;
        /** @brief Typed native handle for renderers that talk directly to a window system. */
        CNA::Platform::NativeWindowHandle nativeHandle;
        /** @brief Initial drawable size in physical pixels. */
        CNA::Platform::WindowSize drawableSize;
        /** @brief Initial logical-to-physical display scale; 1.0 on unscaled/windowless hosts. */
        float displayScale = 1.0f;
    };

    /** @brief Arguments for creating a graphics renderer. */
    struct GraphicsRendererCreateArgs
    {
        /** @brief Platform-neutral description of the renderer's presentation surface. */
        RendererSurfaceInfo surface;
        /**
         * @brief OpenGL context service for GL-family renderers, otherwise null.
         *
         * The pointer is non-owning; the platform outlives every renderer. GL renderers use it
         * with @ref RendererSurfaceInfo::windowId and never receive an `IPlatformWindow` or a
         * native window-toolkit type.
         */
        CNA::Platform::IPlatformGlContext* glContext = nullptr;
        /**
         * @brief Vulkan presentation-surface service for Vulkan-family renderers, otherwise null.
         *
         * The pointer is non-owning; the platform outlives the renderer. The renderer identifies
         * its target only by @ref RendererSurfaceInfo::windowId.
         */
        CNA::Platform::IPlatformVulkanSurface* vulkanSurface = nullptr;
        /**
         * @brief CPU-frame presentation service for raster renderers, otherwise null.
         *
         * The pointer is non-owning. GraphicsDevice keeps the presenter alive until after its
         * renderer has been destroyed, and the presenter in turn remains bound to @ref surface.
         */
        CNA::Platform::IPlatformSurfacePresenter* surfacePresenter = nullptr;
        /// Virtual (game-logic) resolution the renderer should present at. A renderer maps this
        /// coordinate space onto the actual platform surface according to presentationMode.
        /// 0 means "unset"; the renderer should ignore logical presentation.
        int virtualWidth = 0;
        int virtualHeight = 0;
        /// Presentation/scaling policy. Default is FixedHeightDynamicWidth:
        /// keeps preferred height fixed and derives logical width from the
        /// actual surface aspect ratio, matching XNA/Windows Phone behaviour.
        CnaPresentationMode presentationMode = CnaPresentationMode::FixedHeightDynamicWidth;
        /// When false, the EasyGL renderer will not keep CPU-side copies of
        /// texture pixels or vertex/index data and will not register resources
        /// with the ResourceRegistry. This eliminates the per-texture CPU
        /// shadow copy overhead at the cost of making GL context-loss recovery
        /// impossible. Safe on desktop where context loss never occurs.
        bool contextRecoveryEnabled = true;
        /// Desired multisample count (1 = no MSAA, 4 = 4× MSAA, etc.).
        /// Renderers that do not support MSAA silently clamp to 1.
        int multiSampleCount = 1;
        /// Swap interval for vertical sync.
        ///   0 = immediate (no VSync)
        ///   1 = wait for 1 vertical retrace (VSync, default)
        ///   2 = wait for 2 vertical retraces (half refresh rate)
        /// Corresponds to PresentInterval: Default/One→1, Two→2, Immediate→0.
        int swapInterval = 1;
        /// CNAEXT (plans/plan_dx9.md D9-30). Requested back-buffer pixel format -- raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::SurfaceFormat, avoiding coupling this
        /// renderer-agnostic header to the XNA namespace (mirrors CreateTexture3D's own
        /// surfaceFormat int convention). Renderers that don't need real format fidelity (every
        /// existing renderer except D3D9, whose goal is XNA authenticity rather than parity) may
        /// ignore this and keep hardcoding their own default, exactly as before this field existed.
        int backBufferFormat = 0;  // SurfaceFormat::Color
        /// CNAEXT (plans/plan_dx9.md D9-30). Requested depth/stencil format -- raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::DepthFormat. See backBufferFormat's own doc for
        /// the int-ordinal convention and the same "existing renderers may ignore this" note.
        int depthStencilFormat = 0;  // DepthFormat::None
        /// CNAEXT (plans/plan_dx9.md D9-30). Whether the game requested exclusive fullscreen. Existing
        /// renderers that already have their own fullscreen handling via the SDL window itself may
        /// continue to ignore this field exactly as before it existed.
        bool isFullScreen = false;
        /// CNAEXT (plans/plan_dx9.md D9-30/D9-32). Requested Microsoft::Xna::Framework::Graphics::
        /// GraphicsProfile ordinal (Reach=0, HiDef=1). Only D3D9 can honestly enforce this today
        /// (a real D3DCAPS9 to consult) -- see plans/plan_dx9.md's "CNA's divergences from XNA 4.0",
        /// Divergence 3. Every other renderer's GraphicsAdapter::IsProfileSupported() keeps its
        /// existing, honest `return true;` and may ignore this field.
        int graphicsProfile = 0;  // GraphicsProfile::Reach
        /// CNAEXT (plans/plan_dx9.md D9-34). Callback a renderer may invoke to report a REAL,
        /// driver-triggered device lifecycle event back to GraphicsDevice (which raises the
        /// corresponding DeviceLost/DeviceResetting/DeviceReset XNA event). Null by default; nine
        /// of the ten renderers never call it -- only a renderer that can genuinely lose its device
        /// the way XNA's own D3D9-based runtime could (i.e. D3D9) has any reason to. A renderer
        /// must never invoke this synchronously from within its own constructor (the
        /// GraphicsDevice that would receive it has not finished constructing yet).
        std::function<void(RendererDeviceEvent)> deviceEventCallback;
    };

    // plans/plan_runtimerenderer.md design decision 4: the renderer factory is NOT declared here.
    //
    // It used to be -- declared once in this header and defined once per renderer family with an
    // identical signature, which is exactly why two renderer archives could never link into the
    // same binary. Each family now declares and defines
    // CNA::Internal::Renderers::<Family>::CreateGraphicsRenderer in its own namespace, and
    // GraphicsRendererRegistry reaches it through GraphicsRendererDescriptor::create.
    //
    // The declaration outlived the change and was removed only later, because a leftover
    // declaration is not inert: it is a name any family's own factory call has to be qualified
    // against (EasyGL's own suite carried a comment about the resulting ambiguity), and it is a
    // standing invitation for a newly added family to define the colliding symbol and appear to
    // work -- which PIXIJS did, undetected until a build tried to link it beside another
    // renderer. scripts/check_runtime_renderer_discipline.py now fails on such a definition.
    //
    // The creation contract itself (GraphicsRendererCreateArgs above) deliberately contains only
    // platform value types; renderer-family-specific native API work starts behind that boundary.
}
