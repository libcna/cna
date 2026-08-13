# CNA platform abstraction

This is the implementer's guide to `CNA::Platform`. It describes the contract a platform backend
must satisfy and the checks that keep the rest of CNA independent from its native host library.
For migration history and task evidence, see `plan_platform.md`; for the SDL lifecycle derivation,
see `docs/platform-sdl-lifecycle-audit.md`.

## What the boundary owns

`IPlatform` owns host integration, not application logic and not rendering:

- subsystem acquisition and release;
- windows, events and monotonic timing;
- input snapshots and device sessions;
- displays, clipboard, dialogs, tray, cameras, filesystem and host information;
- creation seams for OpenGL contexts, Vulkan surfaces and finished CPU-frame presentation.

A graphics renderer receives `RendererSurfaceInfo` and the one narrow service it needs. It then
talks directly to its graphics API. `IPlatformSurfacePresenter` is deliberately a one-finished-
frame API, not a drawing API. Audio selection is orthogonal and uses `CNA_AUDIO_PLATFORM`.

`CNA_PLATFORM` selects the default implementation created by `PlatformFactory::Create()`:

```sh
cmake -S . -B build -DCNA_PLATFORM=SDL3
cmake -S . -B build-headless \
  -DCNA_PLATFORM=HEADLESS -DCNA_GRAPHICS_RENDERER=HEADLESS
cmake -S . -B build-terminal \
  -DCNA_PLATFORM=TERMINAL -DCNA_GRAPHICS_RENDERER=HEADLESS
```

Headless is compiled in every build. Terminal is also compiled on POSIX. This lets the conformance
suite exercise multiple implementations in one process; the selected value determines the
default, not necessarily the only factory name present in the binary.

## Contract rules

### Capabilities are promises

Read `PlatformCapabilities` once and cache it outside hot paths. Every field defaults to false.
An implementation opts into only behavior it actually provides.

- A service accessor is non-null exactly when its presence capability is true.
- Keyboard and mouse quality flags are exceptions to that equality: a Terminal keyboard exists
  while `exactKeyboardState` is false, and its mouse may exist while coordinates are not pixel
  accurate.
- Filesystem and system-information services are always non-null. Their individual queries may
  return an unavailable/unknown result.
- `GetDialogs()` is non-null when either message boxes or native file dialogs are supported.
- Capabilities do not change during an instance's lifetime.

Unsupported behavior refuses deterministically with `PlatformNotSupportedException`, naming the
missing capability. It must not silently succeed. Ordinary absence uses a status, empty value or
null service as documented; operational failure throws `PlatformException` with the CNA operation
name and backend detail text.

### Subsystems are acquired, not globally initialized

There is intentionally no platform-wide `Initialize()` or `Shutdown()` method. CNA production
code acquires `Video`, `Audio`, `Gamepad`, `Haptic` and `Sensor` lazily and balances every
successful acquisition. Global native-library lifetime belongs to the embedding application.

Implementations must:

- refcount nested acquisitions;
- tolerate an unpaired release as a no-op;
- release exactly the references owned by that platform instance in its destructor;
- never route acquisition through a main-thread callback that requires an event pump;
- preserve a display-free branch by doing no video initialization for a headless device;
- serialize process-global native subsystem state across platform instances when the backend
  library itself exposes process-global lifetime.

For SDL3, `SubsystemLifecycleMutex()` is process-wide, while `ownedRefCounts_` is per instance.
The application remains responsible for its own `SDL_Init()`/`SDL_Quit()` calls.

### Events are batches

`PollEvents(std::vector<PlatformEvent>&)` clears and fills a caller-owned vector. The caller reuses
its capacity each frame. Do not replace this with per-event virtual callbacks or return a freshly
allocated container.

Native event mapping belongs inside the implementation. Runtime behavior consumes only
`PlatformEvent`. New event variants must update the event taxonomy helpers, every mapper, the
input bridge where applicable, and the cross-implementation golden transcript.

Window IDs are stable, non-zero identifiers within a platform instance. The current runtime does
not filter all events by its own window ID; that observable behavior is pinned by the golden test.

### Ownership and teardown

The object returned by `PlatformFactory` owns its services. Service pointers are borrowed and
remain valid only while their platform exists. Windows, contexts, surfaces, device sessions and
presenters use owning handles (`std::unique_ptr` or the service-specific RAII type).

`Game` declares its owned platform before every other member, installs it as the ambient platform
from that first member initializer, and destroys it last. Code must not cache the ambient pointer
beyond the lifetime of the owner that supplied it.

Destroy resources from the consumer inward: renderer resources, graphics context/surface, window,
then the acquired subsystem. Callback-producing devices must establish a callback barrier before
closing their native handle.

## Native window handles

`NativeWindowHandle` is a trivially-copyable, non-owning snapshot. Always inspect `system` and use
the matching `TryGet*` accessor. Never reinterpret a field based on the build host.

| `NativeWindowSystem` | `display` | `window` | `surface` | `windowId` |
|---|---|---|---|---|
| `Win32` | — | `HWND` | — | — |
| `X11` | `Display*` | — | — | X11 `Window` XID |
| `Wayland` | `wl_display*` | — | `wl_surface*` | — |
| `Cocoa` | — | `NSWindow*` | — | — |
| `Android` | — | `ANativeWindow*` | — | — |
| `Web` | — | — | — | — |
| `Headless` | — | — | — | — |
| `Terminal` | — | — | — | — |

An X11 `Window` is an integer XID, not a pointer. It must stay in `windowId`. Empty Web, Headless
and Terminal handles are valid descriptions of implementations with no native graphical window;
a renderer that requires one must refuse clearly. `Describe()` deliberately reports populated
fields without logging pointer values.

The stable CNA `WindowId` used by events and renderer registries is separate from this native
snapshot and from the legacy integer window-handle token.

## Window units and surface updates

Window client bounds use logical units. Drawable/pixel size uses physical pixels. Display scale
connects the two but must never be inferred by dividing dimensions when the platform can report it
directly. A zero or invalid native scale is normalized to 1.0.

`SetSize()` may be asynchronous; `Sync()` makes requested state observable. The Headless backend
models this deliberately so code cannot accidentally depend on immediate application.

Renderers receive surface changes through their platform-neutral state and keep these concerns
separate:

- logical client coordinates for input;
- physical drawable dimensions for backbuffers and swapchains;
- renderer-owned viewport/letterbox/overscan transforms;
- stable window identity for the renderer registry.

## Performance contract

The platform boundary is outside inner draw, audio-callback and input-element loops.

- Poll native events once per frame into one reused batch.
- Update each input service once per frame, then read snapshots without native polling.
- Cache capabilities and stable services; do not query them per element or per draw.
- Present a completed CPU frame once, never dispatch drawing primitives through `IPlatform`.
- Do not allocate after an event batch or device snapshot reaches steady-state capacity.
- Keep timing calls monotonic and `GetPerformanceFrequency()` non-zero.

`tools/platform/hot_path_lint.py` rejects platform calls inside recognized production hot loops.
The checked-in performance baseline and PLAT-120 comparison define the measured noise floor; a
regression beyond it is investigated rather than waived.

## Adding an implementation

1. Add `modules/platform/src/<Name>/` and implement the entire `IPlatform` surface. Share only
   genuinely portable pieces through `src/Common/`; do not subclass the SDL3 implementation.
2. Add the uppercase selection to `cmake/PlatformSelection.cmake`. Fail loudly for unsupported
   host combinations rather than falling back to another backend.
3. Add conditional sources and private native-library links in `modules/platform/CMakeLists.txt`.
   Native dependencies must not enter the public include interface.
4. Register the stable display name in all three `PlatformFactory` operations: default selection,
   named construction and `GetAvailable()`.
5. Start with every capability false. Flip each field only when the corresponding accessor and
   refusal behavior are complete.
6. Map native input/events into CNA enums and owning event values at the platform edge. Native
   pointers or event-lifetime strings must not cross it.
7. Give every acquired native resource an explicit owner and test constructor-failure as well as
   ordinary teardown order.
8. Run the implementation-neutral suites. They are parameterized over
   `PlatformFactory::GetAvailable()`, so a correctly registered implementation joins
   automatically:

   ```sh
   CnaTests --gtest_filter='EveryImplementation/PlatformConformance.*:EveryImplementation/PlatformWindowConformance.*:EveryImplementation/GameEventSemanticsGoldenTest.*'
   ```

9. Add implementation-specific mapper and native integration tests. These complement rather than
   replace conformance: conformance tests the CNA contract; native tests prove the translation.
10. Configure a build where the new implementation is the selected default, with no environment
    that only the old implementation needs. Build and run the full non-environmental suite.
11. Run all mechanical gates:

    ```sh
    python3 tools/platform/sdl_inventory.py --check
    python3 tools/platform/sdl_classify.py --check
    python3 tools/platform/renderer_sdl_audit.py --check
    python3 tools/platform/sdl_ratchet.py --check --strict
    python3 tools/platform/hot_path_lint.py
    ```

12. Add the selection to the CI matrix with one compatible renderer. Do not multiply every
    platform by every renderer: cover materially different seams and keep the matrix deliberate.

## Where tests belong

- Contract and cross-implementation behavior: `modules/platform/tests/CNA/Platform/` with a
  parameterized suite over `PlatformFactory::GetAvailable()`.
- Native translation and backend integration: the same platform test area, clearly named for the
  implementation and compiled only when it is present.
- Runtime reaction to events: `modules/runtime/tests/.../GameEventSemanticsGoldenTests.cpp`.
- Consumer behavior: the consuming module, using canned platform services rather than native
  event injection.

The generated SDL inventory and ratchet treat any new native reference outside the platform/audio
and four renderer exceptions as a boundary regression. A test that genuinely verifies SDL3 may
remain native-specific, but production consumers do not gain that exception.
