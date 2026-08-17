// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNAEXT.hpp
 * @brief Master include for the CNA extended graphics layer (`namespace CNA::Graphics`).
 *
 * Including this single header pulls in every public type of the extension layer that sits above
 * the XNA 4.0 API: the render-pipeline configuration bag, the material description, the quality
 * and tonemapping enumerations, and the renderer-neutral post-process effects.
 *
 * The whole layer is opt-in. Without the `CNA_CNAEXT` compile definition (CMake option
 * `-DCNA_CNAEXT=ON`) every header below is empty, so including this file costs nothing and
 * declares nothing. Note that this gate applies only to the `CNA::Graphics` engine layer --
 * the `CNAEXT`-marked extension members inside `Microsoft::Xna::Framework::Graphics` (PbrEffect,
 * ShaderEffect, morph targets, ...) are always compiled and are not affected by this option.
 *
 * @see CNAEXT.md for the design of this layer, and `plan_modern.md` for its task backlog.
 */

#ifdef CNA_CNAEXT

// ---- Configuration and enumerations -------------------------------------------------------
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"

// ---- Materials -----------------------------------------------------------------------------
#include "CNA/Graphics/PbrMaterial.hpp"

// ---- Post-process effects ------------------------------------------------------------------
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"

#endif // CNA_CNAEXT
