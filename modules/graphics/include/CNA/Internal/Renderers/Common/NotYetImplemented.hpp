#pragma once

// plans/plan_dx9.md D9-11: a small shared "loud, explicit failure" helper for renderer methods that are
// not implemented yet. DirectX12Renderer already had an identical private static helper of its
// own (DirectX12Renderer::NotYetImplemented); rather than duplicate it a second time for D3D9,
// it is lifted here so both renderers' skeletons can share one implementation. D3D12's own private
// copy is left as-is (out of scope for this plan -- see plans/plan_dx9.md cold-start Sec.4, D3D12/ stays
// untouched); new renderer work should prefer this shared header instead.

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
