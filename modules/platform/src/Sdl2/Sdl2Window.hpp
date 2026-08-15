// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformWindow.hpp"

struct SDL_Window;

namespace CNA::Platform::Sdl2 {

    /** A real SDL2 window. SDL types remain confined to this implementation directory. */
    class Sdl2Window final : public IPlatformWindow
    {
    public:
        explicit Sdl2Window(SDL_Window* window, bool ownsWindow = true);
        ~Sdl2Window() override;

        Sdl2Window(const Sdl2Window&) = delete;
        Sdl2Window& operator=(const Sdl2Window&) = delete;

        [[nodiscard]] WindowId GetId() const override;
        [[nodiscard]] std::uintptr_t GetWindowHandle() const override;
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;
        [[nodiscard]] std::string GetTitle() const override;
        void SetTitle(const std::string& title) override;
        [[nodiscard]] WindowBounds GetClientBounds() const override;
        [[nodiscard]] WindowSize GetPixelSize() const override;
        void SetSize(int width, int height) override;
        [[nodiscard]] float GetDisplayScale() const override;
        [[nodiscard]] bool IsResizable() const override;
        void SetResizable(bool resizable) override;
        [[nodiscard]] bool IsBorderless() const override;
        void SetBorderless(bool borderless) override;
        void SetFullscreenMode(WindowFullscreenMode mode) override;
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override;
        void Show() override;
        void Hide() override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void Sync() override;
        [[nodiscard]] bool HasFocus() const override;
        [[nodiscard]] bool IsMinimized() const override;
        [[nodiscard]] std::string GetDisplayName() const override;

        [[nodiscard]] SDL_Window* GetSdlWindow() const { return window_; }

    private:
        SDL_Window* window_ = nullptr;
        bool ownsWindow_ = true;
    };

} // namespace CNA::Platform::Sdl2
