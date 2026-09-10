// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderPackageEXT.hpp"

namespace CNA::Graphics::detail
{
    /** @brief Creates the portable bloom-extraction fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateBloomExtractShaderPackage();

    /** @brief Creates the portable bloom-blur fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateBloomBlurShaderPackage();

    /** @brief Creates the portable bloom-upsample fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateBloomUpsampleShaderPackage();

    /** @brief Creates the portable bloom-composite fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateBloomCombineShaderPackage();

    /** @brief Creates the portable chromatic-aberration fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateChromaticAberrationShaderPackage();

    /** @brief Creates the portable screen-space contact-shadow shader package. */
    [[nodiscard]] ShaderPackageEXT CreateContactShadowShaderPackage();

    /** @brief Creates the portable CRT fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateCrtShaderPackage();

    /** @brief Creates the portable screen-space decal shader package. */
    [[nodiscard]] ShaderPackageEXT CreateDecalShaderPackage();

    /** @brief Creates the portable filtered-strip colour-grade shader package. */
    [[nodiscard]] ShaderPackageEXT CreateColorGradeStripShaderPackage();

    /** @brief Creates the portable exact/tetrahedral strip colour-grade shader package. */
    [[nodiscard]] ShaderPackageEXT CreateColorGradeInterpolatedStripShaderPackage();

    /** @brief Creates the portable volume-LUT colour-grade shader package. */
    [[nodiscard]] ShaderPackageEXT CreateColorGradeVolumeShaderPackage();

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

    /** @brief Creates the portable analytic height-fog fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateHeightFogShaderPackage();

    /** @brief Creates the portable depth-of-field fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateDepthOfFieldShaderPackage();

    /** @brief Creates the portable camera/object motion-blur fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateMotionBlurShaderPackage();

    /** @brief Creates the portable SSAO-estimation fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateSsaoOcclusionShaderPackage();

    /** @brief Creates the portable SSAO blur/composition fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateSsaoComposeShaderPackage();

    /** @brief Creates the portable screen-space-reflection fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateSsrShaderPackage();

    /** @brief Creates the portable spatial-upscale fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateSpatialUpscaleShaderPackage();

    /** @brief Creates the portable light-shaft fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateLightShaftShaderPackage();
}

#endif // CNA_CNAEXT
