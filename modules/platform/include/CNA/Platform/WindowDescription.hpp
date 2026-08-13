// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Platform {

    /**
     * @brief What a window will be used to draw with.
     *
     * This is creation-time information, not a setting that can be changed afterwards: a window
     * intended for OpenGL must be created with the corresponding native attribute already set,
     * and the same is true for a Vulkan-capable surface. Making it a post-creation setter would
     * be an API that cannot work.
     */
    enum class WindowRenderIntent
    {
        /** @brief No graphics-API-specific window attributes are required. */
        None,
        /** @brief The window will host an OpenGL or OpenGL ES context. */
        OpenGl,
        /** @brief The window will host a Vulkan surface. */
        Vulkan,
        /** @brief The window will host a Metal layer. */
        Metal
    };

    /** @brief How a window occupies the display. */
    enum class WindowFullscreenMode
    {
        /** @brief An ordinary window with the platform's decorations. */
        Windowed,
        /** @brief Fullscreen at the display's current desktop mode, without changing it. */
        BorderlessFullscreen,
        /** @brief Exclusive fullscreen, which may change the display mode. */
        ExclusiveFullscreen
    };

    /** @brief Creation-time framebuffer attributes for an OpenGL-capable window. */
    struct OpenGlFramebufferDescription
    {
        /** @brief Requested depth-buffer precision in bits; zero leaves the platform default. */
        int depthBits = 0;
        /** @brief Requested stencil-buffer precision in bits; zero leaves the platform default. */
        int stencilBits = 0;
        /** @brief Whether the window visual must support double buffering. */
        bool doubleBuffered = false;
        /** @brief Requested sample count; zero or one disables multisampling. */
        int samples = 0;
    };

    /**
     * @brief The parameters a window is created with.
     *
     * An aggregate with defaults chosen so that a default-constructed description produces a
     * reasonable, visible, resizable window — the common case needs to set only a title and a
     * size.
     */
    struct WindowDescription
    {
        /** @brief The window's title. */
        std::string title;

        /** @brief Requested client-area width, in logical units. */
        int width = 800;
        /** @brief Requested client-area height, in logical units. */
        int height = 480;

        /** @brief Minimum client width the user may resize to; zero means unconstrained. */
        int minimumWidth = 0;
        /** @brief Minimum client height the user may resize to; zero means unconstrained. */
        int minimumHeight = 0;
        /** @brief Maximum client width the user may resize to; zero means unconstrained. */
        int maximumWidth = 0;
        /** @brief Maximum client height the user may resize to; zero means unconstrained. */
        int maximumHeight = 0;

        /** @brief Whether the platform should centre the window; when false, @ref x and @ref y are used. */
        bool centered = true;
        /** @brief Requested left position, in logical units. Ignored when @ref centered is true. */
        int x = 0;
        /** @brief Requested top position, in logical units. Ignored when @ref centered is true. */
        int y = 0;

        /** @brief Whether the user may resize the window. */
        bool resizable = true;
        /** @brief Whether the window is created without decorations. */
        bool borderless = false;
        /** @brief Whether the window is visible immediately on creation. */
        bool visible = true;

        /** @brief How the window occupies the display. */
        WindowFullscreenMode fullscreenMode = WindowFullscreenMode::Windowed;

        /**
         * @brief Whether the window should opt into high-DPI drawable sizing.
         *
         * When true and the platform supports it, the drawable pixel size may exceed the logical
         * client size. Ignored by platforms that report no `HighDpi` capability.
         */
        bool highDpi = false;

        /** @brief What the window will be used to draw with. Must be correct at creation time. */
        WindowRenderIntent renderIntent = WindowRenderIntent::None;

        /**
         * @brief OpenGL framebuffer requirements that must be fixed before window creation.
         *
         * Used only when @ref renderIntent is `OpenGl`; other render intents ignore it.
         */
        OpenGlFramebufferDescription openGlFramebuffer;
    };

    /**
     * @brief Returns the stable, human-readable name of a render intent.
     *
     * @param intent The intent to name.
     * @return A stable name such as `"OpenGl"`.
     */
    [[nodiscard]] const std::string& ToString(WindowRenderIntent intent);

    /**
     * @brief Returns the stable, human-readable name of a fullscreen mode.
     *
     * @param mode The mode to name.
     * @return A stable name such as `"BorderlessFullscreen"`.
     */
    [[nodiscard]] const std::string& ToString(WindowFullscreenMode mode);

} // namespace CNA::Platform
