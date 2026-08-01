// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include "CNA/Logger.hpp"
#include "CNA/Internal/Graphics/DxtUtil.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "System/IO/Stream.hpp"
#include "System/NotSupportedException.hpp"

// plan_dx9.md Phase D9-10 (D9-103): GraphicsProfile.Reach/HiDef texture-size ceilings are real,
// enforced-at-creation-time, ONLY on this backend -- the other 9 CNA backends have no profile
// distinction to enforce (matches GraphicsAdapter.cpp's own #ifdef CNA_BACKEND_D3D9 convention).
#ifdef CNA_BACKEND_D3D9
#include "CNA/Internal/Backends/D3D9/D3D9ProfileCapabilities.hpp"
#endif

namespace Microsoft::Xna::Framework::Graphics
{
    using namespace CNA::Internal::Backends;
    using namespace CNA::Internal::Graphics;

    // -----------------------------------------------------------------------
    // Private helpers
    // -----------------------------------------------------------------------

#ifdef CNA_BACKEND_D3D9
    // D9-103: a HiDef-only size requested on a Reach device (or a size exceeding even HiDef's own
    // 4096 ceiling) throws the XNA-correct exception (System::NotSupportedException, matching
    // this file's own established convention for other unsupported-request cases) -- D9-100's own
    // table, checked as a profile CEILING, not a hardware query: even if this dev environment's
    // real device could technically allocate a larger texture, a Reach-profile game is restricted
    // to 2048 and a HiDef-profile game to 4096, matching real XNA's own portability guarantee.
    static void ValidateTextureSizeForProfileEXT(const GraphicsDevice& device, int w, int h)
    {
        const int profile = static_cast<int>(device.getGraphicsProfileProperty());
        const int maxSize = CNA::Internal::Backends::D3D9::MaxTextureSizeForProfileEXT(profile);
        if (w > maxSize || h > maxSize)
        {
            throw System::NotSupportedException(
                "Texture2D: " + std::to_string(w) + "x" + std::to_string(h) +
                " exceeds GraphicsProfile." + (profile == 1 ? std::string("HiDef") : std::string("Reach")) +
                "'s own maximum texture size of " + std::to_string(maxSize));
        }
    }
#endif

    // REMED-CONTENT-001: the native graphics APIs' own validation does not substitute for this --
    // Vulkan's validation layer is advisory (RADV proceeds anyway), and wgpu-native validates
    // lazily at submit time, past CNA's own creation-time null checks. Reject before any
    // backend-specific texture creation is attempted, using the active backend's real reported
    // maximum rather than a value guessed independently of it.
    static void ValidateTextureDimensionEXT(const GraphicsDevice& device, int w, int h)
    {
        const int maxDim = device.GetMaxTextureDimension();
        if (w > maxDim || h > maxDim)
        {
            throw System::NotSupportedException(
                "Texture2D: " + std::to_string(w) + "x" + std::to_string(h) +
                " exceeds this device's maximum texture dimension of " + std::to_string(maxDim));
        }
    }

    static int mipDim(int base, int level)
    {
        return std::max(1, base >> level);
    }

    static void validateMipLevel(const char* api, int level, int levelCount)
    {
        if (level < 0)
            throw std::out_of_range(std::string(api) + ": level must be >= 0");
        if (level >= levelCount)
            throw std::out_of_range(
                std::string(api) + ": level " + std::to_string(level) +
                " must be less than LevelCount " + std::to_string(levelCount));
    }

    void Texture2D::MaybeFreeCpuPixels()
    {
        if (graphicsDevice_ && !graphicsDevice_->contextRecoveryEnabled_)
            cpuPixels_.reset();
    }

    void Texture2D::storeCpuPixels(const uint8_t* rgba, int pixelCount)
    {
        if (!cpuPixels_) cpuPixels_ = std::make_shared<std::vector<uint8_t>>();
        cpuPixels_->assign(rgba, rgba + static_cast<std::size_t>(pixelCount) * 4);
    }

    std::vector<uint8_t>& Texture2D::getMipBuffer(int level)
    {
        validateMipLevel("Texture2D::SetData", level, levelCount_);
        if (level == 0) {
            if (!cpuPixels_) cpuPixels_ = std::make_shared<std::vector<uint8_t>>();
            if (cpuPixels_->empty())
            {
                const std::size_t sz =
                    static_cast<std::size_t>(mipDim(width, 0)) * mipDim(height, 0) * 4;
                if (sz > 0) cpuPixels_->assign(sz, 0);
            }
            return *cpuPixels_;
        }
        const int idx = level - 1;
        if (!extraMipLevels_) extraMipLevels_ = std::make_shared<std::vector<std::vector<uint8_t>>>();
        if (static_cast<int>(extraMipLevels_->size()) <= idx)
            extraMipLevels_->resize(static_cast<std::size_t>(idx + 1));
        if ((*extraMipLevels_)[idx].empty())
        {
            const int w = mipDim(width, level);
            const int h = mipDim(height, level);
            (*extraMipLevels_)[idx].assign(static_cast<std::size_t>(w * h) * 4, 0);
        }
        return (*extraMipLevels_)[idx];
    }

    const std::vector<uint8_t>* Texture2D::getMipBufferConst(int level) const
    {
        validateMipLevel("Texture2D::GetData", level, levelCount_);
        if (level == 0) return (!cpuPixels_ || cpuPixels_->empty()) ? nullptr : cpuPixels_.get();
        if (!extraMipLevels_) return nullptr;
        const int idx = level - 1;
        if (static_cast<int>(extraMipLevels_->size()) <= idx) return nullptr;
        return (*extraMipLevels_)[idx].empty() ? nullptr : &(*extraMipLevels_)[idx];
    }

    // -----------------------------------------------------------------------
    // Constructors
    // -----------------------------------------------------------------------

    Texture2D::Texture2D() = default;

    Texture2D::Texture2D(const std::string& assetName, GraphicsDevice& graphicsDevice)
        : Texture(&graphicsDevice)
    {
        ImageData data = ImageLoader::Load(assetName);
        width    = data.width;
        height   = data.height;
        backend_   = graphicsDevice.GetBackend().CreateTexture(data);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        backend_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    Texture2D::Texture2D(const std::string& assetName)
    {
        ImageData data = ImageLoader::Load(assetName);
        width    = data.width;
        height   = data.height;
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        // No GraphicsDevice — backend stays null until attached.
    }

    Texture2D::Texture2D(GraphicsDevice& graphicsDevice, int w, int h)
        : Texture(&graphicsDevice), width(w), height(h)
    {
#ifdef CNA_BACKEND_D3D9
        ValidateTextureSizeForProfileEXT(graphicsDevice, w, h);
#endif
        ValidateTextureDimensionEXT(graphicsDevice, w, h);
        ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        backend_   = graphicsDevice.GetBackend().CreateTexture(data);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        backend_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    static int CalculateMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    Texture2D::Texture2D(GraphicsDevice& graphicsDevice, int w, int h,
                         bool mipMap, SurfaceFormat format)
        : Texture(&graphicsDevice), width(w), height(h)
    {
#ifdef CNA_BACKEND_D3D9
        ValidateTextureSizeForProfileEXT(graphicsDevice, w, h);
#endif
        ValidateTextureDimensionEXT(graphicsDevice, w, h);
        ValidateFormat(format);
        format_     = format;
        levelCount_ = mipMap ? CalculateMipLevels(w, h) : 1;
        ImageData data;
        data.width     = w;
        data.height    = h;
        data.mipLevels = levelCount_;
        data.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
        backend_   = graphicsDevice.GetBackend().CreateTexture(data);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        backend_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    Texture2D::Texture2D(GraphicsDevice& device, int w, int h, SurfaceFormat fmt,
                         int lvlCount, std::shared_ptr<ITextureBackend> backend)
        : Texture(&device), width(w), height(h), backend_(std::move(backend))
    {
        // Task 774 finding: this constructor (used exclusively by RenderTarget2D) previously
        // skipped ValidateFormat entirely, silently accepting any SurfaceFormat even though
        // CreateRenderTarget2D's own backend call never actually forwards it -- a RenderTarget2D
        // could report a non-Color Format() while its real GPU resource was always Color.
        ValidateFormat(fmt);
        format_     = fmt;
        levelCount_ = lvlCount;
        gpuOnlyContent_ = true;
    }

    Texture2D::~Texture2D() = default;

    void Texture2D::Dispose(bool disposing)
    {
        backend_.reset();
        Texture::Dispose(disposing);
    }

    // -----------------------------------------------------------------------
    // Properties
    // -----------------------------------------------------------------------

    const std::string& Texture2D::GetTypeName() const
    {
        static const std::string name = "Texture2D";
        return name;
    }

    Rectangle Texture2D::getBoundsProperty() const
    {
        return {0, 0, width, height};
    }

    // -----------------------------------------------------------------------
    // SetData
    // -----------------------------------------------------------------------

    void Texture2D::SetData(const Color* data, int elementCount)
    {
        if (!graphicsDevice_ || !data || elementCount <= 0) return;
        const int total = width * height;
        if (elementCount < total)
            throw std::out_of_range("Texture2D::SetData: elementCount is less than the number of pixels in the texture");
        ImageData img;
        img.width     = width;
        img.height    = height;
        img.mipLevels = levelCount_;
        img.pixels.resize(static_cast<std::size_t>(total) * 4);
        for (int i = 0; i < total; ++i)
        {
            img.pixels[i * 4 + 0] = data[i].getRProperty();
            img.pixels[i * 4 + 1] = data[i].getGProperty();
            img.pixels[i * 4 + 2] = data[i].getBProperty();
            img.pixels[i * 4 + 3] = data[i].getAProperty();
        }
        backend_   = graphicsDevice_->GetBackend().CreateTexture(img);
        cpuPixels_ = std::make_shared<std::vector<uint8_t>>(std::move(img.pixels));
        backend_->ShareCpuPixels(cpuPixels_);
        MaybeFreeCpuPixels();
    }

    void Texture2D::SetData(int level, const Rectangle* rect,
                            const Color* data, int startIndex, int elementCount)
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::SetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::SetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::SetData", level, levelCount_);

        const int levelW = mipDim(width,  level);
        const int levelH = mipDim(height, level);

        int x = 0, y = 0, w = levelW, h = levelH;
        if (rect)
        {
            x = rect->X; y = rect->Y;
            w = rect->Width; h = rect->Height;
        }
        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH)
            throw std::out_of_range("Texture2D::SetData: rectangle out of texture bounds");
        if (elementCount < w * h)
            throw std::out_of_range("Texture2D::SetData: elementCount is less than the number of pixels in the requested region");

        // getMipBuffer(0) lazily re-creates cpuPixels_ as a zero-filled buffer when it has
        // been freed (context recovery disabled — see MaybeFreeCpuPixels). If the requested
        // region does not cover the whole level, UpdatePixels below would re-upload that
        // zero-filled buffer wholesale and silently overwrite already-uploaded GPU content
        // outside the region with black. Fail loudly instead of corrupting the texture.
        const bool coversFullLevel = (x == 0 && y == 0 && w == levelW && h == levelH);
        if (level == 0 && backend_ && !cpuPixels_ && !coversFullLevel)
            throw std::runtime_error(
                "Texture2D::SetData: partial update requires CPU-side pixel storage, which was "
                "freed because context recovery is disabled (see GraphicsDevice::SetContextRecoveryEnabled); "
                "update the full level via SetData(Color*, int) instead");

        std::vector<uint8_t>& buf = getMipBuffer(level);

        for (int row = 0; row < h; ++row)
        {
            for (int col = 0; col < w; ++col)
            {
                const int src = startIndex + row * w + col;
                const int dst = ((y + row) * levelW + (x + col)) * 4;
                buf[dst + 0] = data[src].getRProperty();
                buf[dst + 1] = data[src].getGProperty();
                buf[dst + 2] = data[src].getBProperty();
                buf[dst + 3] = data[src].getAProperty();
            }
        }

        if (level == 0)
        {
            if (backend_)
                backend_->UpdatePixels(buf.data(), levelW * 4);
            else if (graphicsDevice_)
            {
                ImageData img;
                img.width     = width;
                img.height    = height;
                img.mipLevels = levelCount_;
                img.pixels    = buf;
                backend_   = graphicsDevice_->GetBackend().CreateTexture(img);
                backend_->ShareCpuPixels(cpuPixels_);
            }
            MaybeFreeCpuPixels();
        }
        else if (backend_)
        {
            backend_->UpdatePixelsLevel(level, buf.data(), levelW, levelH);
        }
    }

    void Texture2D::SetDataRGBA(const uint8_t* data, int pixelCount)
    {
        if (!backend_ || !data || pixelCount <= 0) return;
        storeCpuPixels(data, pixelCount);
        backend_->UpdatePixels(data, width * 4);
    }

    // -----------------------------------------------------------------------
    // GetData
    // -----------------------------------------------------------------------

    // REMED-GFX-149 / REMED-GFX-128. The destination window every GetData overload is authorised to
    // write, validated once, in ELEMENTS.
    //
    //   startIndex   -- an index into the CALLER'S array. XNA documents it as "index of the first
    //                   element to get" and FNA transfers to
    //                   `AddrOfPinnedObject() + startIndex * elementSizeInBytes`, i.e. the pinned
    //                   DESTINATION array's base. It is never a source-texel offset and never a
    //                   byte offset.
    //   elementCount -- the destination capacity available from startIndex. FNA hands it to the
    //                   native transfer as `elementCount * elementSizeInBytes` and derives the
    //                   transfer size from the requested REGION, so a capacity larger than the
    //                   region is legal (`GetData<T>(T[] data)` passes `data.Length`) and must be
    //                   left untouched, while a smaller one must be rejected before any transfer.
    //
    // The sum is computed in 64 bits and rejected before use. Evaluated as `int` it wraps for large
    // arguments and the wrapped value then passes every subsequent bound, so the copy reads and
    // writes far outside both buffers -- measured as a SIGSEGV, not as a rejected call.
    static void validateTransferWindow(const char* api, int startIndex, int elementCount,
                                       int requiredElements)
    {
        if (elementCount < requiredElements)
            throw std::out_of_range(std::string(api) +
                ": elementCount is less than the number of pixels in the requested region");
        if (static_cast<std::int64_t>(startIndex) + static_cast<std::int64_t>(elementCount) >
            static_cast<std::int64_t>(std::numeric_limits<int>::max()))
            throw std::out_of_range(std::string(api) +
                ": startIndex + elementCount does not fit in a 32-bit element index");
    }

    /// Records exactly what a transfer was asked for and what it is authorised to write, so the
    /// shared layer's interpretation can be compared against a backend's without a debugger.
    static void traceTransfer(const char* overload, int resourceW, int resourceH, int level,
                              int x, int y, int w, int h, int startIndex, int elementCount,
                              int requiredElements, const char* source)
    {
        const char* trace = std::getenv("CNA_TEXTURE_TRANSFER_TRACE");
        if (trace == nullptr || *trace == '\0') return;
        std::fprintf(stderr,
                     "[GFX-149] Texture2D::GetData overload=%s resource=%dx%d level=%d "
                     "region=(%d,%d,%dx%d) bytesPerElement=4 bytesPerPixel=4 startIndex=%d "
                     "elementCount=%d requiredElements=%d requiredBytes=%lld "
                     "destElements=[%d,%d) destBytes=[%lld,%lld) rowPitchBytes=%d source=%s\n",
                     overload, resourceW, resourceH, level, x, y, w, h, startIndex, elementCount,
                     requiredElements, static_cast<long long>(requiredElements) * 4,
                     startIndex, startIndex + requiredElements,
                     static_cast<long long>(startIndex) * 4,
                     (static_cast<long long>(startIndex) + requiredElements) * 4,
                     w * 4, source);
        std::fflush(stderr);
    }

    void Texture2D::GetData(Color* data, int startIndex, int elementCount) const
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("data must not be null and elementCount must be > 0");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::GetData: startIndex must be >= 0");
        Texture::ValidateGetDataFormat(format_, 4);

        // The requested region of this overload is the COMPLETE level 0 -- never elementCount,
        // never a viewport, never a previous request. Argument validation runs BEFORE the storage
        // and capability decision below, so an undersized or overflowing request is answered with
        // its own specific error on every backend rather than being weakened into a capability
        // rejection (REMED-GFX-162's precedence, from the texture side).
        const int total = width * height;
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, total);

        if (!cpuPixels_ || cpuPixels_->empty())
        {
            // No CPU-side shadow. For a RenderTarget2D (gpuOnlyContent_), that's normal -- its
            // content comes from GPU rendering, not SetData() -- so fall back to a real backend
            // readback. For a plain Texture2D, an empty shadow means it was freed because context
            // recovery is disabled (MaybeFreeCpuPixels) -- that must still throw below, not
            // silently read back whatever the backend's GPU texture currently holds.
            if (gpuOnlyContent_ && backend_ && total > 0)
            {
                traceTransfer("whole-level(gpu)", width, height, 0, 0, 0, width, height,
                              startIndex, elementCount, total, "backend");
                // REMED-GFX-127: `pixels` is scratch memory THIS layer owns and zero-initializes,
                // so converting it unconditionally is not "leaving the caller's buffer untouched"
                // when the backend has no readback -- it fabricates a complete transparent-black
                // frame. Conversion happens only when the backend reports it wrote the whole
                // region; otherwise the caller's `data` is left byte-for-byte as it was and the
                // missing capability is raised instead of being answered with invented content.
                //
                // Sized to the REQUESTED REGION, not to elementCount: the backend fills exactly
                // width*height texels, so a larger elementCount would otherwise hand the caller
                // this buffer's untouched tail as if it were content.
                std::vector<uint8_t> pixels(static_cast<std::size_t>(total) * 4, 0);
                if (!backend_->GetData(0, 0, 0, width, height, pixels.data(),
                                       static_cast<int>(pixels.size())))
                {
                    throw System::NotSupportedException(
                        "Texture2D::GetData: this graphics backend cannot read a render target's "
                        "colour attachment back to the CPU");
                }
                for (int i = 0; i < total; ++i)
                {
                    const int src = i * 4;
                    data[startIndex + i] =
                        Color(pixels[src + 0], pixels[src + 1], pixels[src + 2], pixels[src + 3]);
                }
                return;
            }
            throw std::runtime_error("Texture2D::GetData: no CPU-side pixel data available");
        }

        const auto& px = *cpuPixels_;
        if (px.size() < static_cast<std::size_t>(total) * 4)
            throw std::runtime_error("Texture2D::GetData: no CPU-side pixel data available");

        traceTransfer("whole-level(cpu)", width, height, 0, 0, 0, width, height,
                      startIndex, elementCount, total, "cpuPixels_");
        for (int i = 0; i < total; ++i)
        {
            const int src = i * 4;
            data[startIndex + i] = Color(px[src + 0], px[src + 1], px[src + 2], px[src + 3]);
        }
    }

    void Texture2D::GetData(Color* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture2D::GetData(int level, const Rectangle* rect,
                            Color* data, int startIndex, int elementCount) const
    {
        if (!data || elementCount <= 0)
            throw std::invalid_argument("Texture2D::GetData: data must not be null");
        if (startIndex < 0)
            throw std::out_of_range("Texture2D::GetData: startIndex must be >= 0");
        validateMipLevel("Texture2D::GetData", level, levelCount_);
        Texture::ValidateGetDataFormat(format_, 4);

        // Delegate before touching the CPU-side mip shadow at all -- the 3-arg overload has its
        // own complete bounds checking and (for a RenderTarget2D with no shadow) backend fallback.
        if (level == 0 && rect == nullptr)
        {
            GetData(data, startIndex, elementCount);
            return;
        }

        const std::vector<uint8_t>* buf = getMipBufferConst(level);
        if (!buf)
        {
            // No CPU-side shadow for this mip level. As above, only fall back to a real backend
            // readback for a RenderTarget2D (gpuOnlyContent_); a plain Texture2D with a freed
            // shadow must still throw, matching the 3-arg overload's own gpuOnlyContent_ gate.
            if (gpuOnlyContent_ && backend_)
            {
                const int levelW = mipDim(width, level);
                const int levelH = mipDim(height, level);
                int x = 0, y = 0, w = levelW, h = levelH;
                if (rect) { x = rect->X; y = rect->Y; w = rect->Width; h = rect->Height; }
                if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelW || y + h > levelH)
                    throw std::out_of_range("Texture2D::GetData: rectangle out of texture bounds");
                // REMED-GFX-149: the required element count comes from THIS rectangle's own
                // dimensions at THIS mip level -- never from level 0 and never from the full
                // resource -- and the destination window is checked overflow-safely.
                validateTransferWindow("Texture2D::GetData", startIndex, elementCount, w * h);
                traceTransfer("rectangle(gpu)", levelW, levelH, level, x, y, w, h,
                              startIndex, elementCount, w * h, "backend");

                // REMED-GFX-127: same contract as the whole-level overload above -- scratch memory
                // this layer zero-initialized is never handed to the caller as if it were content.
                std::vector<uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 0);
                if (!backend_->GetData(level, x, y, w, h, pixels.data(),
                                       static_cast<int>(pixels.size())))
                {
                    throw System::NotSupportedException(
                        "Texture2D::GetData: this graphics backend cannot read a render target's "
                        "colour attachment back to the CPU");
                }
                for (int row = 0; row < h; ++row)
                    for (int col = 0; col < w; ++col)
                    {
                        const int src = (row * w + col) * 4;
                        const int dst = startIndex + row * w + col;
                        data[dst] = Color(pixels[src + 0], pixels[src + 1], pixels[src + 2], pixels[src + 3]);
                    }
                return;
            }
            throw std::runtime_error("Texture2D::GetData: no CPU-side pixel data for requested mip level");
        }

        const int levelW = mipDim(width,  level);
        const int levelH = mipDim(height, level);

        int x = 0, y = 0, w = levelW, h = levelH;
        if (rect)
        {
            x = rect->X; y = rect->Y;
            w = rect->Width; h = rect->Height;
        }

        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH)
            throw std::out_of_range("Texture2D::GetData: rectangle out of texture bounds");
        // REMED-GFX-149: as above -- this rectangle's own dimensions at this mip level, and an
        // overflow-safe destination window.
        validateTransferWindow("Texture2D::GetData", startIndex, elementCount, w * h);
        traceTransfer("rectangle(cpu)", levelW, levelH, level, x, y, w, h,
                      startIndex, elementCount, w * h, "mipShadow");

        for (int row = 0; row < h; ++row)
        {
            for (int col = 0; col < w; ++col)
            {
                const int src = ((y + row) * levelW + (x + col)) * 4;
                const int dst = startIndex + row * w + col;
                data[dst] = Color((*buf)[src + 0],
                                  (*buf)[src + 1],
                                  (*buf)[src + 2],
                                  (*buf)[src + 3]);
            }
        }
    }

    // -----------------------------------------------------------------------
    // FromStream
    // -----------------------------------------------------------------------

    // Minimal DDS header parser — returns true and fills out/w/h if a supported DXT format.
    static bool TryDecodeDds(const uint8_t* buf, std::size_t len,
                              std::vector<uint8_t>& out, int& w, int& h)
    {
        // DDS magic "DDS " + 124-byte DDS_HEADER = 128 bytes minimum
        if (len < 128) return false;
        if (buf[0] != 'D' || buf[1] != 'D' || buf[2] != 'S' || buf[3] != ' ') return false;

        // DDS_HEADER fields (all little-endian uint32)
        auto r32 = [&](std::size_t off) -> uint32_t {
            return static_cast<uint32_t>(buf[off])
                 | (static_cast<uint32_t>(buf[off+1]) << 8)
                 | (static_cast<uint32_t>(buf[off+2]) << 16)
                 | (static_cast<uint32_t>(buf[off+3]) << 24);
        };

        const int height = static_cast<int>(r32(12));
        const int width  = static_cast<int>(r32(16));
        // DDS_PIXELFORMAT starts at offset 76; dwFourCC at offset 84
        const uint32_t fourCC = r32(84);
        const uint8_t* pixels = buf + 128;
        const std::size_t pixLen = len - 128;

        // fourCC codes: 'DXT1'=0x31545844, 'DXT3'=0x33545844, 'DXT5'=0x35545844
        if (fourCC == 0x31545844u)
            out = DxtUtil::DecompressDxt1(pixels, pixLen, width, height);
        else if (fourCC == 0x33545844u)
            out = DxtUtil::DecompressDxt3(pixels, pixLen, width, height);
        else if (fourCC == 0x35545844u)
            out = DxtUtil::DecompressDxt5(pixels, pixLen, width, height);
        else
            return false; // unsupported DDS format — fall through to SDL_image

        w = width;
        h = height;
        return true;
    }

    // Reads the entire stream and decodes it into RGBA8 pixel data — DDS/DXT1/3/5 via
    // DxtUtil, everything else via SDL_image (PNG/JPG/BMP/GIF/... — whatever SDL3_image was
    // built with; see docs/texture-stream-formats.md for the formats verified by CI).
    static ImageData DecodeStreamToImageData(System::IO::Stream& stream)
    {
        using System::IO::intcs;
        using System::IO::bytecs;

        intcs len = stream.getLengthProperty();
        if (len <= 0)
            throw std::runtime_error("Texture2D::FromStream: stream is empty or length unknown");

        std::vector<bytecs> buf(static_cast<std::size_t>(len));
        stream.Read(buf.data(), 0, len);

        const auto* raw = reinterpret_cast<const uint8_t*>(buf.data());

        ImageData img;
        std::vector<uint8_t> ddsOut;
        int ddsW = 0, ddsH = 0;
        if (TryDecodeDds(raw, static_cast<std::size_t>(len), ddsOut, ddsW, ddsH))
        {
            img.width  = ddsW;
            img.height = ddsH;
            img.pixels = std::move(ddsOut);
        }
        else
        {
            img = ImageLoader::LoadFromMemory(raw, static_cast<std::size_t>(len));
        }
        return img;
    }

    Texture2D Texture2D::MakeTextureFromPixels(GraphicsDevice& device, int w, int h,
                                               std::vector<std::uint8_t>&& rgba)
    {
        ImageData img;
        img.width  = w;
        img.height = h;
        img.pixels = std::move(rgba);

        Texture2D tex;
        tex.graphicsDevice_ = &device;
        tex.width           = w;
        tex.height          = h;
        tex.backend_        = device.GetBackend().CreateTexture(img);
        tex.cpuPixels_      = std::make_shared<std::vector<uint8_t>>(std::move(img.pixels));
        tex.backend_->ShareCpuPixels(tex.cpuPixels_);
        tex.MaybeFreeCpuPixels();
        return tex;
    }

    Texture2D Texture2D::FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream)
    {
        ImageData img = DecodeStreamToImageData(stream);
        return MakeTextureFromPixels(graphicsDevice, img.width, img.height, std::move(img.pixels));
    }

    Texture2D Texture2D::FromStream(GraphicsDevice& graphicsDevice, System::IO::Stream& stream,
                                    int width, int height, bool zoom)
    {
        ImageData img = DecodeStreamToImageData(stream);

        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            img.width, img.height, SDL_PIXELFORMAT_RGBA32, img.pixels.data(), img.width * 4);
        if (!surface)
            throw std::runtime_error(std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());

        // Mirrors FNA3D_Image_Load's forceW/forceH/zoom resize-and-crop logic.
        const bool scaleWidth = zoom ? (surface->w < surface->h) : (surface->w > surface->h);
        const float scale = scaleWidth ? (static_cast<float>(width)  / static_cast<float>(surface->w))
                                       : (static_cast<float>(height) / static_cast<float>(surface->h));

        int finalW, finalH;
        SDL_Rect crop{0, 0, surface->w, surface->h};
        if (zoom)
        {
            finalW = width;
            finalH = height;
            if (scaleWidth)
            {
                crop.x = 0;
                crop.y = surface->h / 2 - static_cast<int>((height / scale) / 2);
                crop.w = surface->w;
                crop.h = static_cast<int>(height / scale);
            }
            else
            {
                crop.x = surface->w / 2 - static_cast<int>((width / scale) / 2);
                crop.y = 0;
                crop.w = static_cast<int>(width / scale);
                crop.h = surface->h;
            }
        }
        else
        {
            finalW = static_cast<int>(surface->w * scale);
            finalH = static_cast<int>(surface->h * scale);
        }

        SDL_Surface* scaled = SDL_CreateSurface(finalW, finalH, SDL_PIXELFORMAT_RGBA32);
        if (!scaled)
        {
            SDL_DestroySurface(surface);
            throw std::runtime_error(std::string("SDL_CreateSurface failed: ") + SDL_GetError());
        }
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
        const bool blitOk = zoom
            ? SDL_BlitSurfaceScaled(surface, &crop, scaled, nullptr, SDL_SCALEMODE_LINEAR)
            : SDL_BlitSurfaceScaled(surface, nullptr, scaled, nullptr, SDL_SCALEMODE_LINEAR);
        if (!blitOk)
        {
            SDL_DestroySurface(scaled);
            SDL_DestroySurface(surface);
            throw std::runtime_error(std::string("SDL_BlitSurfaceScaled failed: ") + SDL_GetError());
        }

        std::vector<uint8_t> finalPixels(
            static_cast<uint8_t*>(scaled->pixels),
            static_cast<uint8_t*>(scaled->pixels) + static_cast<std::size_t>(finalW) * finalH * 4);

        SDL_DestroySurface(scaled);
        SDL_DestroySurface(surface);

        return MakeTextureFromPixels(graphicsDevice, finalW, finalH, std::move(finalPixels));
    }

    // -----------------------------------------------------------------------
    // SaveAsPng
    // -----------------------------------------------------------------------

    void Texture2D::SaveAsPng(System::IO::Stream* stream, int targetWidth, int targetHeight) const
    {
        if (!stream)
            throw std::invalid_argument("Texture2D::SaveAsPng: stream is null");
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsPng: no CPU-side pixel data available");

        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_RGBA32,
            const_cast<uint8_t*>(cpuPixels_->data()), width * 4);
        if (!surface)
            throw std::runtime_error(std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());

        SDL_Surface* src = surface;
        SDL_Surface* scaled = nullptr;
        if (targetWidth != width || targetHeight != height)
        {
            scaled = SDL_ScaleSurface(surface, targetWidth, targetHeight, SDL_SCALEMODE_LINEAR);
            if (!scaled)
            {
                SDL_DestroySurface(surface);
                throw std::runtime_error(std::string("SDL_ScaleSurface failed: ") + SDL_GetError());
            }
            src = scaled;
        }

        SDL_IOStream* dst = SDL_IOFromDynamicMem();
        if (!dst)
        {
            SDL_DestroySurface(surface);
            if (scaled) SDL_DestroySurface(scaled);
            throw std::runtime_error(std::string("SDL_IOFromDynamicMem failed: ") + SDL_GetError());
        }

        if (!IMG_SavePNG_IO(src, dst, false))
        {
            SDL_CloseIO(dst);
            SDL_DestroySurface(surface);
            if (scaled) SDL_DestroySurface(scaled);
            throw std::runtime_error(std::string("IMG_SavePNG_IO failed: ") + SDL_GetError());
        }

        const Sint64 size = SDL_TellIO(dst);
        if (size > 0)
        {
            auto* buf = static_cast<uint8_t*>(
                SDL_GetPointerProperty(SDL_GetIOProperties(dst),
                                       SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
            if (buf)
                stream->Write(reinterpret_cast<const System::IO::bytecs*>(buf), 0,
                              static_cast<System::IO::intcs>(size));
        }

        SDL_CloseIO(dst);
        if (scaled) SDL_DestroySurface(scaled);
        SDL_DestroySurface(surface);
    }

    void Texture2D::SaveAsPng(const std::string& filename) const
    {
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsPng: no CPU-side pixel data available");

        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_RGBA32,
            const_cast<uint8_t*>(cpuPixels_->data()), width * 4);

        if (!surface)
            throw std::runtime_error(std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());

        if (!IMG_SavePNG(surface, filename.c_str()))
        {
            SDL_DestroySurface(surface);
            throw std::runtime_error(std::string("IMG_SavePNG failed: ") + SDL_GetError());
        }
        SDL_DestroySurface(surface);
    }

    // -----------------------------------------------------------------------
    // SaveAsJpeg
    // -----------------------------------------------------------------------

    // Mirrors FNA's Texture2D.SaveAsJpeg quality lookup: FNA_GRAPHICS_JPEG_SAVE_QUALITY env var,
    // falling back to 100 if unset or unparseable.
    static int GetJpegSaveQuality()
    {
        const char* qualityString = std::getenv("FNA_GRAPHICS_JPEG_SAVE_QUALITY");
        if (qualityString && *qualityString)
        {
            try { return std::stoi(qualityString); }
            catch (...) { /* fall through to default */ }
        }
        return 100;
    }

    void Texture2D::SaveAsJpeg(System::IO::Stream* stream, int targetWidth, int targetHeight) const
    {
        if (!stream)
            throw std::invalid_argument("Texture2D::SaveAsJpeg: stream is null");
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsJpeg: no CPU-side pixel data available");

        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_RGBA32,
            const_cast<uint8_t*>(cpuPixels_->data()), width * 4);
        if (!surface)
            throw std::runtime_error(std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());

        SDL_Surface* src = surface;
        SDL_Surface* scaled = nullptr;
        if (targetWidth != width || targetHeight != height)
        {
            scaled = SDL_ScaleSurface(surface, targetWidth, targetHeight, SDL_SCALEMODE_LINEAR);
            if (!scaled)
            {
                SDL_DestroySurface(surface);
                throw std::runtime_error(std::string("SDL_ScaleSurface failed: ") + SDL_GetError());
            }
            src = scaled;
        }

        SDL_IOStream* dst = SDL_IOFromDynamicMem();
        if (!dst)
        {
            SDL_DestroySurface(surface);
            if (scaled) SDL_DestroySurface(scaled);
            throw std::runtime_error(std::string("SDL_IOFromDynamicMem failed: ") + SDL_GetError());
        }

        if (!IMG_SaveJPG_IO(src, dst, false, GetJpegSaveQuality()))
        {
            SDL_CloseIO(dst);
            SDL_DestroySurface(surface);
            if (scaled) SDL_DestroySurface(scaled);
            throw std::runtime_error(std::string("IMG_SaveJPG_IO failed: ") + SDL_GetError());
        }

        const Sint64 size = SDL_TellIO(dst);
        if (size > 0)
        {
            auto* buf = static_cast<uint8_t*>(
                SDL_GetPointerProperty(SDL_GetIOProperties(dst),
                                       SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
            if (buf)
                stream->Write(reinterpret_cast<const System::IO::bytecs*>(buf), 0,
                              static_cast<System::IO::intcs>(size));
        }

        SDL_CloseIO(dst);
        if (scaled) SDL_DestroySurface(scaled);
        SDL_DestroySurface(surface);
    }

    void Texture2D::SaveAsJpeg(const std::string& filename) const
    {
        if (!cpuPixels_ || cpuPixels_->empty())
            throw std::runtime_error("Texture2D::SaveAsJpeg: no CPU-side pixel data available");

        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            width, height, SDL_PIXELFORMAT_RGBA32,
            const_cast<uint8_t*>(cpuPixels_->data()), width * 4);
        if (!surface)
            throw std::runtime_error(std::string("SDL_CreateSurfaceFrom failed: ") + SDL_GetError());

        if (!IMG_SaveJPG(surface, filename.c_str(), GetJpegSaveQuality()))
        {
            SDL_DestroySurface(surface);
            throw std::runtime_error(std::string("IMG_SaveJPG failed: ") + SDL_GetError());
        }
        SDL_DestroySurface(surface);
    }

    // -----------------------------------------------------------------------
    // NOXNA helpers
    // -----------------------------------------------------------------------

    SDL_Texture* Texture2D::GetNativeTextureInternal() const
    {
        return backend_ ? backend_->GetNativeTexture() : nullptr;
    }

    Texture2D Texture2D::CreateFromPixels(GraphicsDevice& device,
                                          int w, int h,
                                          const std::vector<std::uint8_t>& rgba)
    {
        ImageData data;
        data.width  = w;
        data.height = h;
        data.pixels = rgba;
        Texture2D tex;
        tex.graphicsDevice_ = &device;
        tex.width           = w;
        tex.height          = h;
        tex.backend_        = device.GetBackend().CreateTexture(data);
        tex.cpuPixels_      = std::make_shared<std::vector<uint8_t>>(std::move(data.pixels));
        tex.backend_->ShareCpuPixels(tex.cpuPixels_);
        tex.MaybeFreeCpuPixels();
        return tex;
    }

    Texture2D Texture2D::CreateCpuOnlyForTests(int w, int h, SurfaceFormat format,
                                               const std::vector<Color>& pixels)
    {
        Texture2D tex;              // default ctor: no GraphicsDevice, null backend
        tex.width       = w;
        tex.height      = h;
        tex.format_     = format;
        tex.levelCount_ = 1;

        auto buf = std::make_shared<std::vector<uint8_t>>();
        buf->reserve(pixels.size() * 4);
        for (const Color& c : pixels)
        {
            buf->push_back(c.getRProperty());
            buf->push_back(c.getGProperty());
            buf->push_back(c.getBProperty());
            buf->push_back(c.getAProperty());
        }
        tex.cpuPixels_ = std::move(buf);
        return tex;
    }

    Texture2D Texture2D::CreateWithBackendForTests(int w, int h,
                                                   std::shared_ptr<ITextureBackend> backend)
    {
        Texture2D tex;              // default ctor: no GraphicsDevice
        tex.width   = w;
        tex.height  = h;
        tex.backend_ = std::move(backend);
        return tex;
    }

    Texture2D Texture2D::ReconstructFromCache(GraphicsDevice& device,
                                              int w, int h,
                                              SurfaceFormat fmt,
                                              int levelCount,
                                              std::shared_ptr<ITextureBackend> backend,
                                              std::shared_ptr<std::vector<uint8_t>> cpuPixels)
    {
        Texture2D tex(device, w, h, fmt, levelCount, std::move(backend));
        tex.cpuPixels_ = std::move(cpuPixels);
        return tex;
    }
}
