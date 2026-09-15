// SPDX-License-Identifier: MS-PL

#include "X11GraphicsServices.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "../Common/SurfaceFrameFitting.hpp"
#include "../Common/SurfaceFrameValidation.hpp"
#include "../Posix/VulkanLoader.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11Window.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <type_traits>

namespace CNA::Platform::X11 {

    namespace {

#if defined(CNA_X11_HAVE_GLX)
        using GlXCreateContextAttribsArb = GLXContext (*)(Display*, GLXFBConfig, GLXContext, XBool,
                                                          const int*);
        using GlXSwapIntervalExt = void (*)(Display*, GLXDrawable, int);
        using GlXSwapIntervalMesa = int (*)(int);
        using GlXSwapIntervalSgi = int (*)(int);

        /// The GLX 1.3 entry points this backend calls, resolved from the GL implementation at
        /// run time -- the way the Vulkan surface reaches the Vulkan loader -- rather than linked.
        ///
        /// Linking libGLX made every X11 build need a GL implementation installed just to start,
        /// HEADLESS and SOFTWARE ones included, and put a GL library into the link closure of
        /// every module above the platform, which the native-SDK-free module gates forbid
        /// (plans/plan_native_platform_validation.md NPV-0121). The GLX header still types every
        /// pointer, so a signature mismatch is a compile error rather than a crash.
        struct GlxApi
        {
            bool loaded = false;
            decltype(&::glXQueryExtension) QueryExtension = nullptr;
            decltype(&::glXQueryVersion) QueryVersion = nullptr;
            decltype(&::glXQueryExtensionsString) QueryExtensionsString = nullptr;
            decltype(&::glXChooseFBConfig) ChooseFBConfig = nullptr;
            decltype(&::glXGetVisualFromFBConfig) GetVisualFromFBConfig = nullptr;
            decltype(&::glXGetFBConfigAttrib) GetFBConfigAttrib = nullptr;
            decltype(&::glXCreateNewContext) CreateNewContext = nullptr;
            decltype(&::glXDestroyContext) DestroyContext = nullptr;
            decltype(&::glXMakeCurrent) MakeCurrent = nullptr;
            decltype(&::glXGetCurrentContext) GetCurrentContext = nullptr;
            decltype(&::glXGetCurrentDrawable) GetCurrentDrawable = nullptr;
            decltype(&::glXSwapBuffers) SwapBuffers = nullptr;
            decltype(&::glXGetProcAddressARB) GetProcAddressARB = nullptr;
        };

        const GlxApi& Glx()
        {
            static const GlxApi api = [] {
                GlxApi resolved;
                // libGLX.so.0 is libglvnd's GLX dispatcher, what a current distribution ships;
                // libGL.so.1 is the classic library that carries the same symbols. RTLD_GLOBAL
                // as SDL loads GL: some drivers resolve GL symbols against the global scope. The
                // library is never closed -- GL drivers do not survive being unloaded.
                void* library = dlopen("libGLX.so.0", RTLD_NOW | RTLD_GLOBAL);
                if (library == nullptr)
                {
                    library = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
                }
                if (library == nullptr)
                {
                    return resolved;
                }
                const auto resolve = [library](auto& entry, const char* name) {
                    entry = reinterpret_cast<std::remove_reference_t<decltype(entry)>>(
                        dlsym(library, name));
                    return entry != nullptr;
                };
                resolved.loaded =
                    resolve(resolved.QueryExtension, "glXQueryExtension") &&
                    resolve(resolved.QueryVersion, "glXQueryVersion") &&
                    resolve(resolved.QueryExtensionsString, "glXQueryExtensionsString") &&
                    resolve(resolved.ChooseFBConfig, "glXChooseFBConfig") &&
                    resolve(resolved.GetVisualFromFBConfig, "glXGetVisualFromFBConfig") &&
                    resolve(resolved.GetFBConfigAttrib, "glXGetFBConfigAttrib") &&
                    resolve(resolved.CreateNewContext, "glXCreateNewContext") &&
                    resolve(resolved.DestroyContext, "glXDestroyContext") &&
                    resolve(resolved.MakeCurrent, "glXMakeCurrent") &&
                    resolve(resolved.GetCurrentContext, "glXGetCurrentContext") &&
                    resolve(resolved.GetCurrentDrawable, "glXGetCurrentDrawable") &&
                    resolve(resolved.SwapBuffers, "glXSwapBuffers") &&
                    resolve(resolved.GetProcAddressARB, "glXGetProcAddressARB");
                return resolved;
            }();
            return api;
        }

        void* LoadGlxProcAddress(const char* name)
        {
            if (!Glx().loaded)
            {
                return nullptr;
            }
            return reinterpret_cast<void*>(
                Glx().GetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
        }

        bool HasGlxExtension(Display* display, const int screen, const char* extension)
        {
            const char* extensions = Glx().QueryExtensionsString(display, screen);
            if (extensions == nullptr || extension == nullptr)
            {
                return false;
            }
            // Substring matching is wrong here: "GLX_EXT_swap_control" is a prefix of
            // "GLX_EXT_swap_control_tear", and a plain strstr would report the first as present
            // on a driver that only has the second. The match has to be whole-token.
            const std::size_t length = std::strlen(extension);
            const char* cursor = extensions;
            while ((cursor = std::strstr(cursor, extension)) != nullptr)
            {
                const bool startsToken = cursor == extensions || cursor[-1] == ' ';
                const bool endsToken = cursor[length] == '\0' || cursor[length] == ' ';
                if (startsToken && endsToken)
                {
                    return true;
                }
                cursor += length;
            }
            return false;
        }
#else
        void* LoadGlxProcAddress(const char*) { return nullptr; }
#endif


    } // namespace

    // --- X11GlContext ---------------------------------------------------------------------------

    X11GlContext::X11GlContext(X11Connection& connection) : connection_(connection)
    {
#if defined(CNA_X11_HAVE_GLX)
        int errorBase = 0;
        int eventBase = 0;
        // No GL implementation installed is an answer, not an error: the capability is false.
        if (Glx().loaded &&
            Glx().QueryExtension(connection_.GetDisplay(), &errorBase, &eventBase) == True)
        {
            int major = 0;
            int minor = 0;
            // 1.3 is the floor because FBConfigs arrived with it. Everything below that offers
            // only XVisualInfo-based context creation, with no way to ask for a core profile.
            if (Glx().QueryVersion(connection_.GetDisplay(), &major, &minor) == True &&
                (major > 1 || (major == 1 && minor >= 3)))
            {
                available_ = true;
            }
        }
#endif
    }

    X11GlContext::~X11GlContext()
    {
#if defined(CNA_X11_HAVE_GLX)
        Display* display = connection_.GetDisplay();
        for (const auto& [handle, record] : contexts_)
        {
            (void) handle;
            if (record.glxContext != nullptr)
            {
                Glx().DestroyContext(display, static_cast<GLXContext>(record.glxContext));
            }
        }
#endif
        contexts_.clear();
    }

    void X11GlContext::RegisterWindow(const WindowId id, X11Window* window)
    {
        if (window == nullptr)
        {
            windows_.erase(id);
            return;
        }
        windows_[id] = window;
    }

    X11Window* X11GlContext::FindWindow(const WindowId id) const
    {
        const auto found = windows_.find(id);
        return found != windows_.end() ? found->second : nullptr;
    }

    X11GlVisual X11GlContext::ChooseVisual(const int depthBits, const int stencilBits,
                                           const bool doubleBuffered, const int samples) const
    {
        X11GlVisual chosen;
#if defined(CNA_X11_HAVE_GLX)
        if (!available_)
        {
            return chosen;
        }
        // "Zero leaves the platform default" (WindowDescription), and this platform's default is
        // what a game's back buffer needs: double-buffered, with XNA's Depth24Stencil8 -- more than
        // SDL's own default (double-buffered, 16-bit depth), never less. The renderers that state
        // their framebuffer only when they create the context (EasyGL) get it from here, because on
        // X11 the window's visual, and every buffer with it, is fixed when the window is made
        // (plans/plan_x11.md X11-0172). A server that cannot give that much is asked for less, one
        // step at a time; what a caller asked for explicitly is a minimum, never lowered. Keeping a
        // depth buffer wins over keeping multisampling.
        struct Candidate
        {
            int depth;
            int stencil;
            bool doubleBuffer;
        };
        std::vector<Candidate> candidates;
        const int depth = depthBits > 0 ? depthBits : 24;
        const int stencil = stencilBits > 0 ? stencilBits : 8;
        candidates.push_back({depth, stencil, true});
        if (stencilBits == 0)
        {
            candidates.push_back({depth, 0, true});
        }
        if (depthBits == 0)
        {
            candidates.push_back({16, stencilBits, true});
            candidates.push_back({0, stencilBits, true});
        }
        if (!doubleBuffered)
        {
            // Double buffering was not required: a single-buffered visual, but only as a last resort.
            const std::size_t doubled = candidates.size();
            for (std::size_t index = 0; index < doubled; ++index)
            {
                candidates.push_back({candidates[index].depth, candidates[index].stencil, false});
            }
        }
        for (const Candidate& candidate : candidates)
        {
            chosen = ChooseExactVisual(candidate.depth, candidate.stencil, candidate.doubleBuffer, samples);
            if (chosen.visual == nullptr && samples > 1)
            {
                // A driver that cannot give the requested sample count refuses the whole config
                // rather than degrading, and a window without antialiasing is better than none.
                chosen = ChooseExactVisual(candidate.depth, candidate.stencil, candidate.doubleBuffer, 0);
            }
            if (chosen.visual != nullptr)
            {
                return chosen;
            }
        }
#else
        (void) depthBits;
        (void) stencilBits;
        (void) doubleBuffered;
        (void) samples;
#endif
        return chosen;
    }

    X11GlVisual X11GlContext::ChooseExactVisual(const int depthBits, const int stencilBits,
                                                const bool doubleBuffered, const int samples) const
    {
        X11GlVisual chosen;
#if defined(CNA_X11_HAVE_GLX)
        Display* display = connection_.GetDisplay();
        const int screen = connection_.GetScreen();

        std::vector<int> attributes = {
            GLX_X_RENDERABLE,  True,
            GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            GLX_RENDER_TYPE,   GLX_RGBA_BIT,
            GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
            GLX_RED_SIZE,      8,
            GLX_GREEN_SIZE,    8,
            GLX_BLUE_SIZE,     8,
            GLX_ALPHA_SIZE,    8,
            GLX_DOUBLEBUFFER,  doubleBuffered ? True : False,
        };
        if (depthBits > 0)
        {
            attributes.push_back(GLX_DEPTH_SIZE);
            attributes.push_back(depthBits);
        }
        if (stencilBits > 0)
        {
            attributes.push_back(GLX_STENCIL_SIZE);
            attributes.push_back(stencilBits);
        }
        if (samples > 1)
        {
            attributes.push_back(GLX_SAMPLE_BUFFERS);
            attributes.push_back(1);
            attributes.push_back(GLX_SAMPLES);
            attributes.push_back(samples);
        }
        attributes.push_back(0);

        int configCount = 0;
        GLXFBConfig* configs =
            Glx().ChooseFBConfig(display, screen, attributes.data(), &configCount);
        if (configs == nullptr || configCount == 0)
        {
            if (configs != nullptr) { XFree(configs); }
            return chosen;
        }

        XVisualInfo* info = Glx().GetVisualFromFBConfig(display, configs[0]);
        if (info != nullptr)
        {
            chosen.visual = info->visual;
            chosen.depth = info->depth;
            // `GLXFBConfig` is itself an opaque POINTER (`struct __GLXFBConfigRec*`), so the
            // config VALUE goes into the void*, not the address of the array slot holding it.
            // Getting that wrong is not a compile error -- both are pointers -- and it produced a
            // GLXBadFBConfig from glXCreateContextAttribsARB with a perfectly valid config, which
            // is what the real-context test caught.
            chosen.fbConfig = static_cast<void*>(configs[0]);
            XFree(info);
        }
        // The ARRAY is XMalloc'd and must be freed; the GLXFBConfig values inside it stay valid
        // for the lifetime of the display, which is what makes carrying one on the window sound.
        XFree(configs);
#else
        (void) depthBits;
        (void) stencilBits;
        (void) doubleBuffered;
        (void) samples;
#endif
        return chosen;
    }

    GlContextHandle X11GlContext::CreateContext(const WindowId window,
                                                 const GlContextDescription& description)
    {
#if !defined(CNA_X11_HAVE_GLX)
        (void) window;
        (void) description;
        throw PlatformNotSupportedException(PlatformCapability::OpenGlContext,
                                            "X11 (built without GLX)");
#else
        if (!available_)
        {
            throw PlatformNotSupportedException(PlatformCapability::OpenGlContext,
                                                "X11 (this server does not provide GLX 1.3)");
        }
        X11Window* target = FindWindow(window);
        if (target == nullptr)
        {
            throw PlatformException("X11GlContext::CreateContext", "unknown window id");
        }
        auto fbConfig = static_cast<GLXFBConfig>(target->GetGlFbConfig());
        if (fbConfig == nullptr)
        {
            // The window was created without WindowRenderIntent::OpenGl, so its visual was not
            // chosen for GL and no context can be made current on it. Saying so here is far
            // better than the BadMatch the driver would raise several calls later.
            throw PlatformException(
                "X11GlContext::CreateContext",
                "this window was not created with WindowRenderIntent::OpenGl, so its X visual is "
                "not GL-capable; an X window's visual is fixed at creation and cannot be changed");
        }

        Display* display = connection_.GetDisplay();
        auto* createContextAttribs = reinterpret_cast<GlXCreateContextAttribsArb>(
            LoadGlxProcAddress("glXCreateContextAttribsARB"));

        GLXContext context = nullptr;
        X11ErrorTrap trap(display);
        if (createContextAttribs != nullptr)
        {
            int profileMask = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;
            if (description.profile == GlProfile::Compatibility)
            {
                profileMask = GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
            }

            std::vector<int> attributes = {
                GLX_CONTEXT_MAJOR_VERSION_ARB, description.majorVersion,
                GLX_CONTEXT_MINOR_VERSION_ARB, description.minorVersion,
            };
            // A profile mask is meaningful only from GL 3.2; asking for one on a 2.1 context is
            // an error rather than a no-op, and GLX_CONTEXT_ES_PROFILE_BIT_EXT needs its own
            // extension, so an ES request falls through to a plain context rather than pretending.
            if (description.profile != GlProfile::Es &&
                (description.majorVersion > 3 ||
                 (description.majorVersion == 3 && description.minorVersion >= 2)))
            {
                attributes.push_back(GLX_CONTEXT_PROFILE_MASK_ARB);
                attributes.push_back(profileMask);
            }
            int flags = 0;
            if (description.robustAccess)
            {
                flags |= GLX_CONTEXT_ROBUST_ACCESS_BIT_ARB;
            }
            if (flags != 0)
            {
                attributes.push_back(GLX_CONTEXT_FLAGS_ARB);
                attributes.push_back(flags);
            }
            attributes.push_back(0);

            context = createContextAttribs(display, fbConfig, nullptr, kXTrue, attributes.data());
        }
        if (context == nullptr)
        {
            // The driver refused the requested version or the extension is absent. A plain
            // GLX 1.3 context is the honest fallback -- GetContextAttributes reports what was
            // actually granted, so a caller that needed 4.5 still finds out.
            context = Glx().CreateNewContext(display, fbConfig, GLX_RGBA_TYPE, nullptr, kXTrue);
        }
        trap.Sync();
        if (context == nullptr)
        {
            throw PlatformException("X11GlContext::CreateContext",
                                    trap.HasError() ? trap.Describe()
                                                    : std::string("GLX refused the context"));
        }

        ContextRecord record;
        record.glxContext = context;
        record.granted = description;
        int value = 0;
        const auto readConfig = [&](const int attribute, int& destination) {
            if (Glx().GetFBConfigAttrib(display, fbConfig, attribute, &value) == 0)
            {
                destination = value;
            }
        };
        readConfig(GLX_RED_SIZE, record.granted.redBits);
        readConfig(GLX_GREEN_SIZE, record.granted.greenBits);
        readConfig(GLX_BLUE_SIZE, record.granted.blueBits);
        readConfig(GLX_ALPHA_SIZE, record.granted.alphaBits);
        readConfig(GLX_DEPTH_SIZE, record.granted.depthBits);
        readConfig(GLX_STENCIL_SIZE, record.granted.stencilBits);
        readConfig(GLX_SAMPLE_BUFFERS, record.granted.multisampleBuffers);
        readConfig(GLX_SAMPLES, record.granted.multisampleSamples);
        if (Glx().GetFBConfigAttrib(display, fbConfig, GLX_DOUBLEBUFFER, &value) == 0)
        {
            record.granted.doubleBuffer = value != 0;
        }
        if (record.granted.depthBits < description.depthBits || record.granted.stencilBits < description.stencilBits ||
            (description.doubleBuffer && !record.granted.doubleBuffer))
        {
            // The window's visual was fixed when the window was made; a context cannot add a depth
            // buffer to it. Said out loud, because the symptom -- walls drawn through walls -- does
            // not point here (plans/plan_x11.md X11-0172).
            std::fprintf(stderr,
                         "[CNA][X11] The GL window's visual has %d depth bits, %d stencil bits and is %s, "
                         "but the context asked for %d, %d and %s. A window's visual is fixed when it is "
                         "made: state the framebuffer in WindowDescription::openGlFramebuffer.\n",
                         record.granted.depthBits, record.granted.stencilBits,
                         record.granted.doubleBuffer ? "double-buffered" : "single-buffered", description.depthBits,
                         description.stencilBits, description.doubleBuffer ? "double buffering" : "either");
            std::fflush(stderr);
        }

        const auto handle = static_cast<GlContextHandle>(context);
        contexts_[handle] = record;
        return handle;
#endif
    }

    void X11GlContext::DestroyContext(const GlContextHandle context)
    {
#if defined(CNA_X11_HAVE_GLX)
        if (context == nullptr)
        {
            return;
        }
        const auto found = contexts_.find(context);
        if (found == contexts_.end())
        {
            return;
        }
        Display* display = connection_.GetDisplay();
        if (Glx().GetCurrentContext() == static_cast<GLXContext>(found->second.glxContext))
        {
            // Destroying the current context leaves GLX with a current-but-freed binding, and the
            // next glXMakeCurrent on that thread is undefined. Unbinding first is not optional.
            Glx().MakeCurrent(display, kNone, nullptr);
            currentWindow_ = 0;
        }
        Glx().DestroyContext(display, static_cast<GLXContext>(found->second.glxContext));
        contexts_.erase(found);
#else
        (void) context;
#endif
    }

    void X11GlContext::MakeCurrent(const WindowId window, const GlContextHandle context)
    {
#if !defined(CNA_X11_HAVE_GLX)
        (void) window;
        (void) context;
        throw PlatformNotSupportedException(PlatformCapability::OpenGlContext,
                                            "X11 (built without GLX)");
#else
        Display* display = connection_.GetDisplay();
        if (context == nullptr)
        {
            Glx().MakeCurrent(display, kNone, nullptr);
            currentWindow_ = 0;
            return;
        }
        X11Window* target = FindWindow(window);
        if (target == nullptr)
        {
            throw PlatformException("X11GlContext::MakeCurrent", "unknown window id");
        }
        const auto found = contexts_.find(context);
        if (found == contexts_.end())
        {
            throw PlatformException("X11GlContext::MakeCurrent",
                                    "the context was not created by this platform");
        }
        X11ErrorTrap trap(display);
        const XBool ok = Glx().MakeCurrent(display, target->GetXWindow(),
                                       static_cast<GLXContext>(found->second.glxContext));
        trap.Sync();
        if (ok != kXTrue)
        {
            throw PlatformException("X11GlContext::MakeCurrent",
                                    trap.HasError()
                                        ? trap.Describe()
                                        : std::string("glXMakeCurrent refused the binding"));
        }
        currentWindow_ = window;
#endif
    }

    GlContextBinding X11GlContext::GetCurrentBinding() const
    {
#if defined(CNA_X11_HAVE_GLX)
        GLXContext context = Glx().GetCurrentContext();
        if (context == nullptr)
        {
            return {};
        }
        return {currentWindow_, static_cast<GlContextHandle>(context)};
#else
        return {};
#endif
    }

    void X11GlContext::SwapBuffers(const WindowId window)
    {
#if !defined(CNA_X11_HAVE_GLX)
        (void) window;
        throw PlatformNotSupportedException(PlatformCapability::OpenGlContext,
                                            "X11 (built without GLX)");
#else
        X11Window* target = FindWindow(window);
        if (target == nullptr)
        {
            throw PlatformException("X11GlContext::SwapBuffers", "unknown window id");
        }
        Glx().SwapBuffers(connection_.GetDisplay(), target->GetXWindow());
#endif
    }

    bool X11GlContext::SetSwapInterval(const int interval)
    {
#if !defined(CNA_X11_HAVE_GLX)
        (void) interval;
        return false;
#else
        Display* display = connection_.GetDisplay();
        const int screen = connection_.GetScreen();

        // Three extensions do the same job and no driver has all three. EXT is per drawable and
        // is the only one that supports adaptive (-1) sync; MESA and SGI are per context, and SGI
        // additionally refuses interval 0, which is why it is tried last.
        if (HasGlxExtension(display, screen, "GLX_EXT_swap_control"))
        {
            if (auto* swap = reinterpret_cast<GlXSwapIntervalExt>(
                    LoadGlxProcAddress("glXSwapIntervalEXT")))
            {
                const GLXDrawable drawable = Glx().GetCurrentDrawable();
                if (drawable != kNone)
                {
                    if (interval < 0 &&
                        !HasGlxExtension(display, screen, "GLX_EXT_swap_control_tear"))
                    {
                        return false;
                    }
                    swap(display, drawable, interval);
                    return true;
                }
            }
        }
        if (interval >= 0 && HasGlxExtension(display, screen, "GLX_MESA_swap_control"))
        {
            if (auto* swap = reinterpret_cast<GlXSwapIntervalMesa>(
                    LoadGlxProcAddress("glXSwapIntervalMESA")))
            {
                return swap(interval) == 0;
            }
        }
        if (interval > 0 && HasGlxExtension(display, screen, "GLX_SGI_swap_control"))
        {
            if (auto* swap = reinterpret_cast<GlXSwapIntervalSgi>(
                    LoadGlxProcAddress("glXSwapIntervalSGI")))
            {
                return swap(interval) == 0;
            }
        }
        return false;
#endif
    }

    void* X11GlContext::GetProcAddress(const std::string& name) const
    {
        return LoadGlxProcAddress(name.c_str());
    }

    GlProcAddressLoader X11GlContext::GetProcAddressLoader() const
    {
        return &LoadGlxProcAddress;
    }

    GlContextDescription X11GlContext::GetContextAttributes(const GlContextHandle context) const
    {
        const auto found = contexts_.find(context);
        if (found == contexts_.end())
        {
            return {};
        }
        return found->second.granted;
    }

    // --- X11VulkanSurface -----------------------------------------------------------------------

    X11VulkanSurface::X11VulkanSurface(X11Connection& connection) : connection_(connection) {}

    std::vector<std::string> X11VulkanSurface::GetInstanceExtensions() const
    {
        return {"VK_KHR_surface", "VK_KHR_xlib_surface"};
    }

    void X11VulkanSurface::RegisterWindow(const WindowId id, X11Window* window)
    {
        if (window == nullptr)
        {
            windows_.erase(id);
            return;
        }
        windows_[id] = window;
    }

    VulkanSurfaceHandle X11VulkanSurface::CreateSurface(const VulkanInstanceHandle instance,
                                                        const WindowId window)
    {
        if (instance == nullptr)
        {
            throw PlatformException("X11VulkanSurface::CreateSurface", "the instance is null");
        }
        const auto found = windows_.find(window);
        if (found == windows_.end() || found->second == nullptr)
        {
            throw PlatformException("X11VulkanSurface::CreateSurface", "unknown window id");
        }

        // The Vulkan declarations this needs, restated locally. Including <vulkan/vulkan_xlib.h>
        // would be the obvious alternative, but it would make the Vulkan headers a hard build
        // dependency of every X11 build -- and the only thing needed from them is one struct
        // whose layout is frozen by the specification and one function pointer type.
        struct VkXlibSurfaceCreateInfo
        {
            int sType;
            const void* next;
            std::uint32_t flags;
            Display* dpy;
            ::Window window;
        };
        // VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR.
        constexpr int kXlibSurfaceCreateInfoType = 1000004000;

        using VkCreateXlibSurfaceFn = int (*)(void*, const VkXlibSurfaceCreateInfo*, const void*,
                                              std::uint64_t*);

        Posix::VkGetInstanceProcAddrFn getInstanceProcAddr = Posix::ResolveVulkanProcAddr();
        if (getInstanceProcAddr == nullptr)
        {
            throw PlatformException(
                "X11VulkanSurface::CreateSurface",
                "no Vulkan loader is reachable: vkGetInstanceProcAddr is absent from this process "
                "and libvulkan.so.1 could not be opened");
        }
        auto* createSurface = reinterpret_cast<VkCreateXlibSurfaceFn>(
            getInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
        if (createSurface == nullptr)
        {
            throw PlatformException(
                "X11VulkanSurface::CreateSurface",
                "the instance does not expose vkCreateXlibSurfaceKHR; VK_KHR_xlib_surface must be "
                "enabled when the instance is created");
        }

        VkXlibSurfaceCreateInfo info{};
        info.sType = kXlibSurfaceCreateInfoType;
        info.dpy = connection_.GetDisplay();
        info.window = found->second->GetXWindow();

        std::uint64_t surface = 0;
        const int result = createSurface(instance, &info, nullptr, &surface);
        if (result != 0 || surface == 0)
        {
            throw PlatformException("X11VulkanSurface::CreateSurface",
                                    "vkCreateXlibSurfaceKHR failed with VkResult " +
                                        std::to_string(result));
        }
        return surface;
    }

    void X11VulkanSurface::DestroySurface(const VulkanInstanceHandle instance,
                                           const VulkanSurfaceHandle surface)
    {
        if (instance == nullptr || surface == 0)
        {
            return;
        }
        using VkDestroySurfaceFn = void (*)(void*, std::uint64_t, const void*);
        Posix::VkGetInstanceProcAddrFn getInstanceProcAddr = Posix::ResolveVulkanProcAddr();
        if (getInstanceProcAddr == nullptr)
        {
            return;
        }
        if (auto* destroySurface = reinterpret_cast<VkDestroySurfaceFn>(
                getInstanceProcAddr(instance, "vkDestroySurfaceKHR")))
        {
            destroySurface(instance, surface, nullptr);
        }
    }

    // --- X11PixelPacker -------------------------------------------------------------------------

    X11PixelPacker::X11PixelPacker(const unsigned long redMask, const unsigned long greenMask,
                                   const unsigned long blueMask)
        : red_(Describe(redMask)), green_(Describe(greenMask)), blue_(Describe(blueMask))
    {
    }

    X11PixelPacker::Channel X11PixelPacker::Describe(const unsigned long mask)
    {
        Channel channel;
        channel.mask = mask;
        if (mask == 0)
        {
            return channel;
        }
        unsigned long probe = mask;
        while ((probe & 1u) == 0)
        {
            probe >>= 1;
            ++channel.shift;
        }
        while ((probe & 1u) != 0)
        {
            probe >>= 1;
            ++channel.bits;
        }
        return channel;
    }

    unsigned long X11PixelPacker::Place(const Channel& channel, const unsigned int value)
    {
        if (channel.mask == 0)
        {
            return 0;
        }
        const unsigned long scaled = channel.bits >= 8
                                         ? (static_cast<unsigned long>(value) << (channel.bits - 8))
                                         : (value >> (8 - channel.bits));
        return (scaled << channel.shift) & channel.mask;
    }

    unsigned long X11PixelPacker::Pack(const unsigned int red, const unsigned int green,
                                       const unsigned int blue) const
    {
        return Place(red_, red) | Place(green_, green) | Place(blue_, blue);
    }

    // --- X11SurfacePresenter --------------------------------------------------------------------

    X11SurfacePresenter::X11SurfacePresenter(X11Window& window) : window_(window)
    {
        Display* display = window_.GetConnection().GetDisplay();
        XGCValues values{};
        graphicsContext_ = XCreateGC(display, window_.GetXWindow(), 0, &values);
        if (graphicsContext_ == nullptr)
        {
            throw PlatformException("X11SurfacePresenter",
                                    "the X server refused a graphics context for this window");
        }
    }

    X11SurfacePresenter::~X11SurfacePresenter()
    {
        ReleaseImage();
        if (graphicsContext_ != nullptr)
        {
            XFreeGC(window_.GetConnection().GetDisplay(), graphicsContext_);
            graphicsContext_ = nullptr;
        }
    }

    void X11SurfacePresenter::SetScaleMode(const PresentScaleMode mode, const PresentFilter filter)
    {
        scaleMode_ = mode;
        filter_ = filter;
    }

    bool X11SurfacePresenter::SetVSync(const bool enabled)
    {
        (void) enabled;
        // XPutImage has no relationship to the vertical blank, and the Present extension's
        // synchronised path is a different presentation model entirely. Reporting false is the
        // contract's answer for "cannot honour it"; presentation continues unsynchronised.
        return false;
    }

    void X11SurfacePresenter::GetTargetSize(int& width, int& height) const
    {
        const WindowSize size = window_.GetPixelSize();
        width = size.width;
        height = size.height;
    }

    void X11SurfacePresenter::ReleaseImage()
    {
        Display* display = window_.GetConnection().GetDisplay();
#if defined(CNA_X11_HAVE_XSHM)
        if (usesSharedMemory_ && image_ != nullptr)
        {
            XShmDetach(display, &sharedMemory_);
            XDestroyImage(image_);
            image_ = nullptr;
            if (sharedMemory_.shmaddr != nullptr)
            {
                shmdt(sharedMemory_.shmaddr);
                sharedMemory_.shmaddr = nullptr;
            }
            if (sharedMemory_.shmid >= 0)
            {
                shmctl(sharedMemory_.shmid, IPC_RMID, nullptr);
                sharedMemory_.shmid = -1;
            }
            usesSharedMemory_ = false;
            return;
        }
#endif
        if (image_ != nullptr)
        {
            // XDestroyImage frees the data pointer too, so the buffer must be released from the
            // image rather than double-freed with the vector.
            image_->data = nullptr;
            XDestroyImage(image_);
            image_ = nullptr;
        }
        pixels_.clear();
        pixels_.shrink_to_fit();
        (void) display;
    }

    void X11SurfacePresenter::EnsureImage(const int width, const int height)
    {
        if (image_ != nullptr && imageWidth_ == width && imageHeight_ == height)
        {
            return;
        }
        ReleaseImage();

        Display* display = window_.GetConnection().GetDisplay();
        Visual* visual = window_.GetVisual();
        const int depth = window_.GetDepth();

#if defined(CNA_X11_HAVE_XSHM)
        // Shared memory only helps when the server is on this machine, and XShmAttach on a remote
        // display fails with a protocol error rather than gracefully. XShmQueryExtension answers
        // the first question; the error trap catches the second.
        if (XShmQueryExtension(display) == True)
        {
            sharedMemory_ = XShmSegmentInfo{};
            sharedMemory_.shmid = -1;
            XImage* candidate = XShmCreateImage(display, visual, static_cast<unsigned int>(depth),
                                                ZPixmap, nullptr, &sharedMemory_,
                                                static_cast<unsigned int>(width),
                                                static_cast<unsigned int>(height));
            if (candidate != nullptr)
            {
                const std::size_t bytes = static_cast<std::size_t>(candidate->bytes_per_line) *
                                          static_cast<std::size_t>(height);
                sharedMemory_.shmid = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
                if (sharedMemory_.shmid >= 0)
                {
                    sharedMemory_.shmaddr = static_cast<char*>(shmat(sharedMemory_.shmid, nullptr, 0));
                    if (sharedMemory_.shmaddr != reinterpret_cast<char*>(-1))
                    {
                        candidate->data = sharedMemory_.shmaddr;
                        sharedMemory_.readOnly = False;
                        X11ErrorTrap trap(display);
                        const XBool attached = XShmAttach(display, &sharedMemory_);
                        trap.Sync();
                        if (attached == kXTrue && !trap.HasError())
                        {
                            image_ = candidate;
                            imageWidth_ = width;
                            imageHeight_ = height;
                            usesSharedMemory_ = true;
                            // Marked for destruction immediately: the segment stays alive while
                            // it is attached, and this way it cannot survive a crash as a leaked
                            // system-wide resource.
                            shmctl(sharedMemory_.shmid, IPC_RMID, nullptr);
                            return;
                        }
                        shmdt(sharedMemory_.shmaddr);
                    }
                    shmctl(sharedMemory_.shmid, IPC_RMID, nullptr);
                    sharedMemory_.shmid = -1;
                }
                sharedMemory_.shmaddr = nullptr;
                candidate->data = nullptr;
                XDestroyImage(candidate);
            }
        }
#endif

        // Plain path. bits_per_pixel is not derivable from the depth -- a 24-bit visual is almost
        // always 32 bits per pixel on the wire -- so the image is created first and the buffer
        // sized from what it reports.
        XImage* plain = XCreateImage(display, visual, static_cast<unsigned int>(depth), ZPixmap, 0,
                                     nullptr, static_cast<unsigned int>(width),
                                     static_cast<unsigned int>(height), 32, 0);
        if (plain == nullptr)
        {
            throw PlatformException("X11SurfacePresenter::Present",
                                    "the X server refused an image for this visual");
        }
        pixels_.assign(static_cast<std::size_t>(plain->bytes_per_line) *
                           static_cast<std::size_t>(height),
                       0);
        plain->data = reinterpret_cast<char*>(pixels_.data());
        image_ = plain;
        imageWidth_ = width;
        imageHeight_ = height;
        usesSharedMemory_ = false;
    }

    void X11SurfacePresenter::Present(const SurfaceFrame& frame)
    {
        const int stride = Common::ValidateSurfaceFrame(frame, "X11SurfacePresenter::Present");

        Display* display = window_.GetConnection().GetDisplay();
        const WindowSize target = window_.GetPixelSize();
        if (target.width <= 0 || target.height <= 0)
        {
            // An unmapped or zero-sized window. Nothing to present to, and XPutImage with a zero
            // extent is a protocol error rather than a no-op.
            return;
        }

        const Common::PresentRect rect = Common::ComputePresentRect(scaleMode_, frame.width, frame.height,
                                                                     target.width, target.height);
        if (rect.width <= 0 || rect.height <= 0)
        {
            return;
        }

        EnsureImage(rect.width, rect.height);

        Visual* visual = window_.GetVisual();
        const X11PixelPacker packer(visual != nullptr ? visual->red_mask : 0x00FF0000uL,
                                    visual != nullptr ? visual->green_mask : 0x0000FF00uL,
                                    visual != nullptr ? visual->blue_mask : 0x000000FFuL);

        // A 32-bit pixel in this machine's own byte order is written straight into the image --
        // exactly the bytes XPutPixel would store. Any other layout (16 bits per pixel, 24 packed,
        // or a display whose byte order differs from this machine's) keeps XPutPixel, which
        // handles all of them.
        constexpr int kHostByteOrder = std::endian::native == std::endian::little ? LSBFirst
                                                                                   : MSBFirst;
        const bool direct = image_->bits_per_pixel == 32 && image_->byte_order == kHostByteOrder;

        // Scaling and conversion in one pass (the scaling is shared with the Wayland presenter,
        // plans/plan_wayland.md WAYLAND-0013). Doing both in one loop avoids an intermediate
        // full-size buffer, which for a 4K frame is 32 MB of traffic that would otherwise happen
        // every frame.
        Common::ScaleSurfaceFrame(
            frame, stride, rect.width, rect.height, filter_, sourceColumns_,
            [this, &packer, direct](const int x, const int y, const unsigned int red, const unsigned int green,
                                    const unsigned int blue) {
                const unsigned long value = packer.Pack(red, green, blue);
                if (direct)
                {
                    const auto word = static_cast<std::uint32_t>(value);
                    std::memcpy(image_->data + static_cast<std::size_t>(y) * static_cast<std::size_t>(image_->bytes_per_line) +
                                    static_cast<std::size_t>(x) * 4u,
                                &word, sizeof(word));
                }
                else
                {
                    XPutPixel(image_, x, y, value);
                }
            });

#if defined(CNA_X11_HAVE_XSHM)
        if (usesSharedMemory_)
        {
            // send_event False: the caller does not need a completion event, and requesting one
            // would put an XShmCompletionEvent into the application's own event queue that the
            // pump would have to know to discard.
            XShmPutImage(display, window_.GetXWindow(), graphicsContext_, image_, 0, 0, rect.x,
                         rect.y, static_cast<unsigned int>(rect.width),
                         static_cast<unsigned int>(rect.height), False);
            // The server reads the shared segment asynchronously, so the next frame must not
            // overwrite it before the server is done. XSync is the coarse but correct answer.
            XSync(display, False);
            return;
        }
#endif
        XPutImage(display, window_.GetXWindow(), graphicsContext_, image_, 0, 0, rect.x, rect.y,
                  static_cast<unsigned int>(rect.width), static_cast<unsigned int>(rect.height));
        XFlush(display);
    }

} // namespace CNA::Platform::X11
