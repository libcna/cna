// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "SoftwareFramebufferErrors.hpp"

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    SoftwareRenderTargetRenderer::SoftwareRenderTargetRenderer(
        int w, int h, int depthFormat, bool mipMap, int multiSampleCount,
        bool hasRealDepthBuffer, bool hasStandaloneStencilBuffer)
        : framebuffer_(hasRealDepthBuffer,
                       hasStandaloneStencilBuffer || hasRealDepthBuffer)
        , depthFormat_(depthFormat), mipMap_(mipMap), multiSampleCount_(multiSampleCount)
        , hasRealDepthBuffer_(hasRealDepthBuffer)
        , hasStandaloneStencilBuffer_(hasStandaloneStencilBuffer)
    {
        const SoftwareFramebufferAllocationRequest request{
            w, h, hasRealDepthBuffer,
            hasStandaloneStencilBuffer || hasRealDepthBuffer,
            multiSampleCount == 4 ? 4 : 0, mipMap};
        const SoftwareFramebufferAllocationLayout layout =
            PlanSoftwareFramebufferAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidFramebufferLayout(request, layout.error);

        framebuffer_.Resize(w, h);
        framebuffer_.SetMultiSampleCount(multiSampleCount_);
        multiSampleCount_ = framebuffer_.multiSampleCount;
        if (mipMap_)
        {
            int levelWidth = framebuffer_.width;
            int levelHeight = framebuffer_.height;
            while (levelWidth > 1 || levelHeight > 1)
            {
                levelWidth = std::max(1, levelWidth / 2);
                levelHeight = std::max(1, levelHeight / 2);
                ++levelCount_;
            }
            try
            {
                mipLevels_.resize(static_cast<std::size_t>(levelCount_ - 1));
            }
            catch (const std::bad_alloc&)
            {
                ThrowFramebufferAllocationFailure(request, layout);
            }
            catch (const std::length_error&)
            {
                ThrowFramebufferAllocationFailure(request, layout);
            }
        }
    }

    void SoftwareRenderTargetRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr) return;
        const std::size_t rowBytes = static_cast<std::size_t>(framebuffer_.width) * 4u;
        // REMED-GFX-230: RenderTarget2D uploads used to ignore the supplied pitch and read one
        // tight height*width*4 span. That ingested padding into later rows and skipped the real
        // source pixels. Validate before changing storage, then copy only each row's RGBA bytes.
        if (stride > 0 && static_cast<std::size_t>(stride) < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "A positive render-target upload stride must be at least " +
                    std::to_string(rowBytes) + " bytes (width * 4 for RGBA8).");
        const std::size_t effectiveStride =
            stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
        for (int row = 0; row < framebuffer_.height; ++row)
        {
            std::copy(rgba + static_cast<std::size_t>(row) * effectiveStride,
                      rgba + static_cast<std::size_t>(row) * effectiveStride + rowBytes,
                      framebuffer_.color.begin() +
                          static_cast<std::ptrdiff_t>(row) *
                              static_cast<std::ptrdiff_t>(rowBytes));
        }
        framebuffer_.CopyResolvedColorToMultiSample();
        mipLevelsReady_ = false;
        // A direct CPU upload is already complete unless this target is actively being rendered.
        // Generate now so an uploaded, unbound RenderTarget2D never exposes stale lower levels.
        if (!bound_)
            GenerateMipMaps();
    }

    int SoftwareRenderTargetRenderer::MipWidth(int level) const
    {
        int width = framebuffer_.width;
        for (int i = 0; i < level; ++i)
            width = std::max(1, width / 2);
        return width;
    }

    int SoftwareRenderTargetRenderer::MipHeight(int level) const
    {
        int height = framebuffer_.height;
        for (int i = 0; i < level; ++i)
            height = std::max(1, height / 2);
        return height;
    }

    int SoftwareRenderTargetRenderer::ColorWidth(int level) const
    {
        return level > 0 && level < ColorLevelCount() ? MipWidth(level) : framebuffer_.width;
    }

    int SoftwareRenderTargetRenderer::ColorHeight(int level) const
    {
        return level > 0 && level < ColorLevelCount() ? MipHeight(level) : framebuffer_.height;
    }

    const std::vector<std::uint8_t>& SoftwareRenderTargetRenderer::ColorPixels(int level) const
    {
        if (level > 0 && level < ColorLevelCount())
            return mipLevels_[static_cast<std::size_t>(level - 1)];
        return framebuffer_.color;
    }

    void SoftwareRenderTargetRenderer::GenerateMipMaps()
    {
        if (!mipMap_ || levelCount_ <= 1)
            return;

        const SoftwareFramebufferAllocationRequest request{
            framebuffer_.width, framebuffer_.height,
            framebuffer_.allocateDepthStorage, framebuffer_.allocateStencilStorage,
            framebuffer_.multiSampleCount, true};
        const SoftwareFramebufferAllocationLayout layout =
            PlanSoftwareFramebufferAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidFramebufferLayout(request, layout.error);

        try
        {
            const std::vector<std::uint8_t>* source = &framebuffer_.color;
            int sourceWidth = framebuffer_.width;
            int sourceHeight = framebuffer_.height;
            for (int level = 1; level < levelCount_; ++level)
            {
                const int destinationWidth = MipWidth(level);
                const int destinationHeight = MipHeight(level);
                std::vector<std::uint8_t>& destination =
                    mipLevels_[static_cast<std::size_t>(level - 1)];
                destination.resize(static_cast<std::size_t>(destinationWidth) *
                                   static_cast<std::size_t>(destinationHeight) * 4u);

                // Match the CPU box-filter convention used by the D3D12 render-target renderer: a
                // 2x2 average with the second source coordinate clamped for odd dimensions.
                for (int y = 0; y < destinationHeight; ++y)
                {
                    const int sy0 = std::min(sourceHeight - 1, y * 2);
                    const int sy1 = std::min(sourceHeight - 1, y * 2 + 1);
                    for (int x = 0; x < destinationWidth; ++x)
                    {
                        const int sx0 = std::min(sourceWidth - 1, x * 2);
                        const int sx1 = std::min(sourceWidth - 1, x * 2 + 1);
                        for (int channel = 0; channel < 4; ++channel)
                        {
                            const int sum = (*source)[(static_cast<std::size_t>(sy0) * sourceWidth + sx0) * 4u + channel]
                                          + (*source)[(static_cast<std::size_t>(sy0) * sourceWidth + sx1) * 4u + channel]
                                          + (*source)[(static_cast<std::size_t>(sy1) * sourceWidth + sx0) * 4u + channel]
                                          + (*source)[(static_cast<std::size_t>(sy1) * sourceWidth + sx1) * 4u + channel];
                            destination[(static_cast<std::size_t>(y) * destinationWidth + x) * 4u + channel] =
                                static_cast<std::uint8_t>(sum / 4);
                        }
                    }
                }

                source = &destination;
                sourceWidth = destinationWidth;
                sourceHeight = destinationHeight;
            }
        }
        catch (const std::bad_alloc&)
        {
            for (std::vector<std::uint8_t>& level : mipLevels_)
                std::vector<std::uint8_t>().swap(level);
            ThrowFramebufferAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            for (std::vector<std::uint8_t>& level : mipLevels_)
                std::vector<std::uint8_t>().swap(level);
            ThrowFramebufferAllocationFailure(request, layout);
        }
        mipLevelsReady_ = true;
    }

    void SoftwareRenderTargetRenderer::BindAsRenderTarget()
    {
        // Lower levels describe the prior completed pass and must never be sampled while this
        // target is being changed again. They become available only after UnbindAsRenderTarget.
        mipLevelsReady_ = false;
        bound_ = true;
    }

    void SoftwareRenderTargetRenderer::UnbindAsRenderTarget()
    {
        if (!bound_)
            return;
        // Multisampled rendering writes the per-sample plane. Resolve it before consumers observe
        // level zero and before mip generation derives lower levels from the resolved image.
        framebuffer_.ResolveColor();
        GenerateMipMaps();
        bound_ = false;
    }

    bool SoftwareRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                              void* data, int dataLength) const
    {
        ++readbackCallCount_;
        if (data == nullptr)
            throw System::ArgumentNullException("data");
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level >= levelCount_)
            throw System::NotSupportedException(
                "SoftwareRenderTargetRenderer::GetData: this render target has " +
                std::to_string(levelCount_) + " mip level(s); level " +
                std::to_string(level) + " was requested.");
        if (level > 0 && !mipLevelsReady_)
            throw System::NotSupportedException(
                "SoftwareRenderTargetRenderer::GetData: generated mip levels are unavailable "
                "until the active render-target pass is unbound.");
        if (w <= 0 || h <= 0)
            throw System::ArgumentOutOfRangeException(
                "w", std::to_string(w) + "x" + std::to_string(h),
                "The requested rectangle must have a positive width and height.");
        const int levelWidth = MipWidth(level);
        const int levelHeight = MipHeight(level);
        // 64-bit throughout, so a rectangle near INT_MAX cannot wrap into an apparently valid one.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 ||
            right > static_cast<std::int64_t>(levelWidth) ||
            bottom > static_cast<std::int64_t>(levelHeight))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves mip level " + std::to_string(level) + " (" +
                    std::to_string(levelWidth) + "x" + std::to_string(levelHeight) + ").");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");

        // An active 4x target writes its per-sample plane, while `color` is the resolved cache used
        // by every public colour consumer. Refresh that cache for this level-zero snapshot without
        // ending the render pass or making generated mip levels visible. Further draws keep writing
        // the sample plane and the next read/unbind resolves the new contents again.
        if (level == 0)
            framebuffer_.ResolveColor();

        // The colour attachment is the ONLY storage read here -- framebuffer_.depthBuffer is never
        // consulted, so depth/stencil content can never leak through a colour readback.
        const std::vector<std::uint8_t>& source = ColorPixels(level);
        auto* dst = static_cast<std::uint8_t*>(data);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
        {
            const std::size_t srcOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(levelWidth) +
                 static_cast<std::size_t>(x)) * 4u;
            std::copy(source.begin() + static_cast<std::ptrdiff_t>(srcOffset),
                      source.begin() + static_cast<std::ptrdiff_t>(srcOffset + rowBytes),
                      dst + static_cast<std::size_t>(row) * rowBytes);
        }
        return true;
    }
}
