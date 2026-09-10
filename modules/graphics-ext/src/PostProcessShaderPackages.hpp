// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderPackageEXT.hpp"

namespace CNA::Graphics::detail
{
    /** @brief Creates the portable chromatic-aberration fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateChromaticAberrationShaderPackage();

    /** @brief Creates the portable FXAA fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateFxaaShaderPackage();

    /** @brief Creates the portable film-grain fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateFilmGrainShaderPackage();

    /** @brief Creates the portable tonemap fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateTonemapShaderPackage();

    /** @brief Creates the portable lens-flare fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateLensFlareShaderPackage();

    /** @brief Creates the portable HDR-display-output fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateHdrDisplayShaderPackage();
}

#endif // CNA_CNAEXT
