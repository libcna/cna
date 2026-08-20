// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Core (plans/MODULARIZATION_PLAN.md §4): logging, exceptions and the
// renderer-identity enum must be usable without graphics, content, runtime or networking.
// The paired ModuleLinkClosure_Core ctest inspects this executable's generated link line.
#include "CNA/CNAException.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Logger.hpp"

#include <cstdio>

int main()
{
    CNA::Logger::Info("core probe: logger reachable");
    try {
        throw CNA::CNAException("core probe exception");
    } catch (const CNA::CNAException& e) {
        std::printf("core probe: caught '%s' renderer=%d\n", e.what(),
                    static_cast<int>(CNA::getCurrentGraphicsRendererType()));
    }
    return 0;
}
