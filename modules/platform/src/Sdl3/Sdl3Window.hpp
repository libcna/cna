// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformWindow.hpp"

struct SDL_Window;

namespace CNA::Platform::Sdl3 {

    /**
     * @brief An SDL3-backed window.
     *
     * Owns the underlying `SDL_Window` and destroys it on destruction. Created only through
     * Sdl3Platform::CreateWindow, which is what guarantees the video subsystem is up first.
     *
     * This is the one place in the module where an `SDL_Window*` is stored. Everything outside
     * receives a NativeWindowHandle instead, which is what lets a renderer work identically
     * against a future non-SDL platform.
     */
    class Sdl3Window final : public IPlatformWindow
    {
    public:
        /**
         * @brief Wraps an already-created SDL window.
         *
         * @param window The window to wrap. Must not be null.
         * @param ownsWindow Whether destruction also destroys the native window.
         */
        explicit Sdl3Window(SDL_Window* window, bool ownsWindow = true);

        /** @brief Destroys the wrapper and, when owned, the underlying SDL window. */
        ~Sdl3Window() override;

        Sdl3Window(const Sdl3Window&) = delete;
        Sdl3Window& operator=(const Sdl3Window&) = delete;

        /** @brief Gets the window's id. @return The SDL window id. */
        [[nodiscard]] WindowId GetId() const override;

        /** @brief Gets the legacy SDL window token. @return The non-owning pointer as an integer. */
        [[nodiscard]] std::uintptr_t GetWindowHandle() const override;

        /** @brief Gets the native handle for a renderer. @return The handle for this window system. */
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;

        /** @brief Gets the title. @return The current title. */
        [[nodiscard]] std::string GetTitle() const override;

        /** @brief Sets the title. @param title The new title. */
        void SetTitle(const std::string& title) override;

        /** @brief Gets the client area in logical units. @return The client bounds. */
        [[nodiscard]] WindowBounds GetClientBounds() const override;

        /** @brief Gets the drawable size in physical pixels. @return The pixel size. */
        [[nodiscard]] WindowSize GetPixelSize() const override;

        /** @brief Sets the client size. @param width New width. @param height New height. */
        void SetSize(int width, int height) override;

        /** @brief Gets the display scale. @return Logical-to-physical scale factor. */
        [[nodiscard]] float GetDisplayScale() const override;

        /** @brief Gets whether the user may resize. @return True when resizing is enabled. */
        [[nodiscard]] bool IsResizable() const override;

        /** @brief Sets whether the user may resize. @param resizable True to allow resizing. */
        void SetResizable(bool resizable) override;

        /** @brief Gets whether the window is undecorated. @return True when borderless. */
        [[nodiscard]] bool IsBorderless() const override;

        /** @brief Sets whether the window is undecorated. @param borderless True to remove the border. */
        void SetBorderless(bool borderless) override;

        /** @brief Sets how the window occupies the display. @param mode The requested mode. */
        void SetFullscreenMode(WindowFullscreenMode mode) override;

        /** @brief Gets how the window occupies the display. @return The current mode. */
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override;

        /** @brief Makes the window visible. */
        void Show() override;

        /** @brief Hides the window. */
        void Hide() override;

        /** @brief Minimises the window. */
        void Minimize() override;

        /** @brief Maximises the window. */
        void Maximize() override;

        /** @brief Returns the window to its normal state. */
        void Restore() override;

        /** @brief Blocks until pending state changes have been applied. */
        void Sync() override;

        /** @brief Gets whether the window has keyboard focus. @return True if focused. */
        [[nodiscard]] bool HasFocus() const override;

        /** @brief Gets whether the window is minimised. @return True if minimised. */
        [[nodiscard]] bool IsMinimized() const override;

        /** @brief Gets the current display's name. @return The name, or empty when unavailable. */
        [[nodiscard]] std::string GetDisplayName() const override;

        /**
         * @brief Gets the underlying SDL window.
         *
         * Implementation-internal: it exists so sibling SDL3 services (displays, GL contexts,
         * the surface presenter) can reach the window without the public contract growing an
         * SDL type. Never exposed outside `CNA::Platform::Sdl3`.
         *
         * @return The owned `SDL_Window`; never null.
         */
        [[nodiscard]] SDL_Window* GetSdlWindow() const { return window_; }

    private:
        SDL_Window* window_;
        bool ownsWindow_;
        // SDL's web backend may transiently fail a size query immediately after creation while
        // its canvas finishes asynchronously. Preserve the last good value across that tick.
        mutable WindowBounds lastKnownBounds_;
    };

} // namespace CNA::Platform::Sdl3
