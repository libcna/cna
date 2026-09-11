// SPDX-License-Identifier: MS-PL
// SOFTWARE-110/345: allocation, clear and mode-transition proof for per-sample attachments, plus
// XNA/D3D Color-target UNORM8 quantization at exact half-byte boundaries.

#include "CNA/Internal/Renderers/Software/SoftwareFramebufferAllocation.hpp"
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

#include <algorithm>
#include <cstdio>

using namespace CNA::Internal::Renderers::Software;

namespace
{
    bool Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        return condition;
    }
}

int main()
{
    bool ok = true;
    constexpr std::size_t pixelCount = 8u * 6u;
    const SoftwareFramebufferAllocationLayout layout =
        PlanSoftwareFramebufferAllocation({8, 6, true, true, 4});
    ok &= Check(layout.IsValid() && layout.colorBytes == pixelCount * 4u &&
                    layout.depthBytes == pixelCount * sizeof(float) &&
                    layout.stencilBytes == pixelCount &&
                    layout.multiSampleBytes == pixelCount * 4u * 4u &&
                    layout.multiSampleDepthElementCount == pixelCount * 4u &&
                    layout.multiSampleDepthBytes == pixelCount * 4u * sizeof(float) &&
                    layout.multiSampleStencilBytes == pixelCount * 4u &&
                    layout.totalBytes == pixelCount * 45u,
                "4x allocation budget includes color, depth and stencil sample planes");

    const SoftwareFramebufferAllocationLayout wideLayout =
        PlanSoftwareFramebufferAllocation({8, 6, true, true, 4, true, true});
    ok &= Check(wideLayout.IsValid() &&
                    wideLayout.wideColorElementCount == pixelCount * 4u &&
                    wideLayout.wideColorBytes == pixelCount * 4u * sizeof(float) &&
                    wideLayout.multiSampleWideColorElementCount == pixelCount * 16u &&
                    wideLayout.multiSampleWideColorBytes == pixelCount * 16u * sizeof(float) &&
                    wideLayout.mipBytes == 15u * 4u &&
                    wideLayout.wideMipBytes == 15u * 4u * sizeof(float) &&
                    wideLayout.totalBytes == 6300u,
                "wide 4x mip allocation budget includes every float-domain plane");

    SoftwareFramebuffer framebuffer(true, true);
    framebuffer.Resize(8, 6);
    framebuffer.WriteColor(0u, -1,
                           {0.5f, 64.5f / 255.0f, 190.5f / 255.0f, 1.0f}, 0x0F);
    ok &= Check(framebuffer.color[0] == 128u && framebuffer.color[1] == 65u &&
                    framebuffer.color[2] == 191u && framebuffer.color[3] == 255u,
                "Color-target UNORM8 stores round to nearest at half-byte boundaries");
    framebuffer.depthBuffer[0] = 0.25f;
    framebuffer.stencilBuffer[0] = 17u;
    framebuffer.SetMultiSampleCount(4);
    ok &= Check(framebuffer.multiSampleColor.size() == pixelCount * 4u * 4u &&
                    framebuffer.multiSampleDepthBuffer.size() == pixelCount * 4u &&
                    framebuffer.multiSampleStencilBuffer.size() == pixelCount * 4u,
                "enabling 4x allocates every per-sample attachment");
    ok &= Check(std::all_of(framebuffer.multiSampleDepthBuffer.begin(),
                            framebuffer.multiSampleDepthBuffer.begin() + 4,
                            [](float value) { return value == 0.25f; }) &&
                    std::all_of(framebuffer.multiSampleStencilBuffer.begin(),
                                framebuffer.multiSampleStencilBuffer.begin() + 4,
                                [](std::uint8_t value) { return value == 17u; }),
                "enabling 4x initializes all samples from the single-sample attachment");

    framebuffer.ClearDepthValue(0.75f);
    framebuffer.ClearStencilValue(93);
    ok &= Check(std::all_of(framebuffer.depthBuffer.begin(), framebuffer.depthBuffer.end(),
                            [](float value) { return value == 0.75f; }) &&
                    std::all_of(framebuffer.multiSampleDepthBuffer.begin(),
                                framebuffer.multiSampleDepthBuffer.end(),
                                [](float value) { return value == 0.75f; }) &&
                    std::all_of(framebuffer.stencilBuffer.begin(), framebuffer.stencilBuffer.end(),
                                [](std::uint8_t value) { return value == 93u; }) &&
                    std::all_of(framebuffer.multiSampleStencilBuffer.begin(),
                                framebuffer.multiSampleStencilBuffer.end(),
                                [](std::uint8_t value) { return value == 93u; }),
                "depth/stencil clears initialize resolved and all per-sample storage");

    framebuffer.multiSampleDepthBuffer[0] = 0.125f;
    framebuffer.multiSampleStencilBuffer[0] = 201u;
    framebuffer.SetMultiSampleCount(0);
    ok &= Check(framebuffer.multiSampleColor.empty() &&
                    framebuffer.multiSampleDepthBuffer.empty() &&
                    framebuffer.multiSampleStencilBuffer.empty() &&
                    framebuffer.depthBuffer[0] == 0.125f &&
                    framebuffer.stencilBuffer[0] == 201u,
                "disabling 4x preserves sample zero and releases every sample plane");

    return ok ? 0 : 1;
}
