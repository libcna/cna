// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace CNA::Internal::Graphics
{
    /**
     * @brief Shader-ready dielectric Fresnel endpoints for `KHR_materials_ior` and
     *        `KHR_materials_specular` (`GLTF-343`/`GLTF-344`).
     *
     * CNAEXT internal transport, not XNA public API. The material's metallic branch still gets
     * its F0 from baseColor; these values describe the dielectric branch only.
     */
    struct PbrDielectricFresnelEXT
    {
        /** @brief Per-channel dielectric reflectance at normal incidence. */
        std::array<float, 3> f0{0.04f, 0.04f, 0.04f};
        /** @brief Dielectric reflectance at grazing incidence. */
        float f90 = 1.0f;
    };

    /**
     * @brief Applies the Khronos IOR/specular interaction rule to factor-only material state.
     *
     * The required order is significant: `iorF0 * specularColor` is clamped per channel to 1
     * before `specularFactor` weights it. F90 is the scalar specular factor, not an unconditional
     * one, so a material that suppresses reflection also suppresses it at grazing incidence.
     * Inputs are not silently clamped; glTF validation owns their declared ranges and the PBR
     * effect setters follow the existing metallic/roughness setters' pass-through convention.
     *
     * @param ior The dielectric index of refraction.
     * @param specularFactor The scalar reflection strength.
     * @param specularColorFactor The linear-RGB normal-incidence colour factor.
     * @return The shader-ready dielectric F0 and F90 endpoints.
     */
    [[nodiscard]] inline PbrDielectricFresnelEXT ComputePbrDielectricFresnelEXT(
        float ior, float specularFactor, const std::array<float, 3>& specularColorFactor)
    {
        const float ratio = (ior - 1.0f) / (ior + 1.0f);
        const float iorF0 = ratio * ratio;

        PbrDielectricFresnelEXT out;
        for (std::size_t channel = 0; channel < out.f0.size(); ++channel)
        {
            out.f0[channel] =
                std::min(iorF0 * specularColorFactor[channel], 1.0f) * specularFactor;
        }
        out.f90 = specularFactor;
        return out;
    }
}
