// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace CNA::Internal::Renderers::Software
{
    namespace
    {
        int CalculateCubeTargetMipLevels(int size)
        {
            int levels = 1;
            while (size > 1)
            {
                size = std::max(1, size / 2);
                ++levels;
            }
            return levels;
        }

        bool ValidFaceRegion(int face, int level, int levelCount, int dimension,
                             int x, int y, int width, int height, const void* data,
                             int dataLength)
        {
            if (data == nullptr || face < 0 || face > 5 || level < 0 || level >= levelCount ||
                width <= 0 || height <= 0 || x < 0 || y < 0 ||
                x > dimension - width || y > dimension - height || dataLength < 0)
                return false;
            const std::size_t required = static_cast<std::size_t>(width) *
                                         static_cast<std::size_t>(height) * 4u;
            return static_cast<std::size_t>(dataLength) >= required;
        }
    }

    SoftwareRenderTargetCubeRenderer::SoftwareRenderTargetCubeRenderer(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
        : size_(size), depthFormat_(depthFormat), mipMap_(mipMap),
          levelCount_(mipMap ? CalculateCubeTargetMipLevels(size) : 1)
    {
        (void)preserveContents;
        if (size <= 0)
            throw std::invalid_argument(
                "SoftwareRenderTargetCubeRenderer: size must be positive");
        if (depthFormat < 0 || depthFormat > 3)
            throw std::invalid_argument(
                "SoftwareRenderTargetCubeRenderer: invalid DepthFormat ordinal");

        const bool hasDepth = depthFormat_ != 0;
        const bool hasStencil = depthFormat_ == 3;
        for (SoftwareFramebuffer& framebuffer : framebuffers_)
        {
            framebuffer.allocateDepthStorage = hasDepth;
            framebuffer.allocateStencilStorage = hasStencil;
            framebuffer.Resize(size_, size_);
            framebuffer.SetMultiSampleCount(multiSampleCount);
        }
        multiSampleCount_ = framebuffers_[0].multiSampleCount;

        sharedDepth_ = framebuffers_[0].depthBuffer;
        sharedStencil_ = framebuffers_[0].stencilBuffer;
        sharedMultiSampleDepth_ = framebuffers_[0].multiSampleDepthBuffer;
        sharedMultiSampleStencil_ = framebuffers_[0].multiSampleStencilBuffer;

        mipLevels_.resize(static_cast<std::size_t>(levelCount_));
        supplied_.resize(static_cast<std::size_t>(levelCount_));
        for (int face = 0; face < 6; ++face)
            supplied_[0][static_cast<std::size_t>(face)] = true;
        for (int level = 1; level < levelCount_; ++level)
        {
            const int dimension = CubeFaceDimension(level);
            const std::size_t bytes = static_cast<std::size_t>(dimension) *
                                      static_cast<std::size_t>(dimension) * 4u;
            for (std::vector<std::uint8_t>& face :
                 mipLevels_[static_cast<std::size_t>(level)])
                face.assign(bytes, 0u);
        }
    }

    int SoftwareRenderTargetCubeRenderer::CubeFaceDimension(int level) const
    {
        if (level <= 0)
            return size_;
        if (level >= levelCount_)
            return 1;
        return std::max(1, size_ >> level);
    }

    int SoftwareRenderTargetCubeRenderer::CubeFaceLevelCount(int face) const
    {
        if (face < 0 || face > 5)
            return 1;
        return faceLevelCounts_[static_cast<std::size_t>(face)];
    }

    const std::vector<std::uint8_t>& SoftwareRenderTargetCubeRenderer::CubeFacePixels(
        int face, int level) const
    {
        const int validFace = face >= 0 && face < 6 ? face : 0;
        const int validLevel = level >= 0 && level < levelCount_ ? level : 0;
        if (validLevel == 0)
        {
            framebuffers_[static_cast<std::size_t>(validFace)].ResolveColor();
            return framebuffers_[static_cast<std::size_t>(validFace)].color;
        }
        return mipLevels_[static_cast<std::size_t>(validLevel)]
                         [static_cast<std::size_t>(validFace)];
    }

    std::vector<std::uint8_t>& SoftwareRenderTargetCubeRenderer::MutableFacePixels(
        int face, int level)
    {
        if (level == 0)
            return framebuffers_[static_cast<std::size_t>(face)].color;
        return mipLevels_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)];
    }

    bool SoftwareRenderTargetCubeRenderer::SetData(
        int face, int level, int x, int y, int width, int height,
        const void* data, int dataLength)
    {
        const int dimension = CubeFaceDimension(level);
        if (!ValidFaceRegion(face, level, levelCount_, dimension,
                             x, y, width, height, data, dataLength))
            return false;

        SoftwareFramebuffer& framebuffer = framebuffers_[static_cast<std::size_t>(face)];
        if (level == 0 && framebuffer.HasMultiSampleColor())
            framebuffer.ResolveColor();

        std::vector<std::uint8_t>& destination = MutableFacePixels(face, level);
        const auto* source = static_cast<const std::uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
        for (int row = 0; row < height; ++row)
        {
            const std::size_t sourceOffset = static_cast<std::size_t>(row) * rowBytes;
            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dimension) +
                 static_cast<std::size_t>(x)) * 4u;
            std::copy_n(source + sourceOffset, rowBytes,
                        destination.begin() +
                            static_cast<std::ptrdiff_t>(destinationOffset));
        }

        if (level == 0)
        {
            framebuffer.CopyResolvedColorToMultiSample();
            if (!boundFaces_[static_cast<std::size_t>(face)])
                GenerateMipMaps(face);
        }
        if (x == 0 && y == 0 && width == dimension && height == dimension)
        {
            supplied_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)] = true;
            int contiguous = 0;
            while (contiguous < levelCount_ &&
                   supplied_[static_cast<std::size_t>(contiguous)]
                            [static_cast<std::size_t>(face)])
                ++contiguous;
            faceLevelCounts_[static_cast<std::size_t>(face)] = std::max(1, contiguous);
        }
        return true;
    }

    bool SoftwareRenderTargetCubeRenderer::GetData(
        int face, int level, int x, int y, int width, int height,
        void* data, int dataLength) const
    {
        const int dimension = CubeFaceDimension(level);
        if (!ValidFaceRegion(face, level, levelCount_, dimension,
                             x, y, width, height, data, dataLength))
            return false;
        if (level > 0 && boundFaces_[static_cast<std::size_t>(face)])
            return false;

        const std::vector<std::uint8_t>& source = CubeFacePixels(face, level);
        auto* destination = static_cast<std::uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
        for (int row = 0; row < height; ++row)
        {
            const std::size_t sourceOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(dimension) +
                 static_cast<std::size_t>(x)) * 4u;
            const std::size_t destinationOffset = static_cast<std::size_t>(row) * rowBytes;
            std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(sourceOffset), rowBytes,
                        destination + destinationOffset);
        }
        return true;
    }

    void SoftwareRenderTargetCubeRenderer::LoadSharedDepthStencil(
        SoftwareFramebuffer& framebuffer)
    {
        framebuffer.depthBuffer = sharedDepth_;
        framebuffer.stencilBuffer = sharedStencil_;
        framebuffer.multiSampleDepthBuffer = sharedMultiSampleDepth_;
        framebuffer.multiSampleStencilBuffer = sharedMultiSampleStencil_;
    }

    void SoftwareRenderTargetCubeRenderer::StoreSharedDepthStencil(
        const SoftwareFramebuffer& framebuffer)
    {
        sharedDepth_ = framebuffer.depthBuffer;
        sharedStencil_ = framebuffer.stencilBuffer;
        sharedMultiSampleDepth_ = framebuffer.multiSampleDepthBuffer;
        sharedMultiSampleStencil_ = framebuffer.multiSampleStencilBuffer;
    }

    void SoftwareRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        if (bound_)
            UnbindAsRenderTarget();

        (void)BindForMrt(face, true);
        activeFace_ = face;
        bound_ = true;
    }

    void SoftwareRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        if (!bound_)
            return;
        UnbindForMrt(activeFace_, true);
        bound_ = false;
        activeFace_ = -1;
    }

    SoftwareFramebuffer& SoftwareRenderTargetCubeRenderer::BindForMrt(
        int face, bool ownsDepthStencil)
    {
        if (face < 0 || face > 5)
            throw std::out_of_range(
                "SoftwareRenderTargetCubeRenderer: invalid cube face");
        const std::size_t faceIndex = static_cast<std::size_t>(face);
        if (boundFaces_[faceIndex])
            throw std::logic_error(
                "SoftwareRenderTargetCubeRenderer: cube face is already bound");

        SoftwareFramebuffer& framebuffer = framebuffers_[faceIndex];
        if (ownsDepthStencil)
            LoadSharedDepthStencil(framebuffer);
        for (int level = 1; level < levelCount_; ++level)
            supplied_[static_cast<std::size_t>(level)][faceIndex] = false;
        faceLevelCounts_[faceIndex] = 1;
        boundFaces_[faceIndex] = true;
        return framebuffer;
    }

    void SoftwareRenderTargetCubeRenderer::UnbindForMrt(
        int face, bool ownsDepthStencil)
    {
        if (face < 0 || face > 5)
            throw std::out_of_range(
                "SoftwareRenderTargetCubeRenderer: invalid cube face");
        const std::size_t faceIndex = static_cast<std::size_t>(face);
        if (!boundFaces_[faceIndex])
            return;
        SoftwareFramebuffer& framebuffer = framebuffers_[faceIndex];
        framebuffer.ResolveColor();
        GenerateMipMaps(face);
        if (ownsDepthStencil)
            StoreSharedDepthStencil(framebuffer);
        boundFaces_[faceIndex] = false;
    }

    void SoftwareRenderTargetCubeRenderer::GenerateMipMaps(int face)
    {
        if (!mipMap_ || levelCount_ <= 1)
            return;
        SoftwareFramebuffer& framebuffer = framebuffers_[static_cast<std::size_t>(face)];
        framebuffer.ResolveColor();
        const std::vector<std::uint8_t>* source = &framebuffer.color;
        int sourceDimension = size_;
        for (int level = 1; level < levelCount_; ++level)
        {
            const int destinationDimension = CubeFaceDimension(level);
            std::vector<std::uint8_t>& destination = MutableFacePixels(face, level);
            for (int y = 0; y < destinationDimension; ++y)
            {
                const int y0 = std::min(sourceDimension - 1, y * 2);
                const int y1 = std::min(sourceDimension - 1, y * 2 + 1);
                for (int x = 0; x < destinationDimension; ++x)
                {
                    const int x0 = std::min(sourceDimension - 1, x * 2);
                    const int x1 = std::min(sourceDimension - 1, x * 2 + 1);
                    for (int channel = 0; channel < 4; ++channel)
                    {
                        const std::size_t destinationIndex =
                            (static_cast<std::size_t>(y) * destinationDimension + x) * 4u +
                            static_cast<std::size_t>(channel);
                        const int sum =
                            (*source)[(static_cast<std::size_t>(y0) * sourceDimension + x0) * 4u + channel] +
                            (*source)[(static_cast<std::size_t>(y0) * sourceDimension + x1) * 4u + channel] +
                            (*source)[(static_cast<std::size_t>(y1) * sourceDimension + x0) * 4u + channel] +
                            (*source)[(static_cast<std::size_t>(y1) * sourceDimension + x1) * 4u + channel];
                        destination[destinationIndex] = static_cast<std::uint8_t>(sum / 4);
                    }
                }
            }
            supplied_[static_cast<std::size_t>(level)][static_cast<std::size_t>(face)] = true;
            source = &destination;
            sourceDimension = destinationDimension;
        }
        faceLevelCounts_[static_cast<std::size_t>(face)] = levelCount_;
    }

    SoftwareFramebuffer& SoftwareRenderTargetCubeRenderer::Framebuffer()
    {
        if (!bound_ || activeFace_ < 0)
            throw std::logic_error(
                "SoftwareRenderTargetCubeRenderer: no face is bound");
        return framebuffers_[static_cast<std::size_t>(activeFace_)];
    }

    const SoftwareFramebuffer& SoftwareRenderTargetCubeRenderer::Framebuffer() const
    {
        if (!bound_ || activeFace_ < 0)
            throw std::logic_error(
                "SoftwareRenderTargetCubeRenderer: no face is bound");
        return framebuffers_[static_cast<std::size_t>(activeFace_)];
    }
}
