#pragma once

// plan_dx9.md D9-11: a small shared "loud, explicit failure" helper for backend methods that are
// not implemented yet. D3D12GraphicsBackend already had an identical private static helper of its
// own (D3D12GraphicsBackend::NotYetImplemented); rather than duplicate it a second time for D3D9,
// it is lifted here so both backends' skeletons can share one implementation. D3D12's own private
// copy is left as-is (out of scope for this plan -- see plan_dx9.md cold-start Sec.4, D3D12/ stays
// untouched); new backend work should prefer this shared header instead.

#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends
{
    /**
     * @brief Throws a std::runtime_error naming the unimplemented capability and the backend.
     *
     * Used to override a base-interface virtual that otherwise has a *silently empty* default
     * body (e.g. IGraphicsBackend::SetScissorRect()) -- turning an invisible no-op into a loud,
     * explicit failure a caller cannot mistake for success. Methods whose base default already
     * throws do not need this; only override them once the real implementation lands.
     *
     * @param backendName Short backend identifier, e.g. "D3D9".
     * @param what        The capability that is not yet implemented, e.g. "Clear".
     */
    [[noreturn]] inline void NotYetImplemented(const char* backendName, const char* what)
    {
        throw std::runtime_error(std::string(backendName) + " backend: " + what + " not yet implemented");
    }
}
