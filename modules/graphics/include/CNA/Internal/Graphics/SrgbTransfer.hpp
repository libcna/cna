// SPDX-License-Identifier: MS-PL
#pragma once

#include <cmath>

/**
 * @file
 * @brief The sRGB transfer function, stated once (plan_gltf.md `GLTF-209`/`GLTF-210`).
 *
 * glTF §3.9.2 assigns each material texture a colour space: `baseColorTexture` and
 * `emissiveTexture` are **sRGB-encoded**, while `normalTexture`, `occlusionTexture` and
 * `metallicRoughnessTexture` are **linear**. Lighting is defined in linear space, so the two sRGB
 * maps must be decoded before they are used and the lit result encoded again for display.
 *
 * `docs/gltf-api-change-review.md` §1.7 records why CNA does that in the shader (option **B**)
 * rather than with sRGB texture formats (option A) or a decode baked into the texture bytes at
 * import (option C).
 *
 * ## Why this file exists at all
 *
 * The formula lives in two places that cannot share code: C++ and GLSL. Two hand-written copies of
 * a piecewise function with a 0.0031308 knee and a 1/2.4 exponent would drift, and the drift would
 * be a subtle brightness error rather than a crash. So the GLSL is *generated from this header* —
 * @ref kSrgbTransferGlsl is the text every renderer pastes into its shader — and the C++ functions
 * next to it are the same arithmetic, unit-tested against the specification's own values. A test
 * that checks the two agree at the knee and at the endpoints is what keeps them one formula.
 *
 * Neither direction clamps its input: a value above 1 is a legitimate HDR emissive
 * (`KHR_materials_emissive_strength`, `GLTF-222`) and silently clamping it here would undo the
 * extension. Callers clamp when they write to an 8-bit target, which is where the range actually
 * ends.
 */
/**
 * @brief `cnaSrgbToLinear(vec3)` / `cnaLinearToSrgb(vec3)` as GLSL, for shader concatenation.
 *
 * See `CNA::Internal::Graphics::kSrgbTransferGlsl` for the same text as a value, and the two C++
 * functions beside it for the same arithmetic. The C++ and the GLSL are held to the same curve by
 * `SrgbTransferTests`, which evaluates both at the knee and the endpoints.
 */
#define CNA_GLSL_SRGB_TRANSFER \
    "vec3 cnaSrgbToLinear(vec3 c){\n" \
    "    vec3 lo=c/12.92;\n" \
    "    vec3 hi=pow((c+0.055)/1.055,vec3(2.4));\n" \
    "    return mix(lo,hi,step(vec3(0.04045),c));\n" \
    "}\n" \
    "vec3 cnaLinearToSrgb(vec3 c){\n" \
    "    vec3 lo=c*12.92;\n" \
    "    vec3 hi=1.055*pow(max(c,vec3(0.0)),vec3(1.0/2.4))-0.055;\n" \
    "    return mix(lo,hi,step(vec3(0.0031308),c));\n" \
    "}\n"

namespace CNA::Internal::Graphics
{
    /**
     * @brief Decodes one sRGB-encoded channel to linear (IEC 61966-2-1).
     *
     * @param encoded The sRGB-encoded value, normally in [0,1].
     * @return The linear value.
     */
    [[nodiscard]] inline float SrgbToLinearEXT(float encoded) noexcept
    {
        return encoded <= 0.04045f
                   ? encoded / 12.92f
                   : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    /**
     * @brief Encodes one linear channel to sRGB (IEC 61966-2-1).
     *
     * @param linear The linear value, normally in [0,1].
     * @return The sRGB-encoded value.
     */
    [[nodiscard]] inline float LinearToSrgbEXT(float linear) noexcept
    {
        return linear <= 0.0031308f
                   ? linear * 12.92f
                   : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }

    /**
     * @brief The same two functions as GLSL source, for a renderer to paste into its shaders.
     *
     * Declares `cnaSrgbToLinear(vec3)` and `cnaLinearToSrgb(vec3)`. Component-wise, and written
     * with `mix`/`step` rather than a branch so it costs the same on every fragment.
     *
     * Alpha is deliberately absent from both: glTF §3.9.4 makes alpha a coverage value, never a
     * colour, so encoding it would be wrong — which is why these take `vec3` and not `vec4`.
     *
     * A **macro** of adjacent string literals rather than a variable, because a renderer builds its
     * shader source by literal concatenation (`"…" CNA_GLSL_SRGB_TRANSFER "…"`) and a `const char*`
     * cannot take part in that. @ref kSrgbTransferGlsl below is the same text as a value, for a
     * caller that wants one.
     */
    inline constexpr const char* kSrgbTransferGlsl = CNA_GLSL_SRGB_TRANSFER;
}
