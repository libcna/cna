// SPDX-License-Identifier: MS-PL

#include "Sdl3GraphicsServices.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "Sdl3Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <type_traits>

namespace CNA::Platform::Sdl3 {

    namespace {

        Sdl3Window& RequireSdl3Window(IPlatformWindow& window, const char* operation)
        {
            auto* sdlWindow = dynamic_cast<Sdl3Window*>(&window);
            if (sdlWindow == nullptr)
            {
                throw PlatformException(operation, "window was not created by the SDL3 platform");
            }
            return *sdlWindow;
        }

        SDL_Window* RequireSdl3Window(const WindowId window, const char* operation)
        {
            SDL_Window* nativeWindow =
                SDL_GetWindowFromID(static_cast<SDL_WindowID>(window));
            if (nativeWindow == nullptr)
            {
                throw PlatformException(operation, "unknown or expired window id");
            }
            return nativeWindow;
        }

        void* LoadSdlGlProcAddress(const char* name)
        {
            return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
        }

        /// Converts between VkSurfaceKHR and the contract's std::uint64_t handle.
        ///
        /// VkSurfaceKHR is always 64 bits wide, but its C *type* is not uniform: Vulkan defines
        /// non-dispatchable handles as a pointer on 64-bit targets and as uint64_t on 32-bit
        /// ones. So a single static_cast does not compile everywhere, and this is exactly why the
        /// contract types the handle as std::uint64_t rather than void* -- void* would be too
        /// narrow on 32-bit and silently truncate.
        template <typename TSurface>
        VulkanSurfaceHandle ToSurfaceHandle(const TSurface surface)
        {
            if constexpr (std::is_pointer_v<TSurface>)
            {
                return reinterpret_cast<VulkanSurfaceHandle>(surface);
            }
            else
            {
                return static_cast<VulkanSurfaceHandle>(surface);
            }
        }

        template <typename TSurface>
        TSurface FromSurfaceHandle(const VulkanSurfaceHandle handle)
        {
            if constexpr (std::is_pointer_v<TSurface>)
            {
                return reinterpret_cast<TSurface>(handle);
            }
            else
            {
                return static_cast<TSurface>(handle);
            }
        }

        /// Ensures SDL has loaded a Vulkan loader, and reports whether one exists.
        ///
        /// SDL_Vulkan_GetInstanceExtensions must not be called before the library is loaded --
        /// doing so crashes rather than returning an error, which is how this was found. The
        /// result is cached because the answer cannot change within a process and the probe
        /// dlopen()s a library.
        bool VulkanLoaderAvailable()
        {
            static const bool available = SDL_Vulkan_LoadLibrary(nullptr);
            return available;
        }

        int ToSdlGlProfile(const GlProfile profile)
        {
            switch (profile)
            {
                case GlProfile::Core:          return SDL_GL_CONTEXT_PROFILE_CORE;
                case GlProfile::Compatibility: return SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
                case GlProfile::Es:            return SDL_GL_CONTEXT_PROFILE_ES;
            }
            return SDL_GL_CONTEXT_PROFILE_CORE;
        }

    } // namespace

    // --- OpenGL context (PLAT-41) -------------------------------------------------------------------

    GlContextHandle Sdl3GlContext::CreateContext(const WindowId window,
                                                 const GlContextDescription& description)
    {
        SDL_Window* nativeWindow = RequireSdl3Window(window, "GlContext::CreateContext");

        // Attributes must be set BEFORE context creation; setting them afterwards is silently
        // ignored, which is how a renderer ends up with a context it did not ask for.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, description.majorVersion);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, description.minorVersion);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, ToSdlGlProfile(description.profile));
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, description.depthBits);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, description.stencilBits);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, description.multisampleBuffers);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, description.multisampleSamples);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, description.doubleBuffer ? 1 : 0);

        SDL_GLContext context = SDL_GL_CreateContext(nativeWindow);
        if (context == nullptr)
        {
            throw PlatformException("GlContext::CreateContext", SDL_GetError());
        }
        return static_cast<GlContextHandle>(context);
    }

    void Sdl3GlContext::DestroyContext(const GlContextHandle context)
    {
        if (context == nullptr)
        {
            return;
        }
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context));
    }

    void Sdl3GlContext::MakeCurrent(const WindowId window, const GlContextHandle context)
    {
        SDL_Window* nativeWindow = RequireSdl3Window(window, "GlContext::MakeCurrent");
        if (!SDL_GL_MakeCurrent(nativeWindow, static_cast<SDL_GLContext>(context)))
        {
            throw PlatformException("GlContext::MakeCurrent", SDL_GetError());
        }
    }

    void Sdl3GlContext::SwapBuffers(const WindowId window)
    {
        SDL_GL_SwapWindow(RequireSdl3Window(window, "GlContext::SwapBuffers"));
    }

    bool Sdl3GlContext::SetSwapInterval(const int interval)
    {
        // Returns a status rather than throwing: a driver that declines adaptive vsync should
        // leave rendering running at whatever interval it already had, not abort the frame.
        return SDL_GL_SetSwapInterval(interval);
    }

    void* Sdl3GlContext::GetProcAddress(const std::string& name) const
    {
        return LoadSdlGlProcAddress(name.c_str());
    }

    GlProcAddressLoader Sdl3GlContext::GetProcAddressLoader() const
    {
        return &LoadSdlGlProcAddress;
    }

    GlContextDescription Sdl3GlContext::GetContextAttributes(GlContextHandle) const
    {
        // What the driver actually granted, which may exceed or fall short of the request. A
        // renderer that assumes it got exactly what it asked for breaks on an unfamiliar driver.
        GlContextDescription granted;
        int value = 0;

        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &value)) { granted.majorVersion = value; }
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &value)) { granted.minorVersion = value; }
        if (SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &value)) { granted.depthBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &value)) { granted.stencilBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &value)) { granted.multisampleBuffers = value; }
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &value)) { granted.multisampleSamples = value; }
        if (SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &value)) { granted.doubleBuffer = value != 0; }

        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &value))
        {
            if (value == SDL_GL_CONTEXT_PROFILE_ES) { granted.profile = GlProfile::Es; }
            else if (value == SDL_GL_CONTEXT_PROFILE_COMPATIBILITY) { granted.profile = GlProfile::Compatibility; }
            else { granted.profile = GlProfile::Core; }
        }

        return granted;
    }

    // --- Vulkan surface (PLAT-42) -----------------------------------------------------------------

    std::vector<std::string> Sdl3VulkanSurface::GetInstanceExtensions() const
    {
        if (!VulkanLoaderAvailable())
        {
            throw PlatformNotSupportedException(PlatformCapability::VulkanSurface, "SDL3");
        }

        Uint32 count = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
        if (extensions == nullptr)
        {
            return {};
        }

        std::vector<std::string> names;
        names.reserve(count);
        for (Uint32 i = 0; i < count; ++i)
        {
            names.emplace_back(extensions[i]);
        }
        return names;
    }

    VulkanSurfaceHandle Sdl3VulkanSurface::CreateSurface(const VulkanInstanceHandle instance,
                                                         IPlatformWindow& window)
    {
        if (!VulkanLoaderAvailable())
        {
            throw PlatformNotSupportedException(PlatformCapability::VulkanSurface, "SDL3");
        }

        Sdl3Window& sdlWindow = RequireSdl3Window(window, "VulkanSurface::CreateSurface");

        // Value-initialised rather than VK_NULL_HANDLE: that macro needs a real Vulkan header,
        // and {} is the null handle for both the pointer and the integer form.
        VkSurfaceKHR surface{};
        if (!SDL_Vulkan_CreateSurface(sdlWindow.GetSdlWindow(), static_cast<VkInstance>(instance),
                                      nullptr, &surface))
        {
            throw PlatformException("VulkanSurface::CreateSurface", SDL_GetError());
        }
        return ToSurfaceHandle(surface);
    }

    void Sdl3VulkanSurface::DestroySurface(const VulkanInstanceHandle instance,
                                           const VulkanSurfaceHandle surface)
    {
        if (surface == 0)
        {
            return;
        }
        SDL_Vulkan_DestroySurface(static_cast<VkInstance>(instance),
                                  FromSurfaceHandle<VkSurfaceKHR>(surface), nullptr);
    }

    // --- surface presenter (PLAT-128) --------------------------------------------------------------

    Sdl3SurfacePresenter::Sdl3SurfacePresenter(Sdl3Window& window)
        : window_(window)
    {
        renderer_ = SDL_CreateRenderer(window_.GetSdlWindow(), nullptr);
        if (renderer_ == nullptr)
        {
            throw PlatformException("CreateSurfacePresenter", SDL_GetError());
        }
    }

    Sdl3SurfacePresenter::~Sdl3SurfacePresenter()
    {
        if (texture_ != nullptr)
        {
            SDL_DestroyTexture(texture_);
        }
        if (renderer_ != nullptr)
        {
            SDL_DestroyRenderer(renderer_);
        }
    }

    void Sdl3SurfacePresenter::SetScaleMode(const PresentScaleMode mode, const PresentFilter filter)
    {
        scaleMode_ = mode;
        filter_ = filter;

        // The scale mode belongs to the texture, so a mode change after a texture exists has to
        // be applied to it too -- otherwise the setting silently takes effect only on the next
        // resize.
        if (texture_ != nullptr)
        {
            SDL_SetTextureScaleMode(texture_, filter_ == PresentFilter::Linear ? SDL_SCALEMODE_LINEAR
                                                                              : SDL_SCALEMODE_NEAREST);
        }
    }

    bool Sdl3SurfacePresenter::SetVSync(const bool enabled)
    {
        return SDL_SetRenderVSync(renderer_, enabled ? 1 : SDL_RENDERER_VSYNC_DISABLED);
    }

    void Sdl3SurfacePresenter::EnsureTexture(const int width, const int height)
    {
        if (texture_ != nullptr && textureWidth_ == width && textureHeight_ == height)
        {
            return;
        }
        if (texture_ != nullptr)
        {
            SDL_DestroyTexture(texture_);
            texture_ = nullptr;
        }

        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                     width, height);
        if (texture_ == nullptr)
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }
        textureWidth_ = width;
        textureHeight_ = height;

        SDL_SetTextureScaleMode(texture_, filter_ == PresentFilter::Linear ? SDL_SCALEMODE_LINEAR
                                                                          : SDL_SCALEMODE_NEAREST);
    }

    void Sdl3SurfacePresenter::Present(const SurfaceFrame& frame)
    {
        if (frame.pixels == nullptr || frame.width <= 0 || frame.height <= 0)
        {
            throw PlatformException("SurfacePresenter::Present",
                                    "frame must have non-null pixels and a positive size");
        }

        EnsureTexture(frame.width, frame.height);

        const int stride = frame.strideBytes > 0 ? frame.strideBytes : frame.width * 4;
        if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels, stride))
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }

        const SDL_RendererLogicalPresentation logicalPresentation =
            scaleMode_ == PresentScaleMode::Letterbox ? SDL_LOGICAL_PRESENTATION_LETTERBOX
            : scaleMode_ == PresentScaleMode::Overscan ? SDL_LOGICAL_PRESENTATION_OVERSCAN
            : scaleMode_ == PresentScaleMode::Stretch ? SDL_LOGICAL_PRESENTATION_STRETCH
                                                      : SDL_LOGICAL_PRESENTATION_DISABLED;
        SDL_SetRenderLogicalPresentation(renderer_, frame.width, frame.height, logicalPresentation);

        SDL_RenderClear(renderer_);
        SDL_FRect unscaledDestination{};
        const SDL_FRect* destination = nullptr;
        if (scaleMode_ == PresentScaleMode::None || scaleMode_ == PresentScaleMode::Native)
        {
            int targetWidth = 0;
            int targetHeight = 0;
            GetTargetSize(targetWidth, targetHeight);
            unscaledDestination.x = scaleMode_ == PresentScaleMode::None
                ? static_cast<float>(targetWidth - frame.width) * 0.5f : 0.0f;
            unscaledDestination.y = scaleMode_ == PresentScaleMode::None
                ? static_cast<float>(targetHeight - frame.height) * 0.5f : 0.0f;
            unscaledDestination.w = static_cast<float>(frame.width);
            unscaledDestination.h = static_cast<float>(frame.height);
            destination = &unscaledDestination;
        }
        SDL_RenderTexture(renderer_, texture_, nullptr, destination);
        SDL_RenderPresent(renderer_);
    }

    void Sdl3SurfacePresenter::GetTargetSize(int& width, int& height) const
    {
        // Read from the renderer rather than cached: it tracks the window, and a caller sizes its
        // own rasterisation buffer from this every frame.
        if (!SDL_GetRenderOutputSize(renderer_, &width, &height))
        {
            width = 0;
            height = 0;
        }
    }

} // namespace CNA::Platform::Sdl3
