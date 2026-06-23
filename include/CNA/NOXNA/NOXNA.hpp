// SPDX-License-Identifier: MS-PL
/**
 * @file NOXNA.hpp
 * @brief Master include for CNA NOXNA extended graphics layer.
 *
 * Include this header to access all CNA-specific extensions that go beyond
 * the XNA 4.0 API.  Only available when compiled with CNA_NOXNA=ON.
 *
 * @code{.cmake}
 *   cmake -DCNA_NOXNA=ON -DCNA_GRAPHICS_BACKEND=EASYGL ..
 * @endcode
 *
 * @code{.cpp}
 * #ifdef CNA_NOXNA
 * #include "CNA/NOXNA/NOXNA.hpp"
 * using namespace CNA::Graphics;
 *
 * RenderPipelineSettings pipeline;
 * pipeline.setHDREnabled(true);
 * pipeline.setTonemappingMode(TonemappingMode::Aces);
 * pipeline.setBloomEnabled(true);
 *
 * PbrMaterial mat;
 * mat.setMetallicFactor(0.0f);
 * mat.setRoughnessFactor(0.75f);
 * #endif
 * @endcode
 */
#pragma once

#ifdef CNA_NOXNA

#include "CNA/NOXNA/TonemappingMode.hpp"
#include "CNA/NOXNA/RenderQuality.hpp"
#include "CNA/NOXNA/ShadowQuality.hpp"
#include "CNA/NOXNA/RenderPipelineSettings.hpp"
#include "CNA/NOXNA/PbrMaterial.hpp"

#endif // CNA_NOXNA
