// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"
#include "X11Headers.hpp"

#include <map>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;
    class X11Window;

    /**
     * @brief A GLX framebuffer configuration chosen for a window, before the window exists.
     *
     * ### Why this is a creation-time decision
     *
     * A GLX context can only be made current on a window whose `Visual` is compatible with the
     * context's `FBConfig`. An X window's visual is fixed at `XCreateWindow` and cannot be
     * changed afterwards, so the framebuffer format has to be picked *before* the window is
     * created — which is exactly why `WindowDescription` carries `renderIntent` and
     * `openGlFramebuffer` rather than offering a post-creation setter. The contract already had
     * the right shape; this is what consumes it.
     */
    struct X11GlVisual
    {
        /** @brief The chosen visual, or null when no GLX config matched. */
        Visual* visual = nullptr;
        /** @brief The visual's depth in bits. */
        int depth = 0;
        /** @brief The `GLXFBConfig` as an opaque pointer, or null. */
        void* fbConfig = nullptr;
    };

    /**
     * @brief OpenGL contexts on X11, through GLX.
     *
     * GLX rather than EGL. Three reasons, in order of weight: the window this must attach to is
     * an Xlib window with an Xlib `Visual`, which is GLX's native currency and EGL's awkward one;
     * GLX is part of every X server's own GL stack, so it needs no second vendor library; and
     * `glXCreateContextAttribsARB` gives the same version and profile control EGL does. An EGL
     * path would be the right choice for a future Wayland backend, which is precisely the
     * platform-specific decision this class is allowed to make and a shared "Linux" layer would
     * have had to compromise on.
     */
    class X11GlContext final : public IPlatformGlContext
    {
    public:
        /**
         * @brief Builds the GL service for one connection.
         *
         * @param connection The connection contexts are created on.
         */
        explicit X11GlContext(X11Connection& connection);

        /** @brief Destroys every context still outstanding. */
        ~X11GlContext() override;

        X11GlContext(const X11GlContext&) = delete;
        X11GlContext& operator=(const X11GlContext&) = delete;

        /**
         * @brief Reports whether GLX is usable on this connection.
         *
         * @return True when the server advertises GLX 1.3 or newer.
         */
        [[nodiscard]] bool IsAvailable() const { return available_; }

        /**
         * @brief Chooses a visual for a window that will host a GL context.
         *
         * @param depthBits Requested depth precision; zero for the platform default.
         * @param stencilBits Requested stencil precision; zero for the platform default.
         * @param doubleBuffered Whether the visual must support double buffering.
         * @param samples Requested MSAA sample count; zero or one disables it.
         * @return The chosen visual; its `visual` member is null when nothing matched.
         */
        [[nodiscard]] X11GlVisual ChooseVisual(int depthBits, int stencilBits, bool doubleBuffered,
                                               int samples) const;

        /**
         * @brief Registers a window so contexts can be bound to it by id.
         *
         * @param id The CNA window id.
         * @param window The window, or null to unregister.
         */
        void RegisterWindow(WindowId id, X11Window* window);

        /**
         * @brief Creates a context for a window.
         * @param window The window the context must be compatible with.
         * @param description The requested attributes.
         * @return A non-null context handle.
         * @throws PlatformException If GLX is unavailable or the driver refused the request.
         */
        [[nodiscard]] GlContextHandle CreateContext(WindowId window,
                                                    const GlContextDescription& description) override;

        /** @brief Destroys a context. @param context The context to destroy. */
        void DestroyContext(GlContextHandle context) override;

        /**
         * @brief Makes a context current on a window.
         * @param window The window to bind to; ignored when @p context is null.
         * @param context The context, or null to unbind.
         */
        void MakeCurrent(WindowId window, GlContextHandle context) override;

        /** @brief Gets the calling thread's current binding. @return The binding. */
        [[nodiscard]] GlContextBinding GetCurrentBinding() const override;

        /** @brief Presents the back buffer. @param window The window to swap. */
        void SwapBuffers(WindowId window) override;

        /**
         * @brief Sets the swap interval.
         * @param interval 0 none, 1 vsync, -1 adaptive.
         * @return True when a GLX swap-control extension applied it.
         */
        bool SetSwapInterval(int interval) override;

        /**
         * @brief Resolves a GL entry point.
         * @param name The entry point.
         * @return The pointer, or null.
         */
        [[nodiscard]] void* GetProcAddress(const std::string& name) const override;

        /** @brief Gets the loader callback. @return The resolver. */
        [[nodiscard]] GlProcAddressLoader GetProcAddressLoader() const override;

        /**
         * @brief Gets the attributes the driver actually granted.
         * @param context The context to describe.
         * @return The granted attributes.
         */
        [[nodiscard]] GlContextDescription GetContextAttributes(
            GlContextHandle context) const override;

    private:
        struct ContextRecord
        {
            void* glxContext = nullptr;
            GlContextDescription granted;
        };

        [[nodiscard]] X11Window* FindWindow(WindowId id) const;

        X11Connection& connection_;
        bool available_ = false;
        std::map<GlContextHandle, ContextRecord> contexts_;
        std::map<WindowId, X11Window*> windows_;
        WindowId currentWindow_ = 0;
    };

    /**
     * @brief Vulkan surfaces on X11, through `VK_KHR_xlib_surface`.
     *
     * ### The platform links no Vulkan loader
     *
     * `vkCreateXlibSurfaceKHR` is resolved through the `vkGetInstanceProcAddr` belonging to the
     * instance the caller already created. That keeps the dependency to headers only: a machine
     * with no Vulkan driver still builds and runs everything else, and the renderer's own loader
     * stays the single one in the process. Linking `libvulkan` here would add a hard runtime
     * dependency to every X11 build for the benefit of the builds that use Vulkan.
     */
    class X11VulkanSurface final : public IPlatformVulkanSurface
    {
    public:
        /**
         * @brief Builds the Vulkan surface service for one connection.
         *
         * @param connection The connection surfaces are created against.
         */
        explicit X11VulkanSurface(X11Connection& connection);

        /**
         * @brief Gets the instance extensions a caller must enable.
         *
         * @return `VK_KHR_surface` and `VK_KHR_xlib_surface`.
         */
        [[nodiscard]] std::vector<std::string> GetInstanceExtensions() const override;

        /**
         * @brief Creates a surface for a window.
         * @param instance The `VkInstance`.
         * @param window The window to present to.
         * @return A non-zero surface handle.
         * @throws PlatformException If the entry point could not be resolved or creation failed.
         */
        [[nodiscard]] VulkanSurfaceHandle CreateSurface(VulkanInstanceHandle instance,
                                                        WindowId window) override;

        /**
         * @brief Destroys a surface.
         * @param instance The instance it was created against.
         * @param surface The surface to destroy.
         */
        void DestroySurface(VulkanInstanceHandle instance, VulkanSurfaceHandle surface) override;

        /**
         * @brief Registers a window so surfaces can be created by id.
         *
         * @param id The CNA window id.
         * @param window The window, or null to unregister.
         */
        void RegisterWindow(WindowId id, X11Window* window);

    private:
        X11Connection& connection_;
        std::map<WindowId, X11Window*> windows_;
    };

    /**
     * @brief Presents finished CPU pixels with `XPutImage`.
     *
     * ### Not a drawing API
     *
     * One finished RGBA8 frame per `Present`, scaled into the window. The conversion from the
     * contract's RGBA8 to whatever the window's visual actually wants is the real work here:
     * a 24-bit TrueColor visual on a little-endian host wants BGRX, a 16-bit one wants 5-6-5,
     * and a big-endian server wants the bytes the other way round. Every one of those is read
     * from the visual's own masks rather than assumed.
     *
     * MIT-SHM is used when the extension is available and the server is local, because it turns a
     * per-frame copy of the whole image through the socket into a copy into shared memory. It is
     * an optimisation and nothing depends on it: over a network connection, or on a server built
     * without it, the plain path runs.
     */
    class X11SurfacePresenter final : public IPlatformSurfacePresenter
    {
    public:
        /**
         * @brief Creates a presenter for a window.
         *
         * @param window The window to present to.
         */
        explicit X11SurfacePresenter(X11Window& window);

        /** @brief Releases the image, any shared-memory segment and the graphics context. */
        ~X11SurfacePresenter() override;

        X11SurfacePresenter(const X11SurfacePresenter&) = delete;
        X11SurfacePresenter& operator=(const X11SurfacePresenter&) = delete;

        /**
         * @brief Sets how a frame is fitted to the window.
         * @param mode The scaling mode.
         * @param filter The filter used when scaling.
         */
        void SetScaleMode(PresentScaleMode mode, PresentFilter filter) override;

        /**
         * @brief Sets vertical-blank synchronisation.
         * @param enabled True to synchronise.
         * @return False; the X core protocol has no vsync for `XPutImage`.
         */
        bool SetVSync(bool enabled) override;

        /**
         * @brief Presents one frame.
         * @param frame The pixels to present.
         * @throws PlatformException If the frame is malformed.
         */
        void Present(const SurfaceFrame& frame) override;

        /**
         * @brief Gets the current target size in physical pixels.
         * @param width Receives the width.
         * @param height Receives the height.
         */
        void GetTargetSize(int& width, int& height) const override;

    private:
        void EnsureImage(int width, int height);
        void ReleaseImage();

        X11Window& window_;
        GC graphicsContext_ = nullptr;
        XImage* image_ = nullptr;
        std::vector<unsigned char> pixels_;
        int imageWidth_ = 0;
        int imageHeight_ = 0;
        PresentScaleMode scaleMode_ = PresentScaleMode::Letterbox;
        PresentFilter filter_ = PresentFilter::Nearest;
        bool usesSharedMemory_ = false;
#if defined(CNA_X11_HAVE_XSHM)
        XShmSegmentInfo sharedMemory_{};
#endif
    };

} // namespace CNA::Platform::X11
