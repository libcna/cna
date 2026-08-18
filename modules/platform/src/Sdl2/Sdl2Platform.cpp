// SPDX-License-Identifier: MS-PL

#include "Sdl2Platform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "Sdl2Window.hpp"

#include <SDL.h>

#include <algorithm>
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

        Scancode ToScancode(const SDL_Scancode scancode)
        {
            const auto value = static_cast<std::uint16_t>(scancode);
            return IsKnownScancode(value) ? static_cast<Scancode>(value) : Scancode::Unknown;
        }

        std::uint16_t ToModifiers(const SDL_Keymod modifiers)
        {
            std::uint16_t result = 0;
            const auto add = [&result](const KeyModifier modifier) {
                result |= static_cast<std::uint16_t>(modifier);
            };
            if ((modifiers & KMOD_SHIFT) != 0) { add(KeyModifier::Shift); }
            if ((modifiers & KMOD_CTRL) != 0) { add(KeyModifier::Control); }
            if ((modifiers & KMOD_ALT) != 0) { add(KeyModifier::Alt); }
            if ((modifiers & KMOD_GUI) != 0) { add(KeyModifier::Gui); }
            if ((modifiers & KMOD_CAPS) != 0) { add(KeyModifier::CapsLock); }
            if ((modifiers & KMOD_NUM) != 0) { add(KeyModifier::NumLock); }
            if ((modifiers & KMOD_SCROLL) != 0) { add(KeyModifier::ScrollLock); }
            if ((modifiers & KMOD_MODE) != 0) { add(KeyModifier::Mode); }
            return result;
        }

        KeyCode ToKeyCode(const SDL_Keycode key)
        {
            // SDL's printable keycodes are Unicode while CNA uses Windows virtual-key values.
            // These two ranges are ASCII and therefore genuinely contiguous.
            if (key >= SDLK_a && key <= SDLK_z)
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>('A' + (key - SDLK_a)));
            }
            if (key >= SDLK_0 && key <= SDLK_9)
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>('0' + (key - SDLK_0)));
            }
            // The function keys are TWO ranges, not one, on both sides of the mapping, and this
            // used to be written as a single `key >= SDLK_F1 && key <= SDLK_F24` test. That was
            // wrong twice over. SDL2 leaves a gap between F12 (scancode 69) and F13 (scancode
            // 104) and fills it with the navigation, editing and keypad keys, so the single range
            // swallowed SDLK_LEFT, SDLK_HOME, SDLK_DELETE, SDLK_KP_1 and about thirty others
            // before the switch below could ever see them -- SDLK_LEFT (scancode 80) came out as
            // 112 + (80 - 58) = 134, an F-key, so on this backend every arrow key reported as F23.
            // Windows virtual-key codes have their own gap in the same place: F1..F12 are
            // 0x70..0x7B and F13..F24 restart at 0x7C, so arithmetic from F1 would be off by the
            // scancode gap even if nothing sat inside it. PLAT-137 hit the identical shape in the
            // terminal keyboard against HID usage codes; two independent keyboard mappings making
            // the same mistake is what makes it worth a comment this long.
            if (key >= SDLK_F1 && key <= SDLK_F12)
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>(112 + (key - SDLK_F1)));
            }
            if (key >= SDLK_F13 && key <= SDLK_F24)
            {
                return static_cast<KeyCode>(static_cast<std::uint16_t>(124 + (key - SDLK_F13)));
            }
            switch (key)
            {
                case SDLK_SPACE: return KeyCode::Space;
                case SDLK_RETURN: return KeyCode::Enter;
                case SDLK_ESCAPE: return KeyCode::Escape;
                case SDLK_BACKSPACE: return KeyCode::Back;
                case SDLK_TAB: return KeyCode::Tab;
                case SDLK_LEFT: return KeyCode::Left;
                case SDLK_RIGHT: return KeyCode::Right;
                case SDLK_UP: return KeyCode::Up;
                case SDLK_DOWN: return KeyCode::Down;
                case SDLK_HOME: return KeyCode::Home;
                case SDLK_END: return KeyCode::End;
                case SDLK_PAGEUP: return KeyCode::PageUp;
                case SDLK_PAGEDOWN: return KeyCode::PageDown;
                case SDLK_INSERT: return KeyCode::Insert;
                case SDLK_DELETE: return KeyCode::Delete;
                case SDLK_LSHIFT: return KeyCode::LeftShift;
                case SDLK_RSHIFT: return KeyCode::RightShift;
                case SDLK_LCTRL: return KeyCode::LeftControl;
                case SDLK_RCTRL: return KeyCode::RightControl;
                case SDLK_LALT: return KeyCode::LeftAlt;
                case SDLK_RALT: return KeyCode::RightAlt;
                case SDLK_LGUI: return KeyCode::LeftWindows;
                case SDLK_RGUI: return KeyCode::RightWindows;
                case SDLK_CAPSLOCK: return KeyCode::CapsLock;
                case SDLK_NUMLOCKCLEAR: return KeyCode::NumLock;
                case SDLK_SCROLLLOCK: return KeyCode::Scroll;
                case SDLK_KP_0: return KeyCode::NumPad0;
                case SDLK_KP_1: return KeyCode::NumPad1;
                case SDLK_KP_2: return KeyCode::NumPad2;
                case SDLK_KP_3: return KeyCode::NumPad3;
                case SDLK_KP_4: return KeyCode::NumPad4;
                case SDLK_KP_5: return KeyCode::NumPad5;
                case SDLK_KP_6: return KeyCode::NumPad6;
                case SDLK_KP_7: return KeyCode::NumPad7;
                case SDLK_KP_8: return KeyCode::NumPad8;
                case SDLK_KP_9: return KeyCode::NumPad9;
                case SDLK_KP_ENTER: return KeyCode::Enter;
                case SDLK_KP_PLUS: return KeyCode::Add;
                case SDLK_KP_MINUS: return KeyCode::Subtract;
                case SDLK_KP_MULTIPLY: return KeyCode::Multiply;
                case SDLK_KP_DIVIDE: return KeyCode::Divide;
                case SDLK_KP_DECIMAL: return KeyCode::Decimal;
                default: return KeyCode::None;
            }
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
        // PLAT-SDL2-5. Each flag below names the service that backs it; PLAT-117's conformance
        // rule is that a service is non-null exactly when its presence capability is true, so a
        // flag may only be turned on in the same change that wires its accessor.
        capabilities.multipleWindows = true;       // CreateWindow has no single-window slot.
        capabilities.highDpi = true;               // Sdl2Window measures drawable-vs-logical size.
        capabilities.multipleDisplays = true;      // GetDisplays() -> Sdl2Displays.
        capabilities.borderlessFullscreen = true;  // SDL_WINDOW_FULLSCREEN_DESKTOP.
        capabilities.openGlContext = true;         // GetGlContext() -> Sdl2Platform::GlContext.
        // A quality flag, not a presence one: the keyboard service exists and its snapshot comes
        // straight from SDL_GetKeyboardState, so every press really does have a matching release.
        capabilities.exactKeyboardState = true;
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
            else if (source.type == SDL_KEYDOWN || source.type == SDL_KEYUP)
            {
                KeyEvent key;
                key.window = static_cast<WindowId>(source.key.windowID);
                key.scancode = ToScancode(source.key.keysym.scancode);
                key.keycode = ToKeyCode(source.key.keysym.sym);
                key.modifiers = ToModifiers(static_cast<SDL_Keymod>(source.key.keysym.mod));
                key.pressed = source.type == SDL_KEYDOWN;
                key.repeat = source.key.repeat != 0;
                destination.emplace_back(key);
            }
            else if (source.type == SDL_TEXTINPUT)
            {
                TextInputEvent text;
                text.window = static_cast<WindowId>(source.text.windowID);
                text.text = source.text.text;
                destination.emplace_back(std::move(text));
            }
            else if (source.type == SDL_MOUSEMOTION)
            {
                MouseMotionEvent motion;
                motion.window = static_cast<WindowId>(source.motion.windowID);
                motion.x = static_cast<float>(source.motion.x);
                motion.y = static_cast<float>(source.motion.y);
                motion.deltaX = static_cast<float>(source.motion.xrel);
                motion.deltaY = static_cast<float>(source.motion.yrel);
                destination.emplace_back(motion);
            }
            else if (source.type == SDL_MOUSEBUTTONDOWN || source.type == SDL_MOUSEBUTTONUP)
            {
                MouseButtonEvent button;
                button.window = static_cast<WindowId>(source.button.windowID);
                button.button = source.button.button;
                button.pressed = source.type == SDL_MOUSEBUTTONDOWN;
                button.clicks = source.button.clicks;
                button.x = static_cast<float>(source.button.x);
                button.y = static_cast<float>(source.button.y);
                destination.emplace_back(button);
            }
            else if (source.type == SDL_MOUSEWHEEL)
            {
                MouseWheelEvent wheel;
                wheel.window = static_cast<WindowId>(source.wheel.windowID);
                wheel.x = static_cast<float>(source.wheel.x);
                wheel.y = static_cast<float>(source.wheel.y);
                if (source.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                {
                    wheel.x = -wheel.x;
                    wheel.y = -wheel.y;
                }
                destination.emplace_back(wheel);
            }
        }
    }

    std::uint64_t Sdl2Platform::GetPerformanceCounter() const { return SDL_GetPerformanceCounter(); }
    std::uint64_t Sdl2Platform::GetPerformanceFrequency() const { return SDL_GetPerformanceFrequency(); }
    std::uint64_t Sdl2Platform::GetTicksMilliseconds() const { return SDL_GetTicks64(); }
    void Sdl2Platform::Delay(const std::uint32_t milliseconds) { SDL_Delay(milliseconds); }

    IPlatformKeyboard* Sdl2Platform::GetKeyboard() { return &keyboard_; }
    IPlatformMouse* Sdl2Platform::GetMouse() { return nullptr; }
    IPlatformGamepad* Sdl2Platform::GetGamepad() { return nullptr; }
    IPlatformJoystick* Sdl2Platform::GetJoystick() { return nullptr; }
    IPlatformTextInput* Sdl2Platform::GetTextInput() { return nullptr; }
    IPlatformSensors* Sdl2Platform::GetSensors() { return nullptr; }
    IPlatformHaptics* Sdl2Platform::GetHaptics() { return nullptr; }
    IPlatformInputDevices* Sdl2Platform::GetInputDevices() { return nullptr; }
    IPlatformClipboard* Sdl2Platform::GetClipboard() { return nullptr; }
    IPlatformDisplays* Sdl2Platform::GetDisplays() { return &displays_; }
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

    void Sdl2Platform::Keyboard::Update()
    {
        snapshot_.pressedKeys.clear();
        int count = 0;
        const Uint8* state = SDL_GetKeyboardState(&count);
        for (int index = 0; state != nullptr && index < count; ++index)
        {
            if (state[index] == 0) { continue; }
            const KeyCode key = ToKeyCode(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(index)));
            if (key != KeyCode::None) { snapshot_.pressedKeys.push_back(key); }
        }
        snapshot_.modifiers = ToModifiers(SDL_GetModState());
    }
    const KeyboardSnapshot& Sdl2Platform::Keyboard::GetSnapshot() const { return snapshot_; }
    bool Sdl2Platform::Keyboard::HasKeyboard() const { return true; }
    KeyCode Sdl2Platform::Keyboard::GetKeyFromScancode(const Scancode scancode) const
    {
        return ToKeyCode(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode)));
    }
    std::string Sdl2Platform::Keyboard::GetScancodeName(const Scancode scancode) const
    {
        const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
        return name != nullptr ? std::string(name) : std::string();
    }
    Scancode Sdl2Platform::Keyboard::GetScancodeFromName(const std::string& name) const
    {
        return ToScancode(SDL_GetScancodeFromName(name.c_str()));
    }
    std::string Sdl2Platform::Keyboard::GetKeyName(const Scancode scancode) const
    {
        const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode)));
        return name != nullptr ? std::string(name) : std::string();
    }
    KeyCode Sdl2Platform::Keyboard::GetKeyFromName(const std::string& name) const
    {
        return ToKeyCode(SDL_GetKeyFromName(name.c_str()));
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
