// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <LLGL/Surface.h>
#include <LLGL/Types.h>

#include <cstddef>

struct SDL_Window;

namespace CNA::Internal::Renderers::Llgl
{
    /**
     * @brief Presents an existing SDL window to LLGL as an LLGL::Surface. CNAEXT.
     *
     * LLGL ships its own windowing layer (LLGL::Window/LLGL::Display) and will create a window for
     * a swap chain if none is supplied. CNA never lets it: the window belongs to
     * `Microsoft::Xna::Framework::Graphics::GraphicsDevice`, is shared with SDL's input and event
     * handling, and is created before any renderer exists. This class is the adapter that lets LLGL
     * render into that window instead of one of its own.
     *
     * Only the X11 SDL video driver is supported. LLGL 0.04b compiles Wayland support in only when
     * explicitly enabled, and CNA's integration does not enable it, so a Wayland session is
     * reported as an error rather than silently producing a surface LLGL cannot present to.
     */
    class LlglSdlSurface final : public LLGL::Surface
    {
    public:
        /**
         * @brief Wraps an SDL window that has already been created.
         *
         * @param window The window to present to LLGL; must outlive this surface.
         * @throws std::runtime_error If @p window is null, or the SDL video driver in use is not
         *         one this surface can express as an LLGL native handle.
         */
        explicit LlglSdlSurface(SDL_Window* window);

        /** @brief Frees the cached X11 visual info, if any was ever resolved. */
        ~LlglSdlSurface() override;

        /**
         * @brief Fills in the platform native handle LLGL needs to create its context or surface.
         *
         * On X11 this reports the display connection, the window, and the window's own visual.
         * Supplying the real visual matters: LLGL otherwise picks one itself, and a GLX context
         * built on a visual that does not match the window's fails to become current.
         *
         * @param nativeHandle Storage for an LLGL::NativeHandle.
         * @param nativeHandleSize Size of that storage, for LLGL's own robustness check.
         * @return True if the handle was written.
         */
        bool GetNativeHandle(void* nativeHandle, std::size_t nativeHandleSize) override;

        /**
         * @brief Returns the window's drawable size in pixels.
         *
         * @return The size in pixels, which on a scaled display differs from the window size in
         *         window coordinates.
         */
        [[nodiscard]] LLGL::Extent2D GetContentSize() const override;

        /**
         * @brief Adapts the window to a requested resolution and fullscreen state.
         *
         * @param resolution In/out resolution; on return it holds the size actually adopted.
         * @param fullscreen In/out fullscreen flag; on return it holds the state actually adopted.
         * @return True if the request was satisfied unchanged.
         */
        bool AdaptForVideoMode(LLGL::Extent2D* resolution, bool* fullscreen) override;

        /**
         * @brief Returns the LLGL display this window is considered resident in.
         *
         * @return The primary display, or null when LLGL reports no displays.
         */
        [[nodiscard]] LLGL::Display* FindResidentDisplay() const override;

        /** @brief Returns the wrapped SDL window. */
        [[nodiscard]] SDL_Window* GetSdlWindow() const { return window_; }

    private:
        SDL_Window* window_ = nullptr;

        // The window's visual and colormap are fixed for its lifetime (SDL commits to one at
        // window creation), so both are resolved at most once and cached rather than re-queried --
        // and, on X11, re-allocated -- on every GetNativeHandle() call. Untyped so this header does
        // not need Xlib's XVisualInfo; the .cpp is the only translation unit that includes X11
        // headers (see its own top comment for why).
        void* cachedVisualInfo_ = nullptr;
        bool visualResolved_ = false;
        unsigned long cachedColorMap_ = 0;
    };
}
