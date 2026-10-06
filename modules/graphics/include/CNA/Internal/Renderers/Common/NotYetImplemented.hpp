#pragma once

// plans/plan_dx9.md D9-11: a small shared "loud, explicit failure" helper for renderer methods that are
// not implemented yet; new renderer work should prefer this shared header to a private copy.

#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers
{
    /**
     * @brief Throws a std::runtime_error naming the unimplemented capability and the renderer.
     *
     * Used to override a base-interface virtual that otherwise has a *silently empty* default
     * body (e.g. IGraphicsRenderer::SetScissorRect()) -- turning an invisible no-op into a loud,
     * explicit failure a caller cannot mistake for success. Methods whose base default already
     * throws do not need this; only override them once the real implementation lands.
     *
     * @param rendererName Short renderer identifier, e.g. "DIRECTX9".
     * @param what        The capability that is not yet implemented, e.g. "Clear".
     */
    [[noreturn]] inline void NotYetImplemented(const char* rendererName, const char* what)
    {
        throw std::runtime_error(std::string(rendererName) + " renderer: " + what + " not yet implemented");
    }
}
