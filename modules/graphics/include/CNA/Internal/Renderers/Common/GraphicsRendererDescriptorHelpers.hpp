#pragma once

// plans/plan_runtimerenderer.md RTR-P1: the hooks most renderer families have nothing to say about.
//
// GraphicsRendererDescriptor requires every function pointer to be non-null, so that no call site
// needs a null check (see that header's own note). These are the shared implementations a family
// uses when a hook does not apply to it -- one definition each, rather than 42 identical lambdas.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers
{
    /**
     * @brief An isAvailable() hook for a renderer with no meaningful cheap probe.
     *
     * Returning true is not a promise that construction will succeed -- it states only that there
     * is no cheap, side-effect-free way to know otherwise, which is the honest answer for most
     * families. A renderer that CAN be probed (a loadable driver library, an enumerable device)
     * should implement a real probe instead of reusing this.
     *
     * @return true.
     */
    [[nodiscard]] inline bool AlwaysAvailable()
    {
        return true;
    }
}
