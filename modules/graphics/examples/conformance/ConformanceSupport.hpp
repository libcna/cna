// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/GraphicsBackendCategory.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

namespace CNA::Examples::Conformance
{
    /** @brief The exit code CTest reports as SKIP (SKIP_RETURN_CODE 77 is applied to every test). */
    inline constexpr int kSkipExitCode = 77;

    /**
     * @brief Whether this build's renderer produces pixels a conformance program can observe.
     *
     * RRC-045: a Diagnostic-category renderer (`HEADLESS`, `STUB`) makes no pixel claim -- it
     * refuses back-buffer and render-target read-back and its occlusion counts are not sample
     * tallies -- so a program whose oracle is a read-back pixel or a sample count cannot judge it.
     * Such a program asks here first and exits 77 (CTest SKIP) with that reason instead of failing
     * on the refusal; the programs whose contract is the API itself (adapter queries, capability
     * truth) do not ask and run everywhere. The programs are registered per family against the
     * build's default renderer, which is the identity asked about.
     *
     * @param program The program's name, for the skip line.
     * @return true when the renderer rasterizes; false after printing `[SKIP]`.
     */
    inline bool ProbePixelOutputAvailable(const char* program)
    {
        const GraphicsRendererType type =
            Internal::Renderers::GraphicsRendererRegistry::Default().type;
        if (getGraphicsBackendCategory(type) != GraphicsBackendCategory::Diagnostic)
            return true;
        std::printf("[SKIP] %s: the %s renderer is a Diagnostic-category renderer and makes no "
                    "pixel claim, so this program's read-back oracle cannot judge it\n",
                    program, std::string(getGraphicsRendererName(type)).c_str());
        return false;
    }

    /**
     * @brief Whether @p e is the shared refusal of a renderer with no metallic-roughness model.
     *
     * `CNA::Internal::Renderers::RequirePbrShadingSupportEXT` (plans/plan_gltf.md GLTF-477) refuses
     * a `PbrEffect`/`SkinnedPbrEffect` draw at the draw call on a renderer that has no PBR shading
     * path (`FNA3D`). That is a capability answer, not a defect, so a program treats it as the skip
     * of the leg that drew; any other exception from a draw stays a failure.
     *
     * @param e The exception a draw threw.
     * @return true when it is that named refusal.
     */
    inline bool IsNamedPbrRefusal(const std::exception& e)
    {
        return std::string_view(e.what()).find("no metallic-roughness shading path") !=
               std::string_view::npos;
    }

    /**
     * @brief Whether @p e is FNA3D's named refusal of an instanced draw through a stock effect.
     *
     * FNA's stock effects declare no per-instance input, so `FNA3D` refuses
     * `DrawInstancedPrimitives` through one rather than drawing every instance at the origin; CNA's
     * other families read the per-instance world matrix. A capability answer, treated as the skip of
     * the leg that drew.
     *
     * @param e The exception a draw threw.
     * @return true when it is that named refusal.
     */
    inline bool IsNamedStockInstancingRefusal(const std::exception& e)
    {
        return std::string_view(e.what()).find("stock effects do not declare instance semantics") !=
               std::string_view::npos;
    }

    /**
     * @brief Whether this build's renderer evaluates CNA's metallic-roughness lighting model.
     *
     * Renderers with a PBR stock path return true. A renderer with no PBR path at all refuses at
     * the draw instead (`IsNamedPbrRefusal`).
     *
     * @param program The program's name, for the skip line.
     * @return true when the renderer evaluates the model; false after printing `[SKIP]`.
     */
    inline bool ProbePbrLightingEvaluated(const char* program)
    {
        (void)program;
        return true;
    }
}
