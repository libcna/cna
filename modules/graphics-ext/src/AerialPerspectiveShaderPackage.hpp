// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ShaderPackageEXT.hpp"

namespace CNA::Graphics::detail
{
    /** @brief Creates the portable aerial-perspective fullscreen shader package. */
    [[nodiscard]] ShaderPackageEXT CreateAerialPerspectiveShaderPackage();
}

#endif // CNA_CNAEXT
