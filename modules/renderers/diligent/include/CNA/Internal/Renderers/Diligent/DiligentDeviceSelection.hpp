// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <string>
#include <vector>

// DILIGENT-57: split out of DiligentRenderer.hpp deliberately -- this device-selection
// logic needs no DiligentCore type at all (pure enum/string parsing), so it can be included from
// translation units that have no DiligentCore include path wired up (GraphicsDevice.cpp, part of
// the CNA target, not the Diligent renderer target `cna_link_diligent()` sets up private include
// dirs for). GraphicsDevice.cpp's own SDL-window-flag selection needs this SAME parser -- see its
// own comment -- to resolve every CNA_DILIGENT_DEVICE alias exactly the way
// DiligentRenderer::CreateDeviceAndSwapChain() itself does, rather than maintaining a
// second, narrower parser that can silently disagree with this one.
namespace CNA::Internal::Renderers::Diligent
{
    /**
     * @brief CNAEXT. The Diligent Engine device type (native graphics API) a renderer instance runs on.
     *
     * Diligent is itself an abstraction over several native APIs, so unlike every other CNA
     * renderer the choice is made at runtime rather than by the `CNA_GRAPHICS_RENDERER` CMake
     * option. Which values are actually available in a given build is decided by the
     * `CNA_DILIGENT_HAS_*` compile definitions set by `cna_link_diligent()`.
     */
    enum class DiligentDeviceType
    {
        /** @brief Direct3D 12 (Windows only). */
        D3D12,
        /** @brief Vulkan. */
        Vulkan,
        /** @brief Direct3D 11 (Windows only). */
        D3D11,
        /** @brief OpenGL / OpenGL ES. */
        OpenGL,
    };

    /**
     * @brief CNAEXT. Returns the stable, human-readable name of a device type ("Vulkan", "OpenGL", …).
     *
     * @param type Device type to name.
     * @return Name of the device type; never empty.
     */
    CNAEXT [[nodiscard]] const char* GetDeviceTypeName(DiligentDeviceType type);

    /**
     * @brief CNAEXT. Parses the `CNA_DILIGENT_DEVICE` environment variable value.
     *
     * Accepted values are case-insensitive: `d3d12`/`direct3d12`, `vulkan`/`vk`, `d3d11`/
     * `direct3d11`, `opengl`/`gl`/`gles`, and `auto`/empty (meaning "use the built-in preference
     * order").
     *
     * @param value Raw environment variable text.
     * @return The requested device type, or the full preference order for `auto`/empty.
     * @throws std::runtime_error If @p value names no known device type.
     */
    CNAEXT [[nodiscard]] std::vector<DiligentDeviceType> ParseDeviceTypeOverride(const std::string& value);

    /**
     * @brief CNAEXT. Returns the device types this build can attempt, in preference order.
     *
     * The order is D3D12 → Vulkan → D3D11 → OpenGL, filtered down to the engines DiligentCore
     * actually built for this platform. Device creation walks the list and uses the first entry
     * that succeeds, so an entry being present means "worth attempting", not "known to work".
     *
     * @return Preference-ordered device types; empty only if no engine was built at all.
     */
    CNAEXT [[nodiscard]] std::vector<DiligentDeviceType> GetDeviceTypePreferenceOrder();
}
