// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"

#include "WaylandProtocols.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Wayland {

    class WaylandConnection;
    class WaylandWindow;
    class WaylandShmBuffer;

    /**
     * @brief Waits, bounded, for a `wl_surface.frame` callback on a queue of its own
     * (plans/plan_wayland.md D-23, D-25, WAYLAND-0083).
     *
     * Frame callbacks are how a Wayland client learns the compositor is ready for the next frame,
     * and waiting on one is vsync. Mesa's EGL waits for them unboundedly, which hangs a game whose
     * window is hidden or occluded inside `eglSwapBuffers`; this waits at most a deadline, on a
     * private event queue, so the application's own events are never dispatched from inside a
     * swap or a present.
     */
    class WaylandFramePacer
    {
    public:
        /**
         * @brief Creates the pacer for one surface.
         * @param display The display.
         * @param surface The surface whose frames are paced.
         */
        WaylandFramePacer(wl_display* display, wl_surface* surface);

        /** @brief Destroys the callback, the surface wrapper and the queue. */
        ~WaylandFramePacer();

        WaylandFramePacer(const WaylandFramePacer&) = delete;
        WaylandFramePacer& operator=(const WaylandFramePacer&) = delete;

        /**
         * @brief Waits for the frame requested last time, at most `timeout`.
         * @param timeout The longest wait.
         * @return True when the compositor signalled the frame (or none was pending).
         */
        bool Wait(std::chrono::milliseconds timeout);

        /** @brief Requests a callback for the next commit; call just before it. */
        void Request();

        /** @brief Drops a pending request (vsync turned off). */
        void Cancel();

    private:
        wl_display* display_ = nullptr;
        wl_event_queue* queue_ = nullptr;
        wl_surface* wrapper_ = nullptr;
        wl_callback* callback_ = nullptr;
    };

    /**
     * @brief OpenGL and OpenGL ES contexts through EGL on the Wayland platform (WAYLAND-0080).
     *
     * libEGL and libwayland-egl are opened at run time (D-1): a Wayland build that never draws
     * with GL needs no GL implementation installed. The EGL display is initialised on first use --
     * that loads the GPU driver, which a process that never makes a context should not pay for --
     * and is one per platform instance, made from the platform's own `wl_display`.
     *
     * ### One framebuffer per window
     *
     * A window's EGL config is chosen by its first context, from the window's
     * `openGlFramebuffer` (depth 24 and stencil 8 by default, as the X11 backend's visual is) and
     * the context's colour request; later contexts for the window use the same config, so any of
     * them can be made current on the window's one `EGLSurface`. The surface is presented opaque
     * (`EGL_EXT_present_opaque`, and an opaque region): an XNA back buffer has an alpha channel,
     * and on Wayland an alpha channel in a window's buffer is transparency.
     *
     * ### Vsync (D-23)
     *
     * EGL's own swap interval is 0 on every surface, and an interval of 1 is CNA's: before each
     * swap the previous frame's callback is awaited, at most 100 ms, so a hidden window keeps
     * running at ten frames a second instead of hanging.
     */
    class WaylandGlContext final : public IPlatformGlContext
    {
    public:
        /**
         * @brief Creates the service; loads nothing yet.
         * @param connection The platform's connection.
         */
        explicit WaylandGlContext(WaylandConnection& connection);

        /** @brief Destroys every context and surface, then terminates the EGL display. */
        ~WaylandGlContext() override;

        WaylandGlContext(const WaylandGlContext&) = delete;
        WaylandGlContext& operator=(const WaylandGlContext&) = delete;

        /**
         * @brief Gets whether EGL for Wayland works here: both libraries load, a client extension
         * for the Wayland platform exists, and an EGL display on this connection initialised.
         *
         * Answered in the constructor, because `openGlContext` is a promise: a machine where
         * libEGL loads but no display initialises must report the capability false rather than
         * accept a GL window and fail at the context (WAYLAND-0129).
         *
         * @return True when a context can be created.
         */
        [[nodiscard]] bool IsAvailable() const { return available_; }

        /**
         * @brief Makes a window known to the service, or forgets one (its EGL surface and
         * `wl_egl_window` are destroyed now, before the window's surface is).
         * @param id The window's id.
         * @param window The window, or null to forget it.
         */
        void RegisterWindow(WindowId id, WaylandWindow* window);

        /**
         * @brief Creates a context for a window.
         * @param window The window's id.
         * @param description The requested attributes.
         * @return The context.
         * @throws PlatformException If EGL refuses the display, config or context.
         */
        [[nodiscard]] GlContextHandle CreateContext(WindowId window, const GlContextDescription& description) override;
        /** @brief Destroys a context, unbinding it first if current. @param context The context. */
        void DestroyContext(GlContextHandle context) override;
        /**
         * @brief Makes a context current on a window's surface, or unbinds.
         * @param window The window's id.
         * @param context The context, or null to unbind.
         * @throws PlatformException If EGL refuses the binding.
         */
        void MakeCurrent(WindowId window, GlContextHandle context) override;
        /** @brief Gets the current binding. @return The window and context, or empty. */
        [[nodiscard]] GlContextBinding GetCurrentBinding() const override;
        /** @brief Presents a window's back buffer, paced by its frame callback. @param window The window's id. */
        void SwapBuffers(WindowId window) override;
        /**
         * @brief Sets the swap interval of the current window: 0 none, 1 (or adaptive -1) the
         * compositor's frame pacing.
         * @param interval The interval.
         * @return True when a window is current.
         */
        bool SetSwapInterval(int interval) override;
        /** @brief Resolves an entry point through EGL. @param name The name. @return The pointer or null. */
        [[nodiscard]] void* GetProcAddress(const std::string& name) const override;
        /** @brief Gets the resolver as a C callback. @return The callback. */
        [[nodiscard]] GlProcAddressLoader GetProcAddressLoader() const override;
        /** @brief Gets what a context was granted. @param context The context. @return The attributes. */
        [[nodiscard]] GlContextDescription GetContextAttributes(GlContextHandle context) const override;

        /**
         * @brief Gets the EGL display, initialising it (tests).
         * @return The `EGLDisplay`, or null.
         */
        [[nodiscard]] void* GetEglDisplay();

    private:
        struct WindowRecord;
        struct ContextRecord;

        [[nodiscard]] bool EnsureDisplay();
        [[nodiscard]] WindowRecord* Find(WindowId id);
        void DestroySurface(WindowRecord& record);
        void ChooseConfig(WindowRecord& record, const GlContextDescription& description, bool es,
                          const char* operation);
        void EnsureSurface(WindowRecord& record, const char* operation);

        WaylandConnection& connection_;
        bool available_ = false;
        bool displayTried_ = false;
        void* eglDisplay_ = nullptr;
        bool presentOpaque_ = false;
        std::map<WindowId, std::unique_ptr<WindowRecord>> windows_;
        std::map<GlContextHandle, ContextRecord> contexts_;
        WindowId currentWindow_ = 0;
        GlContextHandle currentContext_ = nullptr;
    };

    /**
     * @brief Vulkan surfaces for Wayland windows through `VK_KHR_wayland_surface` (WAYLAND-0081).
     *
     * `vkCreateWaylandSurfaceKHR` is resolved through the caller's `vkGetInstanceProcAddr` (D-24):
     * the platform links no Vulkan loader.
     */
    class WaylandVulkanSurface final : public IPlatformVulkanSurface
    {
    public:
        /**
         * @brief Creates the service.
         * @param connection The platform's connection.
         */
        explicit WaylandVulkanSurface(WaylandConnection& connection) : connection_(connection) {}

        /**
         * @brief Makes a window known to the service, or forgets one.
         * @param id The window's id.
         * @param window The window, or null.
         */
        void RegisterWindow(WindowId id, WaylandWindow* window);

        /** @brief Gets the instance extensions. @return `VK_KHR_surface`, `VK_KHR_wayland_surface`. */
        [[nodiscard]] std::vector<std::string> GetInstanceExtensions() const override;
        /**
         * @brief Creates a surface for a window.
         * @param instance The `VkInstance`.
         * @param window The window's id.
         * @return The `VkSurfaceKHR`.
         * @throws PlatformException Without a loader, the extension, or on failure.
         */
        [[nodiscard]] VulkanSurfaceHandle CreateSurface(VulkanInstanceHandle instance, WindowId window) override;
        /** @brief Destroys a surface. @param instance The instance. @param surface The surface. */
        void DestroySurface(VulkanInstanceHandle instance, VulkanSurfaceHandle surface) override;

    private:
        WaylandConnection& connection_;
        std::map<WindowId, WaylandWindow*> windows_;
    };

    /**
     * @brief Presents CPU frames through `wl_shm` (WAYLAND-0082, D-25).
     *
     * XRGB8888 buffers -- every compositor must accept that format, and a frame with no alpha
     * channel cannot make a window transparent -- in a ring of at most three, reused only after
     * the compositor releases them. The frame is fitted into the window's pixel size with the
     * shared scaling (Common::ScaleSurfaceFrame), the letterbox bars cleared to black, and the
     * whole buffer damaged. Vsync is the frame callback, awaited at most 100 ms.
     */
    class WaylandSurfacePresenter final : public IPlatformSurfacePresenter
    {
    public:
        /**
         * @brief Creates a presenter for a window.
         * @param connection The platform's connection.
         * @param window The window; must outlive the presenter.
         */
        WaylandSurfacePresenter(WaylandConnection& connection, WaylandWindow& window);

        /** @brief Destroys the buffers. */
        ~WaylandSurfacePresenter() override;

        WaylandSurfacePresenter(const WaylandSurfacePresenter&) = delete;
        WaylandSurfacePresenter& operator=(const WaylandSurfacePresenter&) = delete;

        /** @brief Sets scaling. @param mode The mode. @param filter The filter. */
        void SetScaleMode(PresentScaleMode mode, PresentFilter filter) override;
        /** @brief Paces presents by the frame callback. @param enabled Wait for it. @return True. */
        bool SetVSync(bool enabled) override;
        /**
         * @brief Presents a frame.
         * @param frame The pixels.
         * @throws PlatformException For a malformed frame or without shared memory.
         */
        void Present(const SurfaceFrame& frame) override;
        /** @brief Gets the target size. @param width Receives it. @param height Receives it. */
        void GetTargetSize(int& width, int& height) const override;

        /** @brief Gets how many buffers exist (tests: the ring stays bounded). @return The count. */
        [[nodiscard]] std::size_t GetBufferCount() const { return buffers_.size() + retired_.size(); }

    private:
        WaylandShmBuffer* AcquireBuffer(int width, int height);

        WaylandConnection& connection_;
        WaylandWindow& window_;
        std::unique_ptr<WaylandFramePacer> pacer_;
        std::vector<std::unique_ptr<WaylandShmBuffer>> buffers_;
        std::vector<std::unique_ptr<WaylandShmBuffer>> retired_;
        std::vector<int> columns_;
        PresentScaleMode scaleMode_ = PresentScaleMode::Letterbox;
        PresentFilter filter_ = PresentFilter::Nearest;
        bool vsync_ = true;
    };

} // namespace CNA::Platform::Wayland
