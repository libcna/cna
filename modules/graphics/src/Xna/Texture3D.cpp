// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

// plan_dx9.md Phase D9-10 (D9-103 follow-up): GraphicsProfile.Reach/HiDef volume-texture ceilings.
// plan_runtimerenderer.md design decision 9: asked of the active renderer rather than the
// preprocessor -- only D3D9 has a real capability structure to answer from.

namespace Microsoft::Xna::Framework::Graphics
{
    // Mirrors FNA's Texture.CalculateMipLevels(width, height) — depth does not participate,
    // matching Texture3D.cs's constructor: LevelCount = mipMap ? CalculateMipLevels(width, height) : 1.
    static int CalculateMipLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }

    // D9-103 follow-up: D9-100's own table -- GraphicsProfile.Reach does not support volume
    // textures AT ALL (a reported extent of 0), not merely a small size ceiling; GraphicsProfile
    // .HiDef caps at 256 in any dimension. Renderers with no profile distinction report no ceiling.
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
        // REMED-CONTENT-004: Headless and Software both leave IGraphicsRenderer::CreateTexture3D()
        // at its shared default (returns nullptr) -- Headless has no real GPU resource of any kind
        // by design; Software's Texture3D support is an explicit, documented v1 scope boundary
        // (plan_software.md Boundaries), not an oversight. Previously this left renderer_ null and
        // every subsequent SetData()/GetData() call silently no-op'd instead of failing -- a caller
        // had no way to know their data was silently discarded. Checked ahead of renderer creation,
        // matching this file's own D3D9 profile-ceiling check immediately below and the
        // GraphicsCapability doc's own "query before relying on the feature" convention.
        if (!device.SupportsCapability(CNA::GraphicsCapability::Texture3D))
        {
            throw System::NotSupportedException(
                "Texture3D: this renderer does not support real volume (3D) texture storage");
        }
        ValidateVolumeSizeForProfileEXT(device, width, height, depth);
        Texture::ValidateFormat(format);
        format_     = format;
        levelCount_ = mipMap ? CalculateMipLevels(width, height) : 1;
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
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture3D");
        if (!data)
            throw std::invalid_argument("Texture3D::SetData: data must not be null");
        if (elementCount <= 0)
            throw std::out_of_range("Texture3D::SetData: elementCount must be > 0");
        if (startIndex < 0)
            throw std::out_of_range("Texture3D::SetData: startIndex must be >= 0");
        if (level < 0)
            throw std::out_of_range("Texture3D::SetData: level must be >= 0");
        if (left < 0 || left >= right || top < 0 || top >= bottom || front < 0 || front >= back)
            throw std::out_of_range("Texture3D::SetData: box position/size is invalid");
        const int boxVoxels = (right - left) * (bottom - top) * (back - front);
        if (elementCount < boxVoxels)
            throw std::out_of_range("Texture3D::SetData: elementCount is less than the number of voxels in the requested region");

        // REMED-GFX-135 -- see TextureCube::SetData's identical note: converted to the REQUESTED
        // BOX rather than to elementCount, so the call never reads source elements it does not
        // upload and the buffer length always matches the region the renderer is told to write.
        const auto rgba = colorsToRgba(data, startIndex, boxVoxels);
        SetDataPointerEXT(level, left, top, right, bottom, front, back,
                          rgba.data(), static_cast<int>(rgba.size()));
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
        if (getIsDisposedProperty())
            throw System::ObjectDisposedException("Texture3D");
        if (!data)
            throw std::invalid_argument("Texture3D::GetData: data must not be null");
        if (elementCount <= 0)
            throw std::out_of_range("Texture3D::GetData: elementCount must be > 0");
        if (startIndex < 0)
            throw std::out_of_range("Texture3D::GetData: startIndex must be >= 0");
        if (level < 0)
            throw std::out_of_range("Texture3D::GetData: level must be >= 0");
        if (left < 0 || left >= right || top < 0 || top >= bottom || front < 0 || front >= back)
            throw std::out_of_range("Texture3D::GetData: box position/size is invalid");

        const int boxW = right - left;
        const int boxH = bottom - top;
        const int boxD = back - front;
        if (elementCount < boxW * boxH * boxD)
            throw std::out_of_range("Texture3D::GetData: elementCount is less than the number of voxels in the requested region");
        Texture::ValidateGetDataFormat(format_, 4);

        // REMED-GFX-130 -- see TextureCube::GetData for the full reasoning. `rgba` is scratch memory
        // this layer zero-initializes, so it is never handed to the caller unless the renderer
        // reports it filled the whole box; otherwise the caller's `data` stays byte-for-byte as it
        // was and the missing capability is raised. A null renderer (ASCII keeps
        // IGraphicsRenderer::CreateTexture3D's nullptr default while still reporting
        // GraphicsCapability::Texture3D through SupportsCapability's own `return true` default) is
        // the same answer one step earlier: no volume storage exists, so there is nothing to read.
        if (!renderer_)
        {
            throw System::NotSupportedException(
                "Texture3D::GetData: this graphics renderer creates no volume texture resource, so "
                "its content cannot be read back");
        }

        // Sized to the REQUESTED BOX, not to elementCount -- see TextureCube::GetData's identical
        // note: a larger elementCount would otherwise return this buffer's untouched tail as content.
        std::vector<uint8_t> rgba(
            static_cast<std::size_t>(boxW) * static_cast<std::size_t>(boxH) *
            static_cast<std::size_t>(boxD) * 4, 0);
        if (!renderer_->GetData(level, left, top, front, boxW, boxH, boxD,
                               rgba.data(), static_cast<int>(rgba.size())))
        {
            throw System::NotSupportedException(
                "Texture3D::GetData: this graphics renderer cannot read a volume texture back to the "
                "CPU at the requested mip level");
        }
        rgbaToColors(rgba, data, startIndex, boxW * boxH * boxD);
    }
}
