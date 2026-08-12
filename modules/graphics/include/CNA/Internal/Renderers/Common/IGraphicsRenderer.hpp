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
#include "CNA/Unsupported3DGraphicsCallBehavior.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/GraphicsCapability.hpp"

struct SDL_Window;

namespace Microsoft::Xna::Framework::Graphics { class Effect; }

namespace CNA::Internal::Renderers
{
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
    class IOcclusionQueryRenderer
    {
    public:
        virtual ~IOcclusionQueryRenderer() = default;
        virtual void Begin() = 0;
        virtual void End()   = 0;
        [[nodiscard]] virtual bool IsComplete() const = 0;
        [[nodiscard]] virtual int  PixelCount() const = 0;
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
        /// RenderTarget2D::getDepthStencilFormatProperty() != DepthFormat::None). SDL_Renderer's
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
        // that create no cube render target at all (Software, SDL_Renderer, ASCII, Canvas, DIRECTX3, GDI)
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
        /// plan_graphics.md Task 863: binds a volume texture to the given sampler unit (0-based),
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
        /// BasicEffect/SkinnedEffect: real XNA `PreferPerPixelLighting` value (plan_dx9.md
        /// Divergence 1 / D9-81 item 1). When true, XNA selects a per-pixel-lit shader
        /// (`VSBasicPixelLighting*`/`PSBasicPixelLighting*`); when false (XNA's own default),
        /// it selects a per-vertex-lit shader instead. Renderers that generate both lighting
        /// families honour this (D3D9, D3D11, D3D12, WebGPU, Vulkan, bgfx, EasyGL, OpenGL4,
        /// Magnum, Diligent); fixed-function renderers evaluate lighting per vertex by construction; a
        /// renderer with neither renders per-pixel regardless of its value -- a known, tracked
        /// divergence from XNA's default, not fixed by adding this field alone. Only meaningful
        /// when `lightingEnabled` is true.
        bool preferPerPixelLighting = false;
        /// EnvironmentMapEffect: real XNA `specularEnabled` value (plan_dx9.md Divergence 1 /
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
        /// (plan_cnj.md CNB-58, Phase 13A).
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
        /// plan_cnj.md CNB-58 (Phase 13A): PbrEffect's normal map (tangent-space, RGB), or null.
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
        /// PbrEffect: metallic factor [0,1], multiplied with pbrMetallicRoughnessMap's B channel
        /// when bound (or used alone as a constant when it isn't).
        float pbrMetallicFactor = 1.0f;
        /// PbrEffect: roughness factor [0,1], multiplied with pbrMetallicRoughnessMap's G channel
        /// when bound (or used alone as a constant when it isn't).
        float pbrRoughnessFactor = 1.0f;
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

    class IGraphicsRenderer
    {
    public:
        virtual ~IGraphicsRenderer() = default;
        virtual void Clear(float r, float g, float b, float a) = 0;
        virtual void Present() = 0;
        virtual void GetViewportSize(int& width, int& height) = 0;
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
        /// Renderers that cannot change VSync at runtime (e.g. Vulkan) silently ignore this.
        virtual void SetSwapInterval(int /*interval*/) {}
        /// Task 902: reconfigures the backbuffer's MSAA sample count in place, called from
        /// GraphicsDevice::Reset() so GraphicsDeviceManager.PreferMultiSampling (and any other
        /// preference-driven MultiSampleCount change) actually reaches the renderer instead of
        /// being silently ignored after construction. Returns the actual, device-clamped sample
        /// count applied (0 = no MSAA). Default: unsupported -- renderers that cannot change this
        /// post-construction report back whatever GetMultiSampleCount() already is.
        virtual int ApplyMultiSampleCount(int /*requestedMultiSampleCount*/) { return GetMultiSampleCount(); }
        /// CNAEXT (plan_dx9.md D9-30/D9-33, found empirically). Reconfigures the renderer's tracked
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
        /** @brief Depth/stencil counterpart of GetAppliedBackBufferFormatEXT(). */
        [[nodiscard]] virtual int GetAppliedDepthStencilFormatEXT(int requestedFormat) const
        {
            return requestedFormat;
        }
        /// Returns the backbuffer's actual (device-clamped) MSAA sample count; 0 if none/unsupported.
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /// Converts a point from SDL window-coordinate space to logical (virtual) game
        /// coordinates. A renderer whose drawable pixel size differs from SDL_GetWindowSize()
        /// must account for that density internally. Returns true on success. Default: no-op.
        virtual bool TransformWindowToLogical(float windowX, float windowY,
                                              float& logX, float& logY) const { return false; }
        /// Converts a point from logical (virtual) game coordinates to SDL window-coordinate
        /// space — the inverse of TransformWindowToLogical. Returns true on success. Default:
        /// no-op (returns false), i.e. window == logical (no scaling). Used by Mouse::SetPosition
        /// to place the OS cursor correctly on a scaled/letterboxed window.
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

        /// Compiles a shader program from GLSL/HLSL source strings.
        /// Returns nullptr on renderers that do not support programmable shaders.
        virtual std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                                      const std::string& fragSrc)
        { return nullptr; }

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
         * (SDL_Renderer) never has a depth/stencil buffer regardless of what was requested and
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
        /// Creates a 32-bit index buffer. Default delegates to CreateIndexBuffer16 for
        /// renderers that do not yet support 32-bit indices.
        virtual std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity)
        {
            return CreateIndexBuffer16(index_capacity);
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
        /// returns true except for multi-stream input. Every renderer with a narrower contract --
        /// including no-renderer, 2D-only, fixed-function, experimental, or device-dependent
        /// capability gaps -- must override the applicable entries truthfully.
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
            return true;
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

        // ---- Window → renderer registry ----
        // Renderers that implement TransformWindowToLogical register themselves here
        // so that SdlInputBridge can map physical mouse coordinates to logical ones
        // even for renderers that have no SDL_Renderer (e.g. EasyGL).

        static void RegisterForWindow(SDL_Window* window, IGraphicsRenderer* renderer)
        {
            windowRegistry()[window] = renderer;
        }
        static void UnregisterForWindow(SDL_Window* window)
        {
            windowRegistry().erase(window);
        }
        static IGraphicsRenderer* GetForWindow(SDL_Window* window)
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

        static std::unordered_map<SDL_Window*, IGraphicsRenderer*>& windowRegistry()
        {
            static std::unordered_map<SDL_Window*, IGraphicsRenderer*> reg;
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
     * @brief CNAEXT (plan_dx9.md D9-34). Real, driver-triggered device lifecycle events a renderer
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
     * @brief Arguments for creating a graphics renderer.
     * Currently minimal, but allows for easier extension.
     */
    struct GraphicsRendererCreateArgs
    {
        // TODO: SDL dependency should be abstracted later
        SDL_Window* window = nullptr;
        /// Virtual (game-logic) resolution the renderer should present at.
        /// SDL_SetRenderLogicalPresentation will be set to this size so that
        /// the game always draws in its own coordinate space and the renderer
        /// scales to the real surface automatically.
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
        /// CNAEXT (plan_dx9.md D9-30). Requested back-buffer pixel format -- raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::SurfaceFormat, avoiding coupling this
        /// renderer-agnostic header to the XNA namespace (mirrors CreateTexture3D's own
        /// surfaceFormat int convention). Renderers that don't need real format fidelity (every
        /// existing renderer except D3D9, whose goal is XNA authenticity rather than parity) may
        /// ignore this and keep hardcoding their own default, exactly as before this field existed.
        int backBufferFormat = 0;  // SurfaceFormat::Color
        /// CNAEXT (plan_dx9.md D9-30). Requested depth/stencil format -- raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::DepthFormat. See backBufferFormat's own doc for
        /// the int-ordinal convention and the same "existing renderers may ignore this" note.
        int depthStencilFormat = 0;  // DepthFormat::None
        /// CNAEXT (plan_dx9.md D9-30). Whether the game requested exclusive fullscreen. Existing
        /// renderers that already have their own fullscreen handling via the SDL window itself may
        /// continue to ignore this field exactly as before it existed.
        bool isFullScreen = false;
        /// CNAEXT (plan_dx9.md D9-30/D9-32). Requested Microsoft::Xna::Framework::Graphics::
        /// GraphicsProfile ordinal (Reach=0, HiDef=1). Only D3D9 can honestly enforce this today
        /// (a real D3DCAPS9 to consult) -- see plan_dx9.md's "CNA's divergences from XNA 4.0",
        /// Divergence 3. Every other renderer's GraphicsAdapter::IsProfileSupported() keeps its
        /// existing, honest `return true;` and may ignore this field.
        int graphicsProfile = 0;  // GraphicsProfile::Reach
        /// CNAEXT (plan_dx9.md D9-34). Callback a renderer may invoke to report a REAL,
        /// driver-triggered device lifecycle event back to GraphicsDevice (which raises the
        /// corresponding DeviceLost/DeviceResetting/DeviceReset XNA event). Null by default; nine
        /// of the ten renderers never call it -- only a renderer that can genuinely lose its device
        /// the way XNA's own D3D9-based runtime could (i.e. D3D9) has any reason to. A renderer
        /// must never invoke this synchronously from within its own constructor (the
        /// GraphicsDevice that would receive it has not finished constructing yet).
        std::function<void(RendererDeviceEvent)> deviceEventCallback;
    };

    // Factory function to be implemented by each renderer
    // INTERNAL API - SDL dependency should be abstracted later
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);
}
