// SPDX-License-Identifier: MS-PL

#include "Sdl3GraphicsServices.hpp"

#include "../Common/SurfaceFrameValidation.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Sdl3Synchronization.hpp"
#include "Sdl3Window.hpp"

#include <SDL3/SDL.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5_webgl.h>
#endif

#include <mutex>
#include <optional>
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

        void SetGlAttributeOrThrow(const SDL_GLAttr attribute, const int value,
                                   const char* operation)
        {
            if (!SDL_GL_SetAttribute(attribute, value))
            {
                throw PlatformException(operation, SDL_GetError());
            }
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
        SdlGlobalStateLock lock;
        SDL_Window* nativeWindow = RequireSdl3Window(window, "GlContext::CreateContext");

        // Attributes must be set BEFORE context creation; setting them afterwards is silently
        // ignored, which is how a renderer ends up with a context it did not ask for.
        SDL_GL_ResetAttributes();
        SetGlAttributeOrThrow(SDL_GL_CONTEXT_MAJOR_VERSION, description.majorVersion,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_CONTEXT_MINOR_VERSION, description.minorVersion,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_CONTEXT_PROFILE_MASK, ToSdlGlProfile(description.profile),
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_RED_SIZE, description.redBits,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_GREEN_SIZE, description.greenBits,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_BLUE_SIZE, description.blueBits,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_ALPHA_SIZE, description.alphaBits,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_DEPTH_SIZE, description.depthBits,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_STENCIL_SIZE, description.stencilBits,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_MULTISAMPLEBUFFERS, description.multisampleBuffers,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_MULTISAMPLESAMPLES, description.multisampleSamples,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(SDL_GL_DOUBLEBUFFER, description.doubleBuffer ? 1 : 0,
                              "GlContext::CreateContext");
        SetGlAttributeOrThrow(
            SDL_GL_CONTEXT_FLAGS,
            description.robustAccess ? SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG : 0,
            "GlContext::CreateContext");
        SetGlAttributeOrThrow(
            SDL_GL_CONTEXT_RESET_NOTIFICATION,
            description.loseContextOnReset ? SDL_GL_CONTEXT_RESET_LOSE_CONTEXT : 0,
            "GlContext::CreateContext");

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
        // Serialised with CreateContext and window creation: GL context teardown touches the same
        // process-global SDL video state (RUST-UPSTREAM-023).
        std::optional<SdlGlobalStateLock> lock;
        if (!SdlGlobalStateHeldByThisThread())
        {
            lock.emplace();
        }
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context));
    }

    void Sdl3GlContext::MakeCurrent(const WindowId window, const GlContextHandle context)
    {
        SDL_Window* nativeWindow = RequireSdl3Window(window, "GlContext::MakeCurrent");
#if defined(__EMSCRIPTEN__)
        // SDL's current-context cache can say that a context is already current on a freshly
        // created Wasm pthread even though Emscripten's GL TLS still has no current context.
        // Bind the real WebGL handle first so OFFSCREEN_FRAMEBUFFER can proxy this thread's GL
        // calls to the browser thread that owns it.
        const auto webGlContext = reinterpret_cast<EMSCRIPTEN_WEBGL_CONTEXT_HANDLE>(context);
        if (emscripten_webgl_get_current_context() != webGlContext &&
            emscripten_webgl_make_context_current(webGlContext) != EMSCRIPTEN_RESULT_SUCCESS)
        {
            throw PlatformException(
                "GlContext::MakeCurrent", "Emscripten could not make the WebGL context current");
        }
#endif
        if (!SDL_GL_MakeCurrent(nativeWindow, static_cast<SDL_GLContext>(context)))
        {
            throw PlatformException("GlContext::MakeCurrent", SDL_GetError());
        }
    }

    GlContextBinding Sdl3GlContext::GetCurrentBinding() const
    {
        const SDL_GLContext context = SDL_GL_GetCurrentContext();
        if (context == nullptr)
        {
            return {};
        }

        SDL_Window* const window = SDL_GL_GetCurrentWindow();
        return {
            window != nullptr ? static_cast<WindowId>(SDL_GetWindowID(window)) : 0,
            static_cast<GlContextHandle>(context)
        };
    }

    void Sdl3GlContext::SwapBuffers(const WindowId window)
    {
        if (!SDL_GL_SwapWindow(RequireSdl3Window(window, "GlContext::SwapBuffers")))
        {
            throw PlatformException("GlContext::SwapBuffers", SDL_GetError());
        }
    }

    bool Sdl3GlContext::SetSwapInterval(const int interval)
    {
#if defined(__EMSCRIPTEN__)
        // Browser presentation is compositor-controlled. SDL implements this call by changing
        // Emscripten's registered main-loop timing, but CNA's Asyncify loop deliberately retains the
        // XNA Run() stack and therefore has no separately registered Emscripten main loop.
        (void) interval;
        return true;
#else
        // Returns a status rather than throwing: a driver that declines adaptive vsync should
        // leave rendering running at whatever interval it already had, not abort the frame.
        return SDL_GL_SetSwapInterval(interval);
#endif
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
        if (SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &value)) { granted.redBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &value)) { granted.greenBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &value)) { granted.blueBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &value)) { granted.alphaBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &value)) { granted.depthBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &value)) { granted.stencilBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &value)) { granted.multisampleBuffers = value; }
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &value)) { granted.multisampleSamples = value; }
        if (SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &value)) { granted.doubleBuffer = value != 0; }
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &value))
        {
            granted.robustAccess = (value & SDL_GL_CONTEXT_ROBUST_ACCESS_FLAG) != 0;
        }
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_RESET_NOTIFICATION, &value))
        {
            granted.loseContextOnReset = value == SDL_GL_CONTEXT_RESET_LOSE_CONTEXT;
        }

        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &value))
        {
            if (value == SDL_GL_CONTEXT_PROFILE_ES) { granted.profile = GlProfile::Es; }
            else if (value == SDL_GL_CONTEXT_PROFILE_COMPATIBILITY) { granted.profile = GlProfile::Compatibility; }
            else { granted.profile = GlProfile::Core; }
        }

        return granted;
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
        // The scale mode belongs to the texture, so a mode change after a texture exists has to
        // be applied to it too -- otherwise the setting silently takes effect only on the next
        // resize.
        if (texture_ != nullptr)
        {
            if (!SDL_SetTextureScaleMode(texture_,
                                         filter == PresentFilter::Linear ? SDL_SCALEMODE_LINEAR
                                                                         : SDL_SCALEMODE_NEAREST))
            {
                throw PlatformException("SurfacePresenter::SetScaleMode", SDL_GetError());
            }
        }
        scaleMode_ = mode;
        filter_ = filter;
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
        SDL_Texture* replacement =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                              width, height);
        if (replacement == nullptr)
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }
        if (!SDL_SetTextureScaleMode(replacement,
                                     filter_ == PresentFilter::Linear ? SDL_SCALEMODE_LINEAR
                                                                      : SDL_SCALEMODE_NEAREST))
        {
            const std::string detail = SDL_GetError();
            SDL_DestroyTexture(replacement);
            throw PlatformException("SurfacePresenter::Present", detail);
        }

        if (texture_ != nullptr)
        {
            SDL_DestroyTexture(texture_);
        }
        texture_ = replacement;
        textureWidth_ = width;
        textureHeight_ = height;
    }

    void Sdl3SurfacePresenter::Present(const SurfaceFrame& frame)
    {
        const int stride =
            Common::ValidateSurfaceFrame(frame, "SurfacePresenter::Present");

        EnsureTexture(frame.width, frame.height);

        if (!SDL_UpdateTexture(texture_, nullptr, frame.pixels, stride))
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }

        const SDL_RendererLogicalPresentation logicalPresentation =
            scaleMode_ == PresentScaleMode::Letterbox ? SDL_LOGICAL_PRESENTATION_LETTERBOX
            : scaleMode_ == PresentScaleMode::Overscan ? SDL_LOGICAL_PRESENTATION_OVERSCAN
            : scaleMode_ == PresentScaleMode::Stretch ? SDL_LOGICAL_PRESENTATION_STRETCH
                                                      : SDL_LOGICAL_PRESENTATION_DISABLED;
        if (!SDL_SetRenderLogicalPresentation(renderer_, frame.width, frame.height,
                                              logicalPresentation))
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }

        if (!SDL_RenderClear(renderer_))
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }
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
        if (!SDL_RenderTexture(renderer_, texture_, nullptr, destination))
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }
        if (!SDL_RenderPresent(renderer_))
        {
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        }
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
