// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <vector>

namespace CNA::Internal::Renderers::Llgl::Detail
{
    /**
     * @brief An LLGL renderer module this build can select at runtime. CNAEXT.
     *
     * LLGL is itself a graphics abstraction, so the native API is chosen when the process starts
     * rather than when CNA is compiled -- this enum names the candidates. Which entries are
     * actually available is a build-time fact (`CNA_LLGL_HAS_OPENGL` / `CNA_LLGL_HAS_VULKAN` /
     * `CNA_LLGL_HAS_NULL`, set by cmake/ThirdPartyLLGL.cmake from the modules it built).
     */
    enum class RendererModule
    {
        /** @brief LLGL's OpenGL module -- its most mature renderer. */
        OpenGL,

        /** @brief LLGL's Vulkan module, which LLGL itself still marks experimental. */
        Vulkan,

        /** @brief LLGL's Null module: it accepts every command and renders nothing. */
        Null
    };

    /**
     * @brief Returns the module's LLGL name, i.e. the string LLGL::RenderSystem::Load expects.
     *
     * @param module Module to name.
     * @return A static string ("OpenGL", "Vulkan" or "Null"); valid for the lifetime of the program.
     */
    [[nodiscard]] const char* GetRendererModuleName(RendererModule module);

    /**
     * @brief Returns whether this build actually contains the given module.
     *
     * A module that was not compiled in can never be selected, no matter what the environment
     * asks for -- LLGL's static module registry would simply fail to resolve it.
     *
     * @param module Module to test.
     * @return True if the module was built into this binary.
     */
    [[nodiscard]] bool IsRendererModuleCompiledIn(RendererModule module);

    /**
     * @brief Returns the preference order used when nothing overrides the choice.
     *
     * Vulkan is tried first and OpenGL second; modules missing from this build are filtered out.
     * The Null module is never part of this list: a renderer that silently draws nothing must
     * only ever be reached by an explicit request.
     *
     * @return Candidate modules, most preferred first; never empty in a valid build.
     */
    [[nodiscard]] std::vector<RendererModule> GetDefaultRendererPreference();

    /**
     * @brief Parses the value of the CNA_LLGL_RENDERER environment variable.
     *
     * Accepted values are case- and punctuation-insensitive: "auto" (the default preference),
     * "opengl"/"gl", "vulkan"/"vk", and "null". A single named module yields a single-element
     * list -- an explicit request is honoured exactly, with no silent fallback to another API.
     *
     * @param value Raw environment value; null or empty is treated as "auto".
     * @return Candidate modules, most preferred first.
     * @throws std::runtime_error If the value names no known module, or names one this build
     *         does not contain.
     */
    [[nodiscard]] std::vector<RendererModule> ParseRendererModuleOverride(const char* value);

    /**
     * @brief Resolves the candidate list from an environment value, without touching the GPU.
     *
     * @param environmentValue Raw CNA_LLGL_RENDERER value, or null when unset.
     * @return Candidate modules, most preferred first.
     * @throws std::runtime_error If @p environmentValue is invalid for this build.
     */
    [[nodiscard]] std::vector<RendererModule> ResolveRendererPreference(const char* environmentValue);

    /**
     * @brief Determines, once per process, which module this run will actually use.
     *
     * Walks the resolved candidate list and loads each module until one succeeds, which is a real
     * device-level probe rather than a guess. The answer is cached because it is needed twice and
     * must be identical both times: `GraphicsDevice` asks before creating the window (an OpenGL
     * module needs a window whose visual can carry a GL context), and the renderer asks again when
     * it creates its render system.
     *
     * @return The module this process will use.
     * @throws std::runtime_error If no candidate module could be loaded.
     */
    [[nodiscard]] RendererModule ResolveRendererModule();

    /**
     * @brief Returns whether the given module needs an OpenGL-capable platform window.
     *
     * True only for the OpenGL module. Vulkan gets its surface straight from the native window
     * handle and needs no API-specific window flag at all, and the Null module presents nothing.
     *
     * @param module Module to test.
     * @return True if the platform window must be created with OpenGL render intent.
     */
    [[nodiscard]] bool RendererModuleNeedsOpenGLWindow(RendererModule module);
}
