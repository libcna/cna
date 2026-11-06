// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"

struct SDL_Surface;

namespace CNA::Platform::Sdl3 {

    class Sdl3Window;

    /** @brief SDL3-backed OpenGL context management. */
    class Sdl3GlContext final : public IPlatformGlContext
    {
    public:
        /**
         * @brief Creates a context for a window.
         * @param window The stable id of the window to create the context for.
         * @param description The requested attributes.
         * @return A non-null context handle.
         */
        [[nodiscard]] GlContextHandle CreateContext(WindowId window,
                                                    const GlContextDescription& description) override;
        /** @brief Destroys a context. @param context The context to destroy. */
        void DestroyContext(GlContextHandle context) override;
        /**
         * @brief Makes a context current.
         * @param window The stable id of the window to bind to.
         * @param context The context, or null to unbind.
         */
        void MakeCurrent(WindowId window, GlContextHandle context) override;
        /** @brief Gets the calling thread's current GL binding. @return The current binding. */
        [[nodiscard]] GlContextBinding GetCurrentBinding() const override;
        /** @brief Presents the back buffer. @param window The stable id of the window to swap. */
        void SwapBuffers(WindowId window) override;
        /**
         * @brief Sets the swap interval.
         * @param interval 0 none, 1 vsync, -1 adaptive.
         * @return True if applied.
         */
        bool SetSwapInterval(int interval) override;
        /**
         * @brief Resolves a GL entry point.
         * @param name The entry point.
         * @return The pointer, or null when unavailable.
         */
        [[nodiscard]] void* GetProcAddress(const std::string& name) const override;
        /** @brief Gets the GL loader callback. @return Platform-owned resolver. */
        [[nodiscard]] GlProcAddressLoader GetProcAddressLoader() const override;
        /**
         * @brief Gets the attributes the driver actually granted.
         * @param context The context to query.
         * @return The granted attributes.
         */
        [[nodiscard]] GlContextDescription GetContextAttributes(GlContextHandle context) const override;
    };

    /** @brief Presents CPU pixels through the window's physical surface and dirty rectangles. */
    class Sdl3SurfacePresenter final : public IPlatformSurfacePresenter
    {
    public:
        /**
         * @brief Creates a presenter targeting a window.
         * @param window The window to present to.
         */
        explicit Sdl3SurfacePresenter(Sdl3Window& window);

        /** @brief Releases the presenter; the window owns its SDL surface. */
        ~Sdl3SurfacePresenter() override;

        Sdl3SurfacePresenter(const Sdl3SurfacePresenter&) = delete;
        Sdl3SurfacePresenter& operator=(const Sdl3SurfacePresenter&) = delete;

        /**
         * @brief Sets how a frame is fitted to the window.
         * @param mode The scaling mode.
         * @param filter The filter used when scaling.
         */
        void SetScaleMode(PresentScaleMode mode, PresentFilter filter) override;
        /**
         * @brief Sets vertical-blank synchronisation.
         * @param enabled True to synchronise.
         * @return True if applied.
         */
        bool SetVSync(bool enabled) override;
        /** @brief Presents one frame. @param frame The pixels to present. */
        void Present(const SurfaceFrame& frame) override;
        /**
         * @brief Updates a physical region without clearing the rest of the native client.
         * @param frame Borrowed RGBA8 source.
         * @param destination Original, unclipped client rectangle.
         * @return False when the native path cannot preserve an existing partial client.
         */
        bool PresentRegion(const SurfaceFrame& frame, const SurfaceDestination& destination) override;
        /**
         * @brief Gets the current target size.
         * @param width Receives the width.
         * @param height Receives the height.
         */
        void GetTargetSize(int& width, int& height) const override;

    private:
        SDL_Surface* GetSurface() const;
        void BlitFrame(const SurfaceFrame& frame, const SurfaceDestination& destination,
                       SDL_Surface* target) const;

        Sdl3Window& window_;
        PresentScaleMode scaleMode_ = PresentScaleMode::Letterbox;
        PresentFilter filter_ = PresentFilter::Nearest;
    };

} // namespace CNA::Platform::Sdl3
