// SPDX-License-Identifier: MS-PL

#include "Sdl3Platform.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include "Sdl3EventMapper.hpp"
#include "Sdl3Window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace CNA::Platform::Sdl3 {

    namespace {

        /// Maps CNA's subsystem vocabulary onto SDL's init flags. Video implies SDL's events
        /// subsystem, and gamepad implies joystick; SDL handles both dependencies itself, which
        /// is why they are not listed separately here (see the lifecycle audit).
        SDL_InitFlags ToSdlFlag(const PlatformSubsystem subsystem)
        {
            switch (subsystem)
            {
                case PlatformSubsystem::Video:   return SDL_INIT_VIDEO;
                case PlatformSubsystem::Audio:   return SDL_INIT_AUDIO;
                case PlatformSubsystem::Gamepad: return SDL_INIT_GAMEPAD;
                case PlatformSubsystem::Haptic:  return SDL_INIT_HAPTIC;
                case PlatformSubsystem::Sensor:  return SDL_INIT_SENSOR;
            }
            return SDL_INIT_VIDEO;
        }

        /// Whether this host has a Vulkan loader at all. Probed once: a capability that claims
        /// Vulkan works on a machine with no loader would break the model's central promise --
        /// true means the calls succeed, false means they refuse.
        bool HostHasVulkan()
        {
            static const bool available = SDL_Vulkan_LoadLibrary(nullptr);
            return available;
        }

        std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error != nullptr ? std::string(error) : std::string();
        }

    } // namespace

    Sdl3Platform::Sdl3Platform()
    {
#ifdef __ANDROID__
        // Android's back button is framework navigation input. Keep the SDL-specific hint at the
        // platform edge rather than making GraphicsDevice depend on the native toolkit.
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
    }

    Sdl3Platform::~Sdl3Platform()
    {
        // Release exactly what this instance acquired, and nothing else. Deliberately no
        // SDL_Quit(): PLAT-4 established that global SDL lifetime belongs to the host
        // application, and calling it here would tear down subsystems the host still holds.
        // Cached native handles must close before the subsystems that own them.
        sensors_.Deactivate();
        haptics_.Deactivate();
        for (const auto& [subsystem, count] : ownedRefCounts_)
        {
            for (int i = 0; i < count; ++i)
            {
                SDL_QuitSubSystem(ToSdlFlag(subsystem));
            }
        }
    }

    const std::string& Sdl3Platform::GetName() const
    {
        static const std::string name = "SDL3";
        return name;
    }

    PlatformCapabilities Sdl3Platform::GetCapabilities() const
    {
        PlatformCapabilities capabilities;

        // A capability is true only when the service behind it is actually wired up. SDL3 can do
        // all of these in principle, but the contract's rule is that a service is null exactly
        // when its capability is false -- so advertising one whose accessor still returns null
        // would break the very promise callers use to decide whether to ask. The conformance
        // suite caught exactly that and is why this list is conservative rather than aspirational.

        // Windowing and presentation: implemented.
        capabilities.multipleWindows = true;
        capabilities.highDpi = true;
        capabilities.multipleDisplays = true;
        capabilities.borderlessFullscreen = true;
        capabilities.nativeWindowHandle = true;
        capabilities.surfacePresentation = true;
        capabilities.openGlContext = true;
        capabilities.vulkanSurface = HostHasVulkan();

        // System services: implemented.
        capabilities.clipboard = true;
        capabilities.powerInfo = true;
        capabilities.managedEntrypoint = true;

        // Input services: implemented. exactKeyboardState and pixelAccurateMouse are the two
        // capabilities that exist because a terminal cannot provide them -- SDL delivers real
        // key-release events and true pixel coordinates, so both are honestly true here.
        capabilities.exactKeyboardState = true;
        capabilities.pixelAccurateMouse = true;
        capabilities.relativeMouse = true;
        capabilities.cursorShapes = true;
        capabilities.gamepad = true;
        capabilities.joystick = true;
        capabilities.gamepadRumble = true;
        capabilities.gamepadSensors = true;
        capabilities.textInput = true;
        capabilities.ime = true;
        capabilities.sensors = true;
        capabilities.haptics = true;
        capabilities.globalPointer = true;
        capabilities.inputDeviceEnumeration = true;
        capabilities.messageBox = true;
        capabilities.nativeFileDialog = true;

        // Still unwired. Each flips true in the task that wires its accessor, not before.
        //   capabilities.tray                PLAT-105
        //   capabilities.camera              PLAT-106

        return capabilities;
    }

    void Sdl3Platform::AcquireSubsystem(const PlatformSubsystem subsystem)
    {
        if (!SDL_InitSubSystem(ToSdlFlag(subsystem)))
        {
            throw PlatformException("AcquireSubsystem(" + ToString(subsystem) + ")", LastSdlError());
        }
        ++ownedRefCounts_[subsystem];
    }

    void Sdl3Platform::ReleaseSubsystem(const PlatformSubsystem subsystem)
    {
        // An unpaired release is a documented no-op, not an error: cleanup may follow partial
        // initialization even though successful owners balance every acquisition.
        const auto it = ownedRefCounts_.find(subsystem);
        if (it == ownedRefCounts_.end() || it->second == 0)
        {
            return;
        }
        --it->second;
        if (it->second == 0 && subsystem == PlatformSubsystem::Sensor)
        {
            // SDL invalidates native sensor handles when the last subsystem reference goes away.
            // Empty the service cache first so its destructor cannot later close stale pointers.
            sensors_.Deactivate();
        }
        else if (it->second == 0 && subsystem == PlatformSubsystem::Haptic)
        {
            // SDL invalidates native haptic handles when the last subsystem reference goes away.
            // Empty the service cache first so its destructor cannot later close stale pointers.
            haptics_.Deactivate();
        }
        SDL_QuitSubSystem(ToSdlFlag(subsystem));
    }

    bool Sdl3Platform::IsSubsystemInitialized(const PlatformSubsystem subsystem) const
    {
        const SDL_InitFlags flag = ToSdlFlag(subsystem);
        return (SDL_WasInit(flag) & flag) != 0;
    }

    std::unique_ptr<IPlatformWindow> Sdl3Platform::CreateWindow(const WindowDescription& description)
    {
        // Creation-time flags only. Render intent and high-DPI in particular CANNOT be applied
        // afterwards -- the native surface attributes are chosen when the window is made -- which
        // is why WindowDescription carries them rather than exposing setters.
        SDL_WindowFlags flags = 0;
        if (description.resizable)  { flags |= SDL_WINDOW_RESIZABLE; }
        if (description.borderless) { flags |= SDL_WINDOW_BORDERLESS; }
        if (!description.visible)   { flags |= SDL_WINDOW_HIDDEN; }
        if (description.highDpi)    { flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY; }

        switch (description.renderIntent)
        {
            case WindowRenderIntent::OpenGl: flags |= SDL_WINDOW_OPENGL; break;
            case WindowRenderIntent::Vulkan: flags |= SDL_WINDOW_VULKAN; break;
            case WindowRenderIntent::Metal:  flags |= SDL_WINDOW_METAL; break;
            case WindowRenderIntent::None:   break;
        }

        if (description.renderIntent == WindowRenderIntent::OpenGl)
        {
            const OpenGlFramebufferDescription& framebuffer = description.openGlFramebuffer;
            if (framebuffer.depthBits > 0)
                SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, framebuffer.depthBits);
            if (framebuffer.stencilBits > 0)
                SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, framebuffer.stencilBits);
            if (framebuffer.doubleBuffered)
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            if (framebuffer.samples > 1)
            {
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, framebuffer.samples);
            }
        }

        if (description.fullscreenMode != WindowFullscreenMode::Windowed)
        {
            flags |= SDL_WINDOW_FULLSCREEN;
        }

        SDL_Window* raw = SDL_CreateWindow(description.title.c_str(), description.width,
                                           description.height, flags);
        if (raw == nullptr)
        {
            throw PlatformException("CreateWindow(" + description.title + ")", LastSdlError());
        }

        auto window = std::make_unique<Sdl3Window>(raw);

        // Post-creation state that genuinely can be applied afterwards. Position is applied only
        // when the caller asked for an explicit one; SDL centres by default.
        if (!description.centered)
        {
            SDL_SetWindowPosition(raw, description.x, description.y);
        }
        if (description.minimumWidth > 0 || description.minimumHeight > 0)
        {
            SDL_SetWindowMinimumSize(raw, description.minimumWidth, description.minimumHeight);
        }
        if (description.maximumWidth > 0 || description.maximumHeight > 0)
        {
            SDL_SetWindowMaximumSize(raw, description.maximumWidth, description.maximumHeight);
        }
        if (description.fullscreenMode == WindowFullscreenMode::BorderlessFullscreen)
        {
            SDL_SetWindowFullscreenMode(raw, nullptr);
        }

        return window;
    }

    std::unique_ptr<IPlatformWindow> Sdl3Platform::AdoptWindow(const WindowId windowId)
    {
        if (windowId == 0)
        {
            throw PlatformException("AdoptWindow", "window id must be non-zero");
        }

        SDL_Window* raw = SDL_GetWindowFromID(static_cast<SDL_WindowID>(windowId));
        if (raw == nullptr)
        {
            throw PlatformException(
                "AdoptWindow(" + std::to_string(windowId) + ")",
                "no SDL window has that id");
        }
        return std::make_unique<Sdl3Window>(raw, false);
    }

    std::unique_ptr<IPlatformWindow> Sdl3Platform::AdoptWindowHandle(
        const std::uintptr_t handle)
    {
        if (handle == 0)
        {
            throw PlatformException("AdoptWindowHandle", "window handle must be non-zero");
        }

        auto* raw = reinterpret_cast<SDL_Window*>(handle);
        const SDL_WindowID id = SDL_GetWindowID(raw);
        if (id == 0 || SDL_GetWindowFromID(id) != raw)
        {
            throw PlatformException("AdoptWindowHandle", "no SDL window has that handle");
        }
        return std::make_unique<Sdl3Window>(raw, false);
    }

    void Sdl3Platform::PollEvents(std::vector<PlatformEvent>& destination)
    {
        // clear() keeps the vector's capacity, which is the whole reason the buffer belongs to
        // the caller: a reused batch stops allocating after the first few frames.
        destination.clear();

        SDL_Event event;
        PlatformEvent translated;
        while (SDL_PollEvent(&event))
        {
            if (MapSdlEvent(event, translated))
            {
                mouse_.ObserveEvent(translated);
                if (const auto* device = std::get_if<DeviceEvent>(&translated))
                {
                    joystick_.ObserveEvent(*device);
                }
                destination.push_back(translated);
            }
        }
    }

    std::uint64_t Sdl3Platform::GetPerformanceCounter() const
    {
        return SDL_GetPerformanceCounter();
    }

    std::uint64_t Sdl3Platform::GetPerformanceFrequency() const
    {
        return SDL_GetPerformanceFrequency();
    }

    std::uint64_t Sdl3Platform::GetTicksMilliseconds() const
    {
        return SDL_GetTicks();
    }

    void Sdl3Platform::Delay(const std::uint32_t milliseconds)
    {
        SDL_Delay(milliseconds);
    }

    IPlatformKeyboard* Sdl3Platform::GetKeyboard() { return &keyboard_; }
    IPlatformMouse* Sdl3Platform::GetMouse() { return &mouse_; }
    IPlatformGamepad* Sdl3Platform::GetGamepad() { return &gamepad_; }
    IPlatformJoystick* Sdl3Platform::GetJoystick() { return &joystick_; }
    IPlatformTextInput* Sdl3Platform::GetTextInput() { return &textInput_; }
    IPlatformSensors* Sdl3Platform::GetSensors() { return &sensors_; }
    IPlatformHaptics* Sdl3Platform::GetHaptics() { return &haptics_; }

    IPlatformInputDevices* Sdl3Platform::GetInputDevices() { return &inputDevices_; }
    IPlatformClipboard* Sdl3Platform::GetClipboard() { return &clipboard_; }
    IPlatformDisplays* Sdl3Platform::GetDisplays() { return &displays_; }
    IPlatformDialogs* Sdl3Platform::GetDialogs() { return &dialogs_; }
    IPlatformFileSystem* Sdl3Platform::GetFileSystem() { return &fileSystem_; }
    IPlatformSystemInfo* Sdl3Platform::GetSystemInfo() { return &systemInfo_; }
    IPlatformGlContext* Sdl3Platform::GetGlContext() { return &glContext_; }
    IPlatformVulkanSurface* Sdl3Platform::GetVulkanSurface()
    {
        // Null exactly when the capability is false. Handing back a service that would refuse
        // every call would break the rule a caller relies on to decide whether to ask at all.
        return HostHasVulkan() ? &vulkanSurface_ : nullptr;
    }

    std::unique_ptr<IPlatformSurfacePresenter> Sdl3Platform::CreateSurfacePresenter(IPlatformWindow& window)
    {
        auto* sdlWindow = dynamic_cast<Sdl3Window*>(&window);
        if (sdlWindow == nullptr)
        {
            throw PlatformException("CreateSurfacePresenter",
                                    "window was not created by the SDL3 platform");
        }
        return std::make_unique<Sdl3SurfacePresenter>(*sdlWindow);
    }

} // namespace CNA::Platform::Sdl3
