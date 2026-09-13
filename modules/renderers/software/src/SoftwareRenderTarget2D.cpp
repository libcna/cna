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
        bool hasRealDepthBuffer, bool hasStandaloneStencilBuffer, int surfaceFormat)
        : framebuffer_(hasRealDepthBuffer,
                       hasStandaloneStencilBuffer ||
                           (hasRealDepthBuffer && depthFormat == 3),
                       surfaceFormat)
        , depthFormat_(depthFormat), mipMap_(mipMap), multiSampleCount_(multiSampleCount)
        , surfaceFormat_(surfaceFormat)
        , hasRealDepthBuffer_(hasRealDepthBuffer)
        , hasStandaloneStencilBuffer_(hasStandaloneStencilBuffer)
    {
        const SoftwareFramebufferAllocationRequest request{
            w, h, hasRealDepthBuffer,
            hasStandaloneStencilBuffer || (hasRealDepthBuffer && depthFormat == 3),
            multiSampleCount == 4 ? 4 : 0, mipMap, surfaceFormat != 0};
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
                if (framebuffer_.HasWideColor())
                    mipWideLevels_.resize(static_cast<std::size_t>(levelCount_ - 1));
                for (int level = 1; level < levelCount_; ++level)
                {
                    const std::size_t elementCount =
                        static_cast<std::size_t>(MipWidth(level)) * MipHeight(level) * 4u;
                    mipLevels_[static_cast<std::size_t>(level - 1)].assign(elementCount, 0u);
                    if (framebuffer_.HasWideColor())
                    {
                        mipWideLevels_[static_cast<std::size_t>(level - 1)].assign(
                            elementCount, 0.0f);
                    }
                }
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
        const std::size_t rowBytes = static_cast<std::size_t>(framebuffer_.width) *
                                     framebuffer_.DeclaredColorTexelSize();
        // REMED-GFX-230: RenderTarget2D uploads used to ignore the supplied pitch and read one
        // tight height*width*4 span. That ingested padding into later rows and skipped the real
        // source pixels. Validate before changing storage, then copy only each row's RGBA bytes.
        if (stride > 0 && static_cast<std::size_t>(stride) < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "A positive render-target upload stride must be at least " +
                    std::to_string(rowBytes) + " bytes (width * declared-format texel size).");
        framebuffer_.LoadDeclaredColor(rgba, stride);
        mipLevelsReady_ = false;
        // A direct CPU upload is already complete unless this target is actively being rendered.
        // Generate now so an uploaded, unbound RenderTarget2D never exposes stale lower levels.
        if (!bound_)
            GenerateMipMaps();
    }

    void SoftwareRenderTargetRenderer::UpdatePixelsLevel(
        int level, const uint8_t* data, int levelW, int levelH)
    {
        if (data == nullptr || level < 0 || level >= levelCount_ ||
            levelW != MipWidth(level) || levelH != MipHeight(level))
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level),
                "The supplied render-target mip dimensions do not match the declared chain.");
        if (level == 0)
        {
            UpdatePixels(data, levelW * framebuffer_.DeclaredColorTexelSize());
            return;
        }

        const int texelSize = framebuffer_.DeclaredColorTexelSize();
        std::vector<std::uint8_t>& mirror = mipLevels_[static_cast<std::size_t>(level - 1)];
        mirror.resize(static_cast<std::size_t>(levelW) * levelH * 4u);
        std::vector<float>* wide = framebuffer_.HasWideColor()
            ? &mipWideLevels_[static_cast<std::size_t>(level - 1)] : nullptr;
        if (wide != nullptr)
            wide->resize(static_cast<std::size_t>(levelW) * levelH * 4u);

        for (std::size_t pixel = 0;
             pixel < static_cast<std::size_t>(levelW) * levelH; ++pixel)
        {
            const std::array<float, 4> value = framebuffer_.DecodeDeclaredColor(
                data + pixel * static_cast<std::size_t>(texelSize));
            if (wide != nullptr)
                std::copy(value.begin(), value.end(), wide->begin() +
                          static_cast<std::ptrdiff_t>(pixel * 4u));
            for (int channel = 0; channel < 4; ++channel)
                mirror[pixel * 4u + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>(std::clamp(
                        value[static_cast<std::size_t>(channel)], 0.0f, 1.0f) * 255.0f);
        }
        mipLevelsReady_ = true;
    }

    bool SoftwareRenderTargetRenderer::HasDefinedMipLevel(int level) const noexcept
    {
        return level == 0 || (level > 0 && level < levelCount_ && mipLevelsReady_);
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

    void SoftwareRenderTargetRenderer::FetchColorTexel(
        int level, int x, int y, float& r, float& g, float& b, float& a) const
    {
        const int width = ColorWidth(level);
        const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
        if (framebuffer_.HasWideColor())
        {
            const std::vector<float>& source = level == 0
                ? framebuffer_.wideColor
                : mipWideLevels_[static_cast<std::size_t>(level - 1)];
            const std::size_t offset = pixel * 4u;
            r = source[offset + 0];
            g = source[offset + 1];
            b = source[offset + 2];
            a = source[offset + 3];
            return;
        }
        SoftwareColorSurface::FetchColorTexel(level, x, y, r, g, b, a);
    }

    void SoftwareRenderTargetRenderer::GenerateMipMaps()
    {
        if (!mipMap_ || levelCount_ <= 1)
            return;

        const SoftwareFramebufferAllocationRequest request{
            framebuffer_.width, framebuffer_.height,
            framebuffer_.allocateDepthStorage, framebuffer_.allocateStencilStorage,
            framebuffer_.multiSampleCount, true, framebuffer_.HasWideColor()};
        const SoftwareFramebufferAllocationLayout layout =
            PlanSoftwareFramebufferAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidFramebufferLayout(request, layout.error);

        try
        {
            if (framebuffer_.HasWideColor())
            {
                const std::vector<float>* source = &framebuffer_.wideColor;
                int sourceWidth = framebuffer_.width;
                int sourceHeight = framebuffer_.height;
                for (int level = 1; level < levelCount_; ++level)
                {
                    const int destinationWidth = MipWidth(level);
                    const int destinationHeight = MipHeight(level);
                    std::vector<float>& destination =
                        mipWideLevels_[static_cast<std::size_t>(level - 1)];
                    std::vector<std::uint8_t>& mirror =
                        mipLevels_[static_cast<std::size_t>(level - 1)];
                    const std::size_t pixelCount =
                        static_cast<std::size_t>(destinationWidth) * destinationHeight;
                    destination.resize(pixelCount * 4u);
                    mirror.resize(pixelCount * 4u);

                    for (int y = 0; y < destinationHeight; ++y)
                    {
                        const int sy0 = std::min(sourceHeight - 1, y * 2);
                        const int sy1 = std::min(sourceHeight - 1, y * 2 + 1);
                        for (int x = 0; x < destinationWidth; ++x)
                        {
                            const int sx0 = std::min(sourceWidth - 1, x * 2);
                            const int sx1 = std::min(sourceWidth - 1, x * 2 + 1);
                            std::array<float, 4> average{};
                            for (int channel = 0; channel < 4; ++channel)
                            {
                                const auto at = [&](int sx, int sy) {
                                    return (*source)[
                                        (static_cast<std::size_t>(sy) * sourceWidth + sx) * 4u +
                                        static_cast<std::size_t>(channel)];
                                };
                                average[static_cast<std::size_t>(channel)] =
                                    (at(sx0, sy0) + at(sx1, sy0) +
                                     at(sx0, sy1) + at(sx1, sy1)) * 0.25f;
                            }
                            const std::array<float, 4> stored =
                                framebuffer_.QuantizeColor(average);
                            const std::size_t offset =
                                (static_cast<std::size_t>(y) * destinationWidth + x) * 4u;
                            for (int channel = 0; channel < 4; ++channel)
                            {
                                const float value = stored[static_cast<std::size_t>(channel)];
                                destination[offset + static_cast<std::size_t>(channel)] = value;
                                mirror[offset + static_cast<std::size_t>(channel)] =
                                    static_cast<std::uint8_t>(
                                        std::clamp(value, 0.0f, 1.0f) * 255.0f);
                            }
                        }
                    }
                    source = &destination;
                    sourceWidth = destinationWidth;
                    sourceHeight = destinationHeight;
                }
                mipLevelsReady_ = true;
                return;
            }

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
            for (std::vector<float>& level : mipWideLevels_)
                std::vector<float>().swap(level);
            ThrowFramebufferAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            for (std::vector<std::uint8_t>& level : mipLevels_)
                std::vector<std::uint8_t>().swap(level);
            for (std::vector<float>& level : mipWideLevels_)
                std::vector<float>().swap(level);
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
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) *
            framebuffer_.DeclaredColorTexelSize();
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

        if (framebuffer_.HasWideColor())
        {
            if (level == 0)
            {
                framebuffer_.StoreDeclaredColor(x, y, w, h, data);
                return true;
            }
            const int texelSize = framebuffer_.DeclaredColorTexelSize();
            const std::vector<float>& source =
                mipWideLevels_[static_cast<std::size_t>(level - 1)];
            auto* destination = static_cast<std::uint8_t*>(data);
            for (int row = 0; row < h; ++row)
            {
                for (int column = 0; column < w; ++column)
                {
                    const std::size_t sourceOffset =
                        (static_cast<std::size_t>(y + row) * levelWidth + (x + column)) * 4u;
                    framebuffer_.EncodeDeclaredColor(
                        {source[sourceOffset + 0], source[sourceOffset + 1],
                         source[sourceOffset + 2], source[sourceOffset + 3]},
                        destination +
                            (static_cast<std::size_t>(row) * w + column) * texelSize);
                }
            }
            return true;
        }

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
