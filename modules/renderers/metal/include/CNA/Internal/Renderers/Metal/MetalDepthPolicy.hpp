// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Renderers::Metal
{
    /**
     * @brief Resolves the native depth-write flag from independently retained XNA state.
     *
     * @param depthEnabled Whether depth testing/storage is currently enabled.
     * @param depthWriteRequested The caller's retained DepthBufferWriteEnable value.
     * @return True only when depth is enabled and writes were requested.
     */
    [[nodiscard]] constexpr bool MetalEffectiveDepthWriteEnabled(
        bool depthEnabled,
        bool depthWriteRequested) noexcept
    {
        return depthEnabled && depthWriteRequested;
    }
}
