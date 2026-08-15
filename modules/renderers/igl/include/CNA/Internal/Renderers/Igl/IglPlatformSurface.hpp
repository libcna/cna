// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"
#include "CNA/Internal/Renderers/Igl/IglRendererSelection.hpp"

#include <igl/Texture.h>

#include <memory>

namespace igl
{
    class ICommandQueue;
    class IDevice;
    class IFramebuffer;
}

namespace CNA::Internal::Renderers::Igl
{
    /**
     * @brief Brings up an `igl::IDevice` on a CNA platform window and owns its swap surface. CNAEXT.
     *
     * Every backend-specific include IGL needs -- X11/GLX for the OpenGL backend, volk/Vulkan for
     * the Vulkan one -- is confined to this class's translation unit, so the rest of the renderer
     * compiles against nothing but IGL's own backend-neutral interfaces.
     *
     * The presentation surface is described only by the platform-neutral @ref RendererSurfaceInfo
     * snapshot CNA hands every renderer; the OpenGL backend additionally borrows the GL context
     * CNA's own `IPlatformGlContext` service creates for that window, rather than opening a second
     * connection to the window system of its own.
     */
    class IglPlatformSurface final
    {
    public:
        /**
         * @brief Creates the IGL device, its command queue and the swap-surface textures.
         *
         * @param args    Creation arguments handed down by `GraphicsDevice`.
         * @param backend Backend resolved for this process by `Detail::ResolveRendererBackend()`.
         * @throws CNA::Platform::PlatformNotSupportedException If the OpenGL backend was selected
         *         but the platform publishes no GL context service.
         * @throws std::runtime_error If the native window cannot be expressed for the selected
         *         backend, or IGL fails to create a device.
         */
        IglPlatformSurface(const GraphicsRendererCreateArgs& args,
                           Detail::RendererBackend backend);

        /** @brief Destroys the swap surface, the device and, on OpenGL, releases the GL context. */
        ~IglPlatformSurface();

        IglPlatformSurface(const IglPlatformSurface&) = delete;
        IglPlatformSurface& operator=(const IglPlatformSurface&) = delete;

        /** @brief Returns the backend this surface actually brought up. */
        [[nodiscard]] Detail::RendererBackend GetBackend() const noexcept { return backend_; }

        /** @brief Returns the live IGL device. Never null for a constructed surface. */
        [[nodiscard]] igl::IDevice& GetDevice() const noexcept { return *device_; }

        /** @brief Returns the command queue every frame is submitted through. */
        [[nodiscard]] igl::ICommandQueue& GetCommandQueue() const noexcept { return *commandQueue_; }

        /**
         * @brief Acquires this frame's swap-chain colour and depth textures.
         *
         * Called once per frame before the first render pass. On Vulkan this advances the swap
         * chain image; on OpenGL it resolves the default framebuffer's stand-in textures, which are
         * cached until the drawable size changes.
         *
         * @return The colour texture and, when a depth/stencil surface exists, the depth texture.
         * @throws std::runtime_error If the backend could not produce a drawable this frame.
         */
        [[nodiscard]] igl::SurfaceTextures AcquireSurfaceTextures();

        /**
         * @brief Returns the framebuffer wrapping this frame's swap-chain textures.
         *
         * The framebuffer object itself is created once and re-pointed at each frame's drawable
         * with `IFramebuffer::updateDrawable`, matching IGL's own shell: recreating it per frame
         * would discard the backend's cached attachment state for no benefit.
         *
         * @return The back-buffer framebuffer; never null.
         * @throws std::runtime_error If the framebuffer could not be created.
         */
        [[nodiscard]] const std::shared_ptr<igl::IFramebuffer>& AcquireBackBufferFramebuffer();

        /** @brief Presents the frame that was just submitted, if the backend needs a separate step. */
        void Present();

        /**
         * @brief Refreshes size/density and resizes the swap chain when the drawable changed.
         *
         * @param surface New platform surface snapshot.
         */
        void OnSurfaceChanged(const RendererSurfaceInfo& surface);

        /**
         * @brief Applies a swap interval where the backend supports changing it at runtime.
         *
         * @param interval 0 for immediate, 1 for vsync, 2 for half refresh rate.
         * @return True when the interval was applied.
         */
        bool SetSwapInterval(int interval);

        /** @brief Returns the current physical drawable size in pixels. */
        [[nodiscard]] CNA::Platform::WindowSize GetDrawableSize() const noexcept
        {
            return surface_.GetDrawableSize();
        }

        /** @brief Returns the stable platform window identity. */
        [[nodiscard]] CNA::Platform::WindowId GetWindowId() const noexcept
        {
            return surface_.GetWindowId();
        }

        /** @brief Converts one logical window coordinate to physical drawable pixels. */
        [[nodiscard]] float WindowToDrawable(const float value) const noexcept
        {
            return surface_.WindowToDrawable(value);
        }

        /** @brief Converts one physical drawable coordinate to logical window units. */
        [[nodiscard]] float DrawableToWindow(const float value) const noexcept
        {
            return surface_.DrawableToWindow(value);
        }

        /**
         * @brief Returns the real, device-granted MSAA sample count of the presented surface.
         *
         * On the OpenGL backend this is the sample count the platform's GL visual was actually
         * created with, which is what the driver resolves on swap. On Vulkan it is 1: IGL's swap
         * chain images are single-sample and this renderer does not fabricate a count it does not
         * have.
         *
         * @return Applied sample count; 1 when the surface is not multisampled.
         */
        [[nodiscard]] int GetSurfaceSampleCount() const noexcept { return surfaceSampleCount_; }

        /** @brief Returns the colour format the swap-chain texture actually uses. */
        [[nodiscard]] igl::TextureFormat GetBackBufferColorFormat() const noexcept
        {
            return backBufferColorFormat_;
        }

        /** @brief Returns the depth/stencil format of the swap surface, or Invalid when there is none. */
        [[nodiscard]] igl::TextureFormat GetBackBufferDepthFormat() const noexcept
        {
            return backBufferDepthFormat_;
        }

    private:
        struct Impl;

        void CreateDevice(const GraphicsRendererCreateArgs& args);
        void ResizeSwapChain();

        Detail::RendererBackend backend_;
        PlatformRendererSurfaceState surface_;

        // Declared before device_ so the GL context this device borrows outlives every IGL
        // destructor that still needs it current.
        std::unique_ptr<Impl> impl_;

        std::unique_ptr<igl::IDevice> device_;
        std::shared_ptr<igl::ICommandQueue> commandQueue_;
        std::shared_ptr<igl::IFramebuffer> backBuffer_;
        igl::SurfaceTextures surfaceTextures_;

        int swapChainWidth_ = 0;
        int swapChainHeight_ = 0;
        int surfaceSampleCount_ = 1;
        int swapInterval_ = 1;
        igl::TextureFormat backBufferColorFormat_ = igl::TextureFormat::RGBA_UNorm8;
        igl::TextureFormat backBufferDepthFormat_ = igl::TextureFormat::Invalid;
    };
}
