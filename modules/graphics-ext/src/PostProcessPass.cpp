// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/PostProcessPass.hpp"

#ifdef CNA_CNAEXT

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Graphics {

    bool PostProcessPass::isSupported(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device) const
    {
        // The question every shader-based pass shares, and it is two questions. A renderer without
        // custom effects is not broken -- the 2D-only and DOM renderers are 2D by identity -- so a
        // pass answers false and copies instead of failing. But `CustomEffects` only means the
        // renderer *accepts* an effect: SOFTWARE and HEADLESS accept any shader source and render
        // with their own fixed path, and a pass that believed them reported success while copying
        // its input, which is worse than refusing (plans/plan_modern.md `MOD-1699`). Both must hold.
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && device.ExecutesShaderEffectSourceEXT();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
