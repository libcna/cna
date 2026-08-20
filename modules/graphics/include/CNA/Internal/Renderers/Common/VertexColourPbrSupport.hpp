#pragma once

// plans/plan_gltf.md GLTF-465: the shared refusal for a core glTF semantic a renderer cannot honour.
//
// glTF 2.0 §3.7.2.1/§3.9.2 make `COLOR_0` "an additional linear multiplier to base color" -- a term
// in the metallic-roughness product, alpha included. CNA's importer, `.cnj` path, vertex ABI and both
// PBR effects carry it (stride 60 rigid, stride 80 skinned), but a renderer has to evaluate it, and
// only some do.
//
// The point of this header is that a renderer which cannot has exactly TWO acceptable behaviours, and
// the third one is a defect rather than a limitation:
//
//   1. render the product correctly, or
//   2. refuse the draw explicitly and safely -- limited backend coverage, no wrong picture,
//
// and never
//
//   3. accept the asset and draw it with the opaque-white identity substituted for the authored
//      colour, which is a visibly wrong surface presented as a successful draw.
//
// The escape hatch is deliberate and belongs to the application, not to the renderer: setting
// `PbrEffect::VertexColorEnabledEXT` / `SkinnedPbrEffect::VertexColorEnabledEXT` to false makes the
// identity an explicit choice, and this guard then permits the draw. An uncoloured primitive is
// unaffected either way -- the importer fills its colour slot with opaque white AND leaves the effect
// flag false, so nothing that rendered before this guard existed stops rendering.

#include <cstddef>
#include <stdexcept>
#include <string>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers
{
    /**
     * @brief True when a draw asks for a `COLOR_0` product the caller has enabled.
     *
     * Stride 60 (rigid PBR + `TEXCOORD_1` + packed `COLOR_0`) and stride 80 (the skinned PBR record
     * with the same colour appended) are the only canonical layouts carrying a colour a PBR shader is
     * meant to multiply by. Both always physically contain the slot, so the effect's own switch is
     * what separates "an authored colour" from "the opaque-white filler".
     *
     * @param params Draw state as the effect filled it.
     * @param strideInBytes The vertex buffer's stride.
     * @return True when the draw is a PBR draw whose vertex record carries an enabled `COLOR_0`.
     */
    [[nodiscard]] inline bool DrawCarriesEnabledVertexColourPbrEXT(const GpuDrawParams& params,
                                                                   std::size_t strideInBytes) noexcept
    {
        return params.pbr && params.vertexColorEnabled &&
               (strideInBytes == 60 || strideInBytes == 80);
    }

    /**
     * @brief Refuses a PBR draw whose `COLOR_0` this renderer would silently ignore.
     *
     * Call it at the entry of a PBR draw path in any renderer that does not multiply the attribute
     * into base colour. A renderer that does implement the product must NOT call it.
     *
     * @param params Draw state as the effect filled it.
     * @param strideInBytes The vertex buffer's stride.
     * @param rendererName Short renderer identifier, e.g. "BGFX".
     * @throws std::runtime_error When the draw carries an enabled `COLOR_0` on stride 60 or 80.
     */
    inline void RequireVertexColourPbrSupportEXT(const GpuDrawParams& params,
                                                 std::size_t strideInBytes,
                                                 const char* rendererName)
    {
        if (!DrawCarriesEnabledVertexColourPbrEXT(params, strideInBytes)) { return; }
        throw std::runtime_error(
            std::string(rendererName) +
            " renderer: this metallic-roughness primitive carries a COLOR_0 vertex colour (stride " +
            std::to_string(strideInBytes) +
            "), and glTF 2.0 3.9.2 makes that an additional linear multiplier on base colour "
            "including its alpha. This renderer does not evaluate it yet (plans/plan_gltf.md GLTF-465), so "
            "the draw is refused rather than rendered with the opaque-white identity, which would be "
            "a visibly wrong surface reported as a successful draw. Use a renderer that implements it "
            "(EasyGL: OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2, SOFTWARE, IGL, OPENGL2, OPENGL4, "
            "VULKAN, DIRECTX9, DIRECTX11, DIRECTX12, MAGNUM, DILIGENT, BGFX, LLGL, SDL_GPU, WEBGPU), or "
            "set "
            "VertexColorEnabledEXT=false on the effect to accept "
            "the identity deliberately.");
    }

    /**
     * @brief Refuses a metallic-roughness draw on a renderer that has no PBR shading at all.
     *
     * plans/plan_gltf.md GLTF-477. The guard above is for a renderer that shades glTF materials but
     * cannot evaluate one *term* of them. This one is for the other shape: a renderer with no
     * metallic-roughness path whatsoever, whose stock-effect selector would otherwise fall through
     * and shade an authored glTF material as something else entirely.
     *
     * The partition is the same and so is the reasoning. `SOKOL`, `TINYGL`, `GLIDE`, `OPENGLES1`
     * and `PORTABLEGL` already refuse such a draw in their own words; this is that decision made
     * once, so a renderer cannot join them by accident or drift out of them silently.
     *
     * Call it at the entry of the params-carrying draw paths, before any GPU state is touched: a
     * refusal that happens after the data has been submitted through the wrong shading model is not
     * a refusal.
     *
     * @param params Draw state as the effect filled it.
     * @param rendererName Short renderer identifier, e.g. "FNA3D".
     * @throws std::runtime_error When the draw came from PbrEffect or SkinnedPbrEffect.
     */
    inline void RequirePbrShadingSupportEXT(const GpuDrawParams& params, const char* rendererName)
    {
        if (!params.pbr) { return; }
        throw std::runtime_error(
            std::string(rendererName) +
            " renderer: this primitive came from PbrEffect/SkinnedPbrEffect, and this renderer has "
            "no metallic-roughness shading path -- no BRDF, and none of glTF 2.0 3.9.2's normal, "
            "metallic-roughness, emissive or occlusion maps. Shading it with the nearest stock "
            "effect would present a visibly different material as a successful draw, so the draw is "
            "refused instead (plans/plan_gltf.md GLTF-477). Use a renderer that implements the model "
            "(EasyGL: OPENGLES2/OPENGLES3/OPENGL33/WEBGL1/WEBGL2, OPENGL2, OPENGL4, VULKAN, IGL, "
            "MAGNUM, DILIGENT, BGFX, LLGL, SDL_GPU, WEBGPU, DIRECTX9/11/12), or a reduced one whose "
            "boundary is documented (SOFTWARE).");
    }
}
