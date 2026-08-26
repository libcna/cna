// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_RENDER_TARGET_DETAIL_HPP
#define CNA_C_API_RENDER_TARGET_DETAIL_HPP

#include "CNA/C/render_target.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
class GraphicsDevice;
}

namespace CNA::C::Detail {

[[nodiscard]] CNA_Result GetTrackedRenderTargetBindings(
    Microsoft::Xna::Framework::Graphics::GraphicsDevice* device,
    std::vector<CNA_RenderTargetBinding>* outBindings);

void SetTrackedRenderTargetBindings(
    Microsoft::Xna::Framework::Graphics::GraphicsDevice* device,
    std::vector<CNA_RenderTargetBinding> bindings);

[[nodiscard]] CNA_Result ResolveRenderTargetScopeReference(
    CNA_Handle handle,
    CNA_Handle parentGame,
    std::shared_ptr<void>* outOwner,
    uint64_t** outReferenceCount);

} // namespace CNA::C::Detail

#endif
