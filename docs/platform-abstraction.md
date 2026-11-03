# CNA platform abstraction

This is the current implementer's guide to CNA::Platform. The completed PLAT migration remains
in Git history; source, contracts and focused tests define the implemented boundary. See
[renderer boundary](platform-renderer-boundary.md), [physical modules](physical-modules.md) and
[SDL lifecycle audit](platform-sdl-lifecycle-audit.md) for the related ownership/translation rules.

## Responsibilities and selection

IPlatform owns host integration: subsystem acquisition, windows/events/timing, input snapshots/
sessions, display/system/filesystem services, optional clipboard/dialog/tray/camera services,
and narrow GL-context/finished-CPU-frame presentation seams. Application logic stays in Game and
drawing stays in the renderer. Audio uses its separate CNA_AUDIO_PLATFORM contract.

CNA_PLATFORM selects the default PlatformFactory.Create implementation:

| Selection | Implemented boundary |
|---|---|
| SDL3 | The graphical platform implementation, using SDL's compiled/available native video driver. Source presence does not qualify every SDL target/driver. |
| HEADLESS | Virtual window/state for tests/windowless use; no native graphical display, presentation or input service. Always compiled into the platform library. |
| TERMINAL | Non-Windows/termios implementation with a terminal surface and input quality limits. Availability depends on its terminal probe; see [terminal analysis](platform-terminal-analysis.md). |

Factory names are case-sensitive: SDL3, Headless, Terminal. Default construction and named
construction have separate translation units: a consumer using the default factory need not link
every named implementation. GetAvailable reports implementations compiled into the library, not
a guarantee every one is retained in a final static executable.

Example selections (reuse the matching existing build tree):

```sh
cmake -S . -B cmake-build-debug -DCNA_PLATFORM=SDL3
cmake -S . -B cmake-build-headless -DCNA_PLATFORM=HEADLESS -DCNA_GRAPHICS_RENDERER=HEADLESS
cmake -S . -B cmake-build-terminal -DCNA_PLATFORM=TERMINAL -DCNA_GRAPHICS_RENDERER=SOFTWARE
```

Those recipes select a windowing platform; HEADLESS/TERMINAL alone do not remove the selected
audio/renderer dependencies. An explicitly SDL-free composition additionally uses CNA_ENABLE_SDL=OFF,
compatible HEADLESS/SOFTWARE/STUB rendering and NULL or Linux ALSA audio. Clear an incompatible
CNA_GRAPHICS_RENDERERS list when reusing a multi tree. SdlAvailability.cmake refuses combinations
requiring SDL rather than silently substituting a renderer/platform/audio implementation.

Windowed targets use SDL3; CNA's direct WIN32/X11/WAYLAND platforms and SDL2 platform/audio choices
are retired and refused. SDL12 and a direct EMSCRIPTEN platform remain reserved/unimplemented;
the browser currently uses SDL3's Emscripten driver.

## Host services and capability meaning

PlatformCapabilities fields default false. Implementations opt into implemented operations;
callers cache the stable service/capability information outside inner loops. Repeated-query
stability is covered by conformance, but a capability is not proof of hardware presence,
permission, a running desktop host or successful future allocation.

- Presence accessors agree with the corresponding capability; quality flags such as
  exactKeyboardState/pixelAccurateMouse do not imply that the keyboard/mouse service is absent.
  Terminal deliberately supplies lower-quality services.
- Filesystem/system-information services are always present; individual values can be unknown.
  GetDialogs serves either of message-box/native-file-dialog capabilities.
- GetCamera/GetTray are non-virtual link anchors forwarding to protected implementation hooks.
  In the SDL3 implementation they install/resolve lazy service factories; a program that only
  queries support need not pull the capture/tray implementation.
- SDL3's whole capability query or Supports(Camera) can initialize the camera subsystem. Other
  Supports queries avoid that side effect. A driver/init probe is not a physical capture or
  permission guarantee; failures can prevent a camera service from being available.
- Game caches its exposed capability set on the first GetPlatformCapabilitiesEXT call, rather
  than constructing optional camera services during Game construction.

Unsupported operations use PlatformNotSupportedException naming the capability.
Ordinary absence returns the documented empty/status/null result; operational failure uses
PlatformException with operation/detail. Higher-level CNA wrappers can intentionally map those
exceptions to their own false/empty outcomes; see [device extensions](devices-ext.md).

A platform with multipleWindows=false refuses a second live virtual/native window and releases
that capacity on destruction. Borderless/exclusive fullscreen are distinct requests; unsupported
behavior must not be reported as successful substitution.

## Subsystems, ownership and teardown

There is no platform-wide Initialize/Shutdown. CNA acquires Video/Audio/Gamepad/Haptic/Sensor
lazily, balances successful acquisitions, tolerates an unpaired release as a no-op and releases
its own references on platform destruction. An embedding application's native-library references
remain its responsibility.

Process-global native state must be serialized across platform instances. SDL3 uses
SdlGlobalStateMutex with per-instance ownedRefCounts; GL attribute transactions share that
serialization. Acquiring a subsystem must not wait for a main-thread callback that needs the
blocked thread's event pump. Windowless paths must avoid video initialization.

Service pointers are borrowed from the owning platform. Windows/sessions/context handles/
presenters need explicit lifetime owners and callback barriers as appropriate. Game owns and
installs its platform before later members, restores the ambient installation on construction
failure and destroys the platform last. Standalone consumers must respect their ambient platform's
lifetime; they cannot retain a borrowed service after its owner is gone.

Destroy inward resources before their host: renderer/context resources, CPU presenter, window,
then the video acquisition. CPU presenter ownership is GraphicsDevice's; GL renderers own their
created context while borrowing the platform GL service.

## Events, snapshots and runtime reaction

PollEvents clears/fills a caller-owned vector so capacity can be reused. Native mapping produces
owning PlatformEvent values inside the implementation. Update event taxonomy, mappers, input bridge
and the golden transcript together when adding a variant.

Game owns the event drain, sends each event through the input bridge first and then performs
runtime reactions. It intentionally does not filter every window event by its own id.
WindowId is a stable nonzero platform identity, separate from native handle fields and opaque
XNA window tokens. There is no second renderer event queue.

Input services normally advance once per Game pump and expose snapshots afterwards; individual
contracts still define consume-on-read data such as relative mouse deltas. Platform-owned sensor
callbacks and asynchronous dialog/device completion do not become game-thread events merely
because the window/input pump uses batches.

Size/density refresh and surface invalidation are separate renderer calls. Mobile Game suspension
is a runtime-loop policy, not an OnSurfaceChanged reason enum.
See [platform-renderer-boundary.md](platform-renderer-boundary.md) for the actual call table.

## Native window handles and units

NativeWindowHandle is a trivially-copyable borrowed discriminated value. Inspect system and use
the matching TryGet accessor. An X11 XID is an integer, not a pointer.

| System | Populated native fields |
|---|---|
| Win32 | window = HWND |
| X11 | display = Display*, windowId = Window XID |
| Wayland | display = wl_display*, surface = wl_surface* |
| Cocoa | window = NSWindow* |
| UIKit | window = UIWindow* |
| Android | window = ANativeWindow* |
| Web, Headless, Terminal | No native pointer payload; platform/browser presentation has its own route |

Native DirectX9/WebGPU use applicable typed native handles; EasyGL uses IPlatformGlContext.
SDLGPU/FNA3D have explicit internal SDL interop. Handle availability is not renderer qualification
on that OS (for example, a UIKit handle alone does not qualify native WebGPU).
Native handle lifetime ends with the native window; Describe omits pointer values.

Client bounds/input use logical units, drawable dimensions use physical pixels and scale comes
from the platform. Invalid density is normalized by consumers as documented. SetSize can be
asynchronous; Sync makes requests observable. Headless intentionally models pending size changes.

SurfaceFrame is forward row-major RGBA8. Stride zero means width*4; a nonzero stride must fit a row.
Presenters validate dimensions/stride/address-span arithmetic, but cannot infer the allocation
length behind a raw pointer. The caller retains complete storage through Present.

## Layering and performance policy

The platform describes CNA's needs rather than copying one host API. Contract headers expose no
SDL/native-toolkit declarations; native handles carry discriminated opaque fields instead.
CNA::Platform itself is not XNA API, while CNAEXT accessors/constructors can intentionally expose
its types from XNA headers. Private forward declarations/members also appear there; the old claim
that the namespace never appears in an XNA header was incorrect.

The SDL renderer allowlist is SDLGPU (SDL GPU by identity) and FNA3D (SDL-dependent wrapped library
plus small integration seams). Other families are SDL-free under the source audit.
CNA_ENABLE_SDL=OFF also removes the native audio edge only with an SDL-free audio selection.
WAV-to-PCM decoding is CNA-owned and independent of window/audio selection; it does not require
SDL merely to decode content. ALSA is a separate mixer/device integration, whose host PCM routing
is documented in [audio-alsa.md](audio-alsa.md).

Keep platform dispatch outside primitive/vertex/pixel loops, reuse event/snapshot capacity and
present completed CPU frames. This is a design policy, not a universal allocation-free guarantee
for every provider or enabled extension. hot_path_lint recognizes platform calls in selected loop
shapes; it is not an allocator profiler or proof of every runtime call path.
[platform-performance.md](platform-performance.md) records historical bounded measurements, not
a performance baseline for today's entire tree.

## Adding an implementation

New platform work needs a concrete host/capability benefit. The retired duplicate direct desktop
implementations do not authorize recreating them incidentally.

1. Implement the IPlatform surface under modules/platform/src/<Name>, sharing genuinely portable
   helpers through Common and keeping native includes/libraries at the implementation edge.
2. Add explicit host/selection rules in PlatformSelection, source/link rules in the module manifest
   and default/named/available factory behavior. Unsupported hosts must fail clearly.
3. Start capabilities false, implementing each accessor/refusal/quality contract before enabling it.
   Give native resources explicit owners and constructor-failure/teardown coverage.
4. Map owning events/CNA input enums at the edge; add implementation-specific translation tests and
   parameterized platform/window conformance. Add a selected-default build and compatible renderer.
5. Run implementation-neutral, native integration and mechanical gates. Use the private runner
   for window/GPU tests, for example:

```sh
tools/platform/run_gpu_tests_private.sh --exec cmake-build-multi/CnaTests \
  '--gtest_filter=EveryImplementation/PlatformConformance.*:EveryImplementation/PlatformWindowConformance.*:EveryImplementation/GameEventSemanticsGoldenTest.*'
python3 tools/platform/sdl_inventory.py --check
python3 tools/platform/sdl_classify.py --check
python3 tools/platform/renderer_sdl_audit.py --check
python3 tools/platform/sdl_ratchet.py --check --strict
python3 tools/platform/hot_path_lint.py
python3 tools/platform/nonproduction_sdl_audit.py --check
python3 tools/platform/check_contract.py
```

Platform contract/native tests live under modules/platform/tests, runtime reactions under
modules/runtime/tests, and consumers use canned platform services in their own modules.
The renderer audit's --check enforces coupling metadata; compare regenerated report output
separately for textual freshness. SDL inventory --check does compare the generated block below.

## Current audit boundary

The 2026-11-01 reconciliation checks current headers/selection/ownership/event paths and
regenerates the inventory. Passing mechanical gates do not qualify every native OS, permission
flow or device lifecycle. Fresh focused build/test evidence is recorded below.

## SDL inventory (generated)

Regenerate with `python3 tools/platform/sdl_inventory.py --update`; `--check` (one of the gates
above) fails when it is stale. Do not hand-edit the block. A reference counts when it is a textual
SDL identifier or an `SDL3/`-rooted include, including forward declarations and comments.
The tool excludes CNA renderer identity/build-macro spellings rather than counting them as SDL APIs.

<!-- BEGIN GENERATED: tools/platform/sdl_inventory.py -->

| Metric | Value |
|---|---|
| Distinct `SDL_*` identifiers referenced anywhere under `modules/` | **965** |
| Files referencing SDL (all) | **138** |
| Production files (`src/` + `include/`) referencing SDL | **52** |
| …of which are renderer production files | **14** |
| Test/example files referencing SDL | **86** |
| Distinct `SDL_PROP_WINDOW_*` native-handle properties read | **9** |
| Renderer families reaching for `SDL_GL_*` directly | **0** |

Production SDL surface per module (`src/` + `include/` only):

| Module | Files | Dominant concern |
|---|---:|---|
| `modules/platform` | 32 | - |
| `modules/audio` | 6 | audio device/stream, mixer, microphone |
| `modules/renderers/*` | 14 | native window handle, GL context, SDL GPU device (2 families) |

Window properties consumed by platform/renderer interop are listed below. Some populate
`NativeWindowHandle`; the browser canvas identifier instead serves its platform route.
The list is not a one-to-one field layout of that native-handle value:

```text
SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER
SDL_PROP_WINDOW_COCOA_WINDOW_POINTER
SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING
SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER
SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER
SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER
SDL_PROP_WINDOW_WIN32_HWND_POINTER
SDL_PROP_WINDOW_X11_DISPLAY_POINTER
SDL_PROP_WINDOW_X11_WINDOW_NUMBER
```

No renderer family calls `SDL_GL_*` directly; GL window/context operations use
`IPlatformGlContext` at the platform edge.

<!-- END GENERATED: tools/platform/sdl_inventory.py -->
