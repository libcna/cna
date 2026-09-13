// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformWindow.hpp"

#include "X11Headers.hpp"

#include <string>

namespace CNA::Platform::X11 {

    class X11Connection;

    /**
     * @brief A real X11 window.
     *
     * Xlib types stay inside this implementation directory: the only thing that leaves is the
     * `NativeWindowHandle`, which carries an opaque `void* display` and the XID as an integer.
     *
     * ### Two identities, deliberately
     *
     * The X server's identity for this window is its XID. CNA's identity is a `WindowId`, a dense
     * counter assigned by the platform. They are kept separate because the contract's
     * `WindowId` is documented as a small stable integer used in every `PlatformEvent`, while an
     * XID is a sparse 29-bit server resource id whose value encodes the owning client. Using the
     * XID as the `WindowId` would work by accident on a 32-bit `WindowId` today and break the
     * first time a server hands out an id above `UINT32_MAX`.
     *
     * ### What this owns
     *
     * The window, its colormap when it created one, and its input context. Not the connection,
     * not the visual, and not the event pump: those belong to the platform.
     */
    class X11Window final : public IPlatformWindow
    {
    public:
        /**
         * @brief Wraps an X window this object may or may not own.
         *
         * @param connection The connection the window lives on.
         * @param window The X window XID.
         * @param id The CNA window id assigned by the platform.
         * @param colormap A colormap created for this window, or the null resource id.
         * @param visual The visual the window was created with; null for an adopted window.
         * @param depth The window's depth in bits.
         * @param ownsWindow False for an adopted window CNA must not destroy.
         */
        X11Window(X11Connection& connection, ::Window window, WindowId id, Colormap colormap,
                  Visual* visual, int depth, bool ownsWindow);

        /** @brief Destroys the input context, the window and the colormap this object owns. */
        ~X11Window() override;

        X11Window(const X11Window&) = delete;
        X11Window& operator=(const X11Window&) = delete;

        /** @brief Gets the CNA window id. @return A non-zero id stable for the window's lifetime. */
        [[nodiscard]] WindowId GetId() const override { return id_; }

        /**
         * @brief Gets the legacy integer handle for XNA-compatible window-handle properties.
         *
         * The X window XID, which is what an application interoperating with another X toolkit
         * actually needs. `AdoptWindowHandle` accepts the same value back.
         *
         * @return The XID as an integer token.
         */
        [[nodiscard]] std::uintptr_t GetWindowHandle() const override;

        /** @brief Gets the renderer-facing native handle. @return `{X11, display, windowId=XID}`. */
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;

        /** @brief Gets the window's title. @return The current `_NET_WM_NAME`. */
        [[nodiscard]] std::string GetTitle() const override;

        /** @brief Sets the window's title. @param title The new title, UTF-8. */
        void SetTitle(const std::string& title) override;

        /** @brief Gets the client area in root coordinates. @return The client bounds. */
        [[nodiscard]] WindowBounds GetClientBounds() const override;

        /** @brief Gets the drawable size in physical pixels. @return The drawable size. */
        [[nodiscard]] WindowSize GetPixelSize() const override;

        /**
         * @brief Resizes the client area.
         * @param width The new width in logical units.
         * @param height The new height in logical units.
         */
        void SetSize(int width, int height) override;

        /** @brief Gets the display scale. @return The connection's scale; see design decision 10. */
        [[nodiscard]] float GetDisplayScale() const override;

        /** @brief Gets whether the user may resize. @return True when resizing is allowed. */
        [[nodiscard]] bool IsResizable() const override { return resizable_; }

        /** @brief Sets whether the user may resize. @param resizable True to allow resizing. */
        void SetResizable(bool resizable) override;

        /** @brief Gets whether decorations are hidden. @return True when borderless. */
        [[nodiscard]] bool IsBorderless() const override { return borderless_; }

        /** @brief Sets whether decorations are hidden. @param borderless True to remove them. */
        void SetBorderless(bool borderless) override;

        /**
         * @brief Sets how the window occupies the display.
         * @param mode The requested mode.
         * @throws PlatformNotSupportedException For `ExclusiveFullscreen`, and for
         * `BorderlessFullscreen` when the window manager does not advertise it.
         */
        void SetFullscreenMode(WindowFullscreenMode mode) override;

        /** @brief Gets the current fullscreen mode. @return The current mode. */
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override;

        /** @brief Maps the window. */
        void Show() override;
        /** @brief Unmaps the window without destroying it. */
        void Hide() override;
        /** @brief Iconifies the window. */
        void Minimize() override;
        /** @brief Maximises the window through `_NET_WM_STATE`. */
        void Maximize() override;
        /** @brief Returns the window to its normal state. */
        void Restore() override;
        /** @brief Round-trips the connection so pending state changes have been applied. */
        void Sync() override;

        /** @brief Gets whether the window has keyboard focus. @return True when focused. */
        [[nodiscard]] bool HasFocus() const override { return focused_; }

        /** @brief Gets whether the window is iconified. @return True when minimised. */
        [[nodiscard]] bool IsMinimized() const override;

        /** @brief Gets the name of the display this window is on. @return The display's name. */
        [[nodiscard]] std::string GetDisplayName() const override;

        // --- used by the platform and its services, not part of the contract -------------------

        /** @brief Gets the X window XID. @return The XID. */
        [[nodiscard]] ::Window GetXWindow() const { return window_; }

        /** @brief Gets the visual the window was created with. @return The visual, or null. */
        [[nodiscard]] Visual* GetVisual() const { return visual_; }

        /** @brief Gets the window's depth in bits. @return The depth. */
        [[nodiscard]] int GetDepth() const { return depth_; }

        /**
         * @brief Gets the GLX framebuffer configuration this window's visual was chosen from.
         *
         * Null unless the window was created with `WindowRenderIntent::OpenGl`. An X window's
         * visual is fixed at creation, so a context can only be made current on a window whose
         * visual came from a compatible config — which is why this is carried on the window
         * rather than looked up when a context is created.
         *
         * @return An opaque pointer to a `GLXFBConfig`, or null.
         */
        [[nodiscard]] void* GetGlFbConfig() const { return glFbConfig_; }

        /**
         * @brief Records the framebuffer configuration this window's visual came from.
         *
         * @param config An opaque pointer to a `GLXFBConfig`.
         */
        void SetGlFbConfig(void* config) { glFbConfig_ = config; }

        /** @brief Gets the connection this window lives on. @return The connection. */
        [[nodiscard]] X11Connection& GetConnection() const { return connection_; }

        /** @brief Gets the input context, or null when text input is not started. */
        [[nodiscard]] XIC GetInputContext() const { return inputContext_; }

        /**
         * @brief Attaches an input context created for this window.
         *
         * Ownership transfers: the window destroys the context before destroying itself, which is
         * the order Xlib requires.
         *
         * @param context The context, or null to detach and destroy the current one.
         */
        void SetInputContext(XIC context);

        /** @brief Records that the window gained or lost focus. @param focused The new state. */
        void SetFocused(bool focused) { focused_ = focused; }

        /** @brief Records the last size the server reported. @param width Width. @param height Height. */
        void SetCachedSize(int width, int height);

        /** @brief Records the last position the server reported. @param x Left. @param y Top. */
        void SetCachedPosition(int x, int y);

        /** @brief Gets the last size the server reported. @return The cached size. */
        [[nodiscard]] WindowSize GetCachedSize() const { return {cachedWidth_, cachedHeight_}; }

        /** @brief Gets whether the window is currently mapped. @return True when mapped. */
        [[nodiscard]] bool IsMapped() const { return mapped_; }

        /** @brief Records that the window was mapped or unmapped. @param mapped The new state. */
        void SetMapped(bool mapped) { mapped_ = mapped; }

        /** @brief Records the fullscreen mode observed from `_NET_WM_STATE`. @param mode The mode. */
        void SetObservedFullscreenMode(WindowFullscreenMode mode) { fullscreenMode_ = mode; }

        /**
         * @brief Applies creation-time size constraints and the resizable policy.
         *
         * @param minimumWidth Minimum client width, or zero for unconstrained.
         * @param minimumHeight Minimum client height, or zero for unconstrained.
         * @param maximumWidth Maximum client width, or zero for unconstrained.
         * @param maximumHeight Maximum client height, or zero for unconstrained.
         */
        void ApplySizeConstraints(int minimumWidth, int minimumHeight, int maximumWidth,
                                  int maximumHeight);

        /** @brief Records whether the user may resize, without talking to the server. */
        void SetResizableFlag(bool resizable) { resizable_ = resizable; }

        /** @brief Records the borderless flag, without talking to the server. */
        void SetBorderlessFlag(bool borderless) { borderless_ = borderless; }

    private:
        void ApplyNormalHints();
        void ApplyMotifDecorations(bool decorated);
        bool SetNetWmState(Atom first, Atom second, bool enabled);
        [[nodiscard]] bool HasNetWmState(Atom state) const;

        X11Connection& connection_;
        ::Window window_ = kNone;
        WindowId id_ = 0;
        Colormap colormap_ = kNone;
        Visual* visual_ = nullptr;
        int depth_ = 0;
        bool ownsWindow_ = true;
        XIC inputContext_ = nullptr;
        void* glFbConfig_ = nullptr;

        bool resizable_ = true;
        bool borderless_ = false;
        bool focused_ = false;
        bool mapped_ = false;
        WindowFullscreenMode fullscreenMode_ = WindowFullscreenMode::Windowed;

        int minimumWidth_ = 0;
        int minimumHeight_ = 0;
        int maximumWidth_ = 0;
        int maximumHeight_ = 0;

        int cachedWidth_ = 0;
        int cachedHeight_ = 0;
        int cachedX_ = 0;
        int cachedY_ = 0;
    };

} // namespace CNA::Platform::X11
