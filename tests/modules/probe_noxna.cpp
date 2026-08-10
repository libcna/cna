// SPDX-License-Identifier: MS-PL
// Composition probe for the CNA::NoXna compatibility umbrella (MODULARIZATION_PLAN.md §11):
// linking the historical umbrella name must provide BOTH extension modules (graphics-ext +
// devices-ext) without dragging in networking. The paired ModuleLinkClosure_NoXnaComposition
// ctest REQUIRES both extension archives on the link line, proving the umbrella is a
// composition over the real extension modules rather than a monolith. The extension
// surfaces themselves are option-gated (CNA_NOXNA / CNA_DEVICES, default OFF).
#include "CNA/Devices/PowerState.hpp"
#include "CNA/Graphics/RenderQuality.hpp"

#include <cstdio>

int main()
{
#if defined(CNA_NOXNA) && defined(CNA_DEVICES)
    std::printf("noxna umbrella probe: quality=%d power=%d\n",
                static_cast<int>(CNA::Graphics::RenderQuality::High),
                static_cast<int>(CNA::Devices::PowerState::Charging));
#else
    std::printf("noxna umbrella probe: extension surfaces gated off; "
                "composition link closure still verified\n");
#endif
    return 0;
}
