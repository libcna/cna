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
#include <array>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/GraphicsCapability.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace Microsoft::Xna::Framework::Graphics { class Effect; }

namespace CNA::Internal::Backends
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
     * @brief Backend-neutral BlendState output-merger write state (REMED-GFX-077).
     *
     * Carries the two BlendState fields that are NOT expressible through the six blend
     * factor/function ordinals of ApplyBlendState: the four per-render-target colour write
     * masks (`BlendState.ColorWriteChannels` / `ColorWriteChannels1` / `ColorWriteChannels2` /
     * `ColorWriteChannels3`, MRT slots 0..3) and the coverage sample mask
     * (`BlendState.MultiSampleMask`). Kept as a small POD appended to ApplyBlendState so every
     * backend's existing factor/function→native mapping is untouched; only this genuinely-new
     * output state is added. `colorWriteChannels[i]` holds the raw XNA `ColorWriteChannels` int
     * (bit0=R, bit1=G, bit2=B, bit3=A; 15 = All) — a bit layout identical to the native colour
     * masks of Vulkan/D3D9/D3D11/D3D12/WebGPU/SDL_GPU/bgfx, so the value is usable directly on
     * mask-capable backends and via the ColorWriteHas* helpers on boolean backends.
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
     * @brief Backend handle for a vertex buffer.
     *
     * Owned by `Microsoft::Xna::Framework::Graphics::VertexBuffer`. The
     * concrete type is backend-specific (e.g. an OpenGL VBO+VAO pair) and
     * intentionally hidden from the public XNA-like API.
     *
     * @note Status: IMPLEMENTED for VertexPositionColor (stride 16),
     *       VertexPositionTexture (20), VertexPositionColorTexture (24),
     *       and VertexPositionNormalTexture (32) on the EasyGL backend.
     */
    class IVertexBufferBackend
    {
    public:
        virtual ~IVertexBufferBackend() = default;
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
         * Backends may use @p options to select a more efficient upload path
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
         * @brief Supplies the caller's own complete vertex declaration so a backend can bind
         * genuinely custom vertex layouts generically instead of only the fixed set of
         * byte-strides its 3D draw path otherwise recognizes.
         *
         * Called immediately before `SetData()`/`SetDataWithOptions()`.  This is deliberately a
         * required backend operation: each implementation must make an explicit decision to use
         * or ignore a declaration, so a newly added backend cannot silently discard one.
         *
         * @param vertexDeclaration Full declaration, including stride and elements in declaration
         *                          order.
         */
        virtual void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) = 0;

        [[nodiscard]] virtual int GetVertexCount() const = 0;
    };

    /**
     * @brief Backend handle for a 16- or 32-bit index buffer.
     */
    class IIndexBufferBackend
    {
    public:
        virtual ~IIndexBufferBackend() = default;
        virtual void SetData16(const void* data, int index_count) = 0;
        virtual void SetData32(const void* data, int index_count)
        {
            throw std::runtime_error("SetData32 not supported by this backend");
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
     * @brief Backend handle for a GPU occlusion query.
     *
     * On OpenGL ES 3.0 (EasyGL), uses GL_ANY_SAMPLES_PASSED — so PixelCount()
     * returns 0 (no visible samples) or 1 (at least one visible sample), not an
     * exact pixel count. This matches FNA's behaviour on GLES3.
     */
    class IOcclusionQueryBackend
    {
    public:
        virtual ~IOcclusionQueryBackend() = default;
        virtual void Begin() = 0;
        virtual void End()   = 0;
        [[nodiscard]] virtual bool IsComplete() const = 0;
        [[nodiscard]] virtual int  PixelCount() const = 0;
    };

    /** @brief Backend interface for a cube map texture. */
    class ITextureCubeBackend
    {
    public:
        virtual ~ITextureCubeBackend() = default;
        /**
         * @brief Uploads raw RGBA8 pixels into a sub-rectangle of a single cube face.
         *
         * REMED-GFX-135, the write-side counterpart of `GetData`'s contract below. Returns **true
         * only when the COMPLETE requested region was stored**, and false when this backend stored
         * nothing. There is no third state: an implementation that cannot store the region, or that
         * fails part-way through, must return false rather than reporting a partial write as
         * success.
         *
         * There is deliberately no default body. `void` was the whole defect: it left an
         * implementation no way to say "I stored nothing", so `TextureCube::SetData`'s
         * `if (backend_) backend_->SetData(...)` returned normally after an upload that had been
         * validated, traced and discarded (Headless), dropped for an unstored mip level (Software),
         * or never attempted at all. The shared layer now raises `System::NotSupportedException` on
         * false, so the one thing a backend can never do is accept data it does not keep.
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
         * @return True if the whole region was stored; false if this backend stored nothing.
         */
        [[nodiscard]] virtual bool SetData(int face, int level, int x, int y, int w, int h,
                                           const void* data, int dataLength) = 0;
        /**
         * @brief Reads back raw RGBA8 pixels from a sub-rectangle of a single cube face.
         *
         * REMED-GFX-130, extending REMED-GFX-127's contract to this interface. Returns **true only
         * when the complete requested region was written into @p data**, and false when this
         * backend performed no readback at all. There is no third state: an implementation that
         * fails part-way through, or that cannot complete the transfer, must return false rather
         * than reporting a partially written buffer as success.
         *
         * The default is `false` -- "this backend has no cube-map readback" -- because a silent
         * no-op default was worse than useless here. `TextureCube::GetData` hands this method a
         * scratch buffer it zero-initialized itself and converts the result for the caller, so a
         * no-op default did not leave the caller's destination untouched: it fabricated a complete,
         * uniformly transparent-black cube face that passed both "did GetData write anything?" and
         * any expectation whose content happened to be transparent black. The shared layer now
         * converts only on `true` and raises `System::NotSupportedException` on `false`, so the one
         * thing an unimplemented backend can never do is answer with content it never read.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; exactly w * h * 4.
         * @return True if the whole region was written; false if this backend read nothing back.
         */
        [[nodiscard]] virtual bool GetData(int face, int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
        {
            (void)face; (void)level; (void)x; (void)y; (void)w; (void)h;
            (void)data; (void)dataLength;
            return false;
        }
        /// Binds this cube map to the currently active GL texture unit. No-op on non-GL backends.
        virtual void BindGL() const {}
        /// Shares a reference to the CPU pixel buffer owned by TextureCube::cpuPixels_[face] for
        /// one cube face's level 0. Mirrors ITextureBackend::ShareCpuPixels()'s own purpose
        /// exactly (OpenGL-style backend context-loss restoration) -- default no-op; only OPENGL1
        /// currently implements it.
        virtual void ShareCpuPixels(int /*face*/, std::shared_ptr<std::vector<uint8_t>> /*pixels*/) {}
    };

    /** @brief Backend interface for a 3D (volume) texture. */
    class ITexture3DBackend
    {
    public:
        virtual ~ITexture3DBackend() = default;
        /**
         * @brief Uploads raw RGBA8 voxels into a sub-volume of the given mip level.
         *
         * REMED-GFX-135. Identical contract to `ITextureCubeBackend::SetData` -- see its
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
         * @return True if the whole box was stored; false if this backend stored nothing.
         */
        [[nodiscard]] virtual bool SetData(int level, int x, int y, int z,
                                           int w, int h, int depth,
                                           const void* data, int dataLength) = 0;
        /**
         * @brief Reads back raw RGBA8 voxels from a sub-volume of the given mip level.
         *
         * REMED-GFX-130. Identical contract to `ITextureCubeBackend::GetData` above -- see its
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
         * @return True if the whole box was written; false if this backend read nothing back.
         */
        [[nodiscard]] virtual bool GetData(int level, int x, int y, int z,
                                           int w, int h, int depth,
                                           void* data, int dataLength) const
        {
            (void)level; (void)x; (void)y; (void)z; (void)w; (void)h; (void)depth;
            (void)data; (void)dataLength;
            return false;
        }
        /// Binds this volume texture to the currently active GL texture unit. No-op on non-GL backends.
        virtual void BindGL() const {}
    };

    /**
     * Backend texture handle. Shared lifetime identity lets bounded consumers retain weak
     * bindings without keeping disposed public Texture2D resources alive or storing raw pointers.
     */
    class ITextureBackend : public std::enable_shared_from_this<ITextureBackend>
    {
    public:
        virtual ~ITextureBackend() = default;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Texture* GetNativeTexture() const = 0;
        /// Replaces full level-0 texture pixels in-place. stride = row bytes (width * 4 for RGBA).
        virtual void UpdatePixels(const uint8_t* rgba, int stride) {}
        /// Uploads a specific mip level. levelW/levelH are the dimensions at that level.
        virtual void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) {}
        /// Binds the underlying GL texture handle (no-op on non-GL backends).
        virtual void BindGL() const {}
        /// Shares a reference to the CPU pixel buffer owned by Texture2D::cpuPixels_.
        /// The backend stores this reference for OpenGL context-loss restoration instead
        /// of keeping its own duplicate copy of the pixel data.
        virtual void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> /*pixels*/) {}
        /**
         * @brief Reads back raw RGBA8 pixels from a sub-rectangle of the given mip level.
         *
         * REMED-GFX-127. Returns **true only when the complete requested region was written into
         * @p data**, and false when this backend performed no readback at all. There is no third
         * state: an implementation that fails part-way through, or that cannot complete the
         * transfer, must return false rather than reporting a partially written buffer as success.
         *
         * The default is `false` — "this backend has no render-target/texture readback" — because a
         * silent no-op default was worse than useless here. `Texture2D::GetData` hands this method a
         * scratch buffer it zero-initialized itself and converts the result for the caller, so a
         * no-op default did not leave the caller's destination untouched: it fabricated a complete,
         * uniformly transparent-black frame that passed both "did GetData write anything?" and any
         * expectation whose content happened to be transparent black. The shared layer now converts
         * only on `true` and raises `System::NotSupportedException` on `false`, so the one thing an
         * unimplemented backend can never do is answer with content it never read.
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
         * @return True if the whole region was written; false if this backend read nothing back.
         */
        [[nodiscard]] virtual bool GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
        {
            (void)level; (void)x; (void)y; (void)w; (void)h; (void)data; (void)dataLength;
            return false;
        }
    };

    /// Backend handle for a 2D render target (off-screen FBO on EasyGL).
    class IRenderTargetBackend : public ITextureBackend
    {
    public:
        /// Bind the FBO so subsequent draws go to this render target.
        virtual void BindAsRenderTarget() = 0;
        /// Unbind and restore the default framebuffer (back buffer).
        virtual void UnbindAsRenderTarget() = 0;
        /// Returns the native GL color texture handle; returns 0 on non-GL backends.
        [[nodiscard]] virtual unsigned int GetColorGLHandle() const { return 0; }
        /// Returns the actual (device-clamped) multisample count this target was created
        /// with; 0 if none/not supported by this backend. Matches FNA's semantics where
        /// RenderTarget2D.MultiSampleCount reflects the real clamped value, not the raw
        /// constructor request (FNA3D_GetMaxMultiSampleCount).
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /// Returns whether this specific target instance actually has a real depth-stencil
        /// buffer backing it, as opposed to merely being requested via DepthFormat at
        /// construction time. Most backends honor whatever DepthFormat was requested, so the
        /// default mirrors that (via @p depthFormatWasRequested, computed by the caller from
        /// RenderTarget2D::getDepthStencilFormatProperty() != DepthFormat::None). SDL_Renderer's
        /// 2D-only render targets never allocate real depth-buffer storage regardless of what
        /// format was requested, and overrides this to always return false (Task 708).
        [[nodiscard]] virtual bool HasRealDepthBuffer(bool depthFormatWasRequested) const { return depthFormatWasRequested; }
    };

    /// Backend handle for a cube-map render target.
    /// Each face can be activated independently for rendering.
    /// Inherits ITextureCubeBackend so RenderTargetCube can share a single GPU image
    /// with its TextureCube base class (same pattern as IRenderTargetBackend : ITextureBackend).
    class IRenderTargetCubeBackend : public ITextureCubeBackend
    {
    public:
        virtual ~IRenderTargetCubeBackend() = default;
        /// Returns the width/height of each cube face in pixels.
        [[nodiscard]] virtual int GetSize() const = 0;
        /// Activates face @p face (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z) as the draw target.
        virtual void BindAsRenderTargetFace(int face) = 0;
        /// Unbind and restore the default framebuffer.
        virtual void UnbindAsRenderTarget() = 0;
        /// Returns the underlying GL texture handle so the cube map can be sampled.
        [[nodiscard]] virtual unsigned int GetGLHandle() const { return 0; }
        /// See IRenderTargetBackend::GetMultiSampleCount.
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /// Cube equivalent of IRenderTargetBackend::HasRealDepthBuffer.
        [[nodiscard]] virtual bool HasRealDepthBuffer(bool depthFormatWasRequested) const
        {
            return depthFormatWasRequested;
        }

        /**
         * @brief Reports that this backend cannot upload CPU pixels into a rendered cube face.
         *
         * REMED-GFX-135. This body used to be `{}` -- an empty override that made
         * `RenderTargetCube::SetData` (inherited from `TextureCube`) return normally on every
         * backend while storing nothing, which is exactly the accept-and-discard this finding
         * removes, reached through inheritance instead of a null backend. `false` makes the shared
         * layer raise `System::NotSupportedException` instead.
         *
         * EasyGL overrides this with a real upload into the shared GL cube texture; the other
         * render-target cube backends inherit this refusal, matching `GetData`'s own default
         * immediately below.
         *
         * @return Always false.
         */
        [[nodiscard]] bool SetData(int /*face*/, int /*level*/, int /*x*/, int /*y*/, int /*w*/,
                                   int /*h*/, const void* /*data*/, int /*dataLength*/) override
        {
            return false;
        }

        // ITextureCubeBackend::GetData is deliberately NOT re-declared here: a render-target cube
        // inherits the same `return false` default, which means "this backend cannot read a
        // rendered cube face back to the CPU" and makes `TextureCube::GetData` raise
        // System::NotSupportedException (REMED-GFX-130).
        //
        // REMED-GFX-134 implemented it on every backend that owns a rendered cube resource:
        // EasyGL, Vulkan, Bgfx, D3D9, D3D11 and D3D12 joined SdlGpu and WebGPU, each reusing the
        // mechanism its plain-TextureCube sibling already uses in the same file plus that
        // backend's own rendered-face specifics -- REMED-GFX-067's `originBottomLeft` row
        // normalization on GL and bgfx, the MSAA resolve, and the deferred-draw flush a
        // still-bound or not-yet-presented target needs (REMED-GFX-074/075). The public row order
        // is the one `RenderTarget2D::GetData` already established: top row first.
        //
        // Headless keeps the inherited refusal because it rasterizes nothing, and the backends
        // that create no cube render target at all (Software, SDL_Renderer, ASCII, Canvas, DX3)
        // never reach this class -- `GraphicsDevice::SetRenderTargets` refuses to bind one and
        // `TextureCube::GetData` refuses a null backend one step earlier. Every remaining boundary
        // (a multisampled or mipped cube target on bgfx, a mip level D3D9 never allocated, WebGPU's
        // mipMap=true refusal) is likewise a `false`, never invented content.
    };

    /**
     * @brief Backend-neutral description of one normalized render-target attachment.
     *
     * REMED-GFX-096 replaces the former plural handoff (`IRenderTargetBackend*[]`), which could
     * express only RenderTarget2D and therefore discarded both RenderTargetCube type and face.
     * This value keeps one slot's resource kind, concrete backend, selected subresource, extent,
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
            IRenderTargetBackend* target, int arraySlice, int width, int height,
            int appliedMultiSampleCount)
        {
            return RenderTargetBindingDescriptor(
                Type::RenderTarget2D, target, nullptr, arraySlice, 0,
                width, height, appliedMultiSampleCount);
        }

        static RenderTargetBindingDescriptor ForRenderTargetCubeFace(
            IRenderTargetCubeBackend* target, int face, int size,
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
        [[nodiscard]] IRenderTargetBackend* GetRenderTarget2D() const
        {
            return renderTarget2D_;
        }
        [[nodiscard]] IRenderTargetCubeBackend* GetRenderTargetCube() const
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
            Type type, IRenderTargetBackend* renderTarget2D,
            IRenderTargetCubeBackend* renderTargetCube,
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
        IRenderTargetBackend* renderTarget2D_;
        IRenderTargetCubeBackend* renderTargetCube_;
        int arraySlice_;
        int cubeFace_;
        int width_;
        int height_;
        int appliedMultiSampleCount_;
    };

    /// Backend handle for a compiled shader program (vertex + fragment).
    /// Created via IGraphicsBackend::CreateEffectBackend().
    class IEffectBackend
    {
    public:
        virtual ~IEffectBackend() = default;
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
        virtual void BindTexture(int unit, ITextureBackend* texture) {}
        /// Task 1081: binds a cube texture to the given sampler unit (0-based), for a custom
        /// shader that declares a `samplerCube` uniform (e.g. a reflection map). Separate from
        /// `BindTexture()` since `ITextureCubeBackend` is its own interface, not a subtype of
        /// `ITextureBackend` -- GL itself allows a 2D and a cube texture bound to the same unit
        /// simultaneously, since they occupy distinct binding targets; the shader's own sampler
        /// type (`sampler2D` vs `samplerCube`) determines which one is actually sampled.
        virtual void BindTextureCube(int unit, ITextureCubeBackend* texture) {}
        /// plan_graphics.md Task 863: binds a volume texture to the given sampler unit (0-based),
        /// for a custom shader that declares a `sampler3D` uniform. Same reasoning as
        /// `BindTextureCube()` above -- `ITexture3DBackend` is its own interface, not a subtype of
        /// `ITextureBackend`, and GL allows a 2D/cube/3D texture bound to the same unit
        /// simultaneously since each occupies a distinct binding target; the shader's own sampler
        /// type (`sampler2D`/`samplerCube`/`sampler3D`) determines which one is actually sampled.
        virtual void BindTexture3D(int unit, ITexture3DBackend* texture) {}
    };

    class ISpriteBatchBackend
    {
    public:
        virtual ~ISpriteBatchBackend() = default;
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
         * Default: no-op (backend keeps whatever wrap mode the texture was created with, i.e.
         * Clamp on EasyGL).
         *
         * @param addressU Raw TextureAddressMode int value for U (0=Wrap, 1=Clamp, 2=Mirror).
         * @param addressV Raw TextureAddressMode int value for V (0=Wrap, 1=Clamp, 2=Mirror).
         */
        virtual void SetSamplerAddressMode(int /*addressU*/, int /*addressV*/) {}
        virtual void Draw(const ITextureBackend& texture, float x, float y) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color) = 0;
        virtual void Draw(const ITextureBackend& texture,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth) = 0;
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
     * A backend must read these fields, never the public binding state: `GraphicsDevice`'s
     * `currentVertexBuffers_` is mutable between a deferred enqueue and its replay.
     */
    struct GpuVertexStreamBinding
    {
        /// Public binding slot -- the stream's index in the `SetVertexBuffers` array. Also the
        /// native input slot on every API that has one (D3D11/D3D12 `InputSlot`, Vulkan
        /// `binding`, WebGPU vertex-buffer index, bgfx stream index, SDL_GPU `slot`).
        int slot = 0;

        /// Backend resource for this stream. Never null for `slot < vertexStreamCount`. Only
        /// valid for the duration of the `Draw*PrimitivesEx` call that carries it -- a deferred
        /// backend must copy the concrete handle, exactly as it already does for `instanceVb`.
        const IVertexBufferBackend* buffer = nullptr;

        /// This stream's own `VertexDeclaration` stride, in bytes. Never stream 0's.
        int strideInBytes = 0;

        /// Byte offset at which this stream's declaration begins inside the *combined* layout --
        /// the running sum of the strides of the per-vertex streams before it. CNA's backends
        /// derive their native input elements from a byte stride (`InputElementsForStride` and
        /// the equivalent per-backend tables), whose element offsets are combined-layout offsets;
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

        /// Vertex elements the stream's buffer holds, for backends that must bound a native range.
        int vertexCount = 0;
    };

    /// XNA 4.0 HiDef's `SetVertexBuffers` limit, and therefore the fixed capacity of a draw's
    /// stream list. Matches `GraphicsDevice::kMaxVertexBufferBindings`.
    inline constexpr int kMaxVertexStreams = 16;

    /**
     * @brief Per-draw effect parameters forwarded from the XNA effect layer
     *        to the graphics backend.
     *
     * Populated via Effect::FillGpuDrawParams() before each draw call so the
     * backend can select and configure the appropriate shader variant.
     */
    struct GpuDrawParams
    {
        const ITextureBackend*     texture0 = nullptr;      ///< Texture unit 0 (diffuse), or null
        const ITextureBackend*     texture1 = nullptr;      ///< Texture unit 1 (DualTextureEffect second layer), or null
        const ITextureCubeBackend* envMap   = nullptr;      ///< Cube map for EnvironmentMapEffect, or null
        float diffuseColor[4]  = {1,1,1,1};                ///< RGBA 0..1
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
        /// it selects a per-vertex-lit shader instead. Backends that generate both lighting
        /// families honour this (D3D9, D3D11, D3D12, WebGPU, Vulkan, bgfx, EasyGL, OpenGL4,
        /// Magnum, Diligent); fixed-function backends evaluate lighting per vertex by construction; a
        /// backend with neither renders per-pixel regardless of its value -- a known, tracked
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
        /// other two findings). No backend currently reads this field.
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
        /// Backends compute `keep = 1 - saturate(dot(pos, fogVector))` (their "keep" convention).
        /// All-zero when fog is disabled (dot→0→keep=1, a true no-op) and `{0,0,0,1}` for the
        /// degenerate `fogStart==fogEnd` case (dot→1→keep=0, fully fogged), matching FNA exactly.
        /// Populated by every fog-capable stock effect's FillGpuDrawParams(). This is the
        /// authoritative representation for every fog-capable stock-effect backend.
        float fogVector[4]    = {0, 0, 0, 0};
        bool textureEnabled      = false;
        bool vertexColorEnabled  = true;
        bool lightingEnabled     = false;
        /// When true the backend selects a two-sampler DualTexture shader variant.
        bool dualTexture         = false;
        /// When true the backend selects a cube-map env-mapping shader variant.
        bool envMapping          = false;
        /// When true the backend selects the skinning shader variant.
        bool skinned             = false;
        /// When true the backend selects the PbrEffect (metallic-roughness BRDF) shader variant
        /// (plan_cnj.md CNB-58, Phase 13A).
        bool pbr                 = false;
        /// Number of instances to draw (1 = non-instanced).
        int instanceCount = 1;
        /// REMED-GFX-201/202: every active `VertexBufferBinding`, in public slot order, captured by
        /// value -- per-vertex and per-instance alike, on EVERY draw route. `vertexStreams[0]` is
        /// always the stream `Draw*PrimitivesEx`'s own `vb` argument refers to, so a backend that
        /// reads only `vb` still sees exactly what it saw before this field existed. Entries at or
        /// past `vertexStreamCount` are unset and must not be read.
        ///
        /// This is CNA's `FNA3D_VertexBufferBinding` array, and like FNA's own
        /// `PrepareVertexBindingArray` it is prepared identically for `DrawPrimitives`,
        /// `DrawIndexedPrimitives` and `DrawInstancedPrimitives`. An instance stream is simply an
        /// entry whose `instanceFrequency` is greater than zero; there is no second representation
        /// of "the instance buffer" anywhere.
        std::array<GpuVertexStreamBinding, kMaxVertexStreams> vertexStreams{};
        /// Active entries in `vertexStreams`. 0 only on the internal routes that bind no public
        /// buffer at all (SpriteBatch, `DrawUser*`); 1 for every single-stream draw.
        int vertexStreamCount = 0;
        /// Sum of the per-vertex (`instanceFrequency == 0`) streams' strides -- the byte stride of
        /// the *combined* vertex the shader sees, and therefore the key a stride-dispatched
        /// backend must select its input layout and shader variant with. Equals the single
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
        /// (`Effect::GetEffectBackendPtr()`) and the backend should bind/draw with it directly
        /// instead of selecting one of its own built-in stride-dispatched shaders. Null for every
        /// stock effect (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/
        /// `SkinnedEffect`) and for no-effect draws. Backends that don't implement this (non-EasyGL)
        /// safely ignore it, matching the established accepted-and-ignored pattern for other
        /// not-yet-backend-supported `GpuDrawParams` fields.
        IEffectBackend* customEffectBackend = nullptr;
        /// plan_cnj.md CNB-58 (Phase 13A): PbrEffect's normal map (tangent-space, RGB), or null.
        /// When null the surface normal from the vertex stream is used unperturbed.
        const ITextureBackend* pbrNormalMap = nullptr;
        /// PbrEffect: metallic-roughness map, glTF's own packing convention (G=roughness,
        /// B=metallic; R/A unused), or null (Metallic/RoughnessFactor alone are then the
        /// per-material constant values).
        const ITextureBackend* pbrMetallicRoughnessMap = nullptr;
        /// PbrEffect: emissive map (RGB), or null (EmissiveFactor alone is then constant).
        const ITextureBackend* pbrEmissiveMap = nullptr;
        /// PbrEffect: occlusion map (R channel, 1=fully lit .. 0=fully occluded), or null
        /// (no occlusion darkening applied).
        const ITextureBackend* pbrOcclusionMap = nullptr;
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
     * CNA's backends describe a vertex layout by its byte stride, so their element tables use
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
     * @brief The byte stride a backend must select its input layout and shader variant with.
     *
     * REMED-GFX-201: for a bound-buffer draw this is the sum of the per-vertex streams' strides,
     * which equals the one stream's own stride whenever a single buffer is bound. The internal
     * routes that stage their own temporary buffer -- `DrawUser*`, SpriteBatch,
     * `DrawColoredPrimitives` -- bind no public `VertexBufferBinding` at all and leave
     * `vertexStreamCount` at 0; they keep dispatching on @p fallbackStride, the stride of the one
     * buffer they built, so their behaviour is untouched by this feature.
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
     * backend that implements instancing at all currently binds exactly one, so more than one is
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
     * The single-stream fast path every backend already implements stays selected by this being
     * false, which it is for every draw CNA issued before REMED-GFX-201.
     */
    [[nodiscard]] inline bool HasMultipleVertexStreams(const GpuDrawParams& params)
    {
        return PerVertexStreamCount(params) > 1;
    }

    /**
     * @brief Whether this draw binds more per-instance streams than the single one every
     *        instancing backend already handles.
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
     * for the backend harnesses and examples that drive `DrawInstancedPrimitivesEx` directly with a
     * hand-built `GpuDrawParams` and would otherwise each hand-roll the same two entries.
     *
     * @param params                 Draw parameters to populate.
     * @param perVertexBuffer        Slot 0's buffer -- the same object passed as `vb`.
     * @param instanceBuffer         Slot 1's buffer.
     * @param instanceFrequency      Slot 1's `InstanceFrequency`; must be > 0.
     * @param perVertexStrideInBytes Slot 0's declaration stride, or 0 when the backend resolves it.
     * @param perVertexOffset        Slot 0's `VertexOffset`, in vertex elements.
     * @param instanceStrideInBytes  Slot 1's declaration stride, or 0 when the backend resolves it.
     * @param instanceOffset         Slot 1's `VertexOffset`, in vertex elements.
     */
    inline void SetInstancedVertexStreamsEXT(
        GpuDrawParams& params,
        const IVertexBufferBackend& perVertexBuffer,
        const IVertexBufferBackend& instanceBuffer,
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
     * @brief REMED-GFX-202: the deterministic rejection a backend that binds exactly one stream of
     *        each input rate performs instead of silently rendering from a subset of the array.
     *
     * `GraphicsDevice` already rejects both shapes for a backend that does not report
     * `GraphicsCapability::MultiStreamVertexInput`, so this never fires through the public API. It
     * exists because `Draw*PrimitivesEx` is a public interface method a harness may call with a
     * hand-built `GpuDrawParams`, and because a truncated binding array looks exactly like a
     * correct draw of the wrong data.
     *
     * @param params      The draw being dispatched.
     * @param backendName Name used in the message, e.g. "Vulkan".
     */
    inline void RejectUnsupportedStreamCombination(
        const GpuDrawParams& params, const char* backendName)
    {
        if (!HasMultipleVertexStreams(params) && !HasMultipleInstanceStreams(params))
            return;
        throw std::runtime_error(
            std::string(backendName) +
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

    class IGraphicsBackend
    {
    public:
        virtual ~IGraphicsBackend() = default;
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
        /// size at the window origin, matching every backend's behavior before this method
        /// existed. Only a backend implementing REAL Letterbox/Overscan/Stretch (a physical
        /// sub-rectangle that differs in size and/or is not at the window origin) needs to
        /// override this -- see OpenGL2GraphicsBackend's override for the reference
        /// implementation in this codebase.
        virtual void GetDefaultViewportRect(int& x, int& y, int& width, int& height)
        {
            x = 0;
            y = 0;
            GetViewportSize(width, height);
        }
        /// Updates the backend logical presentation size at runtime.
        /// Called by GraphicsDevice::SetVirtualResolution() when
        /// GraphicsDeviceManager::ApplyChanges() propagates a new
        /// PreferredBackBufferWidth/Height from the game.
        virtual void SetVirtualResolution(int width, int height) = 0;
        /// Updates the backend presentation/scaling mode at runtime.
        /// Called by GraphicsDevice when GraphicsDeviceManager::ApplyChanges() is used.
        virtual void SetPresentationMode(int mode) = 0;
        /// Updates the swap interval at runtime (0=immediate, 1=VSync, 2=half-rate).
        /// Backends that cannot change VSync at runtime (e.g. Vulkan) silently ignore this.
        virtual void SetSwapInterval(int /*interval*/) {}
        /// Task 902: reconfigures the backbuffer's MSAA sample count in place, called from
        /// GraphicsDevice::Reset() so GraphicsDeviceManager.PreferMultiSampling (and any other
        /// preference-driven MultiSampleCount change) actually reaches the backend instead of
        /// being silently ignored after construction. Returns the actual, device-clamped sample
        /// count applied (0 = no MSAA). Default: unsupported -- backends that cannot change this
        /// post-construction report back whatever GetMultiSampleCount() already is.
        virtual int ApplyMultiSampleCount(int /*requestedMultiSampleCount*/) { return GetMultiSampleCount(); }
        /// NOXNA (plan_dx9.md D9-30/D9-33, found empirically). Reconfigures the backend's tracked
        /// back-buffer format/depth-stencil format/fullscreen state in place, called from
        /// GraphicsDevice::Reset() alongside SetVirtualResolution()/ApplyMultiSampleCount() above --
        /// same "actually reach the backend instead of being silently ignored after construction"
        /// rationale as ApplyMultiSampleCount, for the GraphicsBackendCreateArgs::backBufferFormat/
        /// depthStencilFormat/isFullScreen fields specifically. Without this, a GraphicsDeviceManager
        /// that sets PreferredDepthStencilFormat AFTER the backend's initial construction (the
        /// common case: Game lazily constructs a GraphicsDevice with default PresentationParameters
        /// before GraphicsDeviceManager.ApplyChanges() ever runs) would silently keep the backend on
        /// its original construction-time format forever. Default: no-op -- every existing backend
        /// (parity, not authenticity, goals) may continue to ignore this exactly as before it
        /// existed; only a backend that honors real requested formats (D3D9) needs to act on it, and
        /// even then only needs to actually apply it on its own next natural resize/reset point
        /// (immediately reconstructing a device from inside this call is not required).
        virtual void UpdatePresentationFormatEXT(int /*backBufferFormat*/, int /*depthStencilFormat*/,
                                                  bool /*isFullScreen*/) {}
        /// Returns the backbuffer's actual (device-clamped) MSAA sample count; 0 if none/unsupported.
        [[nodiscard]] virtual int GetMultiSampleCount() const { return 0; }
        /// Converts a point from physical window coordinates to logical (virtual)
        /// game coordinates. Returns true on success. Default: no-op (returns false).
        virtual bool TransformWindowToLogical(float windowX, float windowY,
                                              float& logX, float& logY) const { return false; }
        /// Converts a point from logical (virtual) game coordinates to physical window
        /// coordinates — the inverse of TransformWindowToLogical. Returns true on success.
        /// Default: no-op (returns false), i.e. window == logical (no scaling). Used by
        /// Mouse::SetPosition to place the OS cursor correctly on a scaled/letterboxed window.
        virtual bool TransformLogicalToWindow(float logX, float logY,
                                              float& windowX, float& windowY) const { return false; }
        // TODO: SDL dependency should be abstracted later
        virtual SDL_Window* GetWindowInternal() const = 0;
        virtual SDL_Renderer* GetRendererInternal() const = 0;

        virtual std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) = 0;
        virtual std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() = 0;

        /// Reads the rendered backbuffer pixels for the given region into @p pixels (RGBA8).
        /// @p x, @p y are top-left in game coordinates; @p pixels must hold w*h*4 bytes.
        /// Default implementation throws — override in backends that support readback.
        virtual void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
        {
            throw std::runtime_error("ReadBackbuffer: not implemented in this backend");
        }

        /// Creates a backend occlusion query object. Returns nullptr on
        /// backends that do not support hardware occlusion queries.
        virtual std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() { return nullptr; }
        virtual std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) { return nullptr; }
        virtual std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) { return nullptr; }

        /// Creates an off-screen FBO-backed render target. Returns nullptr on
        /// backends that do not support render targets. `depthFormat` is the raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::DepthFormat (None=0, Depth16=1, Depth24=2,
        /// Depth24Stencil8=3), passed as `int` to avoid coupling this backend-agnostic header
        /// to the XNA namespace — mirrors CreateTexture3D/CreateTextureCube's `surfaceFormat`
        /// convention. EasyGL and Bgfx honor the exact requested format (None omits the
        /// depth/stencil attachment entirely); Vulkan always allocates a combined depth+stencil
        /// buffer using its device-wide format regardless of the exact value requested, since
        /// varying it per render target would require a depth-format-keyed render pass/pipeline
        /// cache (Vulkan render-pass-compatibility rules require matching attachment formats
        /// for the pipelines this backend currently shares across the backbuffer and every
        /// render target) — a real architectural change, tracked as Task 911 (Task 877).
        /// `mipMap` requests a full mip chain, auto-generated from level 0 when the target is
        /// unbound (matching FNA3D's OPENGL_ResolveTarget behavior) — all 3 backends implement
        /// this (Task 336/878/906). `multiSampleCount` requests a multisampled color (and depth,
        /// where honored) attachment, resolved into the sampleable texture when the target is
        /// unbound (same FNA3D resolve mechanism; all 3 backends implement this for
        /// RenderTarget2D — Task 337/878/879).
        virtual std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) { return nullptr; }

        /// Activates the given render target (binds its FBO). Pass nullptr to
        /// restore the default back buffer.
        virtual void SetRenderTarget2D(IRenderTargetBackend* rt) {}

        /// Creates a cube-map render target. Returns nullptr on backends that
        /// do not support cube map render targets. See CreateRenderTarget2D for `depthFormat`/
        /// `mipMap`/`multiSampleCount`.
        /// `preserveContents` (REMED-GFX-136) carries the public RenderTargetCube's own
        /// RenderTargetUsage down here, exactly as CreateRenderTarget2D's identically-placed
        /// parameter does — true means "when a face of this target is bound, load what is already
        /// in it", false means "the previous colour need not survive". Both public targets derive
        /// it from the single Microsoft::Xna::Framework::Graphics::
        /// RenderTargetUsagePreservesContentsEXT() mapping, so PlatformContents cannot mean one
        /// thing to the shared layer and another to a backend. Before this parameter existed a
        /// cube target's real usage stopped at RenderTargetCube's constructor and every backend
        /// had to invent an answer: Vulkan and WebGPU both invented "always discard", so a
        /// PreserveContents cube face was wiped on every bind cycle.
        virtual std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) { return nullptr; }

        /// Compiles a shader program from GLSL/HLSL source strings.
        /// Returns nullptr on backends that do not support programmable shaders.
        virtual std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                                      const std::string& fragSrc)
        { return nullptr; }

        /// Activates a specific face of a cube-map render target for rendering.
        /// Pass nullptr to restore the default back buffer.
        virtual void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face)
        {
            if (rt) rt->BindAsRenderTargetFace(face);
            else SetRenderTarget2D(nullptr);
        }

        /// Activates a normalized ordered render-target set. Every backend must explicitly
        /// consume or reject cube-face descriptors; there is deliberately no compatibility
        /// default that could flatten a cube to RenderTarget2D or face +X.
        /// Pass nullptr / count=0 to restore the default back buffer.
        virtual void SetRenderTargets(
            const RenderTargetBindingDescriptor* renderTargets, int count) = 0;

        // ---- Graphics state ----

        /// Applies a BlendState to the backend. Default: no-op.
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

        /// Applies a DepthStencilState to the backend. Default: no-op.
        virtual void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                            int depthFunc,
                                            bool stencilEnable, int stencilFunc,
                                            int stencilPass, int stencilFail, int stencilDepthFail,
                                            int stencilMask, int stencilWriteMask, int referenceStencil,
                                            bool twoSidedStencilMode,
                                            int ccwStencilFunc, int ccwStencilPass,
                                            int ccwStencilFail, int ccwStencilDepthFail) {}

        /// Applies a RasterizerState to the backend. Default: no-op.
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

        /// Sets the constant blend color used with the BlendFactor blend mode.
        /// Maps to glBlendColor on GL backends. Default: no-op.
        virtual void SetBlendFactor(float r, float g, float b, float a) {}

        /// Task 870/319: GraphicsDevice.ReferenceStencil is a real, independent device property
        /// (FNA3D_Get/SetReferenceStencil), analogous to BlendFactor above -- it must take effect
        /// immediately, standalone from a full DepthStencilState re-application. Default: no-op
        /// (backends that only apply ReferenceStencil as part of ApplyDepthStencilState's full
        /// state and don't yet cache it for standalone re-application silently ignore this).
        virtual void SetReferenceStencil(int /*value*/) {}

        /// Sets the scissor clip rectangle. Default: no-op.
        virtual void SetScissorRect(int x, int y, int w, int h) {}

        /// Sets the GPU viewport rectangle and depth range (Task 880). Default: no-op.
        virtual void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) {}

        // ---- 3D pipeline ----

        /**
         * @brief Whether this backend can maintain a real depth/stencil buffer at all, for the
         * default back buffer (as opposed to an explicit RenderTarget2D, which has its own
         * per-instance IRenderTargetBackend::HasRealDepthBuffer() query).
         *
         * Most backends are 3D-capable and honor whatever depth/stencil format the presentation
         * parameters request, so the default is `true`. A backend that is entirely 2D-only
         * (SDL_Renderer) never has a depth/stencil buffer regardless of what was requested and
         * overrides this to `false` — used by GraphicsDevice::Clear(ClearOptions, ...) to mask
         * DepthBuffer/Stencil out of a clear request instead of forwarding it to a
         * ClearColorDepthAndStencil()-style method this backend cannot honor.
         */
        [[nodiscard]] virtual bool SupportsDepthStencil() const { return true; }

        /**
         * Backend boundary used by common public draw/model entry points before they inspect,
         * pack, allocate, bind, or otherwise consume 3D input. Fully 3D-capable backends retain
         * the no-op default; a deliberately 2D-only backend overrides this with its stable
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
         * @note Status: PARTIAL. Only the EasyGL backend honors this; other
         *       backends throw on first 3D usage.
         */
        virtual void SetDepthTestEnabled(bool enabled) = 0;
        virtual void SetBlendEnabled(bool enabled) = 0;
        virtual void SetDepthWriteEnabled(bool enabled) = 0;

        /**
         * @brief Creates a backend-specific vertex buffer for
         *        `VertexPositionColor` data.
         */
        virtual std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) = 0;

        /**
         * @brief Creates a backend-specific 16-bit index buffer.
         */
        virtual std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) = 0;
        /// Creates a 32-bit index buffer. Default delegates to CreateIndexBuffer16 for
        /// backends that do not yet support 32-bit indices.
        virtual std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity)
        {
            return CreateIndexBuffer16(index_capacity);
        }

        /**
         * @brief Draws colored primitives from `vb` using the supplied transform.
         *
         * The backend internally applies a basic colored-vertex shader
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
        virtual void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                           const Matrix& world,
                                           const Matrix& view,
                                           const Matrix& projection,
                                           PrimitiveType primitive,
                                           int primitiveCount) = 0;

        /**
         * @brief Indexed counterpart of `DrawColoredPrimitives`.
         */
        virtual void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                  const IIndexBufferBackend& ib,
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
         * own slot with its own stride and `VertexStreamByteOffset()`. A backend that cannot
         * express the combined layout must throw before native submission rather than render from
         * stream 0 alone; `HasMultipleVertexStreams()` selects that branch.
         *
         * Default implementation falls back to DrawColoredPrimitives so
         * backends that have not yet implemented this path still work.
         */
        virtual void DrawPrimitivesEx(const IVertexBufferBackend& vb,
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
        virtual void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                             const IIndexBufferBackend& ib,
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
         * @brief Instanced indexed draw — default throws on backends that don't support it.
         *
         * REMED-GFX-202: this route takes the SAME complete stream description the two ordinary
         * routes take. @p vb is `params.vertexStreams[0].buffer`, kept as a named argument so the
         * classic single-per-vertex-stream path is unchanged; the per-instance streams are the
         * entries whose `instanceFrequency` is greater than zero, reached through
         * `FirstInstanceStream()` / `NthInstanceStream()`. Each stream carries its own slot,
         * declaration, stride, `VertexOffset` (in vertex ELEMENTS) and `InstanceFrequency`, and the
         * whole array is captured by value, so a deferred backend must copy what it needs from it
         * and must never re-read `GraphicsDevice`'s public binding state at replay.
         *
         * A backend that cannot express the bound combination must throw before native submission
         * rather than render from a subset of the streams; `HasMultipleVertexStreams()` and
         * `HasMultipleInstanceStreams()` select that branch, and `GraphicsDevice` already rejects
         * both shapes for a backend that does not report
         * `GraphicsCapability::MultiStreamVertexInput`.
         */
        virtual void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb,
                                               const IIndexBufferBackend& ib,
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
            throw std::runtime_error(
                "DrawInstancedPrimitives is not supported on this graphics backend.");
        }

        /// Disables context-loss recovery on the running backend.
        /// Safe to call after backend creation when no resources have been
        /// loaded yet (e.g. from Game1 constructor). Future Create* calls
        /// will skip registry registration and CPU shadow copies.
        virtual void SetContextRecoveryEnabled(bool /*enabled*/) {}

        /// Returns whether this backend (and, for device-dependent entries, the current runtime
        /// device/driver) supports the given CNA::GraphicsCapability. Default implementation
        /// returns true for everything -- most backends are fully 3D-capable, so only backends
        /// with a genuine, known gap (SDL_Renderer/DX3/Canvas's 2D-only design, or a specific
        /// device-dependent feature like anisotropic filtering) need to override this.
        [[nodiscard]] virtual bool SupportsCapability(CNA::GraphicsCapability capability) const
        {
            // REMED-GFX-201: MultiStreamVertexInput is the one entry whose default is FALSE. A
            // backend derives its native input elements from a single byte stride, so binding a
            // second per-vertex stream is real work it must opt into by name; defaulting to true
            // would make a backend that silently renders from stream 0 alone claim otherwise.
            if (capability == CNA::GraphicsCapability::MultiStreamVertexInput)
                return false;
            return true;
        }

        /**
         * @brief REMED-GFX-201: how many per-vertex `VertexBufferBinding`s this backend can bind
         *        to one ordinary draw.
         *
         * XNA's public ceiling is `kMaxVertexStreams` (16). A backend that reports
         * `GraphicsCapability::MultiStreamVertexInput` but has a lower native input-slot limit
         * returns it here so `GraphicsDevice` rejects an over-wide binding set deterministically
         * rather than truncating it, collapsing streams, or letting the native API abort. The
         * default is the public maximum; a backend that does not support multi-stream input at
         * all is already rejected by the capability itself and never reaches this.
         */
        [[nodiscard]] virtual int GetMaxVertexStreams() const
        {
            return kMaxVertexStreams;
        }

        /// REMED-CONTENT-001: returns this backend's real maximum single-axis texture dimension
        /// (width or height), used by shared content-reading code to reject an XNB-declared size
        /// before any backend-specific texture creation is attempted. Default of 16384 matches the
        /// guaranteed ceiling on every native API this project targets (D3D11/D3D12 feature level
        /// 11_0's REQ_TEXTURE2D_U_OR_V_DIMENSION, and the value real-world Vulkan/Metal/GL
        /// implementations report) -- no backend currently needs a tighter or looser override.
        /// D3D9's own narrower Reach/HiDef profile ceiling (2048/4096) is enforced separately by
        /// Texture2D.cpp's existing ValidateTextureSizeForProfileEXT and is unaffected by this.
        [[nodiscard]] virtual int GetMaxTextureDimension() const
        {
            return 16384;
        }

        // ---- Debug / testing ----

        /// Inserts a named GPU debug label into the command stream.
        /// Default implementation is a no-op; Vulkan backend overrides with
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

        // ---- Window → backend registry ----
        // Backends that implement TransformWindowToLogical register themselves here
        // so that SdlInputBridge can map physical mouse coordinates to logical ones
        // even for backends that have no SDL_Renderer (e.g. EasyGL).

        static void RegisterForWindow(SDL_Window* window, IGraphicsBackend* backend)
        {
            windowRegistry()[window] = backend;
        }
        static void UnregisterForWindow(SDL_Window* window)
        {
            windowRegistry().erase(window);
        }
        static IGraphicsBackend* GetForWindow(SDL_Window* window)
        {
            auto& reg = windowRegistry();
            auto it = reg.find(window);
            return it != reg.end() ? it->second : nullptr;
        }

    private:
        static std::unordered_map<SDL_Window*, IGraphicsBackend*>& windowRegistry()
        {
            static std::unordered_map<SDL_Window*, IGraphicsBackend*> reg;
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
     * @brief NOXNA (plan_dx9.md D9-34). Real, driver-triggered device lifecycle events a backend
     * can report back to GraphicsDevice via GraphicsBackendCreateArgs::deviceEventCallback.
     *
     * Distinct from the pre-existing IGraphicsBackend::SetContextRecoveryEnabled()/
     * DebugSimulateContextLoss()/DebugRestoreContext() channel, which is one-directional and
     * test-only (GraphicsDevice commands a backend to simulate loss). This is the opposite
     * direction -- backend reports a real, async, driver-detected event up to GraphicsDevice --
     * and it is what actually fires GraphicsDevice::DeviceLost/DeviceResetting/DeviceReset for a
     * genuine device loss (as opposed to GraphicsDevice::Reset()'s own direct Raise() calls for an
     * app-initiated reset with new PresentationParameters -- a different, pre-existing path that
     * this enum does not replace).
     */
    enum class BackendDeviceEvent
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
     * @brief Arguments for creating a graphics backend.
     * Currently minimal, but allows for easier extension.
     */
    struct GraphicsBackendCreateArgs
    {
        // TODO: SDL dependency should be abstracted later
        SDL_Window* window = nullptr;
        /// Virtual (game-logic) resolution the backend should present at.
        /// SDL_SetRenderLogicalPresentation will be set to this size so that
        /// the game always draws in its own coordinate space and the backend
        /// scales to the real surface automatically.
        /// 0 means "unset"; the backend should ignore logical presentation.
        int virtualWidth = 0;
        int virtualHeight = 0;
        /// Presentation/scaling policy. Default is FixedHeightDynamicWidth:
        /// keeps preferred height fixed and derives logical width from the
        /// actual surface aspect ratio, matching XNA/Windows Phone behaviour.
        CnaPresentationMode presentationMode = CnaPresentationMode::FixedHeightDynamicWidth;
        /// When false, the EasyGL backend will not keep CPU-side copies of
        /// texture pixels or vertex/index data and will not register resources
        /// with the ResourceRegistry. This eliminates the per-texture CPU
        /// shadow copy overhead at the cost of making GL context-loss recovery
        /// impossible. Safe on desktop where context loss never occurs.
        bool contextRecoveryEnabled = true;
        /// Desired multisample count (1 = no MSAA, 4 = 4× MSAA, etc.).
        /// Backends that do not support MSAA silently clamp to 1.
        int multiSampleCount = 1;
        /// Swap interval for vertical sync.
        ///   0 = immediate (no VSync)
        ///   1 = wait for 1 vertical retrace (VSync, default)
        ///   2 = wait for 2 vertical retraces (half refresh rate)
        /// Corresponds to PresentInterval: Default/One→1, Two→2, Immediate→0.
        int swapInterval = 1;
        /// NOXNA (plan_dx9.md D9-30). Requested back-buffer pixel format -- raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::SurfaceFormat, avoiding coupling this
        /// backend-agnostic header to the XNA namespace (mirrors CreateTexture3D's own
        /// surfaceFormat int convention). Backends that don't need real format fidelity (every
        /// existing backend except D3D9, whose goal is XNA authenticity rather than parity) may
        /// ignore this and keep hardcoding their own default, exactly as before this field existed.
        int backBufferFormat = 0;  // SurfaceFormat::Color
        /// NOXNA (plan_dx9.md D9-30). Requested depth/stencil format -- raw ordinal of
        /// Microsoft::Xna::Framework::Graphics::DepthFormat. See backBufferFormat's own doc for
        /// the int-ordinal convention and the same "existing backends may ignore this" note.
        int depthStencilFormat = 0;  // DepthFormat::None
        /// NOXNA (plan_dx9.md D9-30). Whether the game requested exclusive fullscreen. Existing
        /// backends that already have their own fullscreen handling via the SDL window itself may
        /// continue to ignore this field exactly as before it existed.
        bool isFullScreen = false;
        /// NOXNA (plan_dx9.md D9-30/D9-32). Requested Microsoft::Xna::Framework::Graphics::
        /// GraphicsProfile ordinal (Reach=0, HiDef=1). Only D3D9 can honestly enforce this today
        /// (a real D3DCAPS9 to consult) -- see plan_dx9.md's "CNA's divergences from XNA 4.0",
        /// Divergence 3. Every other backend's GraphicsAdapter::IsProfileSupported() keeps its
        /// existing, honest `return true;` and may ignore this field.
        int graphicsProfile = 0;  // GraphicsProfile::Reach
        /// NOXNA (plan_dx9.md D9-34). Callback a backend may invoke to report a REAL,
        /// driver-triggered device lifecycle event back to GraphicsDevice (which raises the
        /// corresponding DeviceLost/DeviceResetting/DeviceReset XNA event). Null by default; nine
        /// of the ten backends never call it -- only a backend that can genuinely lose its device
        /// the way XNA's own D3D9-based runtime could (i.e. D3D9) has any reason to. A backend
        /// must never invoke this synchronously from within its own constructor (the
        /// GraphicsDevice that would receive it has not finished constructing yet).
        std::function<void(BackendDeviceEvent)> deviceEventCallback;
    };

    // Factory function to be implemented by each backend
    // INTERNAL API - SDL dependency should be abstracted later
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args);
}
