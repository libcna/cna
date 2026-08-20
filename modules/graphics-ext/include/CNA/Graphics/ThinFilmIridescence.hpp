// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <string>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The colour a thin film of a given thickness reflects (`KHR_materials_iridescence`).
     *
     * The rainbow on a soap bubble, an oil slick, or a heat-tinted steel bolt. It is not a texture
     * and it is not a gradient: light reflecting off the *top* of a film and light reflecting off
     * the layer *beneath* it travel different distances, so they arrive out of phase, and which
     * wavelengths cancel depends on the film's thickness and on the angle. That is why the colour
     * moves as the surface turns -- and why no colour map can stand in for it.
     *
     * The model is Belcour and Barla's: the interference is integrated against the eye's spectral
     * sensitivity, approximated by Gaussians, so the result comes back as an RGB reflectance rather
     * than as a spectrum nothing downstream could carry.
     *
     * @ref evaluate and @ref getGlsl are the same arithmetic in two languages, which is what lets
     * the shader be checked against a number rather than against a screenshot.
     */
    class ThinFilmIridescence
    {
    public:
        /**
         * @brief Returns the reflectance of a thin film over a base.
         *
         * @param outsideIor     The index of refraction of what the light comes from; 1 is air.
         * @param filmIor        The film's own index of refraction; 1.3 is glTF's default.
         * @param cosTheta       The cosine of the angle between the view and the surface normal.
         * @param thicknessNm    The film's thickness in nanometres; 0 disables the interference.
         * @param baseF0         The base layer's normal-incidence reflectance, per channel.
         * @return The reflectance, per channel, never negative.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 evaluate(
            float outsideIor, float filmIor, float cosTheta, float thicknessNm,
            const Microsoft::Xna::Framework::Vector3& baseF0);

        /**
         * @brief Returns the GLSL implementing the same model.
         *
         * Defines `cnaThinFilmIridescence(float outsideIor, float filmIor, float cosTheta,
         * float thicknessNm, vec3 baseF0)`.
         *
         * @return The GLSL source, with no `#version` line.
         */
        [[nodiscard]] static std::string getGlsl();
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
