// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <stdexcept>

namespace CNA::Internal::Renderers::Rlgl
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(
        const GraphicsRendererCreateArgs& /*args*/)
    {
        throw std::runtime_error(
            "RLGL renderer selection is registered, but its GraphicsDevice vertical slice is not "
            "implemented yet (plans/plan_rlgl.md RLGL-006).");
    }
}
