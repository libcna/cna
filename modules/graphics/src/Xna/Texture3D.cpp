// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

// plans/plan_dx9.md Phase D9-10 (D9-103 follow-up): GraphicsProfile.Reach/HiDef volume-texture ceilings.
// plans/plan_runtimerenderer.md design decision 9: asked of the active renderer rather than the
// preprocessor -- only D3D9 has a real capability structure to answer from.

namespace Microsoft::Xna::Framework::Graphics
{
    // XNA passes zero levels to D3D9 for a mipmapped volume, requesting the complete chain. All
    // three dimensions therefore participate; FNA's width/height-only helper is a lower-authority
    // divergence that truncates depth-dominant textures.
    static int CalculateMipLevels(int w, int h, int d)
    {
        int levels = 1;
        while (w > 1 || h > 1 || d > 1)
        {
            w = std::max(1, w / 2);
            h = std::max(1, h / 2);
            d = std::max(1, d / 2);
            ++levels;
        }
        return levels;
    }

    static int MipDimension(int base, int level)
    {
        return std::max(1, base >> level);
    }

    static void ValidateCopyArguments(int startIndex, int elementCount)
    {
        if (startIndex < 0)
            throw System::ArgumentOutOfRangeException("dataIndex");
        if (elementCount <= 0)
            throw System::ArgumentOutOfRangeException("elementCount");
        if (static_cast<std::int64_t>(startIndex) + static_cast<std::int64_t>(elementCount) >
            static_cast<std::int64_t>((std::numeric_limits<int>::max)()))
        {
            throw System::ArgumentOutOfRangeException("elementCount");
        }
    }

    static void ValidateTotalSize(int elementCount, std::size_t requiredElements)
    {
        if (requiredElements > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
            static_cast<std::size_t>(elementCount) != requiredElements)
            throw System::ArgumentException("The data size does not match the requested volume.");
    }

    // D9-103 follow-up: D9-100's own table -- GraphicsProfile.Reach does not support volume
    // textures AT ALL (a reported extent of 0), not merely a small size ceiling; GraphicsProfile
    // .HiDef caps at 256 in any dimension. SOFTWARE-179 made these renderer-independent profile
    // rules the common renderer default rather than a D3D9-only behavior.
    static void ValidateVolumeSizeForProfileEXT(const GraphicsDevice& device, int width, int height, int depth)
    {
        const int profile = static_cast<int>(device.getGraphicsProfileProperty());
        const int maxExtent = device.GetRenderer().GetMaxVolumeExtentForProfileEXT(profile);
        if (maxExtent == 0)
        {
            throw System::NotSupportedException(
                "Texture3D: GraphicsProfile.Reach does not support volume (3D) textures at all");
        }
        if (width > maxExtent || height > maxExtent || depth > maxExtent)
        {
            throw System::NotSupportedException(
                "Texture3D: " + std::to_string(width) + "x" + std::to_string(height) + "x" +
                std::to_string(depth) + " exceeds GraphicsProfile.HiDef's own maximum volume "
                "extent of " + std::to_string(maxExtent) + " in any dimension");
        }
    }

    static void ValidateTexture3DFormatEXT(const GraphicsDevice& device, SurfaceFormat format)
    {
        if (!Texture::IsVolumeFormatAllowedByProfileEXT(
                device.getGraphicsProfileProperty(), format))
        {
            throw System::NotSupportedException(
                "Texture3D: this SurfaceFormat is not available for a volume texture on the "
                "selected GraphicsProfile.");
        }
        switch (device.GetRenderer().ClassifyTexture3DFormatEXT(static_cast<int>(format)))
        {
            case CNA::Internal::Renderers::RendererFormatVerdict::Supported:
                return;
            case CNA::Internal::Renderers::RendererFormatVerdict::Unsupported:
                throw System::NotSupportedException(
                    "Texture3D: this SurfaceFormat is not supported by the active renderer.");
            case CNA::Internal::Renderers::RendererFormatVerdict::Defer:
                Texture::ValidateFormat(format);
                return;
        }
    }

    Texture3D::~Texture3D() = default;
    Texture3D::Texture3D(Texture3D&&) noexcept = default;
    Texture3D& Texture3D::operator=(Texture3D&&) noexcept = default;

    Texture3D::Texture3D(GraphicsDevice& device, int width, int height, int depth, bool mipMap, SurfaceFormat format)
        : Texture(&device)
        , width_(width)
        , height_(height)
        , depth_(depth)
        , renderer_(nullptr)
    {
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(width, "width");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(height, "height");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(depth, "depth");
        // REMED-CONTENT-004: renderers without real volume storage leave CreateTexture3D() at its
        // null shared default. Previously that let every subsequent SetData()/GetData() call
        // silently no-op, so the capability is checked before renderer creation instead.
        if (!device.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        {
            throw System::NotSupportedException(
                "Texture3D: this renderer does not support real volume (3D) texture storage");
        }
        ValidateVolumeSizeForProfileEXT(device, width, height, depth);
        ValidateTexture3DFormatEXT(device, format);
        format_     = format;
        levelCount_ = mipMap ? CalculateMipLevels(width, height, depth) : 1;
        renderer_ = device.GetRenderer().CreateTexture3D(width, height, depth, mipMap, static_cast<int>(format));
    }

    void Texture3D::Dispose(bool disposing)
    {
        renderer_.reset();
        Texture::Dispose(disposing);
    }

    int Texture3D::getWidthProperty() const { return width_; }
    int Texture3D::getHeightProperty() const { return height_; }
    int Texture3D::getDepthProperty() const { return depth_; }

    const std::string& Texture3D::GetTypeName() const
    {
        static const std::string name = "Microsoft.Xna.Framework.Graphics.Texture3D";
        return name;
    }

    int Texture3D::ValidateTypedTransferEXT(
        const char* api, bool setting, int level, int left, int top, int right, int bottom,
        int front, int back, const void* data, int startIndex, int elementCount,
        int elementBytes) const
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture3D");
        if (data == nullptr)
            throw System::ArgumentNullException("data");
        ThrowIfDataTransferResourceInUseEXT(setting);
        if (level < 0 || level >= levelCount_)
        {
            throw System::InvalidOperationException(
                std::string(api) + ": level " + std::to_string(level) +
                " must be in [0, LevelCount " + std::to_string(levelCount_) + ")");
        }
        ValidateCopyArguments(startIndex, elementCount);
        const int formatBytes = Texture::GetFormatSizeEXT(format_);
        if (Texture::GetBlockSizeSquaredEXT(format_) != 1 ||
            formatBytes % elementBytes != 0)
        {
            throw System::ArgumentException(
                std::string(api) +
                ": element width does not divide the uncompressed volume format.");
        }
        if (left < 0 || left >= right || top < 0 || top >= bottom ||
            front < 0 || front >= back)
        {
            throw System::ArgumentException("The box position or size is invalid.", "box");
        }
        if (right > MipDimension(width_, level) || bottom > MipDimension(height_, level) ||
            back > MipDimension(depth_, level))
        {
            throw System::ArgumentException("The box is outside the mip level.", "box");
        }
        const std::size_t boxVoxels =
            static_cast<std::size_t>(right - left) *
            static_cast<std::size_t>(bottom - top) *
            static_cast<std::size_t>(back - front);
        const std::size_t requiredElements =
            boxVoxels * static_cast<std::size_t>(formatBytes) /
            static_cast<std::size_t>(elementBytes);
        ValidateTotalSize(elementCount, requiredElements);
        return static_cast<int>(requiredElements);
    }

    void Texture3D::SetTypedDataBytesEXT(
        int level, int left, int top, int right, int bottom, int front, int back,
        const std::uint8_t* data)
    {
        if (!renderer_)
            throw System::NotSupportedException(
                "Texture3D::SetData: this renderer creates no volume texture resource");
        const int dataLength = (right - left) * (bottom - top) * (back - front) *
            Texture::GetFormatSizeEXT(format_);
        if (!renderer_->SetDataBytesEXT(
                level, left, top, front, right - left, bottom - top, back - front,
                data, dataLength))
        {
            throw System::NotSupportedException(
                "Texture3D::SetData: the active renderer did not store the complete "
                "declared-format volume region");
        }
    }

    void Texture3D::GetTypedDataBytesEXT(
        int level, int left, int top, int right, int bottom, int front, int back,
        std::uint8_t* data) const
    {
        if (!renderer_)
            throw System::NotSupportedException(
                "Texture3D::GetData: this renderer creates no volume texture resource");
        const int dataLength = (right - left) * (bottom - top) * (back - front) *
            Texture::GetFormatSizeEXT(format_);
        if (!renderer_->GetDataBytesEXT(
                level, left, top, front, right - left, bottom - top, back - front,
                data, dataLength))
        {
            throw System::NotSupportedException(
                "Texture3D::GetData: the active renderer did not return the complete "
                "declared-format volume region");
        }
    }

    // Color has a vtable pointer (sizeof(Color) == 24), so we must never pass
    // Color* directly to GL. Always unpack to plain uint8_t RGBA first.

    static std::vector<uint8_t> colorsToRgba(const Color* data, int startIndex, int count)
    {
        std::vector<uint8_t> rgba(static_cast<std::size_t>(count) * 4);
        for (int i = 0; i < count; ++i)
        {
            rgba[i * 4 + 0] = data[startIndex + i].getRProperty();
            rgba[i * 4 + 1] = data[startIndex + i].getGProperty();
            rgba[i * 4 + 2] = data[startIndex + i].getBProperty();
            rgba[i * 4 + 3] = data[startIndex + i].getAProperty();
        }
        return rgba;
    }

    void Texture3D::SetData(const Color* data, int elementCount)
    {
        SetData(data, 0, elementCount);
    }

    void Texture3D::SetData(const Color* data, int startIndex, int elementCount)
    {
        // Matches FNA's Texture3D.SetData<T>(T[],int,int), which delegates to the 10-arg
        // overload covering the full texture at level 0.
        SetData(0, 0, 0, width_, height_, 0, depth_, data, startIndex, elementCount);
    }

    void Texture3D::SetData(int level, int left, int top, int right, int bottom, int front, int back,
                            const Color* data, int startIndex, int elementCount)
    {
        const int requiredColorElements = ValidateTypedTransferEXT(
            "Texture3D::SetData", true, level, left, top, right, bottom, front, back,
            data, startIndex, elementCount, 4);

        // REMED-GFX-135 -- see TextureCube::SetData's identical note: converted to the REQUESTED
        // BOX rather than to elementCount, so the call never reads source elements it does not
        // upload and the buffer length always matches the region the renderer is told to write.
        const auto rgba = colorsToRgba(
            data, startIndex, static_cast<int>(requiredColorElements));
        if (format_ == SurfaceFormat::Color)
        {
            SetDataPointerEXT(level, left, top, right, bottom, front, back,
                              rgba.data(), static_cast<int>(rgba.size()));
        }
        else
        {
            SetTypedDataBytesEXT(level, left, top, right, bottom, front, back, rgba.data());
        }
    }

    void Texture3D::SetDataPointerEXT(int level, int left, int top, int right, int bottom, int front, int back,
                                      const void* data, int dataLength)
    {
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture3D");
        if (!data)
            throw std::invalid_argument("Texture3D::SetDataPointerEXT: data must not be null");

        // REMED-GFX-135: `if (renderer_)` used to drop the upload silently. Texture3D's constructor
        // already refuses a device that reports no GraphicsCapability::Texture3D, so a null renderer
        // here means the resource has been disposed out from under this call or the renderer failed
        // to allocate -- neither of which is a successful store.
        if (!renderer_)
        {
            throw System::NotSupportedException(
                "Texture3D::SetDataPointerEXT: this graphics renderer creates no volume texture "
                "resource, so its content cannot be stored");
        }
        if (!renderer_->SetData(level, left, top, front,
                               right - left, bottom - top, back - front,
                               data, dataLength))
        {
            throw System::NotSupportedException(
                "Texture3D::SetDataPointerEXT: this graphics renderer did not store the complete "
                "requested volume region -- the mip level or box is not supported here");
        }
    }

    static void rgbaToColors(const std::vector<uint8_t>& rgba, Color* data, int startIndex, int count)
    {
        for (int i = 0; i < count; ++i)
            data[startIndex + i] = Color(rgba[i * 4 + 0], rgba[i * 4 + 1],
                                         rgba[i * 4 + 2], rgba[i * 4 + 3]);
    }

    void Texture3D::GetData(Color* data, int elementCount) const
    {
        GetData(data, 0, elementCount);
    }

    void Texture3D::GetData(Color* data, int startIndex, int elementCount) const
    {
        // Matches FNA's Texture3D.GetData<T>(T[],int,int), which delegates to the 10-arg
        // overload covering the full texture at level 0.
        GetData(0, 0, 0, width_, height_, 0, depth_, data, startIndex, elementCount);
    }

    void Texture3D::GetData(int level, int left, int top, int right, int bottom, int front, int back,
                            Color* data, int startIndex, int elementCount) const
    {
        const int requiredColorElements = ValidateTypedTransferEXT(
            "Texture3D::GetData", false, level, left, top, right, bottom, front, back,
            data, startIndex, elementCount, 4);

        const int boxW = right - left;
        const int boxH = bottom - top;
        const int boxD = back - front;
        Texture::ValidateGetDataFormat(format_, 4);

        // REMED-GFX-130 -- see TextureCube::GetData for the full reasoning. `rgba` is scratch memory
        // this layer zero-initializes, so it is never handed to the caller unless the renderer
        // reports it filled the whole box; otherwise the caller's `data` stays byte-for-byte as it
        // was and the missing capability is raised. A null renderer is the same answer one step
        // earlier: no volume storage exists, so there is nothing to read.
        if (!renderer_)
        {
            throw System::NotSupportedException(
                "Texture3D::GetData: this graphics renderer creates no volume texture resource, so "
                "its content cannot be read back");
        }

        // Sized to the REQUESTED BOX, not to elementCount -- see TextureCube::GetData's identical
        // note: a larger elementCount would otherwise return this buffer's untouched tail as content.
        std::vector<uint8_t> rgba(static_cast<std::size_t>(requiredColorElements) * 4u, 0);
        const bool read = format_ == SurfaceFormat::Color
            ? renderer_->GetData(level, left, top, front, boxW, boxH, boxD,
                                 rgba.data(), static_cast<int>(rgba.size()))
            : renderer_->GetDataBytesEXT(level, left, top, front, boxW, boxH, boxD,
                                         rgba.data(), static_cast<int>(rgba.size()));
        if (!read)
        {
            throw System::NotSupportedException(
                "Texture3D::GetData: this graphics renderer cannot read a volume texture back to the "
                "CPU at the requested mip level");
        }
        rgbaToColors(rgba, data, startIndex, static_cast<int>(requiredColorElements));
    }
}
