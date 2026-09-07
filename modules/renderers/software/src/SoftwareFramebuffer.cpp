// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "SoftwareFramebufferErrors.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    void SoftwareFramebuffer::Resize(int w, int h)
    {
        const SoftwareFramebufferAllocationRequest request{
            w, h, allocateDepthStorage, allocateStencilStorage, multiSampleCount};
        const SoftwareFramebufferAllocationLayout layout =
            PlanSoftwareFramebufferAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidFramebufferLayout(request, layout.error);

        std::vector<std::uint8_t> newColor;
        std::vector<float> newDepth;
        std::vector<std::uint8_t> newStencil;
        std::vector<std::uint8_t> newMultiSampleColor;
        std::vector<float> newMultiSampleDepth;
        std::vector<std::uint8_t> newMultiSampleStencil;
        try
        {
            newColor.assign(layout.colorBytes, 0u);
            newDepth.assign(layout.depthElementCount, 1.0f);
            newStencil.assign(layout.stencilBytes, 0u);
            newMultiSampleColor.assign(layout.multiSampleBytes, 0u);
            newMultiSampleDepth.assign(layout.multiSampleDepthElementCount, 1.0f);
            newMultiSampleStencil.assign(layout.multiSampleStencilBytes, 0u);
        }
        catch (const std::bad_alloc&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }

        width = w;
        height = h;
        color = std::move(newColor);
        depthBuffer = std::move(newDepth);
        stencilBuffer = std::move(newStencil);
        multiSampleColor = std::move(newMultiSampleColor);
        multiSampleDepthBuffer = std::move(newMultiSampleDepth);
        multiSampleStencilBuffer = std::move(newMultiSampleStencil);
    }

    void SoftwareFramebuffer::SetMultiSampleCount(int sampleCount)
    {
        // CPU MSAA deliberately has one high-quality, predictable option: a rotated-independent
        // 2x2 grid. Treat every other request as unsupported rather than silently claiming an
        // arbitrary count with a different number of actual samples.
        const int appliedCount = sampleCount == 4 ? 4 : 0;
        if (multiSampleCount == appliedCount)
            return;

        if (appliedCount == 0)
        {
            // Preserve the last rendered image when an application turns the optional feature
            // back off at reset time; otherwise the unresolved colour plane would be discarded.
            ResolveColor();
            const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                           static_cast<std::size_t>(height);
            if (!multiSampleDepthBuffer.empty())
            {
                for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
                    depthBuffer[pixel] = multiSampleDepthBuffer[pixel * 4u];
            }
            if (!multiSampleStencilBuffer.empty())
            {
                for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
                    stencilBuffer[pixel] = multiSampleStencilBuffer[pixel * 4u];
            }
            multiSampleCount = 0;
            std::vector<std::uint8_t>().swap(multiSampleColor);
            std::vector<float>().swap(multiSampleDepthBuffer);
            std::vector<std::uint8_t>().swap(multiSampleStencilBuffer);
            return;
        }

        const SoftwareFramebufferAllocationRequest request{
            width, height, allocateDepthStorage, allocateStencilStorage, appliedCount};
        const SoftwareFramebufferAllocationLayout layout =
            PlanSoftwareFramebufferAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidFramebufferLayout(request, layout.error);

        std::vector<std::uint8_t> newMultiSampleColor;
        std::vector<float> newMultiSampleDepth;
        std::vector<std::uint8_t> newMultiSampleStencil;
        try
        {
            newMultiSampleColor.resize(layout.multiSampleBytes);
            newMultiSampleDepth.resize(layout.multiSampleDepthElementCount);
            newMultiSampleStencil.resize(layout.multiSampleStencilBytes);
        }
        catch (const std::bad_alloc&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowFramebufferAllocationFailure(request, layout);
        }

        multiSampleColor = std::move(newMultiSampleColor);
        multiSampleDepthBuffer = std::move(newMultiSampleDepth);
        multiSampleStencilBuffer = std::move(newMultiSampleStencil);
        multiSampleCount = appliedCount;
        CopyResolvedColorToMultiSample();
        const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            for (int sample = 0; sample < 4; ++sample)
            {
                const std::size_t sampleIndex = pixel * 4u +
                                                static_cast<std::size_t>(sample);
                if (!multiSampleDepthBuffer.empty())
                    multiSampleDepthBuffer[sampleIndex] = depthBuffer[pixel];
                if (!multiSampleStencilBuffer.empty())
                    multiSampleStencilBuffer[sampleIndex] = stencilBuffer[pixel];
            }
        }
    }

    void SoftwareFramebuffer::CopyResolvedColorToMultiSample()
    {
        if (!HasMultiSampleColor())
            return;

        const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const std::size_t resolvedIndex = pixel * 4u;
            for (int sample = 0; sample < 4; ++sample)
            {
                const std::size_t sampleIndex = (pixel * 4u + static_cast<std::size_t>(sample)) * 4u;
                multiSampleColor[sampleIndex + 0] = color[resolvedIndex + 0];
                multiSampleColor[sampleIndex + 1] = color[resolvedIndex + 1];
                multiSampleColor[sampleIndex + 2] = color[resolvedIndex + 2];
                multiSampleColor[sampleIndex + 3] = color[resolvedIndex + 3];
            }
        }
    }

    void SoftwareFramebuffer::ResolveColor() const
    {
        if (!HasMultiSampleColor())
            return;

        const std::size_t pixelCount = static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const std::size_t resolvedIndex = pixel * 4u;
            for (int channel = 0; channel < 4; ++channel)
            {
                unsigned int sum = 0;
                for (int sample = 0; sample < 4; ++sample)
                    sum += multiSampleColor[(pixel * 4u + static_cast<std::size_t>(sample)) * 4u +
                                            static_cast<std::size_t>(channel)];
                color[resolvedIndex + static_cast<std::size_t>(channel)] =
                    static_cast<std::uint8_t>(sum / 4u);
            }
        }
    }

    void SoftwareFramebuffer::ClearColor(float r, float g, float b, float a)
    {
        const std::uint8_t rb = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t gb = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t bb = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t ab = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        for (std::size_t i = 0; i < pixelCount; ++i)
        {
            color[i * 4 + 0] = rb;
            color[i * 4 + 1] = gb;
            color[i * 4 + 2] = bb;
            color[i * 4 + 3] = ab;
            if (HasMultiSampleColor())
            {
                for (int sample = 0; sample < 4; ++sample)
                {
                    const std::size_t sampleIndex = (i * 4u + static_cast<std::size_t>(sample)) * 4u;
                    multiSampleColor[sampleIndex + 0] = rb;
                    multiSampleColor[sampleIndex + 1] = gb;
                    multiSampleColor[sampleIndex + 2] = bb;
                    multiSampleColor[sampleIndex + 3] = ab;
                }
            }
        }
    }

    void SoftwareFramebuffer::ClearDepthValue(float depthValue)
    {
        std::fill(depthBuffer.begin(), depthBuffer.end(), depthValue);
        std::fill(multiSampleDepthBuffer.begin(), multiSampleDepthBuffer.end(), depthValue);
    }

    void SoftwareFramebuffer::ClearStencilValue(int stencilValue)
    {
        std::fill(stencilBuffer.begin(), stencilBuffer.end(),
                  static_cast<std::uint8_t>(std::clamp(stencilValue, 0, 255)));
        std::fill(multiSampleStencilBuffer.begin(), multiSampleStencilBuffer.end(),
                  static_cast<std::uint8_t>(std::clamp(stencilValue, 0, 255)));
    }
}
