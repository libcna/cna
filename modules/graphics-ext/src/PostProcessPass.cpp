// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/PostProcessPass.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Graphics {

    bool PostProcessPass::isSupported(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const
    {
        // The question every shader-based pass shares. A renderer without custom effects is not
        // broken -- the 2D-only and DOM renderers are 2D by identity -- so a pass answers false
        // here and copies instead of failing.
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
