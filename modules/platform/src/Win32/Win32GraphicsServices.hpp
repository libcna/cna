// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"

#include "Win32Common.hpp"
#include "Win32InputServices.hpp"

#include <map>
#include <vector>

namespace CNA::Platform::Win32 {

    class Win32Window;

    /**
     * @brief OpenGL contexts created with WGL.
     *
     * The window's device context is where the pixel format lives, and a pixel format can be set
     * on a device context exactly **once** -- which is why `WindowRenderIntent::OpenGl` has to be
     * right at window creation and why the window class carries `CS_OWNDC`: with a shared DC the
     * format would be lost the next time the window repainted.
     *
     * `wglCreateContextAttribsARB` is used when the driver exports it, because a plain
     * `wglCreateContext` produces a legacy context on which a core profile cannot be requested.
     * Resolving it needs a context to already be current, so a throwaway legacy context is created
     * first and destroyed -- the standard WGL bootstrap, and the reason this is more than three
     * calls.
     */
    class Win32GlContext final : public IPlatformGlContext
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param access The owning platform.
         */
        explicit Win32GlContext(Win32PlatformAccess& access);

        /** @brief Destroys the service and every context it still owns. */
        ~Win32GlContext() override;

        /** @brief Creates a context for a window. @param window The window. @param description The attributes. @return The context. */
        [[nodiscard]] GlContextHandle CreateContext(WindowId window,
                                                    const GlContextDescription& description) override;

        /** @brief Destroys a context. @param context The context, or null. */
        void DestroyContext(GlContextHandle context) override;

        /** @brief Makes a context current. @param window The window. @param context The context, or null to unbind. */
        void MakeCurrent(WindowId window, GlContextHandle context) override;

        /** @brief Gets the calling thread's binding. @return The window/context pair. */
        [[nodiscard]] GlContextBinding GetCurrentBinding() const override;

        /** @brief Presents a window's back buffer. @param window The window. */
        void SwapBuffers(WindowId window) override;

        /** @brief Sets the swap interval. @param interval 0, 1 or -1. @return True when applied. */
        bool SetSwapInterval(int interval) override;

        /** @brief Resolves an entry point. @param name The entry point. @return The pointer, or null. */
        [[nodiscard]] void* GetProcAddress(const std::string& name) const override;

        /** @brief Gets a C-compatible resolver. @return A loader valid for this service's lifetime. */
        [[nodiscard]] GlProcAddressLoader GetProcAddressLoader() const override;

        /** @brief Gets a context's granted attributes. @param context The context. @return Its attributes. */
        [[nodiscard]] GlContextDescription GetContextAttributes(GlContextHandle context) const override;

    private:
        struct ContextRecord
        {
            HGLRC context = nullptr;
            WindowId window = 0;
            GlContextDescription granted;
        };

        [[nodiscard]] HDC DeviceContextFor(WindowId window) const;

        Win32PlatformAccess* access_;
        std::vector<ContextRecord> contexts_;
    };

    /**
     * @brief Vulkan surfaces created with `vkCreateWin32SurfaceKHR`.
     *
     * Every Vulkan entry point is resolved at run time from `vulkan-1.dll`. Linking the loader
     * would make CNA refuse to start on a machine with no Vulkan driver, for a renderer the build
     * may not even have selected -- and the capability is reported false when the load fails, so
     * a caller learns before it asks.
     */
    class Win32VulkanSurface final : public IPlatformVulkanSurface
    {
    public:
        /**
         * @brief Creates the service.
         *
         * @param access The owning platform.
         */
        explicit Win32VulkanSurface(Win32PlatformAccess& access);

        /** @brief Releases the loader reference. */
        ~Win32VulkanSurface() override;

        /**
         * @brief Gets whether a Vulkan loader with a Win32 surface extension is present.
         *
         * @return True when surfaces can actually be created.
         */
        [[nodiscard]] bool IsAvailable() const;

        /** @brief Gets the required instance extensions. @return `VK_KHR_surface` and the Win32 one. */
        [[nodiscard]] std::vector<std::string> GetInstanceExtensions() const override;

        /** @brief Creates a surface. @param instance The `VkInstance`. @param window The window. @return The surface. */
        [[nodiscard]] VulkanSurfaceHandle CreateSurface(VulkanInstanceHandle instance,
                                                        WindowId window) override;

        /** @brief Destroys a surface. @param instance The instance. @param surface The surface, or zero. */
        void DestroySurface(VulkanInstanceHandle instance, VulkanSurfaceHandle surface) override;

    private:
        Win32PlatformAccess* access_;
        HMODULE loader_ = nullptr;
        void* getInstanceProcAddr_ = nullptr;
    };

    /**
     * @brief Presents finished CPU pixels to a window with `StretchDIBits`.
     *
     * The GDI path is the Win32 answer to "put this image on the screen", and it is what lets a
     * CPU rasteriser (`SOFTWARE`, `BLEND2D`) run on this platform without a GPU renderer. It is
     * not a drawing API: one finished frame arrives per present, exactly as the contract states.
     *
     * The presenter borrows its window and must not outlive it. That is the contract's own
     * teardown order -- "destroy resources from the consumer inward: renderer resources, graphics
     * context/surface, window" -- rather than a limitation of this backend, and it is the same
     * relationship every other implementation's presenter has with its window.
     */
    class Win32SurfacePresenter final : public IPlatformSurfacePresenter
    {
    public:
        /**
         * @brief Creates a presenter for a window.
         *
         * @param window The window to present to.
         */
        explicit Win32SurfacePresenter(Win32Window& window);

        /** @brief Releases the presenter's GDI resources. */
        ~Win32SurfacePresenter() override;

        /** @brief Sets how a frame is fitted. @param mode The scaling mode. @param filter The filter. */
        void SetScaleMode(PresentScaleMode mode, PresentFilter filter) override;

        /** @brief Sets vertical-blank synchronisation. @param enabled True to synchronise. @return True when applied. */
        bool SetVSync(bool enabled) override;

        /** @brief Presents one finished frame. @param frame The pixels to present. */
        void Present(const SurfaceFrame& frame) override;

        /** @brief Gets the current target size. @param width Receives the width. @param height Receives the height. */
        void GetTargetSize(int& width, int& height) const override;

    private:
        Win32Window* window_;
        PresentScaleMode scaleMode_ = PresentScaleMode::Stretch;
        PresentFilter filter_ = PresentFilter::Nearest;
        bool vsync_ = false;
        /// Reused across presents so a steady-state frame performs no allocation.
        std::vector<std::uint32_t> converted_;
    };

} // namespace CNA::Platform::Win32
