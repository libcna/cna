// SPDX-License-Identifier: MS-PL

#include "Sdl3Platform.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include "Sdl3EventMapper.hpp"
#include "Sdl3Window.hpp"

#include <SDL3/SDL.h>

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

        std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error != nullptr ? std::string(error) : std::string();
        }

    } // namespace

    Sdl3Platform::Sdl3Platform() = default;

    Sdl3Platform::~Sdl3Platform()
    {
        // Release exactly what this instance acquired, and nothing else. Deliberately no
        // SDL_Quit(): PLAT-4 established that global SDL lifetime belongs to the host
        // application, and calling it here would tear down subsystems the host still holds.
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
        capabilities.multipleWindows = true;
        capabilities.highDpi = true;
        capabilities.multipleDisplays = true;
        capabilities.borderlessFullscreen = true;
        capabilities.nativeWindowHandle = true;
        capabilities.surfacePresentation = true;
        capabilities.openGlContext = true;
        capabilities.vulkanSurface = true;
        capabilities.clipboard = true;
        capabilities.textInput = true;
        capabilities.ime = true;
        // SDL delivers real key-release events and true pixel coordinates -- the two properties
        // the terminal platform cannot provide, and the reason both are capabilities at all.
        capabilities.exactKeyboardState = true;
        capabilities.pixelAccurateMouse = true;
        capabilities.relativeMouse = true;
        capabilities.cursorShapes = true;
        capabilities.gamepad = true;
        capabilities.gamepadRumble = true;
        capabilities.gamepadSensors = true;
        capabilities.haptics = true;
        capabilities.sensors = true;
        capabilities.powerInfo = true;
        capabilities.messageBox = true;
        capabilities.nativeFileDialog = true;
        capabilities.tray = true;
        capabilities.camera = true;
        capabilities.managedEntrypoint = true;
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
        // An unpaired release is a documented no-op, not an error: GraphicsDevice::Dispose
        // releases video unconditionally whether or not the headless path ever acquired it.
        const auto it = ownedRefCounts_.find(subsystem);
        if (it == ownedRefCounts_.end() || it->second == 0)
        {
            return;
        }
        --it->second;
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

    IPlatformKeyboard* Sdl3Platform::GetKeyboard() { return nullptr; }
    IPlatformMouse* Sdl3Platform::GetMouse() { return nullptr; }
    IPlatformGamepad* Sdl3Platform::GetGamepad() { return nullptr; }
    IPlatformTextInput* Sdl3Platform::GetTextInput() { return nullptr; }
    IPlatformSensors* Sdl3Platform::GetSensors() { return nullptr; }
    IPlatformClipboard* Sdl3Platform::GetClipboard() { return &clipboard_; }
    IPlatformDisplays* Sdl3Platform::GetDisplays() { return &displays_; }
    IPlatformDialogs* Sdl3Platform::GetDialogs() { return nullptr; }
    IPlatformFileSystem* Sdl3Platform::GetFileSystem() { return &fileSystem_; }
    IPlatformSystemInfo* Sdl3Platform::GetSystemInfo() { return &systemInfo_; }
    IPlatformGlContext* Sdl3Platform::GetGlContext() { return nullptr; }
    IPlatformVulkanSurface* Sdl3Platform::GetVulkanSurface() { return nullptr; }

    std::unique_ptr<IPlatformSurfacePresenter> Sdl3Platform::CreateSurfacePresenter(IPlatformWindow&)
    {
        // PLAT-128.
        throw PlatformException("CreateSurfacePresenter",
                                "Sdl3SurfacePresenter is not implemented yet (PLAT-128)");
    }

} // namespace CNA::Platform::Sdl3
