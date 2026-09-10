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
}

#endif // CNA_CNAEXT
