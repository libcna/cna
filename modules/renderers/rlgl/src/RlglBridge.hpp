// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"

#include <string>

namespace CNA::Internal::Renderers::Rlgl::Bridge
{
    enum ClearPlane : unsigned int
    {
        /** @brief Selects the active framebuffer's color planes. */
        ColorPlane = 1u << 0u,
        /** @brief Selects the active framebuffer's depth plane. */
        DepthPlane = 1u << 1u,
        /** @brief Selects the active framebuffer's stencil plane. */
        StencilPlane = 1u << 2u,
    };

    /**
     * @brief Loads rlgl's GL dispatch and creates its default resources.
     * @param loader CNA platform entry-point loader.
     * @param width Initial drawable width.
     * @param height Initial drawable height.
     * @return The driver OpenGL version string.
     */
    [[nodiscard]] std::string Initialize(
        CNA::Platform::GlProcAddressLoader loader, int width, int height);

    /** @brief Releases rlgl's default resources while its context is current. */
    void Shutdown() noexcept;

    /**
     * @brief Updates rlgl's physical framebuffer bookkeeping.
     * @param width Drawable width.
     * @param height Drawable height.
     */
    void SetFramebufferSize(int width, int height);

    /**
     * @brief Clears selected framebuffer planes while preserving their write masks.
     * @param planes Bitwise ClearPlane selection.
     * @param r Red clear component.
     * @param g Green clear component.
     * @param b Blue clear component.
     * @param a Alpha clear component.
     * @param depth Depth clear value.
     * @param stencil Stencil clear value.
     */
    void Clear(unsigned int planes, float r, float g, float b, float a,
               float depth, int stencil);

    /**
     * @brief Enables or disables depth testing through rlgl.
     * @param enabled True to enable the test.
     */
    void SetDepthTestEnabled(bool enabled);

    /**
     * @brief Enables or disables color blending through rlgl.
     * @param enabled True to enable blending.
     */
    void SetBlendEnabled(bool enabled);

    /**
     * @brief Enables or disables depth writes through rlgl.
     * @param enabled True to enable depth writes.
     */
    void SetDepthWriteEnabled(bool enabled);

    /**
     * @brief Sets the GL viewport and depth range.
     * @param x Left edge in GL coordinates.
     * @param y Bottom edge in GL coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     * @param minDepth Minimum depth value.
     * @param maxDepth Maximum depth value.
     */
    void SetViewport(int x, int y, int width, int height, float minDepth, float maxDepth);

    /**
     * @brief Sets the GL scissor rectangle.
     * @param x Left edge in GL coordinates.
     * @param y Bottom edge in GL coordinates.
     * @param width Width in pixels.
     * @param height Height in pixels.
     */
    void SetScissor(int x, int y, int width, int height);

    /** @brief Restores the platform default framebuffer as the active draw target. */
    void BindDefaultFramebuffer();
}
