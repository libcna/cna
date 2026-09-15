// SPDX-License-Identifier: MS-PL

#include "WaylandGraphicsServices.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "../Common/SurfaceFrameFitting.hpp"
#include "../Common/SurfaceFrameValidation.hpp"
#include "../Posix/VulkanLoader.hpp"
#include "WaylandConnection.hpp"
#include "WaylandShm.hpp"
#include "WaylandWindow.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <dlfcn.h>
#include <poll.h>

#if defined(CNA_WAYLAND_HAVE_EGL)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <wayland-egl.h>
#endif

namespace CNA::Platform::Wayland {

    namespace {

        /// The longest a swap or a present waits for the compositor's frame callback (D-23): a
        /// visible window runs at the display's rate, a hidden or occluded one -- which the
        /// compositor stops sending frames for -- at ten frames a second instead of hanging.
        constexpr std::chrono::milliseconds kFrameTimeout{100};

#if defined(CNA_WAYLAND_HAVE_EGL)

#if !defined(EGL_PRESENT_OPAQUE_EXT)
#define EGL_PRESENT_OPAQUE_EXT 0x31DF
#endif

        /// libEGL and libwayland-egl, opened at run time (D-1). The function types are taken from
        /// the headers' own prototypes, which names them without linking them.
        struct EglLibrary
        {
            bool loaded = false;
            decltype(&eglGetProcAddress) getProcAddress = nullptr;
            decltype(&eglQueryString) queryString = nullptr;
            decltype(&eglGetDisplay) getDisplay = nullptr;
            decltype(&eglInitialize) initialize = nullptr;
            decltype(&eglTerminate) terminate = nullptr;
            decltype(&eglBindAPI) bindApi = nullptr;
            decltype(&eglChooseConfig) chooseConfig = nullptr;
            decltype(&eglGetConfigAttrib) getConfigAttrib = nullptr;
            decltype(&eglCreateContext) createContext = nullptr;
            decltype(&eglDestroyContext) destroyContext = nullptr;
            decltype(&eglCreateWindowSurface) createWindowSurface = nullptr;
            decltype(&eglDestroySurface) destroySurface = nullptr;
            decltype(&eglMakeCurrent) makeCurrent = nullptr;
            decltype(&eglSwapBuffers) swapBuffers = nullptr;
            decltype(&eglSwapInterval) swapInterval = nullptr;
            decltype(&eglGetError) getError = nullptr;
            PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = nullptr;
            decltype(&wl_egl_window_create) windowCreate = nullptr;
            decltype(&wl_egl_window_destroy) windowDestroy = nullptr;
            decltype(&wl_egl_window_resize) windowResize = nullptr;
            bool platformIsKhr = false;
        };

        bool HasToken(const char* list, const char* token)
        {
            if (list == nullptr)
            {
                return false;
            }
            const std::size_t length = std::strlen(token);
            for (const char* cursor = list; (cursor = std::strstr(cursor, token)) != nullptr; cursor += length)
            {
                const bool starts = cursor == list || cursor[-1] == ' ';
                const bool ends = cursor[length] == '\0' || cursor[length] == ' ';
                if (starts && ends)
                {
                    return true;
                }
            }
            return false;
        }

        const EglLibrary& Egl()
        {
            static const EglLibrary library = [] {
                EglLibrary result;
                // Both never closed: entry points a renderer resolved through them stay in use for
                // as long as it lives.
                void* egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
                void* waylandEgl = dlopen("libwayland-egl.so.1", RTLD_NOW | RTLD_LOCAL);
                if (egl == nullptr || waylandEgl == nullptr)
                {
                    return result;
                }
                const auto get = [](void* library, const char* name, auto& function) {
                    function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(dlsym(library, name));
                    return function != nullptr;
                };
                bool all = true;
                all = get(egl, "eglGetProcAddress", result.getProcAddress) && all;
                all = get(egl, "eglQueryString", result.queryString) && all;
                all = get(egl, "eglGetDisplay", result.getDisplay) && all;
                all = get(egl, "eglInitialize", result.initialize) && all;
                all = get(egl, "eglTerminate", result.terminate) && all;
                all = get(egl, "eglBindAPI", result.bindApi) && all;
                all = get(egl, "eglChooseConfig", result.chooseConfig) && all;
                all = get(egl, "eglGetConfigAttrib", result.getConfigAttrib) && all;
                all = get(egl, "eglCreateContext", result.createContext) && all;
                all = get(egl, "eglDestroyContext", result.destroyContext) && all;
                all = get(egl, "eglCreateWindowSurface", result.createWindowSurface) && all;
                all = get(egl, "eglDestroySurface", result.destroySurface) && all;
                all = get(egl, "eglMakeCurrent", result.makeCurrent) && all;
                all = get(egl, "eglSwapBuffers", result.swapBuffers) && all;
                all = get(egl, "eglSwapInterval", result.swapInterval) && all;
                all = get(egl, "eglGetError", result.getError) && all;
                all = get(waylandEgl, "wl_egl_window_create", result.windowCreate) && all;
                all = get(waylandEgl, "wl_egl_window_destroy", result.windowDestroy) && all;
                all = get(waylandEgl, "wl_egl_window_resize", result.windowResize) && all;
                if (!all)
                {
                    return result;
                }
                // The Wayland platform must be a client extension: EGL 1.5's core
                // eglGetPlatformDisplay with KHR_platform_wayland, or EXT_platform_base's
                // eglGetPlatformDisplayEXT with EXT_platform_wayland. The older eglGetDisplay
                // guesses what kind of native display it was given and is not used.
                const char* clientExtensions = result.queryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
                if (HasToken(clientExtensions, "EGL_KHR_platform_wayland"))
                {
                    result.getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                        dlsym(egl, "eglGetPlatformDisplay"));
                    result.platformIsKhr = result.getPlatformDisplay != nullptr;
                }
                if (result.getPlatformDisplay == nullptr && HasToken(clientExtensions, "EGL_EXT_platform_wayland"))
                {
                    result.getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                        result.getProcAddress("eglGetPlatformDisplayEXT"));
                }
                result.loaded = result.getPlatformDisplay != nullptr;
                return result;
            }();
            return library;
        }

        void* LoadEglProcAddress(const char* name)
        {
            const EglLibrary& egl = Egl();
            if (!egl.loaded || name == nullptr)
            {
                return nullptr;
            }
            // EGL_KHR_get_all_proc_addresses (every Mesa, every libglvnd) answers core entry
            // points too.
            return reinterpret_cast<void*>(egl.getProcAddress(name));
        }

        std::string DescribeEglError(const int code)
        {
            char text[32] = {};
            std::snprintf(text, sizeof(text), "0x%04X", static_cast<unsigned>(code));
            return std::string("EGL error ") + text;
        }

#else
        void* LoadEglProcAddress(const char*) { return nullptr; }
#endif

    } // namespace

    // --- WaylandFramePacer ------------------------------------------------------------------------

    WaylandFramePacer::WaylandFramePacer(wl_display* display, wl_surface* surface) : display_(display)
    {
        queue_ = wl_display_create_queue(display_);
        wrapper_ = static_cast<wl_surface*>(wl_proxy_create_wrapper(surface));
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper_), queue_);
    }

    WaylandFramePacer::~WaylandFramePacer()
    {
        Cancel();
        if (wrapper_ != nullptr)
        {
            wl_proxy_wrapper_destroy(wrapper_);
            wrapper_ = nullptr;
        }
        if (queue_ != nullptr)
        {
            wl_event_queue_destroy(queue_);
            queue_ = nullptr;
        }
    }

    void WaylandFramePacer::Cancel()
    {
        if (callback_ != nullptr)
        {
            wl_callback_destroy(callback_);
            callback_ = nullptr;
        }
    }

    void WaylandFramePacer::Request()
    {
        if (callback_ != nullptr || wrapper_ == nullptr)
        {
            return;
        }
        static const wl_callback_listener listener = {
            .done = [](void* data, wl_callback* callback, std::uint32_t) {
                auto* self = static_cast<WaylandFramePacer*>(data);
                wl_callback_destroy(callback);
                if (self->callback_ == callback)
                {
                    self->callback_ = nullptr;
                }
            },
        };
        callback_ = wl_surface_frame(wrapper_);
        wl_callback_add_listener(callback_, &listener, this);
    }

    bool WaylandFramePacer::Wait(const std::chrono::milliseconds timeout)
    {
        if (callback_ == nullptr)
        {
            return true;
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (callback_ != nullptr)
        {
            if (wl_display_get_error(display_) != 0)
            {
                break;
            }
            // The same prepare/read dance as the platform's pump, on this queue alone: events for
            // the application's queue that arrive meanwhile are read and left queued for its next
            // PollEvents.
            if (wl_display_prepare_read_queue(display_, queue_) != 0)
            {
                wl_display_dispatch_queue_pending(display_, queue_);
                continue;
            }
            wl_display_flush(display_);
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
            {
                wl_display_cancel_read(display_);
                break;
            }
            pollfd descriptor{wl_display_get_fd(display_), POLLIN, 0};
            const int wait = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            const int ready = ::poll(&descriptor, 1, std::max(1, wait));
            if (ready > 0)
            {
                wl_display_read_events(display_);
            }
            else
            {
                wl_display_cancel_read(display_);
            }
            wl_display_dispatch_queue_pending(display_, queue_);
        }
        if (callback_ != nullptr)
        {
            // Given up on: the compositor is not drawing this surface. The request is dropped so
            // the next frame asks afresh.
            Cancel();
            return false;
        }
        return true;
    }

    // --- WaylandGlContext ------------------------------------------------------------------------

    struct WaylandGlContext::WindowRecord
    {
        WaylandWindow* window = nullptr;
#if defined(CNA_WAYLAND_HAVE_EGL)
        EGLConfig config = nullptr;
        EGLSurface surface = EGL_NO_SURFACE;
        wl_egl_window* eglWindow = nullptr;
        bool es = false;
#endif
        std::unique_ptr<WaylandFramePacer> pacer;
        int interval = 1;
        bool intervalApplied = false;
        GlContextDescription granted;
    };

    struct WaylandGlContext::ContextRecord
    {
        WindowId window = 0;
        GlContextDescription granted;
        bool es = false;
    };

    WaylandGlContext::WaylandGlContext(WaylandConnection& connection) : connection_(connection)
    {
#if defined(CNA_WAYLAND_HAVE_EGL)
        available_ = Egl().loaded;
#endif
    }

    WaylandGlContext::~WaylandGlContext()
    {
#if defined(CNA_WAYLAND_HAVE_EGL)
        const EglLibrary& egl = Egl();
        if (eglDisplay_ != nullptr)
        {
            egl.makeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            for (auto& [handle, record] : contexts_)
            {
                (void) record;
                egl.bindApi(record.es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
                egl.destroyContext(eglDisplay_, handle);
            }
            contexts_.clear();
            for (auto& [id, record] : windows_)
            {
                (void) id;
                DestroySurface(*record);
            }
            windows_.clear();
            // Terminated while the wl_display is still connected: EGL's Wayland platform destroys
            // proxies of its own on that display.
            egl.terminate(eglDisplay_);
            eglDisplay_ = nullptr;
        }
#endif
    }

    bool WaylandGlContext::EnsureDisplay()
    {
#if defined(CNA_WAYLAND_HAVE_EGL)
        if (eglDisplay_ != nullptr)
        {
            return true;
        }
        if (displayTried_ || !available_)
        {
            return false;
        }
        displayTried_ = true;
        const EglLibrary& egl = Egl();
        EGLDisplay display = egl.getPlatformDisplay(egl.platformIsKhr ? EGL_PLATFORM_WAYLAND_KHR : EGL_PLATFORM_WAYLAND_EXT,
                                                    connection_.GetDisplay(), nullptr);
        EGLint major = 0;
        EGLint minor = 0;
        if (display == EGL_NO_DISPLAY || egl.initialize(display, &major, &minor) != EGL_TRUE)
        {
            return false;
        }
        eglDisplay_ = display;
        presentOpaque_ = HasToken(egl.queryString(display, EGL_EXTENSIONS), "EGL_EXT_present_opaque");
        return true;
#else
        return false;
#endif
    }

    void* WaylandGlContext::GetEglDisplay()
    {
        return EnsureDisplay() ? eglDisplay_ : nullptr;
    }

    WaylandGlContext::WindowRecord* WaylandGlContext::Find(const WindowId id)
    {
        const auto found = windows_.find(id);
        return found != windows_.end() ? found->second.get() : nullptr;
    }

    void WaylandGlContext::RegisterWindow(const WindowId id, WaylandWindow* window)
    {
        if (window != nullptr)
        {
            auto record = std::make_unique<WindowRecord>();
            record->window = window;
            windows_[id] = std::move(record);
            return;
        }
        const auto found = windows_.find(id);
        if (found == windows_.end())
        {
            return;
        }
        DestroySurface(*found->second);
        windows_.erase(found);
    }

    void WaylandGlContext::DestroySurface(WindowRecord& record)
    {
        record.pacer.reset();
#if defined(CNA_WAYLAND_HAVE_EGL)
        const EglLibrary& egl = Egl();
        if (record.surface != EGL_NO_SURFACE)
        {
            if (currentWindow_ != 0 && Find(currentWindow_) == &record)
            {
                // A surface current on this thread cannot be destroyed out from under the binding.
                egl.makeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                currentWindow_ = 0;
                currentContext_ = nullptr;
            }
            egl.destroySurface(eglDisplay_, record.surface);
            record.surface = EGL_NO_SURFACE;
        }
        if (record.eglWindow != nullptr)
        {
            if (record.window != nullptr)
            {
                record.window->SetPixelSizeListener(nullptr);
            }
            egl.windowDestroy(record.eglWindow);
            record.eglWindow = nullptr;
        }
#endif
    }

    void WaylandGlContext::ChooseConfig(WindowRecord& record, const GlContextDescription& description, const bool es)
    {
#if defined(CNA_WAYLAND_HAVE_EGL)
        const EglLibrary& egl = Egl();
        const OpenGlFramebufferDescription& framebuffer = record.window->GetOpenGlFramebuffer();
        // The window's framebuffer request is the minimum; the X11 backend's default -- what a
        // game's back buffer needs, double-buffered Depth24Stencil8 -- fills what it leaves open
        // (plans/plan_x11.md X11-0172), and the context's own request can only raise it.
        const int depth = std::max(framebuffer.depthBits > 0 ? framebuffer.depthBits : 24, description.depthBits);
        const int stencil = std::max(framebuffer.stencilBits > 0 ? framebuffer.stencilBits : 8, description.stencilBits);
        const int samples = framebuffer.samples > 1 ? framebuffer.samples
                                                     : (description.multisampleBuffers > 0 ? description.multisampleSamples : 0);
        EGLint renderable = EGL_OPENGL_BIT;
        if (es)
        {
            renderable = description.majorVersion >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;
            if (description.majorVersion <= 1) { renderable = EGL_OPENGL_ES_BIT; }
        }
        struct Candidate
        {
            int depth;
            int stencil;
            int samples;
        };
        std::vector<Candidate> candidates = {{depth, stencil, samples}};
        if (framebuffer.stencilBits == 0 && description.stencilBits == 0) { candidates.push_back({depth, 0, samples}); }
        if (framebuffer.depthBits == 0) { candidates.push_back({16, 0, samples}); }
        if (samples > 1)
        {
            const std::size_t count = candidates.size();
            for (std::size_t index = 0; index < count; ++index)
            {
                candidates.push_back({candidates[index].depth, candidates[index].stencil, 0});
            }
        }
        for (const Candidate& candidate : candidates)
        {
            const EGLint attributes[] = {
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, renderable,
                EGL_RED_SIZE, std::max(1, description.redBits),
                EGL_GREEN_SIZE, std::max(1, description.greenBits),
                EGL_BLUE_SIZE, std::max(1, description.blueBits),
                EGL_ALPHA_SIZE, std::max(0, description.alphaBits),
                EGL_DEPTH_SIZE, candidate.depth,
                EGL_STENCIL_SIZE, candidate.stencil,
                EGL_SAMPLE_BUFFERS, candidate.samples > 1 ? 1 : 0,
                EGL_SAMPLES, candidate.samples > 1 ? candidate.samples : 0,
                EGL_NONE,
            };
            EGLint count = 0;
            if (egl.chooseConfig(eglDisplay_, attributes, nullptr, 0, &count) != EGL_TRUE || count <= 0)
            {
                continue;
            }
            std::vector<EGLConfig> configs(static_cast<std::size_t>(count));
            egl.chooseConfig(eglDisplay_, attributes, configs.data(), count, &count);
            // EGL sorts deeper colour first, which would hand an 8-bit back buffer request a
            // 10-bit config: the first with exactly the requested channel sizes wins, and
            // only when there is none does EGL's order decide.
            EGLConfig chosen = configs.front();
            for (EGLConfig config : configs)
            {
                EGLint red = 0, green = 0, blue = 0, alpha = 0;
                egl.getConfigAttrib(eglDisplay_, config, EGL_RED_SIZE, &red);
                egl.getConfigAttrib(eglDisplay_, config, EGL_GREEN_SIZE, &green);
                egl.getConfigAttrib(eglDisplay_, config, EGL_BLUE_SIZE, &blue);
                egl.getConfigAttrib(eglDisplay_, config, EGL_ALPHA_SIZE, &alpha);
                if (red == description.redBits && green == description.greenBits && blue == description.blueBits &&
                    alpha == description.alphaBits)
                {
                    chosen = config;
                    break;
                }
            }
            record.config = chosen;
            record.es = es;
            GlContextDescription granted = description;
            const auto read = [&](const EGLint attribute, int& destination) {
                EGLint value = 0;
                if (egl.getConfigAttrib(eglDisplay_, chosen, attribute, &value) == EGL_TRUE)
                {
                    destination = value;
                }
            };
            read(EGL_RED_SIZE, granted.redBits);
            read(EGL_GREEN_SIZE, granted.greenBits);
            read(EGL_BLUE_SIZE, granted.blueBits);
            read(EGL_ALPHA_SIZE, granted.alphaBits);
            read(EGL_DEPTH_SIZE, granted.depthBits);
            read(EGL_STENCIL_SIZE, granted.stencilBits);
            read(EGL_SAMPLE_BUFFERS, granted.multisampleBuffers);
            read(EGL_SAMPLES, granted.multisampleSamples);
            // An EGL window surface is always double-buffered (EGL_RENDER_BUFFER BACK_BUFFER).
            granted.doubleBuffer = true;
            record.granted = granted;
            return;
        }
        throw PlatformException("WaylandGlContext::CreateContext",
                                "no EGL config matches the requested framebuffer (" + DescribeEglError(egl.getError()) +
                                    ")");
#else
        (void) record;
        (void) description;
        (void) es;
#endif
    }

    void WaylandGlContext::EnsureSurface(WindowRecord& record)
    {
#if defined(CNA_WAYLAND_HAVE_EGL)
        if (record.surface != EGL_NO_SURFACE)
        {
            return;
        }
        const EglLibrary& egl = Egl();
        const WindowSize size = record.window->GetPixelSize();
        record.eglWindow = egl.windowCreate(record.window->GetSurface(), std::max(1, size.width), std::max(1, size.height));
        if (record.eglWindow == nullptr)
        {
            throw PlatformException("WaylandGlContext::CreateContext", "wl_egl_window_create failed");
        }
        const EGLint opaque[] = {EGL_PRESENT_OPAQUE_EXT, EGL_TRUE, EGL_NONE};
        record.surface = egl.createWindowSurface(eglDisplay_, record.config,
                                                 reinterpret_cast<EGLNativeWindowType>(record.eglWindow),
                                                 presentOpaque_ ? opaque : nullptr);
        if (record.surface == EGL_NO_SURFACE)
        {
            const EGLint error = egl.getError();
            egl.windowDestroy(record.eglWindow);
            record.eglWindow = nullptr;
            throw PlatformException("WaylandGlContext::CreateContext",
                                    "eglCreateWindowSurface failed (" + DescribeEglError(error) + ")");
        }
        // Every new pixel size -- a configure, a scale change -- resizes the EGL window before the
        // next frame is drawn, so the buffer that frame commits already has it.
        wl_egl_window* eglWindow = record.eglWindow;
        record.window->SetPixelSizeListener([eglWindow](const int width, const int height) {
            Egl().windowResize(eglWindow, std::max(1, width), std::max(1, height), 0, 0);
        });
        record.pacer = std::make_unique<WaylandFramePacer>(connection_.GetDisplay(), record.window->GetSurface());
        record.intervalApplied = false;
#else
        (void) record;
#endif
    }

    GlContextHandle WaylandGlContext::CreateContext(const WindowId window, const GlContextDescription& description)
    {
#if !defined(CNA_WAYLAND_HAVE_EGL)
        (void) window;
        (void) description;
        throw PlatformNotSupportedException(PlatformCapability::OpenGlContext, "Wayland (built without EGL headers)");
#else
        if (!available_)
        {
            throw PlatformNotSupportedException(PlatformCapability::OpenGlContext,
                                                "Wayland (libEGL or libwayland-egl is not installed, or EGL has no "
                                                "Wayland platform)");
        }
        WindowRecord* record = Find(window);
        if (record == nullptr)
        {
            throw PlatformException("WaylandGlContext::CreateContext", "unknown window id");
        }
        if (record->window->GetRenderIntent() != WindowRenderIntent::OpenGl)
        {
            throw PlatformException("WaylandGlContext::CreateContext",
                                    "this window was not created with WindowRenderIntent::OpenGl");
        }
        if (!EnsureDisplay())
        {
            throw PlatformException("WaylandGlContext::CreateContext",
                                    "EGL could not initialise a display on this Wayland connection");
        }
        const EglLibrary& egl = Egl();
        const bool es = description.profile == GlProfile::Es;
        if (record->config == nullptr)
        {
            ChooseConfig(*record, description, es);
        }
        else if (record->es != es)
        {
            throw PlatformException("WaylandGlContext::CreateContext",
                                    "the window already has a context of the other API (OpenGL vs OpenGL ES)");
        }
        EnsureSurface(*record);

        egl.bindApi(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
        std::vector<EGLint> attributes = {
            EGL_CONTEXT_MAJOR_VERSION, description.majorVersion,
            EGL_CONTEXT_MINOR_VERSION, description.minorVersion,
        };
        if (!es && (description.majorVersion > 3 || (description.majorVersion == 3 && description.minorVersion >= 2)))
        {
            attributes.push_back(EGL_CONTEXT_OPENGL_PROFILE_MASK);
            attributes.push_back(description.profile == GlProfile::Compatibility
                                     ? EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT
                                     : EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT);
        }
        const std::size_t withoutRobustness = attributes.size();
        if (description.robustAccess)
        {
            attributes.push_back(EGL_CONTEXT_OPENGL_ROBUST_ACCESS);
            attributes.push_back(EGL_TRUE);
        }
        if (description.loseContextOnReset)
        {
            attributes.push_back(EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY);
            attributes.push_back(EGL_LOSE_CONTEXT_ON_RESET);
        }
        attributes.push_back(EGL_NONE);

        EGLContext context = egl.createContext(eglDisplay_, record->config, EGL_NO_CONTEXT, attributes.data());
        if (context == EGL_NO_CONTEXT && attributes.size() > withoutRobustness + 1)
        {
            // A driver without robustness still gives a context; GetContextAttributes says so.
            attributes.resize(withoutRobustness);
            attributes.push_back(EGL_NONE);
            context = egl.createContext(eglDisplay_, record->config, EGL_NO_CONTEXT, attributes.data());
        }
        if (context == EGL_NO_CONTEXT)
        {
            throw PlatformException("WaylandGlContext::CreateContext",
                                    "eglCreateContext refused " + std::string(es ? "OpenGL ES " : "OpenGL ") +
                                        std::to_string(description.majorVersion) + "." +
                                        std::to_string(description.minorVersion) + " (" +
                                        DescribeEglError(egl.getError()) + ")");
        }
        ContextRecord contextRecord;
        contextRecord.window = window;
        contextRecord.es = es;
        contextRecord.granted = record->granted;
        contextRecord.granted.majorVersion = description.majorVersion;
        contextRecord.granted.minorVersion = description.minorVersion;
        contextRecord.granted.profile = description.profile;
        contextRecord.granted.robustAccess = description.robustAccess && attributes.size() > withoutRobustness + 1;
        contextRecord.granted.loseContextOnReset = description.loseContextOnReset && attributes.size() > withoutRobustness + 1;
        contexts_[static_cast<GlContextHandle>(context)] = contextRecord;
        return static_cast<GlContextHandle>(context);
#endif
    }

    void WaylandGlContext::DestroyContext(const GlContextHandle context)
    {
#if defined(CNA_WAYLAND_HAVE_EGL)
        const auto found = contexts_.find(context);
        if (context == nullptr || found == contexts_.end())
        {
            return;
        }
        const EglLibrary& egl = Egl();
        if (currentContext_ == context)
        {
            egl.makeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            currentContext_ = nullptr;
            currentWindow_ = 0;
        }
        egl.bindApi(found->second.es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
        egl.destroyContext(eglDisplay_, static_cast<EGLContext>(context));
        contexts_.erase(found);
#else
        (void) context;
#endif
    }

    void WaylandGlContext::MakeCurrent(const WindowId window, const GlContextHandle context)
    {
#if !defined(CNA_WAYLAND_HAVE_EGL)
        (void) window;
        (void) context;
        throw PlatformNotSupportedException(PlatformCapability::OpenGlContext, "Wayland (built without EGL headers)");
#else
        const EglLibrary& egl = Egl();
        if (context == nullptr)
        {
            if (eglDisplay_ != nullptr)
            {
                egl.makeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            }
            currentWindow_ = 0;
            currentContext_ = nullptr;
            return;
        }
        const auto found = contexts_.find(context);
        if (found == contexts_.end())
        {
            throw PlatformException("WaylandGlContext::MakeCurrent", "the context was not created by this platform");
        }
        WindowRecord* record = Find(window);
        if (record == nullptr)
        {
            throw PlatformException("WaylandGlContext::MakeCurrent", "unknown window id");
        }
        if (record->config == nullptr)
        {
            ChooseConfig(*record, found->second.granted, found->second.es);
        }
        EnsureSurface(*record);
        egl.bindApi(found->second.es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
        if (egl.makeCurrent(eglDisplay_, record->surface, record->surface, static_cast<EGLContext>(context)) != EGL_TRUE)
        {
            throw PlatformException("WaylandGlContext::MakeCurrent",
                                    "eglMakeCurrent refused the binding (" + DescribeEglError(egl.getError()) + ")");
        }
        currentWindow_ = window;
        currentContext_ = context;
        if (!record->intervalApplied)
        {
            // EGL's own interval is 0 on every surface: the frame pacing is CNA's (D-23).
            egl.swapInterval(eglDisplay_, 0);
            record->intervalApplied = true;
        }
#endif
    }

    GlContextBinding WaylandGlContext::GetCurrentBinding() const
    {
        if (currentContext_ == nullptr)
        {
            return {};
        }
        return {currentWindow_, currentContext_};
    }

    void WaylandGlContext::SwapBuffers(const WindowId window)
    {
#if !defined(CNA_WAYLAND_HAVE_EGL)
        (void) window;
        throw PlatformNotSupportedException(PlatformCapability::OpenGlContext, "Wayland (built without EGL headers)");
#else
        WindowRecord* record = Find(window);
        if (record == nullptr)
        {
            throw PlatformException("WaylandGlContext::SwapBuffers", "unknown window id");
        }
        if (record->surface == EGL_NO_SURFACE)
        {
            return;
        }
        if (record->interval != 0 && record->pacer != nullptr)
        {
            (void) record->pacer->Wait(kFrameTimeout);
            record->pacer->Request();
        }
        if (Egl().swapBuffers(eglDisplay_, record->surface) == EGL_TRUE)
        {
            record->window->OnContentCommitted();
        }
        connection_.Flush();
#endif
    }

    bool WaylandGlContext::SetSwapInterval(const int interval)
    {
        WindowRecord* record = Find(currentWindow_);
        if (record == nullptr)
        {
            return false;
        }
        // 1 and adaptive (-1) are both "paced by the compositor": Wayland has no late-frame tearing
        // to adapt with.
        record->interval = interval == 0 ? 0 : 1;
        if (record->interval == 0 && record->pacer != nullptr)
        {
            record->pacer->Cancel();
        }
        return true;
    }

    void* WaylandGlContext::GetProcAddress(const std::string& name) const
    {
        return LoadEglProcAddress(name.c_str());
    }

    GlProcAddressLoader WaylandGlContext::GetProcAddressLoader() const
    {
        return &LoadEglProcAddress;
    }

    GlContextDescription WaylandGlContext::GetContextAttributes(const GlContextHandle context) const
    {
        const auto found = contexts_.find(context);
        return found != contexts_.end() ? found->second.granted : GlContextDescription{};
    }

    // --- WaylandVulkanSurface --------------------------------------------------------------------

    void WaylandVulkanSurface::RegisterWindow(const WindowId id, WaylandWindow* window)
    {
        if (window == nullptr)
        {
            windows_.erase(id);
            return;
        }
        windows_[id] = window;
    }

    std::vector<std::string> WaylandVulkanSurface::GetInstanceExtensions() const
    {
        return {"VK_KHR_surface", "VK_KHR_wayland_surface"};
    }

    VulkanSurfaceHandle WaylandVulkanSurface::CreateSurface(const VulkanInstanceHandle instance, const WindowId window)
    {
        if (instance == nullptr)
        {
            throw PlatformException("WaylandVulkanSurface::CreateSurface", "the instance is null");
        }
        const auto found = windows_.find(window);
        if (found == windows_.end() || found->second == nullptr)
        {
            throw PlatformException("WaylandVulkanSurface::CreateSurface", "unknown window id");
        }
        // VkWaylandSurfaceCreateInfoKHR, restated so that no Vulkan header is a build dependency:
        // its layout is frozen by the specification.
        struct VkWaylandSurfaceCreateInfo
        {
            int sType;
            const void* next;
            std::uint32_t flags;
            wl_display* display;
            wl_surface* surface;
        };
        constexpr int kWaylandSurfaceCreateInfoType = 1000006000;  // VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR
        using VkCreateWaylandSurfaceFn = int (*)(void*, const VkWaylandSurfaceCreateInfo*, const void*, std::uint64_t*);

        const Posix::VkGetInstanceProcAddrFn getInstanceProcAddr = Posix::ResolveVulkanProcAddr();
        if (getInstanceProcAddr == nullptr)
        {
            throw PlatformException("WaylandVulkanSurface::CreateSurface",
                                    "no Vulkan loader is reachable: vkGetInstanceProcAddr is absent from this process "
                                    "and libvulkan.so.1 could not be opened");
        }
        auto* create = reinterpret_cast<VkCreateWaylandSurfaceFn>(getInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR"));
        if (create == nullptr)
        {
            throw PlatformException("WaylandVulkanSurface::CreateSurface",
                                    "the instance does not expose vkCreateWaylandSurfaceKHR; VK_KHR_wayland_surface "
                                    "must be enabled when the instance is created");
        }
        VkWaylandSurfaceCreateInfo info{};
        info.sType = kWaylandSurfaceCreateInfoType;
        info.display = connection_.GetDisplay();
        info.surface = found->second->GetSurface();
        std::uint64_t surface = 0;
        const int result = create(instance, &info, nullptr, &surface);
        if (result != 0 || surface == 0)
        {
            throw PlatformException("WaylandVulkanSurface::CreateSurface",
                                    "vkCreateWaylandSurfaceKHR failed with VkResult " + std::to_string(result));
        }
        // What a Vulkan swapchain commits maps the window as a renderer's buffer does.
        found->second->OnContentCommitted();
        return surface;
    }

    void WaylandVulkanSurface::DestroySurface(const VulkanInstanceHandle instance, const VulkanSurfaceHandle surface)
    {
        if (instance == nullptr || surface == 0)
        {
            return;
        }
        using VkDestroySurfaceFn = void (*)(void*, std::uint64_t, const void*);
        const Posix::VkGetInstanceProcAddrFn getInstanceProcAddr = Posix::ResolveVulkanProcAddr();
        if (getInstanceProcAddr == nullptr)
        {
            return;
        }
        if (auto* destroy = reinterpret_cast<VkDestroySurfaceFn>(getInstanceProcAddr(instance, "vkDestroySurfaceKHR")))
        {
            destroy(instance, surface, nullptr);
        }
    }

    // --- WaylandSurfacePresenter ----------------------------------------------------------------

    WaylandSurfacePresenter::WaylandSurfacePresenter(WaylandConnection& connection, WaylandWindow& window)
        : connection_(connection), window_(window)
    {
        pacer_ = std::make_unique<WaylandFramePacer>(connection_.GetDisplay(), window_.GetSurface());
    }

    WaylandSurfacePresenter::~WaylandSurfacePresenter()
    {
        pacer_.reset();
        buffers_.clear();
        retired_.clear();
        connection_.Flush();
    }

    void WaylandSurfacePresenter::SetScaleMode(const PresentScaleMode mode, const PresentFilter filter)
    {
        scaleMode_ = mode;
        filter_ = filter;
    }

    bool WaylandSurfacePresenter::SetVSync(const bool enabled)
    {
        vsync_ = enabled;
        if (!vsync_)
        {
            pacer_->Cancel();
        }
        return true;
    }

    void WaylandSurfacePresenter::GetTargetSize(int& width, int& height) const
    {
        const WindowSize size = window_.GetPixelSize();
        width = size.width;
        height = size.height;
    }

    WaylandShmBuffer* WaylandSurfacePresenter::AcquireBuffer(const int width, const int height)
    {
        // Buffers of a size the window no longer has are retired, and destroyed once the
        // compositor lets go of them: destroying a buffer still in use leaves the surface's
        // contents undefined.
        for (auto it = buffers_.begin(); it != buffers_.end();)
        {
            if ((*it)->GetWidth() != width || (*it)->GetHeight() != height)
            {
                retired_.push_back(std::move(*it));
                it = buffers_.erase(it);
            }
            else
            {
                ++it;
            }
        }
        std::erase_if(retired_, [](const auto& buffer) { return !buffer->IsBusy(); });

        const auto findFree = [this]() -> WaylandShmBuffer* {
            for (const auto& buffer : buffers_)
            {
                if (!buffer->IsBusy())
                {
                    return buffer.get();
                }
            }
            return nullptr;
        };
        if (WaylandShmBuffer* free = findFree())
        {
            return free;
        }
        // Three is enough for any compositor: one on screen, one queued, one being drawn.
        if (buffers_.size() < 3)
        {
            auto created = WaylandShmBuffer::Create(connection_.GetGlobals().shm, width, height, WL_SHM_FORMAT_XRGB8888);
            if (created == nullptr)
            {
                throw PlatformException("WaylandSurfacePresenter::Present", "no shared memory for a frame buffer");
            }
            buffers_.push_back(std::move(created));
            return buffers_.back().get();
        }
        // All three still held: the compositor releases one as it takes the next, so waiting a
        // little is what a full ring means; a compositor that holds all of them past the deadline
        // is not drawing this window, and the frame is dropped.
        (void) connection_.DispatchUntil([&findFree] { return findFree() != nullptr; }, kFrameTimeout);
        return findFree();
    }

    void WaylandSurfacePresenter::Present(const SurfaceFrame& frame)
    {
        const int stride = Common::ValidateSurfaceFrame(frame, "WaylandSurfacePresenter::Present");
        const WindowSize target = window_.GetPixelSize();
        if (target.width <= 0 || target.height <= 0)
        {
            return;
        }
        if (vsync_)
        {
            (void) pacer_->Wait(kFrameTimeout);
        }
        WaylandShmBuffer* buffer = AcquireBuffer(target.width, target.height);
        if (buffer == nullptr)
        {
            return;
        }

        std::uint8_t* pixels = buffer->GetPixels();
        const int bufferStride = buffer->GetStride();
        const Common::PresentRect rect =
            Common::ComputePresentRect(scaleMode_, frame.width, frame.height, target.width, target.height);
        // The bars a letterboxed or centred frame leaves are black, not whatever the buffer held
        // three frames ago.
        const bool covers = rect.x <= 0 && rect.y <= 0 && rect.x + rect.width >= target.width &&
                            rect.y + rect.height >= target.height;
        if (!covers)
        {
            for (int y = 0; y < target.height; ++y)
            {
                std::memset(pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(bufferStride), 0,
                            static_cast<std::size_t>(target.width) * 4u);
            }
        }
        Common::ScaleSurfaceFrame(
            frame, stride, rect.width, rect.height, filter_, columns_,
            [&](const int x, const int y, const unsigned int red, const unsigned int green, const unsigned int blue) {
                const int tx = rect.x + x;
                const int ty = rect.y + y;
                if (tx < 0 || ty < 0 || tx >= target.width || ty >= target.height)
                {
                    return;
                }
                // XRGB8888 is 0xXXRRGGBB in the machine's word (wl_shm formats are little-endian
                // DRM fourcc codes, and every Wayland machine CNA runs on is little-endian).
                const std::uint32_t word = 0xFF000000u | (red << 16) | (green << 8) | blue;
                std::memcpy(pixels + static_cast<std::size_t>(ty) * static_cast<std::size_t>(bufferStride) +
                                static_cast<std::size_t>(tx) * 4u,
                            &word, sizeof(word));
            });

        wl_surface* surface = window_.GetSurface();
        wl_surface_attach(surface, buffer->GetBuffer(), 0, 0);
        if (connection_.GetGlobals().compositorVersion >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
        {
            wl_surface_damage_buffer(surface, 0, 0, target.width, target.height);
        }
        else
        {
            wl_surface_damage(surface, 0, 0, INT32_MAX, INT32_MAX);
        }
        if (vsync_)
        {
            pacer_->Request();
        }
        wl_surface_commit(surface);
        buffer->MarkAttached();
        window_.OnContentCommitted();
        connection_.Flush();
    }

} // namespace CNA::Platform::Wayland
