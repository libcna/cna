// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <vector>

namespace CNA::Internal::Renderers::Igl::Detail
{
    /**
     * @brief An IGL backend this build can select at runtime. CNAEXT.
     *
     * IGL is itself a graphics abstraction (`igl::IDevice` fronts real OpenGL, Vulkan and Metal
     * implementations), so the native API is chosen when the process starts rather than when CNA is
     * compiled -- this enum names the candidates. Which entries are actually available is a
     * build-time fact (`CNA_IGL_HAS_OPENGL` / `CNA_IGL_HAS_VULKAN`, set by cmake/ThirdPartyIGL.cmake
     * from the backends it built).
     */
    enum class RendererBackend
    {
        /** @brief IGL's OpenGL backend, reached on Linux through `igl::opengl::glx`. */
        OpenGL,

        /** @brief IGL's Vulkan backend, reached through `igl::vulkan::HWDevice`. */
        Vulkan
    };

    /**
     * @brief Returns the backend's stable CNA-facing name.
     *
     * @param backend Backend to name.
     * @return A static string ("OpenGL" or "Vulkan"); valid for the lifetime of the program.
     */
    [[nodiscard]] const char* GetRendererBackendName(RendererBackend backend);

    /**
     * @brief Returns whether this build actually contains the given backend.
     *
     * A backend that was not compiled in can never be selected, no matter what the environment asks
     * for -- IGL simply has no device factory for it in this binary.
     *
     * @param backend Backend to test.
     * @return True if the backend was built into this binary.
     */
    [[nodiscard]] bool IsRendererBackendCompiledIn(RendererBackend backend);

    /**
     * @brief Returns the preference order used when nothing overrides the choice.
     *
     * OpenGL is tried first and Vulkan second. Unlike LLGL's own selection, this order is not a
     * maturity judgement about IGL: CNA's platform window has to be created with a single render
     * intent BEFORE the renderer exists, so the choice cannot be re-made after a probe fails, and
     * IGL's OpenGL backend is the one that adopts the context CNA's own platform GL service already
     * creates for the window. Backends missing from this build are filtered out.
     *
     * @return Candidate backends, most preferred first; never empty in a valid build.
     */
    [[nodiscard]] std::vector<RendererBackend> GetDefaultRendererPreference();

    /**
     * @brief Parses the value of the CNA_IGL_BACKEND environment variable.
     *
     * Accepted values are case- and punctuation-insensitive: "auto" (the default preference),
     * "opengl"/"gl", and "vulkan"/"vk". A single named backend yields a single-element list -- an
     * explicit request is honoured exactly, with no silent fallback to another API.
     *
     * @param value Raw environment value; null or empty is treated as "auto".
     * @return Candidate backends, most preferred first.
     * @throws std::runtime_error If the value names no known backend, or names one this build does
     *         not contain.
     */
    [[nodiscard]] std::vector<RendererBackend> ParseRendererBackendOverride(const char* value);

    /**
     * @brief Resolves the candidate list from an environment value, without touching the GPU.
     *
     * @param environmentValue Raw CNA_IGL_BACKEND value, or null when unset.
     * @return Candidate backends, most preferred first.
     * @throws std::runtime_error If @p environmentValue is invalid for this build.
     */
    [[nodiscard]] std::vector<RendererBackend> ResolveRendererPreference(const char* environmentValue);

    /**
     * @brief Determines, once per process, which backend this run will actually use.
     *
     * Deliberately does NOT probe the GPU: the answer is needed twice and must be identical both
     * times -- `GraphicsDevice` asks before creating the window (an OpenGL backend needs a window
     * whose visual can carry a GL context; a Vulkan backend needs a Vulkan-capable one, and a native
     * window cannot be both), and the renderer asks again when it creates its `igl::IDevice`. A
     * probe between those two points could only report a failure that is already unrecoverable, so
     * the first candidate of the resolved preference is the answer and a device that then fails to
     * come up fails loudly by name.
     *
     * @return The backend this process will use.
     * @throws std::runtime_error If this build contains no backend at all, or CNA_IGL_BACKEND names
     *         one it does not contain.
     */
    [[nodiscard]] RendererBackend ResolveRendererBackend();

    /**
     * @brief Returns whether the given backend needs an OpenGL-capable platform window.
     *
     * True only for the OpenGL backend, which adopts a context created on this very window by
     * CNA's platform GL service. IGL's Vulkan backend builds its surface from the native window
     * handle and needs a Vulkan-capable window instead.
     *
     * @param backend Backend to test.
     * @return True if the platform window must be created with OpenGL render intent.
     */
    [[nodiscard]] bool RendererBackendNeedsOpenGLWindow(RendererBackend backend);

    /**
     * @brief Returns whether the given backend needs a Vulkan-capable platform window.
     *
     * The exact complement of @ref RendererBackendNeedsOpenGLWindow for the two backends that
     * exist; kept as its own named query so `GraphicsDevice` never has to encode "not OpenGL means
     * Vulkan", which would silently become wrong the day a Metal backend is added here.
     *
     * @param backend Backend to test.
     * @return True if the platform window must be created with Vulkan render intent.
     */
    [[nodiscard]] bool RendererBackendNeedsVulkanWindow(RendererBackend backend);
}
