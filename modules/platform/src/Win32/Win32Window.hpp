// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformWindow.hpp"

#include "Win32Common.hpp"
#include "Win32EventMapper.hpp"
#include "Win32FullscreenState.hpp"
#include "Win32WindowClass.hpp"

#include <memory>
#include <optional>
#include <string>

namespace CNA::Platform::Win32 {

    class Win32Window;

    /**
     * @brief What a window needs from the platform that owns it.
     *
     * Narrow on purpose. A window must be able to queue events and to tell the platform it is
     * going away, and it must not be able to do anything else to it -- in particular it never
     * reaches back for other windows, which is what keeps the `HWND` registry's invariants in one
     * place.
     */
    class Win32WindowHost : public Win32EventSink
    {
    public:
        /** @brief Destroys the host. */
        ~Win32WindowHost() override = default;

        /**
         * @brief Reports that a window wrapper has been destroyed.
         *
         * Takes the wrapper rather than its id, because an id does not identify one: an adopted
         * wrapper deliberately shares the id of the window it borrows, and erasing the registry
         * entry by id would unregister the *owner* when a borrowed wrapper went out of scope.
         *
         * @param window The wrapper that is going away.
         */
        virtual void OnWindowDestroyed(Win32Window& window) = 0;

        /**
         * @brief Reports raw pointer displacement while relative mouse mode is active.
         *
         * @param deltaX Horizontal displacement in raw device units.
         * @param deltaY Vertical displacement in raw device units.
         */
        virtual void OnRawPointerDelta(int deltaX, int deltaY) = 0;

        /**
         * @brief Reports that the process, not merely a window, is being asked to end.
         *
         * Kept separate from window close so a multi-window application does not terminate
         * because one of its windows was closed.
         */
        virtual void OnQuitRequested() = 0;
    };

    /**
     * @brief A window backed by a native Win32 `HWND`.
     *
     * The one place in CNA that stores an `HWND`. Everything outside receives a
     * `NativeWindowHandle`, which is what lets `DirectX11Renderer` and `DirectX12Renderer` work
     * identically against this window and an SDL3 one without either knowing the other exists.
     *
     * ### Lifetime and the window procedure
     *
     * The `this` pointer is threaded into `CreateWindowExW` through `CREATESTRUCTW::lpCreateParams`,
     * installed into `GWLP_USERDATA` while handling `WM_NCCREATE`, and **cleared while handling
     * `WM_NCDESTROY`** -- the last message a window ever receives. Messages arriving before the
     * first or after the last read a null pointer and fall through to `DefWindowProcW`, so there is
     * no window in a window's life during which the procedure can dereference a stale pointer.
     *
     * A window created here owns its `HWND` and destroys it. A window produced by
     * `IPlatform::AdoptWindow` does not: it never calls `DestroyWindow`, never subclasses the
     * foreign window procedure, and holds no window-class reference.
     */
    class Win32Window final : public IPlatformWindow
    {
    public:
        /** @brief Selects the non-owning constructor. */
        struct AdoptTag
        {
        };

        /**
         * @brief Creates a native window.
         *
         * @param description The creation parameters. `width`/`height` are the **client** size.
         * @param id The stable id events for this window carry.
         * @param host The owning platform.
         * @throws PlatformException If the native window could not be created.
         */
        Win32Window(const WindowDescription& description, WindowId id, Win32WindowHost& host);

        /**
         * @brief Wraps an existing window without taking ownership of it.
         *
         * @param window The existing `HWND`. Must be a valid window.
         * @param id The stable id events for this window carry.
         * @param host The owning platform.
         * @throws PlatformException If @p window is not a valid window handle.
         */
        Win32Window(HWND window, WindowId id, Win32WindowHost& host, AdoptTag);

        /** @brief Destroys the wrapper and, when it owns one, the native window. */
        ~Win32Window() override;

        Win32Window(const Win32Window&) = delete;
        Win32Window& operator=(const Win32Window&) = delete;

        /**
         * @brief The window procedure every CNA window is created with.
         *
         * Static because Win32 has no notion of an instance procedure: it resolves the owning
         * window from `GWLP_USERDATA` and forwards, or defers to `DefWindowProcW` when there is
         * none yet.
         *
         * @param window The window the message is for.
         * @param message The message identifier.
         * @param wParam The message's first parameter.
         * @param lParam The message's second parameter.
         * @return The message's result.
         */
        static LRESULT CALLBACK StaticWindowProc(HWND window, UINT message, WPARAM wParam,
                                                 LPARAM lParam);

        /** @brief Gets the id identifying this window in events. @return A non-zero id. */
        [[nodiscard]] WindowId GetId() const override;

        /** @brief Gets the legacy integer token. @return The `HWND` as an integer. */
        [[nodiscard]] std::uintptr_t GetWindowHandle() const override;

        /** @brief Gets the handle a renderer initializes against. @return A `Win32` handle. */
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;

        /** @brief Gets the window's title. @return The current title, UTF-8 encoded. */
        [[nodiscard]] std::string GetTitle() const override;

        /** @brief Sets the window's title. @param title The new title, UTF-8 encoded. */
        void SetTitle(const std::string& title) override;

        /** @brief Gets the client area in logical units. @return The client bounds. */
        [[nodiscard]] WindowBounds GetClientBounds() const override;

        /** @brief Gets the drawable size in physical pixels. @return The pixel size. */
        [[nodiscard]] WindowSize GetPixelSize() const override;

        /** @brief Sets the client size. @param width New client width. @param height New client height. */
        void SetSize(int width, int height) override;

        /** @brief Gets the display scale. @return The window's DPI divided by 96; never zero. */
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

        /** @brief Hides the window without destroying it. */
        void Hide() override;

        /** @brief Minimises the window. */
        void Minimize() override;

        /** @brief Maximises the window. */
        void Maximize() override;

        /** @brief Returns the window to its normal state. */
        void Restore() override;

        /** @brief Drains this window's pending messages so requested state becomes observable. */
        void Sync() override;

        /** @brief Gets whether the window has keyboard focus. @return True if focused. */
        [[nodiscard]] bool HasFocus() const override;

        /** @brief Gets whether the window is minimised. @return True if minimised. */
        [[nodiscard]] bool IsMinimized() const override;

        /** @brief Gets the name of the display the window is on. @return The device name. */
        [[nodiscard]] std::string GetDisplayName() const override;

        /**
         * @brief Gets the native window handle, for sibling Win32 services only.
         *
         * Implementation-internal: the display, presenter, GL context and Vulkan services need
         * the `HWND` and must not obtain it by casting a public `NativeWindowHandle` back. Never
         * exposed outside `CNA::Platform::Win32`.
         *
         * @return The `HWND`; never null while the window is alive.
         */
        [[nodiscard]] HWND GetHwnd() const { return hwnd_; }

        /**
         * @brief Gets this window's message translator.
         *
         * @return The mapper, whose held-key and pointer bookkeeping the input services read.
         */
        [[nodiscard]] Win32EventMapper& GetMapper() { return mapper_; }

        /**
         * @brief Gets this window's message translator.
         *
         * @return The mapper.
         */
        [[nodiscard]] const Win32EventMapper& GetMapper() const { return mapper_; }

        /**
         * @brief Gets whether this wrapper owns the native window it describes.
         *
         * @return True for a window created by `IPlatform::CreateWindow`, false for one borrowed
         *         through `AdoptWindow`.
         */
        [[nodiscard]] bool OwnsWindow() const { return ownsWindow_; }

        /**
         * @brief Forgets the owning platform, leaving the window itself intact.
         *
         * The contract lets an application destroy a window and its platform in either order. A
         * window that outlives its platform is still a perfectly good native window -- it can be
         * resized, retitled and destroyed -- it simply has nowhere to send events, so it stops
         * producing them rather than writing into a destroyed queue.
         */
        void DetachHost();

        /**
         * @brief Enables or disables raw-input pointer capture for relative mouse mode.
         *
         * @param enabled True to register for raw pointer input and confine the cursor.
         * @return True when the requested state was achieved.
         */
        bool SetRawPointerCapture(bool enabled);

        /**
         * @brief Gets whether raw pointer capture is active on this window.
         *
         * @return True while relative mode holds this window.
         */
        [[nodiscard]] bool HasRawPointerCapture() const { return rawPointerCapture_; }

    private:
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
        void ApplyStyle(LONG_PTR style, LONG_PTR exStyle);
        [[nodiscard]] LONG_PTR CurrentStyle() const;
        [[nodiscard]] LONG_PTR CurrentExStyle() const;
        void EnterBorderlessFullscreen();
        void EnterExclusiveFullscreen();
        void LeaveFullscreen();
        void ResizeClientArea(int width, int height);
        void HandleRawInput(LPARAM lParam);

        HWND hwnd_ = nullptr;
        WindowId id_ = 0;
        Win32WindowHost* host_ = nullptr;
        bool ownsWindow_ = true;
        std::optional<Win32WindowClass> windowClass_;
        Win32EventMapper mapper_;

        WindowFullscreenMode fullscreenMode_ = WindowFullscreenMode::Windowed;
        Win32FullscreenState windowedState_;
        bool exclusiveModeChanged_ = false;

        int minimumWidth_ = 0;
        int minimumHeight_ = 0;
        int maximumWidth_ = 0;
        int maximumHeight_ = 0;

        bool rawPointerCapture_ = false;
        bool pointerCaptured_ = false;
        bool trackingPointerLeave_ = false;
        bool destroying_ = false;
    };

} // namespace CNA::Platform::Win32
