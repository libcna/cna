// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"

#include "WaylandProtocols.hpp"
#include "WaylandScaling.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland {

    class WaylandConnection;
    class WaylandOutput;
    class WaylandWindow;
    class WaylandFrame;

    /** @brief What a window needs from the platform that owns the connection. */
    class WaylandWindowHost
    {
    public:
        /** @brief Destroys the host. */
        virtual ~WaylandWindowHost() = default;

        /** @brief Gets the connection. @return The connection. */
        [[nodiscard]] virtual WaylandConnection& GetConnection() = 0;

        /**
         * @brief Queues an event for the next `PollEvents`.
         * @param event The event.
         */
        virtual void PostEvent(PlatformEvent event) = 0;

        /**
         * @brief Tells the platform a window is going away, before any of its proxies is.
         * @param window The window.
         */
        virtual void OnWindowDestroyed(WaylandWindow& window) = 0;

        /**
         * @brief Finds the output behind a proxy.
         * @param output The `wl_output` from a `wl_surface.enter`.
         * @return The output, or null for one already withdrawn.
         */
        [[nodiscard]] virtual const WaylandOutput* FindOutput(const wl_output* output) const = 0;

        /** @brief Gets how many windows the platform owns. @return The count. */
        [[nodiscard]] virtual std::size_t GetWindowCount() const = 0;

        /**
         * @brief Records which window a surface belongs to, for input routing.
         * @param surface The surface.
         * @param window The window's id, or 0 to forget the surface.
         */
        virtual void MapSurface(wl_surface* surface, WindowId window) = 0;

        /**
         * @brief Asks the compositor to focus a window, through xdg-activation where the last
         * input serial allows it (WAYLAND-0093).
         * @param window The window.
         */
        virtual void RequestActivation(WaylandWindow& window) = 0;

        /**
         * @brief Gets the most recent input serial and the seat it came from, for requests the
         * compositor accepts only in answer to input (move, resize, activation).
         * @param seat Receives the seat, or null.
         * @return The serial, or 0 when there has been no input.
         */
        [[nodiscard]] virtual std::uint32_t GetLatestInputSerial(wl_seat*& seat) const = 0;

        /** @brief Gets whether any seat has a keyboard. @return True when one does. */
        [[nodiscard]] virtual bool HasKeyboard() const = 0;

        /**
         * @brief Records which built-in frame a surface belongs to, for pointer routing.
         * @param surface The frame's surface.
         * @param frame The frame, or null to forget the surface.
         */
        virtual void MapFrameSurface(wl_surface* surface, WaylandFrame* frame) = 0;

        /**
         * @brief Shows a cursor shape over one of the frame's surfaces (a resize arrow on the border).
         * @param pointer The pointer.
         * @param serial Its enter serial.
         * @param cursor The shape.
         */
        virtual void SetFrameCursor(wl_pointer* pointer, std::uint32_t serial, SystemCursor cursor) = 0;
    };

    /**
     * @brief One Wayland top-level window (plans/plan_wayland.md WAYLAND-0033/0034).
     *
     * ### Lifetime of the objects
     *
     * The `wl_surface` lives exactly as long as this object: it is the native handle a renderer
     * holds, and a `wl_egl_window` or a `VkSurfaceKHR` made from it must stay valid across
     * `Hide`/`Show`. Its role -- `xdg_surface` and `xdg_toplevel` -- exists only while the window
     * is shown (D-8): xdg-shell has no "hide", and an unmapped `xdg_surface` returns to the
     * unconfigured state in which a renderer's next buffer would be a fatal protocol error, while
     * a surface with no role may have buffers committed harmlessly.
     *
     * ### The configure state machine (D-7)
     *
     * `NoRole` → (role created, initial commit without a buffer) → `AwaitingInitialConfigure` →
     * (first `xdg_surface.configure`, acknowledged) → `Configured`. `CreateWindow` and `Show`
     * return only once the window is `Configured`, so a renderer never attaches a buffer to an
     * unconfigured surface. Every `xdg_toplevel.configure` is pending until the
     * `xdg_surface.configure` that ends it, and is acknowledged before the commit that applies it.
     *
     * ### Size and scale (D-9, D-13)
     *
     * Logical units are surface coordinates. Where the compositor has a viewporter the surface's
     * extent is always its viewport destination -- the logical size -- whatever buffer a renderer
     * last committed; that keeps the window geometry exactly what a maximized or fullscreen
     * configure asked for even while an EGL buffer is one frame behind, which xdg-shell requires.
     */
    class WaylandWindow final : public IPlatformWindow
    {
    public:
        /**
         * @brief Creates the surface and its per-surface objects; the window is not shown (the
         * platform calls Show once the window is registered).
         * @param host The platform.
         * @param id The window's id.
         * @param description The creation parameters.
         * @throws PlatformException If the compositor refuses a surface.
         */
        WaylandWindow(WaylandWindowHost& host, WindowId id, const WindowDescription& description);

        /** @brief Destroys the role, the per-surface objects and the surface, in that order. */
        ~WaylandWindow() override;

        WaylandWindow(const WaylandWindow&) = delete;
        WaylandWindow& operator=(const WaylandWindow&) = delete;

        /** @brief Gets the id. @return The id. */
        [[nodiscard]] WindowId GetId() const override { return id_; }
        /** @brief Gets the native handle: `wl_display*` and `wl_surface*`. @return The handle. */
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;
        /** @brief Gets the title. @return The title. */
        [[nodiscard]] std::string GetTitle() const override { return title_; }
        /** @brief Sets the title. @param title The title. */
        void SetTitle(const std::string& title) override;
        /** @brief Gets the client area in logical units; x and y are 0 (D-10). @return The bounds. */
        [[nodiscard]] WindowBounds GetClientBounds() const override;
        /** @brief Gets the buffer size a renderer should use. @return The pixel size. */
        [[nodiscard]] WindowSize GetPixelSize() const override;
        /**
         * @brief Sets the content size: at once when floating, remembered when constrained (D-9).
         * @param width The logical width.
         * @param height The logical height.
         */
        void SetSize(int width, int height) override;
        /** @brief Gets the pixel density (D-13). @return Pixels per logical unit. */
        [[nodiscard]] float GetDisplayScale() const override;
        /** @brief Gets whether the user may resize. @return True when resizable. */
        [[nodiscard]] bool IsResizable() const override { return resizable_; }
        /** @brief Sets whether the user may resize (min = max = size when not). @param resizable Allowed. */
        void SetResizable(bool resizable) override;
        /** @brief Gets whether decorations are hidden. @return True when borderless. */
        [[nodiscard]] bool IsBorderless() const override { return borderless_; }
        /** @brief Hides or shows the decorations. @param borderless True to hide them. */
        void SetBorderless(bool borderless) override;
        /**
         * @brief Enters or leaves compositor fullscreen; exclusive is requested the same way and
         * reported as borderless (D-11).
         * @param mode The mode.
         */
        void SetFullscreenMode(WindowFullscreenMode mode) override;
        /** @brief Gets the fullscreen mode the compositor confirmed. @return The mode. */
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override;
        /** @brief Shows the window: creates its role and waits for the first configure (D-8). */
        void Show() override;
        /** @brief Hides the window: destroys its role and unmaps the surface (D-8). */
        void Hide() override;
        /** @brief Asks the compositor to minimize (D-12). */
        void Minimize() override;
        /** @brief Asks the compositor to maximize. */
        void Maximize() override;
        /** @brief Leaves maximized and fullscreen; cannot un-minimize (D-12). */
        void Restore() override;
        /** @brief Waits (bounded) until the compositor has answered every request made so far. */
        void Sync() override;
        /** @brief Gets whether a seat's keyboard focus is on this window. @return True if focused. */
        [[nodiscard]] bool HasFocus() const override;
        /** @brief Gets whether the compositor reports the window suspended (D-12). @return True if so. */
        [[nodiscard]] bool IsMinimized() const override { return suspended_; }
        /** @brief Gets the name of the output the window is on. @return The name, or empty. */
        [[nodiscard]] std::string GetDisplayName() const override;

        // --- the backend's own ---------------------------------------------------------------

        /** @brief Gets the surface. @return The `wl_surface`. */
        [[nodiscard]] wl_surface* GetSurface() const { return surface_; }

        /** @brief Gets the toplevel, or null while hidden. @return The `xdg_toplevel`. */
        [[nodiscard]] xdg_toplevel* GetToplevel() const { return toplevel_; }

        /** @brief Gets what the window was created to draw with. @return The intent. */
        [[nodiscard]] WindowRenderIntent GetRenderIntent() const { return renderIntent_; }

        /** @brief Gets the OpenGL framebuffer the description asked for. @return The description. */
        [[nodiscard]] const OpenGlFramebufferDescription& GetOpenGlFramebuffer() const { return openGlFramebuffer_; }

        /** @brief Gets whether the window is shown and configured. @return True when it may take buffers as a window. */
        [[nodiscard]] bool IsConfigured() const { return configureState_ == ConfigureState::Configured; }

        /** @brief Gets the output the window is on, or null. @return The output. */
        [[nodiscard]] const WaylandOutput* GetCurrentOutput() const;

        /** @brief Gets the scale decision in force. @return The decision. */
        [[nodiscard]] const ScaleDecision& GetScaleDecision() const { return scale_; }

        /**
         * @brief Records a seat's keyboard focus arriving or leaving; emits the focus event when
         * the window's focus changes.
         * @param focused True on `wl_keyboard.enter`, false on `leave`.
         */
        void OnKeyboardFocus(bool focused);

        /**
         * @brief Records that something draws into the surface (a swap, a present, a Vulkan
         * surface), after which state changes are committed without waiting for its next frame.
         */
        void OnContentCommitted();

        /**
         * @brief Installs the function told about every new pixel size before the next frame is
         * drawn -- the GL service's `wl_egl_window_resize`.
         * @param listener The function, or empty to remove it.
         */
        void SetPixelSizeListener(std::function<void(int width, int height)> listener);

        /**
         * @brief Destroys every proxy while the display still exists and detaches from the platform:
         * the platform is going away before this window, and the wrapper stays behind inert.
         */
        void Abandon();

        /**
         * @brief Commits the surface state (viewport, scale, geometry) without a new buffer; only
         * once configured and drawn into.
         */
        void CommitState();

        /**
         * @brief Asks the compositor to start an interactive move (the built-in title bar's drag).
         */
        void BeginMove();

        /**
         * @brief Asks the compositor to start an interactive resize.
         * @param edges The `xdg_toplevel_resize_edge`.
         */
        void BeginResize(std::uint32_t edges);

        /** @brief Toggles maximized (the built-in title bar's double click and button). */
        void ToggleMaximized();

        /** @brief Emits CloseRequested, and QuitEvent for the last window (the close button). */
        void RequestClose();

        /**
         * @brief Forgets an output the compositor withdrew, before its proxy is destroyed.
         * @param output The output's proxy.
         */
        void ForgetOutput(const wl_output* output);

        /** @brief Re-reads the outputs the window is on (an output's scale changed). */
        void RefreshOutputs();

        /**
         * @brief Asks the compositor for its window menu (the built-in title bar's right click).
         * @param x Surface-local x of the content surface.
         * @param y Surface-local y of the content surface.
         */
        void ShowWindowMenu(int x, int y);

        /** @brief Gets whether the compositor reports the window maximized. @return True if so. */
        [[nodiscard]] bool IsMaximized() const { return maximized_; }

        /**
         * @brief Gets which window-management actions the compositor offers (xdg_toplevel v5),
         * as bits `1 << xdg_toplevel_wm_capabilities`.
         * @return The bits; meaningful only when HasWmCapabilities.
         */
        [[nodiscard]] std::uint32_t GetWmCapabilities() const { return wmCapabilities_; }

        /** @brief Gets whether the compositor said which actions it offers. @return True if it did. */
        [[nodiscard]] bool HasWmCapabilities() const { return hasWmCapabilities_; }

    private:
        enum class ConfigureState
        {
            NoRole,
            AwaitingInitialConfigure,
            Configured
        };

        static const wl_surface_listener kSurfaceListener;
        static const xdg_surface_listener kXdgSurfaceListener;
        static const xdg_toplevel_listener kToplevelListener;

        void CreateRole();
        void DestroyRole();
        void ReleaseNativeObjects();
        void ApplyConfigure(std::uint32_t serial);
        void ApplySizeLimits();
        void UpdateScale();
        void ApplySurfaceGeometry(bool notifyListener);
        void UpdateCurrentOutput();
        void Post(WindowEventKind kind, int data1 = 0, int data2 = 0);
        [[nodiscard]] int FrameHeight() const;

        WaylandWindowHost* host_ = nullptr;
        WindowId id_ = 0;
        wl_display* display_ = nullptr;
        wl_surface* surface_ = nullptr;
        xdg_surface* xdgSurface_ = nullptr;
        xdg_toplevel* toplevel_ = nullptr;
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
        wp_viewport* viewport_ = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
        wp_fractional_scale_v1* fractionalScale_ = nullptr;
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
        zxdg_toplevel_decoration_v1* decoration_ = nullptr;
        bool serverDecorated_ = false;
#endif
        std::unique_ptr<WaylandFrame> frame_;

        ConfigureState configureState_ = ConfigureState::NoRole;
        // The toplevel state of the configure sequence in progress, applied by its xdg_surface.configure.
        struct PendingToplevel
        {
            int width = 0;
            int height = 0;
            ToplevelStates states;
        } pendingToplevel_;

        std::string title_;
        std::string appId_;
        WindowRenderIntent renderIntent_ = WindowRenderIntent::None;
        OpenGlFramebufferDescription openGlFramebuffer_;
        LogicalSize size_;
        LogicalSize floatingSize_;
        LogicalSize minimum_;
        LogicalSize maximum_;
        bool resizable_ = true;
        bool borderless_ = false;
        bool highDpi_ = false;
        bool visible_ = false;
        bool hasContent_ = false;
        WindowFullscreenMode requestedFullscreen_ = WindowFullscreenMode::Windowed;
        bool maximized_ = false;
        bool fullscreen_ = false;
        bool tiled_ = false;
        bool activated_ = false;
        bool suspended_ = false;
        bool maximizeRequested_ = false;
        int keyboardFocusCount_ = 0;

        bool preferredBufferScale_ = false;
        LogicalSize bounds_;
        std::uint32_t wmCapabilities_ = 0;
        bool hasWmCapabilities_ = false;
        std::uint64_t configureCount_ = 0;
        bool expectConfigure_ = false;

        ScaleInputs scaleInputs_;
        ScaleDecision scale_;
        WindowSize pixelSize_;
        std::vector<const wl_output*> enteredOutputs_;
        const WaylandOutput* currentOutput_ = nullptr;
        std::function<void(int, int)> pixelSizeListener_;
    };

} // namespace CNA::Platform::Wayland
