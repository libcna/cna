// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_EFFECTS_H
#define CNA_C_EFFECTS_H

#include "CNA/C/abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width effect-parameter class identity. */
typedef uint32_t CNA_EffectParameterClass;
/** @brief Scalar effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_SCALAR UINT32_C(0)
/** @brief Vector effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_VECTOR UINT32_C(1)
/** @brief Matrix effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_MATRIX UINT32_C(2)
/** @brief Object effect parameter, such as a texture or string. */
#define CNA_EFFECT_PARAMETER_CLASS_OBJECT UINT32_C(3)
/** @brief Structured effect parameter. */
#define CNA_EFFECT_PARAMETER_CLASS_STRUCT UINT32_C(4)

/** @brief Fixed-width effect-parameter storage-type identity. */
typedef uint32_t CNA_EffectParameterType;
/** @brief Void-pointer effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_VOID UINT32_C(0)
/** @brief Boolean effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_BOOL UINT32_C(1)
/** @brief Signed 32-bit integer effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_INT32 UINT32_C(2)
/** @brief Single-precision floating-point effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_SINGLE UINT32_C(3)
/** @brief String effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_STRING UINT32_C(4)
/** @brief Texture effect parameter of unspecified dimension. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE UINT32_C(5)
/** @brief One-dimensional texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE1D UINT32_C(6)
/** @brief Two-dimensional texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE2D UINT32_C(7)
/** @brief Three-dimensional texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE3D UINT32_C(8)
/** @brief Cube texture effect parameter. */
#define CNA_EFFECT_PARAMETER_TYPE_TEXTURE_CUBE UINT32_C(9)

#ifdef __cplusplus
}
#endif

#endif
