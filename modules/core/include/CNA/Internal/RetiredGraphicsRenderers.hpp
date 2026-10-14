// SPDX-License-Identifier: MS-PL
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace CNA::Internal
{
    /**
     * @brief A graphics renderer identity CNA has retired, with its permanently reserved C ABI
     *        value.
     *
     * A retired identity is not a renderer: nothing selects it and nothing aliases it to a
     * surviving renderer. It is recorded only so that a configuration or a C caller still naming
     * it is told the renderer was retired, rather than that the name or value is unknown.
     */
    struct RetiredGraphicsRenderer
    {
        /** @brief The identity's spelling in CNA_GRAPHICS_RENDERER, upper case. */
        std::string_view name;
        /** @brief Its CNA_GRAPHICS_RENDERER_* value, reserved forever and never reassigned. */
        std::uint32_t value;
    };

    /**
     * @brief Every retired identity, the run-time copy of cmake/RendererIdentities.cmake's
     *        CNA_RENDERER_RETIRED_IDENTITIES.
     *
     * scripts/check_renderer_identities.py holds this table, the CMake list and its own pinned
     * table equal name for name and value for value (docs/removed-renderers.md).
     */
    inline constexpr std::array<RetiredGraphicsRenderer, 41> RetiredGraphicsRenderers{{
        {"SDL_RENDERER", 1}, {"OPENGLES2", 2}, {"WEBGL1", 5}, {"BGFX", 7}, {"VULKAN", 8},
        {"MAGNUM", 10}, {"DIRECTX11", 14}, {"DIRECTX12", 15}, {"DIRECT2D", 16}, {"CANVAS", 17},
        {"HTML_DOM", 18}, {"SKIA", 19}, {"BLEND2D", 20}, {"FREEDIRECT", 21}, {"DIRECTX1", 23},
        {"DIRECTX2", 24}, {"DIRECTX3", 25}, {"DIRECTX5", 26}, {"DIRECTX6", 27}, {"DIRECTX7", 28},
        {"DIRECTX8", 29}, {"DIRECTX10", 30}, {"OPENGLES1", 32}, {"OPENGL4", 33}, {"OPENGL1", 34},
        {"OPENGL2", 35}, {"WICKED", 36}, {"SOKOL", 37}, {"DILIGENT", 38}, {"GLIDE", 39},
        {"GDI", 40}, {"LLGL", 41}, {"METAL", 42}, {"SVG_DOM", 44}, {"OPENVG", 45},
        {"PORTABLEGL", 46}, {"TINYGL", 47}, {"IGL", 48}, {"PIXIJS", 49}, {"NANOVG", 50},
        {"RLGL", 51},
    }};

    /**
     * @brief Finds a retired identity by name, compared case-insensitively (ASCII).
     *
     * @param name The renderer name a caller asked for.
     * @return The retired entry, or nullptr when @p name is not a retired identity.
     */
    [[nodiscard]] constexpr const RetiredGraphicsRenderer* FindRetiredGraphicsRendererByName(
        std::string_view name) noexcept
    {
        const auto upper = [](char c) {
            return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        };
        for (const RetiredGraphicsRenderer& retired : RetiredGraphicsRenderers)
        {
            if (retired.name.size() == name.size()
                && std::equal(retired.name.begin(), retired.name.end(), name.begin(),
                              [&](char a, char b) { return a == upper(b); }))
                return &retired;
        }
        return nullptr;
    }

    /**
     * @brief Finds a retired identity by its reserved C ABI value.
     *
     * @param value A CNA_GRAPHICS_RENDERER_* value.
     * @return The retired entry, or nullptr when @p value is not a retired identity's (it may be
     *         a live identity's, or one never assigned).
     */
    [[nodiscard]] constexpr const RetiredGraphicsRenderer* FindRetiredGraphicsRendererByValue(
        std::uint32_t value) noexcept
    {
        for (const RetiredGraphicsRenderer& retired : RetiredGraphicsRenderers)
        {
            if (retired.value == value)
                return &retired;
        }
        return nullptr;
    }
}
