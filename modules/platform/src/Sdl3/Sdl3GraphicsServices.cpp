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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
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
        SdlGlobalStateLock lock;
        if (SDL_GetRenderer(window_.GetSdlWindow()) != nullptr)
            throw PlatformException("CreateSurfacePresenter", "the window already has a renderer");
        // SDL_video.c:3598-3660 may replace native framebuffer updates with full texture swaps.
        // Request the native route without overriding an application's higher-priority hint.
        const char* hint = SDL_GetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
        const std::optional<std::string> previous = hint ? std::optional<std::string>(hint) : std::nullopt;
        const bool changed = SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
        SDL_Surface* surface = SDL_GetWindowSurface(window_.GetSdlWindow());
        const std::string error = surface ? std::string{} : SDL_GetError();
        if (changed)
        {
            if (previous) SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, previous->c_str());
            else SDL_ResetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION);
        }
        if (surface == nullptr)
            throw PlatformException("CreateSurfacePresenter", error);
    }

    Sdl3SurfacePresenter::~Sdl3SurfacePresenter() = default;

    SDL_Surface* Sdl3SurfacePresenter::GetSurface() const
    {
        // Reacquire on every frame: SDL_video.c:4305 invalidates after resize, and :3689-3706
        // destroys/recreates the old surface. Never retain its pointer across a present.
        SDL_Surface* surface = SDL_GetWindowSurface(window_.GetSdlWindow());
        if (surface == nullptr)
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        return surface;
    }

    void Sdl3SurfacePresenter::SetScaleMode(const PresentScaleMode mode, const PresentFilter filter)
    {
        scaleMode_ = mode;
        filter_ = filter;
    }

    bool Sdl3SurfacePresenter::SetVSync(const bool enabled)
    {
        return SDL_SetWindowSurfaceVSync(window_.GetSdlWindow(), enabled ? 1 : 0);
    }

    void Sdl3SurfacePresenter::BlitFrame(const SurfaceFrame& frame,
                                        const SurfaceDestination& destination,
                                        SDL_Surface* target) const
    {
        const int stride = Common::ValidateSurfaceFrame(frame, "SurfacePresenter::Present");
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> source(
            SDL_CreateSurfaceFrom(frame.width, frame.height, SDL_PIXELFORMAT_RGBA32,
                                  const_cast<std::uint8_t*>(frame.pixels), stride), SDL_DestroySurface);
        if (!source || !SDL_SetSurfaceBlendMode(source.get(), SDL_BLENDMODE_NONE))
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        const SDL_Rect rectangle{destination.x, destination.y, destination.width, destination.height};
        // SDL_surface.c:1181-1212 retains the requested destination for a clipped scaled blit.
        // Clipping the rectangle first would rescale the whole source into the visible remainder.
        if (!SDL_BlitSurfaceScaled(source.get(), nullptr, target, &rectangle,
                                  filter_ == PresentFilter::Linear ? SDL_SCALEMODE_LINEAR
                                                                   : SDL_SCALEMODE_NEAREST))
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
    }

    void Sdl3SurfacePresenter::Present(const SurfaceFrame& frame)
    {
        Common::ValidateSurfaceFrame(frame, "SurfacePresenter::Present");
        SDL_Surface* target = GetSurface();
        SurfaceDestination destination{0, 0, target->w, target->h};
        if (scaleMode_ == PresentScaleMode::Native || scaleMode_ == PresentScaleMode::None)
        {
            destination.width = frame.width;
            destination.height = frame.height;
        }
        else if (scaleMode_ != PresentScaleMode::Stretch)
        {
            const double sx = static_cast<double>(target->w) / frame.width;
            const double sy = static_cast<double>(target->h) / frame.height;
            const double scale = scaleMode_ == PresentScaleMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
            destination.width = static_cast<int>(std::lround(frame.width * scale));
            destination.height = static_cast<int>(std::lround(frame.height * scale));
        }
        if (scaleMode_ != PresentScaleMode::Native)
        {
            destination.x = (target->w - destination.width) / 2;
            destination.y = (target->h - destination.height) / 2;
        }
        if (!SDL_FillSurfaceRect(target, nullptr, SDL_MapSurfaceRGBA(target, 0, 0, 0, 255)))
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
        BlitFrame(frame, destination, target);
        if (!SDL_UpdateWindowSurface(window_.GetSdlWindow()))
            throw PlatformException("SurfacePresenter::Present", SDL_GetError());
    }

    bool Sdl3SurfacePresenter::PresentRegion(const SurfaceFrame& frame,
                                             const SurfaceDestination& destination)
    {
        Common::ValidateSurfaceFrame(frame, "SurfacePresenter::PresentRegion");
        if (destination.width <= 0 || destination.height <= 0)
            throw PlatformException("SurfacePresenter::PresentRegion", "destination extent must be positive");
        SDL_Surface* target = GetSurface();
        const auto left = std::max<std::int64_t>(destination.x, 0);
        const auto top = std::max<std::int64_t>(destination.y, 0);
        const auto right = std::min<std::int64_t>(static_cast<std::int64_t>(destination.x) + destination.width, target->w);
        const auto bottom = std::min<std::int64_t>(static_cast<std::int64_t>(destination.y) + destination.height, target->h);
        if (right <= left || bottom <= top)
            return true;
        // SDL_video.c:492-524 texture framebuffers present the FULL output. They cannot retain
        // arbitrary preexisting foreign-client pixels outside a first partial update.
        const bool wholeClient = left == 0 && top == 0 && right == target->w && bottom == target->h;
        const char* driver = SDL_GetCurrentVideoDriver();
        const std::string system = driver != nullptr ? driver : "";
        // Only these inspected native framebuffers write dirty rectangles into the existing
        // host. Other systems may replace a whole surface buffer even for a partial damage hint.
        const bool nativeDirtyRectangles = system == "x11" || system == "windows" || system == "dummy";
        if (!wholeClient && (!nativeDirtyRectangles || SDL_GetRenderer(window_.GetSdlWindow()) != nullptr))
            return false;
        BlitFrame(frame, destination, target);
        const SDL_Rect dirty{static_cast<int>(left), static_cast<int>(top),
                             static_cast<int>(right-left), static_cast<int>(bottom-top)};
        if (!SDL_UpdateWindowSurfaceRects(window_.GetSdlWindow(), &dirty, 1))
            throw PlatformException("SurfacePresenter::PresentRegion", SDL_GetError());
        return true;
    }

    void Sdl3SurfacePresenter::GetTargetSize(int& width, int& height) const
    {
        if (!SDL_GetWindowSizeInPixels(window_.GetSdlWindow(), &width, &height))
            width = height = 0;
    }

} // namespace CNA::Platform::Sdl3
