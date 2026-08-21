**It is technically possible**, but SDL2 will be significantly easier than real SDL 1.2.

The most important thing is that you do not try to create a "common SDL wrapper" imitating the intersection of all three APIs. A better architecture is:

```text
CNA Core
   │
   ├── CNA Platform API
   │      ├── SDL3 Platform
   │      ├── SDL2 Platform
   │      └── SDL 1.2 Platform
   │
   ├── CNA Graphics API
   │      ├── Vulkan
   │      ├── OpenGL
   │      ├── Software
   │      ├── GDI
   │      └── ...
   │
   ├── CNA Audio API
   └── CNA Input API
```

The Platform API must describe **what CNA needs**, not SDL's functions.

## SDL2: very realistic

SDL2 and SDL3 are not source-identical; SDL has a separate, extensive migration guide, and return conventions, function names, events and other parts of the API changed. That, however, does not prevent both from implementing the same internal CNA platform abstraction. ([SDL Wiki][1])

SDL2 is suitable especially for:

* older Windows;
* older Linux distributions;
* platforms where SDL3 is not practical;
* games that do not need the SDL3 GPU API;
* a long-term fallback platform path.

The official SDL2 documentation states Windows support going back as far as Windows XP. ([SDL Wiki][2])

As a rough estimate, an SDL2 implementation could be created without a fundamental change to the game API, provided you first remove the direct SDL3 calls from core, input, audio and the individual backends.

## SDL 1.2: possible, but as a limited legacy profile

Classic SDL 1.2 is officially deprecated, and the SDL team warns that it is no longer actively developed and will gradually degrade. The last official release is 1.2.15. ([libsdl.org][3])

That does not mean CNA cannot use it. It means you will own that implementation for the long term.

An SDL 1.2 platform would probably offer only a limited profile:

* a basic window;
* keyboard and mouse;
* the older joystick API;
* timing;
* basic audio;
* OpenGL or a software surface;
* basic fullscreen modes;
* limited clipboard, text input, gamepad and HiDPI contract.

Modern features must be declared unsupported, not emulated with dangerous hacks.

## Watch out for compatibility projects

There are official projects:

* `sdl2-compat`: provides the SDL2 API on top of SDL3;
* `sdl12-compat`: provides the SDL 1.2 API on top of SDL2;
* they can even be chained all the way to SDL3. ([GitHub][4])

These are useful for application compatibility, but **they do not solve your goal of supporting genuinely old platforms**. If `sdl12-compat` ultimately runs on top of SDL2 or SDL3, you do not gain an operating system on which the underlying SDL does not run.

For CNA, therefore, three distinct options make sense:

```text
CNA + real SDL3
CNA + real SDL2 Classic
CNA + real SDL 1.2 Classic
```

The compatibility layers can additionally be test configurations, not a replacement for the real implementations.

## What the CNA Platform API must contain

I would recommend at least these parts:

```cpp
namespace CNA::Platform {

class Application;
class Window;
class EventLoop;
class Keyboard;
class Mouse;
class Gamepad;
class Clipboard;
class Cursor;
class Timer;
class DisplayManager;
class DynamicLibrary;
class FileSystem;
class MessageBox;
class NativeWindowHandle;

}
```

I would consider separating audio:

```text
CNA.Audio.Platform
├── SDL3 Audio
├── SDL2 Audio
├── SDL 1.2 Audio
├── OpenAL
└── other future implementations
```

Likewise, the gamepad can be its own module, because the SDL1 joystick, SDL2 GameController and SDL3 Gamepad have significantly different capabilities.

## Graphics backends must not automatically depend on SDL3

Today a backend may reach directly for:

```cpp
SDL_Window*
SDL_PropertiesID
SDL_GetWindowProperties(...)
```

After the separation it should receive something like:

```cpp
struct NativeWindowHandle {
    NativeWindowSystem system;
    void* display;
    void* window;
    void* surface;
};
```

Or more type-safe variants for:

* Win32 `HWND`;
* X11 `Display*` + `Window`;
* Wayland display/surface;
* Cocoa window/view;
* Android native window;
* web canvas.

Then Vulkan, OpenGL, DirectX, GDI or Glide will not know whether the window was created by SDL3, SDL2, SDL1 or a future native platform backend.

The exceptions will be the deliberately SDL-specific graphics backends:

```text
SDL GPU       → requires SDL3
SDL Renderer  → variant depending on SDL3/SDL2
SDL 1.2 Surface Renderer → legacy path only
```

SDL GPU is a modern API available in SDL3, so it cannot simply be preserved as the same path for SDL2 or SDL1. ([SDL Wiki][5])

## Platform capability model

Just as with graphics, you need capability queries:

```cpp
struct PlatformCapabilities {
    bool supportsMultipleWindows;
    bool supportsHighDpi;
    bool supportsClipboard;
    bool supportsTextInput;
    bool supportsIme;
    bool supportsGamepadRumble;
    bool supportsGamepadSensors;
    bool supportsNativeFileDialog;
    bool supportsVulkanSurface;
    bool supportsOpenGLContext;
};
```

The game or the CNA layer will then not assume that every platform implementation can do everything.

## C++23 remains a separate problem

Separating SDL3 is a necessary condition for old systems, but **it is not sufficient on its own**.

For truly old Windows you will probably also need:

* an older compatible compiler/toolchain;
* a lower language profile than full C++23;
* restrictions on the modern standard library;
* removal of dependencies on new system APIs;
* possibly a C ABI between the modern CNA core and the legacy host.

A practical model may be:

```text
CNA Modern
    C++23
    SDL3 / SDL2
    all modern features

CNA Legacy
    a limited compatible subset
    SDL2 or SDL 1.2
    selected graphics/audio backends
    without some NoXNA extensions
```

You do not necessarily have to compile all 40 graphics backends for every legacy profile.

## The safest implementation order

1. **An inventory of all direct uses of SDL3.**
2. Define the platform API according to CNA's real needs.
3. Move the current SDL3 behavior behind `Sdl3Platform`.
4. Prove behavioral equivalence without adding SDL2.
5. Add `Sdl2Platform`.
6. Create a platform conformance suite.
7. Only then design a limited `Sdl12Platform`.
8. Finally, address the legacy C++/toolchain profile.

An important principle:

> Do not start from an abstraction based on what SDL1, SDL2 and SDL3 have in common. Start from the contract CNA needs, and let each SDL implement that contract to the best of its ability.

So yes: **SDL3 + SDL2 is a very realistic goal. Real SDL 1.2 is also possible, but rather as a historical and limited platform implementation, not a full-featured replacement for SDL3 for all modern CNA features.**

[1]: https://wiki.libsdl.org/SDL3/README-migration?utm_source=chatgpt.com "SDL3/README-migration"
[2]: https://wiki.libsdl.org/SDL2/Installation?utm_source=chatgpt.com "SDL2/Installation"
[3]: https://www.libsdl.org/download-1.2.php?utm_source=chatgpt.com "SDL version 1.2.15 (historic)"
[4]: https://github.com/libsdl-org/sdl2-compat?utm_source=chatgpt.com "An SDL2 compatibility layer that uses SDL3 behind ..."
[5]: https://wiki.libsdl.org/SDL3/CategoryGPU?utm_source=chatgpt.com "SDL3/CategoryGPU"



























With a correct design, performance will go down **practically immeasurably**. For an ordinary CNA game I would expect roughly:

| Platform abstraction design                                 | Estimated impact on whole-game performance |
| ----------------------------------------------------------- | -----------------------------------------: |
| A well-designed, coarse-grained abstraction                  |                             **0–0.3 %** |
| More virtual calls, but only for windows and events          |                               **0–1 %** |
| A badly designed abstraction on frequently called paths      |                               **1–5 %** |
| An abstraction called for every pixel, audio sample or vertex |             potentially a significant drop |

For CNA, the impact on **code complexity, builds and testing** will probably matter more than the runtime performance.

## Why the impact will be small

The platform layer typically handles:

* creating and destroying a window;
* processing events;
* keyboard, mouse and gamepads;
* clipboard;
* cursor;
* timing;
* fullscreen and resolution changes;
* obtaining the native window handle;
* possibly audio devices.

Most of these operations happen once per frame or even less often. Even if each one went through a virtual function or a function pointer, the cost is negligible compared to rendering, physics, audio mixing and game logic.

For example, instead of directly:

```cpp
SDL_PollEvent(&event);
```

CNA can do:

```cpp
platform->PollEvents(eventQueue);
```

One indirect call per frame will have practically no impact. The system's own event processing is much more expensive than selecting an implementation through a vtable.

## Where performance really could get worse

### Calling the platform layer for every single event

A worse design:

```cpp
while (platform->PollSingleEvent(event))
{
    ProcessEvent(event);
}
```

A better design:

```cpp
platform->PollEvents(eventBatch);

for (const auto& event : eventBatch)
{
    ProcessEvent(event);
}
```

The difference will usually be small anyway, but a batch interface reduces the number of indirect calls and better separates SDL data structures from CNA.

### Audio sample by sample

This would be wrong:

```cpp
for (std::size_t i = 0; i < sampleCount; ++i)
{
    output[i] = audioPlatform->MixOneSample();
}
```

Correct:

```cpp
audioPlatform->FillBuffer(output, sampleCount);
```

A platform dispatch once per whole audio buffer is negligible. A dispatch once per sample can already be significant.

### Routing every graphics draw call through the platform API

The graphics backend must not do something like:

```cpp
platform->GraphicsDraw(...);
```

The platform API should only hand the graphics layer:

* the window size;
* the native handle;
* surface information;
* DPI;
* lifecycle events.

The draw calls themselves belong directly in the chosen graphics backend:

```text
Game
  ↓
GraphicsDevice
  ↓
Vulkan / Bgfx / OpenGL / GDI / Glide
```

Not:

```text
Game
  ↓
GraphicsDevice
  ↓
Platform API
  ↓
SDL implementation
  ↓
Graphics backend
```

This avoids another layer on the most frequently called path.

## Recommended structure

```cpp
class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual std::unique_ptr<IWindow>
    CreateWindow(const WindowDescription& description) = 0;

    virtual void PollEvents(std::vector<PlatformEvent>& destination) = 0;

    virtual KeyboardState GetKeyboardState() const = 0;
    virtual MouseState GetMouseState() const = 0;

    virtual std::uint64_t GetPerformanceCounter() const = 0;
    virtual std::uint64_t GetPerformanceFrequency() const = 0;

    virtual PlatformCapabilities GetCapabilities() const = 0;
};
```

Implementations:

```cpp
class Sdl3Platform final : public IPlatform {};
class Sdl2Platform final : public IPlatform {};
class Sdl12Platform final : public IPlatform {};
```

The platform is chosen once at startup:

```cpp
std::unique_ptr<IPlatform> platform =
    PlatformFactory::Create(configuration.platformBackend);
```

After that the pointer does not change. The CPU branch predictor usually remembers the indirect target well.

## An even faster variant

If the platform is chosen at compile time, virtual calls can be avoided entirely:

```cpp
using ActivePlatform = Sdl3Platform;
```

or via CMake:

```text
CNA_PLATFORM=SDL3
CNA_PLATFORM=SDL2
CNA_PLATFORM=SDL12
```

The resulting binary will contain only one implementation. The compiler can inline part of the calls and the runtime overhead will be practically zero.

Dynamic selection can, however, be useful for example for a single binary supporting multiple platform layers. Even then the overhead will not be significant, as long as the dispatch does not get into inner loops.

## The native window handle without a long call chain

I would not pass the whole `IPlatform` to the graphics backend. I would pass it a ready-made description of the native window at initialization:

```cpp
struct NativeWindowHandle {
    NativeWindowSystem system;

    void* display;
    void* window;
    void* surface;
};
```

The backend then stores the handle:

```cpp
vulkanBackend.Initialize(platformWindow.GetNativeHandle());
```

It will not call several layers of the platform API again every frame.

## Input snapshots

Instead of thousands of platform queries:

```cpp
platform->IsKeyDown(Key::A);
platform->IsKeyDown(Key::B);
platform->IsKeyDown(Key::C);
```

it is better to create a snapshot once per frame:

```cpp
platform->UpdateInput();

const KeyboardState keyboard = platform->GetKeyboardState();
```

And the game then reads a local bit set. This can even be faster than the current direct SDL path, if the present implementation performs repeated conversions.

## Do not push capabilities into the hot path

Do not repeatedly call:

```cpp
if (platform->GetCapabilities().supportsHighDpi)
```

Read the capability structure once:

```cpp
const PlatformCapabilities capabilities = platform->GetCapabilities();
```

And then keep it in `GraphicsDeviceManager`, `GameWindow` or the corresponding subsystem.

## The biggest costs will be elsewhere

Adding SDL3/SDL2/SDL1 implementations will probably increase:

* the number of build configurations;
* the size of the source tree;
* the number of CI combinations;
* the number of platform fixtures;
* the complexity of native handle interop;
* the amount of conditional capabilities;
* the maintenance of input and audio differences.

Runtime performance will hardly change. A much greater risk is that one implementation will have different event semantics, DPI, fullscreen, gamepad or timing behavior than the others.

## Separate audio out on its own

The platform abstraction and the audio backend should not necessarily be one thing:

```text
Platform:
  SDL3
  SDL2
  SDL1
  Native Win32
  Headless

Audio:
  SDL3 Audio
  SDL2 Audio
  SDL1 Audio
  OpenAL
  WASAPI
  ALSA
  Null Audio
```

The audio callback then receives a whole buffer. It will not go through a generic platform interface for every sample.

## Recommended performance contract

When modularizing, I would set a simple rule:

> The platform abstraction must not be called inside a per-pixel, per-vertex, per-fragment, per-audio-sample or any other elementary loop.

What is mainly allowed is:

* once at initialization;
* once or a few times per frame;
* once per whole event batch;
* once per whole audio buffer;
* on an actual window or device change.

If this rule is followed, I expect a performance drop for CNA of **typically under 0.5 %, and probably below the threshold of a consistently measurable difference**. Some paths may even get slightly faster after centralizing events and input snapshots.
