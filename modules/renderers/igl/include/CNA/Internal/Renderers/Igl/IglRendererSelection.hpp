// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"

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

    /**
     * @brief Returns the window classification the given backend requires.
     *
     * The renderer-selection layer -- not the descriptor translation unit -- owns this answer,
     * because it already owns the two boolean forms of the same question above and because
     * `GraphicsDevice` and `IglPlatformSurface` must not be able to reach different conclusions
     * about one window. This is expressed in terms of those two queries rather than beside them.
     *
     * @param backend Backend to describe.
     * @return `RendererWindowKind::OpenGL` for the OpenGL backend, `::Vulkan` for the Vulkan one.
     */
    [[nodiscard]] RendererWindowKind GetRendererBackendWindowKind(RendererBackend backend);

    /**
     * @brief Returns the framebuffer attributes that must be fixed before the window is created.
     *
     * GLX chooses a window's visual -- and therefore its depth, stencil and multisample bits --
     * when the window is created, so a renderer asking for them afterwards silently gets whatever
     * the default visual happened to carry (in practice a 0-bit stencil buffer, which makes every
     * `DepthStencilState::StencilEnable` a permanent no-op). This renderer's own OpenGL context
     * request (`IglPlatformSurface.cpp`) is built from the very same values, so the two statements
     * about one window cannot drift apart.
     *
     * @param backend Backend to describe.
     * @return The request for the OpenGL backend; an all-zero ("no request") value for Vulkan,
     *         whose window carries no OpenGL visual at all.
     */
    [[nodiscard]] RendererGlFramebufferRequest GetRendererBackendGlFramebufferRequest(
        RendererBackend backend);

    /**
     * @brief Resolves the backend for the pre-window decision, without ever throwing.
     *
     * Same answer as @ref ResolveRendererBackend for every valid configuration, and backed by the
     * same cache, so the window kind chosen here and the device built later cannot disagree. It
     * exists because the renderer descriptor that carries the window kind is built from a static
     * initializer (the generated registry publishes the compiled-in set before `main()`), where a
     * thrown exception would terminate the process instead of reporting a bad `CNA_IGL_BACKEND`.
     *
     * When the environment names a backend this build does not contain, this reports the build's
     * own first preference so the window is still created for something real; the renderer's own
     * @ref ResolveRendererBackend then fails loudly by name when the device is constructed.
     *
     * @return The backend this process will use.
     */
    [[nodiscard]] RendererBackend ResolveRendererBackendForWindow() noexcept;
}
