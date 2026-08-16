// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"

struct SDL_Renderer;
struct SDL_Texture;

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

    /** @brief SDL3-backed Vulkan surface creation. */
    class Sdl3VulkanSurface final : public IPlatformVulkanSurface
    {
    public:
        /** @brief Gets the required instance extensions. @return Extension names. */
        [[nodiscard]] std::vector<std::string> GetInstanceExtensions() const override;
        /**
         * @brief Creates a surface for a window.
         * @param instance The `VkInstance`.
         * @param window The stable id of the window to present to.
         * @return A non-zero surface handle.
         */
        [[nodiscard]] VulkanSurfaceHandle CreateSurface(VulkanInstanceHandle instance,
                                                        WindowId window) override;
        /**
         * @brief Destroys a surface.
         * @param instance The instance it was created against.
         * @param surface The surface to destroy.
         */
        void DestroySurface(VulkanInstanceHandle instance, VulkanSurfaceHandle surface) override;
    };

    /**
     * @brief Presents CPU-rasterised pixels through SDL's 2D renderer.
     *
     * The implementation PLAT-3's audit showed was missing: `SKIA` and `BLEND2D` rasterise on the
     * CPU and use exactly this call set to get a finished frame onto the screen. Owning an
     * `SDL_Renderer` here is correct and is why the allowlist is about *renderers*, not about the
     * symbol -- `modules/platform` is allowed to use SDL, that is its whole purpose.
     */
    class Sdl3SurfacePresenter final : public IPlatformSurfacePresenter
    {
    public:
        /**
         * @brief Creates a presenter targeting a window.
         * @param window The window to present to.
         */
        explicit Sdl3SurfacePresenter(Sdl3Window& window);

        /** @brief Releases the streaming texture and the renderer. */
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
         * @brief Gets the current target size.
         * @param width Receives the width.
         * @param height Receives the height.
         */
        void GetTargetSize(int& width, int& height) const override;

    private:
        void EnsureTexture(int width, int height);

        Sdl3Window& window_;
        SDL_Renderer* renderer_ = nullptr;
        SDL_Texture* texture_ = nullptr;
        int textureWidth_ = 0;
        int textureHeight_ = 0;
        PresentScaleMode scaleMode_ = PresentScaleMode::Letterbox;
        PresentFilter filter_ = PresentFilter::Nearest;
    };

} // namespace CNA::Platform::Sdl3
