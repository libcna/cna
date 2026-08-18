// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
}

namespace CNA::Graphics::detail {

    /**
     * @brief Reports, once, that a pass's shader did not compile — with the pass's name and the log.
     *
     * plan_modern.md `MOD-219`. A pass whose shader fails to compile falls back to a copy, which is
     * the right behaviour and also a completely silent one: the frame keeps rendering and the effect
     * simply is not there. This is what turns that into something a developer can act on.
     *
     * **It logs; it does not throw.** The row proposed throwing, and measuring the renderers made
     * that the wrong choice: `GraphicsCapability::CustomEffects` is true on renderers that never
     * compile GLSL source at all — Vulkan takes SPIR-V, SOFTWARE and HEADLESS accept and ignore —
     * so throwing on a failed compile would turn a documented capability boundary into a crash on
     * three renderers at once. That is not hypothetical: it is the shape of the bug `MOD-1699`
     * fixed. The message names the pass so a log line is actionable without a stack trace.
     *
     * @param device     The device the effect was built on, used to name the renderer.
     * @param passName   The pass reporting, e.g. `"BloomPass (blur)"`.
     * @param effect     The effect to check. Null counts as "not compiled".
     * @param alreadyLogged Set to true once reported, so a per-frame caller stays quiet after the
     *                      first frame. Pass a member of the pass, not a local.
     * @return True when the effect is valid; false when it is not (and the failure was reported).
     */
    bool reportShaderCompileFailure(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const std::string& passName,
        const Microsoft::Xna::Framework::Graphics::ShaderEffect* effect, bool& alreadyLogged);

} // namespace CNA::Graphics::detail

#endif // CNA_CNAEXT
