// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::GraphicsExt (MODULARIZATION_PLAN.md §11): the CNA graphics
// extensions (the former cna_noxna implementation) must be usable without content, media,
// audio, runtime, devices or networking -- graphics-ext depends on the graphics core only.
// The extension surface itself is option-gated (CNA_NOXNA, default OFF), so the probe uses
// real symbols when the option is on and stays a pure link-closure consumer otherwise; the
// paired ModuleLinkClosure_probe_graphics_ext ctest checks the closure in both shapes.
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"

#include <cstdio>

int main()
{
#ifdef CNA_NOXNA
    CNA::Graphics::RenderPipelineSettings settings;
    (void)settings;
    std::printf("graphics-ext probe: settings ready, quality=%d\n",
                static_cast<int>(CNA::Graphics::RenderQuality::High));
#else
    std::printf("graphics-ext probe: extension surface disabled (CNA_NOXNA off); "
                "link closure still verified\n");
#endif
    return 0;
}
