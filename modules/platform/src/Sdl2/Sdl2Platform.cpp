// SPDX-License-Identifier: MS-PL

#include "Sdl2Platform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "Sdl2Window.hpp"

#include <SDL.h>

#include <mutex>

namespace CNA::Platform::Sdl2 {

    namespace {

        std::mutex& SdlMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        Uint32 ToSdlFlag(const PlatformSubsystem subsystem)
        {
            switch (subsystem)
            {
                case PlatformSubsystem::Video: return SDL_INIT_VIDEO;
                case PlatformSubsystem::Audio: return SDL_INIT_AUDIO;
                case PlatformSubsystem::Gamepad: return SDL_INIT_GAMECONTROLLER;
                case PlatformSubsystem::Haptic: return SDL_INIT_HAPTIC;
                case PlatformSubsystem::Sensor: return SDL_INIT_SENSOR;
            }
            return 0;
        }

        void RequireSdlSuccess(const int result, const std::string& operation)
        {
            if (result != 0)
            {
                throw PlatformException(operation, SDL_GetError());
            }
        }

        SDL_Window* RequireWindow(const WindowId id, const char* operation)
        {
            SDL_Window* window = SDL_GetWindowFromID(static_cast<Uint32>(id));
            if (window == nullptr)
            {
                throw PlatformException(operation, "unknown or expired SDL2 window id");
            }
            return window;
        }

        int ToSdlGlProfile(const GlProfile profile)
        {
            switch (profile)
            {
                case GlProfile::Core: return SDL_GL_CONTEXT_PROFILE_CORE;
                case GlProfile::Compatibility: return SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
                case GlProfile::Es: return SDL_GL_CONTEXT_PROFILE_ES;
            }
            return SDL_GL_CONTEXT_PROFILE_CORE;
        }

        void* LoadSdlGlProcAddress(const char* name)
        {
            return SDL_GL_GetProcAddress(name);
        }

        bool MapWindowEvent(const SDL_WindowEvent& source, PlatformEvent& destination)
        {
            WindowEvent event;
            event.window = static_cast<WindowId>(source.windowID);
            event.data1 = source.data1;
            event.data2 = source.data2;
            switch (source.event)
            {
                case SDL_WINDOWEVENT_SHOWN:
                case SDL_WINDOWEVENT_EXPOSED: event.kind = WindowEventKind::Exposed; break;
                case SDL_WINDOWEVENT_RESIZED: event.kind = WindowEventKind::Resized; break;
                case SDL_WINDOWEVENT_SIZE_CHANGED: event.kind = WindowEventKind::PixelSizeChanged; break;
                case SDL_WINDOWEVENT_FOCUS_GAINED: event.kind = WindowEventKind::FocusGained; break;
                case SDL_WINDOWEVENT_FOCUS_LOST: event.kind = WindowEventKind::FocusLost; break;
                case SDL_WINDOWEVENT_CLOSE: event.kind = WindowEventKind::CloseRequested; break;
                case SDL_WINDOWEVENT_MINIMIZED: event.kind = WindowEventKind::Minimized; break;
                case SDL_WINDOWEVENT_MAXIMIZED: event.kind = WindowEventKind::Maximized; break;
                case SDL_WINDOWEVENT_RESTORED: event.kind = WindowEventKind::Restored; break;
                case SDL_WINDOWEVENT_MOVED: event.kind = WindowEventKind::Moved; break;
                default: return false;
            }
            destination = event;
            return true;
        }

    } // namespace

    Sdl2Platform::Sdl2Platform() = default;

    Sdl2Platform::~Sdl2Platform()
    {
        std::lock_guard<std::mutex> lock(SdlMutex());
        for (const auto& [subsystem, count] : ownedRefCounts_)
        {
            for (int index = 0; index < count; ++index)
            {
                SDL_QuitSubSystem(ToSdlFlag(subsystem));
            }
        }
    }

    const std::string& Sdl2Platform::GetName() const
    {
        static const std::string name = "SDL2";
        return name;
    }

    PlatformCapabilities Sdl2Platform::GetCapabilities() const
    {
        PlatformCapabilities capabilities;
        capabilities.multipleWindows = true;
        capabilities.highDpi = true;
        capabilities.multipleDisplays = true;
        capabilities.borderlessFullscreen = true;
        capabilities.openGlContext = true;
        // The remaining SDL2 services arrive only when their contract implementation does.
        // Reporting them false protects existing games through the capability fallback path.
        return capabilities;
    }

    void Sdl2Platform::AcquireSubsystem(const PlatformSubsystem subsystem)
    {
        std::lock_guard<std::mutex> lock(SdlMutex());
        if (SDL_InitSubSystem(ToSdlFlag(subsystem)) != 0)
        {
            throw PlatformException("AcquireSubsystem(" + ToString(subsystem) + ")", SDL_GetError());
        }
        ++ownedRefCounts_[subsystem];
    }

    void Sdl2Platform::ReleaseSubsystem(const PlatformSubsystem subsystem)
    {
        std::lock_guard<std::mutex> lock(SdlMutex());
        const auto found = ownedRefCounts_.find(subsystem);
        if (found == ownedRefCounts_.end() || found->second == 0) { return; }
        --found->second;
        SDL_QuitSubSystem(ToSdlFlag(subsystem));
    }

    bool Sdl2Platform::IsSubsystemInitialized(const PlatformSubsystem subsystem) const
    {
        std::lock_guard<std::mutex> lock(SdlMutex());
        const Uint32 flag = ToSdlFlag(subsystem);
        return (SDL_WasInit(flag) & flag) != 0;
    }

    std::unique_ptr<IPlatformWindow> Sdl2Platform::CreateWindow(const WindowDescription& description)
    {
        std::lock_guard<std::mutex> lock(SdlMutex());
        Uint32 flags = 0;
        if (description.resizable) { flags |= SDL_WINDOW_RESIZABLE; }
        if (description.borderless) { flags |= SDL_WINDOW_BORDERLESS; }
        if (!description.visible || description.fullscreenMode != WindowFullscreenMode::Windowed)
        {
            flags |= SDL_WINDOW_HIDDEN;
        }
        if (description.highDpi) { flags |= SDL_WINDOW_ALLOW_HIGHDPI; }
        if (description.renderIntent == WindowRenderIntent::OpenGl) { flags |= SDL_WINDOW_OPENGL; }
        if (description.renderIntent == WindowRenderIntent::Vulkan) { flags |= SDL_WINDOW_VULKAN; }

        if (description.renderIntent == WindowRenderIntent::OpenGl)
        {
            SDL_GL_ResetAttributes();
            const auto& framebuffer = description.openGlFramebuffer;
            RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, framebuffer.depthBits),
                              "CreateWindow(OpenGl depth)");
            RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, framebuffer.stencilBits),
                              "CreateWindow(OpenGl stencil)");
            RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,
                                                   framebuffer.doubleBuffered ? 1 : 0),
                              "CreateWindow(OpenGl double buffer)");
            RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,
                                                   framebuffer.samples > 1 ? 1 : 0),
                              "CreateWindow(OpenGl MSAA)");
            RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,
                                                   framebuffer.samples > 1 ? framebuffer.samples : 0),
                              "CreateWindow(OpenGl samples)");
        }

        const int x = description.centered ? SDL_WINDOWPOS_CENTERED : description.x;
        const int y = description.centered ? SDL_WINDOWPOS_CENTERED : description.y;
        SDL_Window* raw = SDL_CreateWindow(description.title.c_str(), x, y, description.width,
                                           description.height, flags);
        if (raw == nullptr)
        {
            throw PlatformException("CreateWindow(" + description.title + ")", SDL_GetError());
        }
        auto window = std::make_unique<Sdl2Window>(raw);
        if (description.minimumWidth > 0 || description.minimumHeight > 0)
        {
            SDL_SetWindowMinimumSize(raw, description.minimumWidth, description.minimumHeight);
        }
        if (description.maximumWidth > 0 || description.maximumHeight > 0)
        {
            SDL_SetWindowMaximumSize(raw, description.maximumWidth, description.maximumHeight);
        }
        if (description.fullscreenMode != WindowFullscreenMode::Windowed)
        {
            window->SetFullscreenMode(description.fullscreenMode);
            if (description.visible) { window->Show(); }
        }
        return window;
    }

    std::unique_ptr<IPlatformWindow> Sdl2Platform::AdoptWindow(const WindowId windowId)
    {
        if (windowId == 0) { throw PlatformException("AdoptWindow", "window id must be non-zero"); }
        SDL_Window* raw = SDL_GetWindowFromID(static_cast<Uint32>(windowId));
        if (raw == nullptr) { throw PlatformException("AdoptWindow", "no SDL2 window has that id"); }
        return std::make_unique<Sdl2Window>(raw, false);
    }

    std::unique_ptr<IPlatformWindow> Sdl2Platform::AdoptWindowHandle(const std::uintptr_t handle)
    {
        if (handle == 0) { throw PlatformException("AdoptWindowHandle", "window handle must be non-zero"); }
        auto* raw = reinterpret_cast<SDL_Window*>(handle);
        const Uint32 id = SDL_GetWindowID(raw);
        if (id == 0 || SDL_GetWindowFromID(id) != raw)
        {
            throw PlatformException("AdoptWindowHandle", "no SDL2 window has that handle");
        }
        return std::make_unique<Sdl2Window>(raw, false);
    }

    void Sdl2Platform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        destination.clear();
        SDL_Event source;
        PlatformEvent translated;
        while (SDL_PollEvent(&source) != 0)
        {
            if (source.type == SDL_QUIT)
            {
                destination.emplace_back(QuitEvent{});
            }
            else if (source.type == SDL_WINDOWEVENT && MapWindowEvent(source.window, translated))
            {
                destination.push_back(translated);
            }
        }
    }

    std::uint64_t Sdl2Platform::GetPerformanceCounter() const { return SDL_GetPerformanceCounter(); }
    std::uint64_t Sdl2Platform::GetPerformanceFrequency() const { return SDL_GetPerformanceFrequency(); }
    std::uint64_t Sdl2Platform::GetTicksMilliseconds() const { return SDL_GetTicks64(); }
    void Sdl2Platform::Delay(const std::uint32_t milliseconds) { SDL_Delay(milliseconds); }

    IPlatformKeyboard* Sdl2Platform::GetKeyboard() { return nullptr; }
    IPlatformMouse* Sdl2Platform::GetMouse() { return nullptr; }
    IPlatformGamepad* Sdl2Platform::GetGamepad() { return nullptr; }
    IPlatformJoystick* Sdl2Platform::GetJoystick() { return nullptr; }
    IPlatformTextInput* Sdl2Platform::GetTextInput() { return nullptr; }
    IPlatformSensors* Sdl2Platform::GetSensors() { return nullptr; }
    IPlatformHaptics* Sdl2Platform::GetHaptics() { return nullptr; }
    IPlatformInputDevices* Sdl2Platform::GetInputDevices() { return nullptr; }
    IPlatformClipboard* Sdl2Platform::GetClipboard() { return nullptr; }
    IPlatformDisplays* Sdl2Platform::GetDisplays() { return nullptr; }
    IPlatformDialogs* Sdl2Platform::GetDialogs() { return nullptr; }
    IPlatformTray* Sdl2Platform::GetTray() { return nullptr; }
    IPlatformCameraProvider* Sdl2Platform::GetCamera() { return nullptr; }
    IPlatformFileSystem* Sdl2Platform::GetFileSystem() { return &fileSystem_; }
    IPlatformSystemInfo* Sdl2Platform::GetSystemInfo() { return &systemInfo_; }
    IPlatformGlContext* Sdl2Platform::GetGlContext() { return &glContext_; }
    IPlatformVulkanSurface* Sdl2Platform::GetVulkanSurface() { return nullptr; }
    std::unique_ptr<IPlatformSurfacePresenter> Sdl2Platform::CreateSurfacePresenter(IPlatformWindow&)
    {
        throw PlatformNotSupportedException(PlatformCapability::SurfacePresentation, GetName());
    }

    GlContextHandle Sdl2Platform::GlContext::CreateContext(
        const WindowId window, const GlContextDescription& description)
    {
        std::lock_guard<std::mutex> lock(SdlMutex());
        SDL_GL_ResetAttributes();
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, description.majorVersion),
                          "GlContext::CreateContext");
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, description.minorVersion),
                          "GlContext::CreateContext");
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, ToSdlGlProfile(description.profile)),
                          "GlContext::CreateContext");
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, description.depthBits),
                          "GlContext::CreateContext");
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, description.stencilBits),
                          "GlContext::CreateContext");
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, description.multisampleBuffers),
                          "GlContext::CreateContext");
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, description.multisampleSamples),
                          "GlContext::CreateContext");
        RequireSdlSuccess(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, description.doubleBuffer ? 1 : 0),
                          "GlContext::CreateContext");
        SDL_GLContext context = SDL_GL_CreateContext(RequireWindow(window, "GlContext::CreateContext"));
        if (context == nullptr) { throw PlatformException("GlContext::CreateContext", SDL_GetError()); }
        return context;
    }
    void Sdl2Platform::GlContext::DestroyContext(const GlContextHandle context)
    {
        if (context != nullptr) { SDL_GL_DeleteContext(context); }
    }
    void Sdl2Platform::GlContext::MakeCurrent(const WindowId window, const GlContextHandle context)
    {
        RequireSdlSuccess(SDL_GL_MakeCurrent(RequireWindow(window, "GlContext::MakeCurrent"), context),
                          "GlContext::MakeCurrent");
    }
    void Sdl2Platform::GlContext::SwapBuffers(const WindowId window)
    {
        SDL_GL_SwapWindow(RequireWindow(window, "GlContext::SwapBuffers"));
    }
    bool Sdl2Platform::GlContext::SetSwapInterval(const int interval)
    {
        return SDL_GL_SetSwapInterval(interval) == 0;
    }
    void* Sdl2Platform::GlContext::GetProcAddress(const std::string& name) const
    {
        return LoadSdlGlProcAddress(name.c_str());
    }
    GlProcAddressLoader Sdl2Platform::GlContext::GetProcAddressLoader() const
    {
        return &LoadSdlGlProcAddress;
    }
    GlContextDescription Sdl2Platform::GlContext::GetContextAttributes(GlContextHandle) const
    {
        GlContextDescription granted;
        int value = 0;
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &value) == 0) { granted.majorVersion = value; }
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &value) == 0) { granted.minorVersion = value; }
        if (SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &value) == 0) { granted.depthBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &value) == 0) { granted.stencilBits = value; }
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &value) == 0) { granted.multisampleBuffers = value; }
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &value) == 0) { granted.multisampleSamples = value; }
        if (SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &value) == 0) { granted.doubleBuffer = value != 0; }
        if (SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &value) == 0)
        {
            granted.profile = value == SDL_GL_CONTEXT_PROFILE_ES ? GlProfile::Es
                              : value == SDL_GL_CONTEXT_PROFILE_COMPATIBILITY ? GlProfile::Compatibility
                                                                                : GlProfile::Core;
        }
        return granted;
    }

} // namespace CNA::Platform::Sdl2
