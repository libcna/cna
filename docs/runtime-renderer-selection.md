# Runtime graphics renderer selection

**Status: usable.** Runtime renderer selection works and is verified across several build sets (see
the table at the end). The renderer-gated test/example corpus has been audited and its applicable
sites converted or deliberately retained as compile-time family/default gates. The remaining work
is limited to the platform-specific validation sets and measurements listed at the end of
`plans/plan_runtimerenderer.md`.

---

## What this is

CNA selects its graphics renderer at **compile time** (`-DCNA_GRAPHICS_RENDERER=<X>`). That stays
the default and recommended mode: it produces the smallest binary and lets the compiler eliminate
every renderer that was not selected.

This document describes a **second, opt-in mode**, in which CNA is compiled with several renderers
linked in and the concrete one is chosen at runtime, before CNA is started.

---

## Status

| Capability | Status |
|---|---|
| Descriptor / registry value types | ✅ present (`GraphicsRendererDescriptor`, `GraphicsRendererRegistry`, `GraphicsRendererFallbackRecord`) |
| Pre-window contract extracted from `GraphicsDevice` | ✅ window flags, `SDL_INIT_VIDEO`, the no-window branch and OPENGL1's GLX attributes are all descriptor-driven |
| Per-family descriptors | ✅ all 36 implementation families / 40 public identities; guarded by `scripts/check_runtime_renderer_discipline.py` |
| Namespaced factories / generated registry | ✅ all 45 factories namespaced; `cmake/RendererRegistry.cmake` emits the table, and the discipline gate checks every identity reaches it |
| `GraphicsRendererSelection` API | ✅ selection, latch, env var, availability; 20 tests |
| Fallback chain | ✅ resolution, recording, logging and exhaustion; cross-window-kind recreation verified with `SDL_RENDERER;OPENGLES3;HEADLESS` and `OPENGLES3;VULKAN;SOFTWARE;HEADLESS;STUB` multi builds |
| Multi-renderer CMake mode | ✅ `CNA_GRAPHICS_RENDERERS`, configure-time combination rules, CI job |
| Runtime identity reporting | ✅ `GraphicsDevice::GetGraphicsRendererType/Name()` report the device's real renderer |

Legend: ✅ implemented and verified · 🟨 exists but unverified · ⬜ not implemented.

`modules/graphics/src` contains no `CNA_RENDERER_*` reference at all as of phase P3; renderer-specific
behaviour reaches the XNA layer through `IGraphicsRenderer` virtuals (queries that have a device) or
`GraphicsRendererDescriptor::adapterQueries` hooks (`GraphicsAdapter` queries, which run before one
exists). `scripts/check_runtime_renderer_discipline.py` enforces both.

---

## What a renderer descriptor answers

Four decisions have to be made *before* an `IGraphicsRenderer` instance exists, so no virtual method
can serve them. Each renderer family answers them through its own
`GraphicsRendererDescriptor` (`modules/renderers/<family>/src/*RendererDescriptor.cpp`):

| Question | Field |
|---|---|
| Does this renderer need a window at all? | `needsWindow` — false for `HEADLESS`, `SOFTWARE`, `STUB`, `PORTABLEGL`, `TINYGL` |
| Must the platform's video subsystem be started? | `needsVideoSubsystem` — false for the same families, which is what lets them run with no display server |
| What kind of window does it need? | `windowKind` (`None`/`Plain`/`OpenGL`/`Vulkan`/`Metal`), plus `wantsHighDpi` |
| Anything to fix before the window exists? | `glFramebuffer` — depth/stencil/double-buffer/multisample bits, which a desktop GLX visual fixes at window-creation time. Only `OPENGL1` and `FNA3D` have real work here |
| Which platform services is it handed? | `needsSurfacePresenter`, `needsGlContext`, `needsVulkanSurface` |

These last three groups were function-pointer hooks (`prepareWindowFlags()`,
`applyPreWindowAttributes()`) when this design was written, and became **data** when the platform
contract landed: every hook implementation only mapped the window kind onto flags the windowing
library understood, and `WindowDescription` already carries those. `GraphicsDevice` performs the
mapping once, so no descriptor names a windowing library at all.

Four families compute their window flags at **runtime**, because their own native API is itself a
runtime choice. This predates runtime renderer selection; the plan generalizes their existing
mechanism rather than inventing one:

| Renderer | Decides | Via |
|---|---|---|
| `BGFX` | Vulkan vs OpenGL/GLES | `Bgfx::Detail::ResolveRendererType()`, honouring `CNA_BGFX_RENDERER` |
| `LLGL` | OpenGL module needs a GL window; Vulkan module needs no flag | `Llgl::Detail::RendererModuleNeedsOpenGLWindow()` |
| `FNA3D` | SDL_GPU / D3D11 / OpenGL | `FNA3D_PrepareWindowAttributes`, which also primes the GL attributes |
| `DILIGENT` | D3D12 / Vulkan / D3D11 / OpenGL | `ParseDeviceTypeOverride()`, honouring `CNA_DILIGENT_DEVICE` |

`DILIGENT` carries a documented limitation here: SDL3 rejects a window created with both
`SDL_WINDOW_VULKAN` and `SDL_WINDOW_OPENGL`, so an `auto` build whose first preference fails at
runtime cannot fall through across that boundary against an already-created window
(plans/plan_diligent.md DILIGENT-57).

---

## The API

See `modules/graphics/examples/renderer_selection/renderer_selection_demo.cpp` for a runnable
reference (`cna_demo_renderer_selection`).

```cpp
#include "CNA/GraphicsRendererSelection.hpp"

int main()
{
    // Before any GraphicsDevice is constructed:
    CNA::GraphicsRendererSelection::SetPreferred(CNA::GraphicsRendererType::Vulkan);

    // Optional, off by default: try these in order if Vulkan is unavailable or fails to start.
    CNA::GraphicsRendererSelection::SetFallbackChain({
        CNA::GraphicsRendererType::OpenGLES3,
        CNA::GraphicsRendererType::Software,
    });

    MyGame game;
    game.Run();
}
```

Selection precedence, highest first:

1. an explicit `SetPreferred()` call,
2. the `CNA_GRAPHICS_RENDERER` **environment variable**,
3. the build's compile-time default.

A `CNA_GRAPHICS_RENDERER` value naming a renderer that is not compiled in **throws**, rather than
being ignored — silently ignoring it would leave you believing you had switched renderer when you
had not. The same applies to `SetPreferred()`.

`GetAvailable()` and `GetSelected()` are usable before any `GraphicsDevice` exists, which is the
whole point: the compiled-in set is published into the selection layer before `main()` runs.

---

## Failure policy

**Default: hard failure.** If the selected renderer is not compiled into the build, reports itself
unavailable, or throws during initialization, CNA throws. There is no silent substitution — a game
that asked for Vulkan and quietly got a CPU rasterizer is a worse outcome than a clear error.

**Fallback is opt-in.** `SetFallbackChain()` or `EnableAutomaticFallback(true)` enables it. When
enabled:

- both failure modes are covered: the renderer's availability probe returning false, and its
  construction throwing;
- every step is logged at warning level and recorded in `GetFallbackHistory()`;
- `GetActive()` reports what was really created, which may differ from `GetSelected()`;
- if the chain is exhausted, CNA throws, carrying the first failure as the primary cause.

### The window-kind limitation

SDL3 refuses to create a window carrying both `SDL_WINDOW_OPENGL` and `SDL_WINDOW_VULKAN`. Falling
back across that boundary therefore requires the SDL window to be destroyed and recreated, which is
only legal while CNA owns it. When the game supplied its own window through
`PresentationParameters::DeviceWindowHandle`, such a candidate is skipped and recorded as
`WindowKindConflict` rather than silently attempted.

---

## The latch

Once CNA has begun using the selected renderer, the selection can no longer change: `SetPreferred()`,
`SetFallbackChain()` and `EnableAutomaticFallback()` all throw `System::InvalidOperationException`
after the first `GraphicsDevice` is constructed.

The latch forbids *changing the selection*, not *creating a renderer again*: `GraphicsDevice::Reset()`
and its multisample reconstruction path legitimately rebuild the same renderer on a live device.

---

## Building with several renderers

```bash
cmake -S . -B cmake-build-multi -G Ninja \
      -DCNA_GRAPHICS_RENDERER=HEADLESS \
      -DCNA_GRAPHICS_RENDERERS="HEADLESS;SOFTWARE;STUB"
```

`CNA_GRAPHICS_RENDERER` keeps its meaning: it names the **default** renderer, the one used when
nothing selects another at runtime. It must be a member of `CNA_GRAPHICS_RENDERERS`, and a default
outside the list is a **configure error**:

```text
CMake Error at cmake/RendererDefaultSelection.cmake:54 (message):
  CNA: CNA_GRAPHICS_RENDERER=SOFTWARE is not a member of
  CNA_GRAPHICS_RENDERERS="OPENGL4;VULKAN".

    CNA_GRAPHICS_RENDERER names the DEFAULT renderer chosen from the compiled-in set, so it has to be one of them.
    Either add SOFTWARE to CNA_GRAPHICS_RENDERERS, or set -DCNA_GRAPHICS_RENDERER=OPENGL4 to make the list's first entry the default.
```

It is refused rather than silently corrected on purpose. The default is not merely which renderer
starts first: it also decides which `CNA_RENDERER_<X>` macro is defined project-wide, which example
targets exist, and what every `CNA_GRAPHICS_RENDERER STREQUAL` gate in the build answers. A build
that asked for one default and quietly got another is a build whose every later renderer question
is about a renderer nobody asked for. It is also the same policy the runtime already applies —
CNA never substitutes a renderer silently — and configure time may not be laxer than run time about
the same question.

The rule is exercised by the `CnaRendererDefaultSelection_*` CTest cases, which run
`cmake/RendererDefaultSelection.cmake` in script mode for a valid default inside the list, a default
that is the list's last entry (proving the resolved set is reordered default-first), an invalid
default outside the list, and an ordinary single-renderer configuration.

Leaving `CNA_GRAPHICS_RENDERERS` unset is single-renderer mode, unchanged in every respect.

### What multi-renderer mode changes

Only the default renderer's `CNA_RENDERER_<X>` macro is defined project-wide; each family's own
macro is private to that family's target. This keeps the compile-time accessors
(`getCurrentGraphicsRendererType()`) and the existing renderer-gated tests and examples meaningful —
they all describe the **default**. Making the test corpus itself renderer-agnostic is a separate
piece of work (`plans/plan_runtimerenderer.md` phase P9).

`CNA_MULTI_RENDERER` is defined when more than one renderer is compiled in, and
`CNA_RENDERER_PRESENT_<IDENTITY>` is defined on the test executable for **every** compiled-in
renderer. The distinction matters: `CNA_RENDERER_<IDENTITY>` means "is the default", while
`CNA_RENDERER_PRESENT_<IDENTITY>` means "is compiled in". A renderer's own device-free test suite
guards on the latter, so it runs whenever that renderer is present.

### Which accessor answers which question

| Question | Use |
|---|---|
| What did this build select **by default**? | `CNA::getCurrentGraphicsRendererType()` — still a constant expression in both modes |
| What will CNA **attempt**? | `GraphicsRendererSelection::GetSelected()` |
| What was actually **created**? | `GraphicsRendererSelection::GetActive()` |
| What is **this device** using? | `GraphicsDevice::GetGraphicsRendererType()` / `GetGraphicsRendererName()` |

`GraphicsDevice::GetGraphicsRendererType()` used to be `constexpr` and ignored `this`, returning the
compile-time identity. That was correct while a build could hold only one renderer and wrong as soon
as it can hold several, so it is now a real accessor. The `constexpr` had to go with it: a
compile-time answer cannot describe a runtime choice. Callers wanting the build's compile-time
identity still have `CNA::getCurrentGraphicsRendererType()`.

## Renderer combinations

Not every pair of renderers can be linked into one binary. Incompatible combinations are rejected at
**configure time with a reason**, never left to surface as a link error. The rules live in
`cmake/RendererCombinations.cmake` and are kept in step with this table by
`scripts/check_renderer_combinations.py`. Each rule below has been verified to actually fire at
configure time, not merely to exist.

| Combination | Why it is refused |
|---|---|
| `PORTABLEGL` + any real-OpenGL renderer | PORTABLEGL is a single-header C library that **defines** the global `gl*` symbols (`glClear`, `glDrawArrays`, …). Linking it beside a renderer that calls the real OpenGL of the same names is a duplicate-symbol error. |
| `GDI` + `SOFTWARE` | GDI compiles the SOFTWARE module's own translation units a second time with `CNA_SOFTWARE_2D_ONLY`. Both in one binary would define the same functions twice with different bodies — an ODR violation. |
| Renderers from different **platform** partitions | Windows-only (the DirectX family, `GLIDE`, `GDI`, `DIRECT2D`), Emscripten-only (`WEBGL1`, `WEBGL2`, `CANVAS`, `HTML_DOM`, `SVG_DOM`, `PIXIJS`) and macOS-only (`METAL`) cannot be targeted by one toolchain. |
| `GLIDE` + anything | GLIDE pins the build to the native 32-bit x86 Glide ABI. |

### Verified combinations

| Set | Status |
|---|---|
| `HEADLESS;SOFTWARE;STUB` | ✅ builds, full test suite green, all three selectable at runtime, real fallback between them verified |
| `WEBGL2;WEBGL1;CANVAS;HTML_DOM;SVG_DOM` (Emscripten) | 🟨 **one wasm bundle carries all five**, and the selection API works inside it — `GetAvailable()` reports all five and `GetSelected()` resolves. Creating a device needs a real browser, which is not yet automated here |
| `PIXIJS;CANVAS;HTML_DOM;SVG_DOM` (Emscripten) | 🟨 configures and **links** — `cna_demo_renderer_selection` builds with all four families in one wasm bundle, and `PIXIJS` is carried whether or not it is the default. Its vendored `pixi.min.js` reaches the link line through `cna_renderer_pixijs`'s own `PUBLIC --extern-pre-js`, which is per-family rather than per-default. Runtime selection between them needs a real browser, not automated here; `PIXIJS`'s own pixel suite (`scripts/run_pixijs_browser_tests.mjs`) covers the single-renderer build |
| `OPENGLES3;OPENGLES1;OPENVG;BLEND2D;SOFTWARE;HEADLESS` | ✅ six renderers; **17/17 dispatch tests** pass. `OPENGLES1` needs an ES 1.1-capable Mesa (`scripts/opengles1-test-env.sh`); without it the suite covers the other five and says so |
| `OPENGLES3;WICKED;SOFTWARE;HEADLESS` | ✅ all four selectable. `WICKED` needs `SDL_VIDEODRIVER=x11` **and** `libdxcompiler.so` in the working directory — its shader compiler loads that path literally |
| `OPENGLES3;DILIGENT;SOFTWARE;HEADLESS` | ✅ 17/17 dispatch tests. First heavy **external artifact** in a multi build — needs `-DCNA_SKIA_ROOT=` and `-DCNA_SKIA_BUILD_DIR=` |
| `OPENGLES3;SOKOL;SOFTWARE;HEADLESS;STUB` | ✅ two GL-based abstractions in one binary; all five selectable |
| `OPENGLES3;MAGNUM;SOFTWARE;HEADLESS` | ✅ all four selectable |
| `OPENGLES3;SKIA;SOFTWARE;HEADLESS` | ✅ 17/17 dispatch tests. First heavy **external artifact** in a multi build — needs `-DCNA_SKIA_ROOT=` and `-DCNA_SKIA_BUILD_DIR=` |
| `OPENGLES3;DILIGENT;SOFTWARE;HEADLESS` | ✅ all four selectable (`DILIGENT` needs `SDL_VIDEODRIVER=x11`). **Two-level dispatch verified**: CNA chooses DILIGENT at runtime, then DiligentCore chooses its own device — `CNA_DILIGENT_DEVICE=opengl` is still honoured |
| `OPENGLES3;LLGL;SOFTWARE;HEADLESS` | ✅ all four selectable (`LLGL` needs `SDL_VIDEODRIVER=x11`; on Wayland it is the real fallback example above) |
| `SOFTWARE;PORTABLEGL;HEADLESS;STUB` | ✅ 6269 passed, 0 failed. PORTABLEGL *can* join a multi build — its global `gl*` symbols only conflict with a renderer that calls the real OpenGL of the same names |
| `SDL_RENDERER;OPENGLES3;SOFTWARE;HEADLESS;STUB` | ✅ builds, all five selectable at runtime, window recreation across window kinds verified. Its 16 test failures are identical to a single-renderer `SDL_RENDERER` build's — pre-existing renderer boundaries, none caused by multi-renderer mode |
| `OPENGLES3;OPENGLES2;OPENGL33;SOFTWARE;HEADLESS` | ✅ **6385 passed, 0 failed.** Three EasyGL GL profiles in one binary — `OPENGL33` really does get a desktop core context (`OpenGL 4.6 (Core Profile)`) while the ES profiles get an ES context |
| `OPENGLES3;OPENGL1;OPENGL2;OPENGL4;SDL_GPU;SDL_RENDERER;SOFTWARE;HEADLESS;STUB` | ✅ **6385 passed, 0 failed.** Nine renderers, four independent OpenGL families among them, all selectable at runtime |
| `OPENGLES3;VULKAN;SOFTWARE;HEADLESS;STUB` | ✅ **6385 passed, 0 failed.** Two different GPU APIs in one binary, both selectable at runtime, including the `SDL_WINDOW_OPENGL` ↔ `SDL_WINDOW_VULKAN` crossing |

### What a multi-renderer build makes newly testable

`CrossRendererContractTests` asks questions that previously required building twice and comparing
artifacts out of band — it walks every compiled-in renderer in one process, against live devices,
and checks the properties every renderer must hold regardless of what it draws: that it reports the
identity it was selected as, that a capability answer is a property of the renderer rather than of
when it was asked (stable across repeat queries and across a renderer rebuild), that `Clear`/
`Present` are accepted, that the logical viewport is never degenerate even without a window, and
that a `Texture2D` round-trip works.

It deliberately does **not** compare two renderers' answers to each other: renderers legitimately
differ (SOFTWARE rasterizes, STUB renders nothing). What they may not do is disagree about the
framework contract.

**What a second renderer costs.** Adding another GL profile is free: `OPENGLES3` alone and
`OPENGLES3;OPENGL33` produce the same binary to within 16 bytes, because after phase P11 the profile
branches are compiled either way. The cost was paid once, by that phase, in the single-renderer
build: the EasyGL archive grew 16.6 % (4.77 MB → 5.56 MB) when `#if`-eliminated branches became
runtime ones. Every profile after the first is free.

Cost of that set versus a single-renderer `HEADLESS` build: the `CnaTests` binary grows from
238.5 MB to 241.2 MB (**+1.2 %**) for two additional renderers. The runtime cost is one
function-pointer call per `GraphicsDevice` construction and none per frame.

### A real fallback, start to finish

`LLGL` needs SDL's `x11` video driver and cannot initialize on a Wayland session — a genuine
environmental failure, nothing simulated:

```
$ cna_demo_renderer_selection LLGL OPENGLES3 SOFTWARE
WARN [RENDER] graphics renderer LLGL was not used (InitializationFailed): LLGL renderer:
  the SDL window exposes no X11 handles (video driver 'wayland'). This renderer needs the
  x11 driver -- run with SDL_VIDEODRIVER=x11.
Requested LLGL -- now selected: LLGL
CNA: graphics renderer: OPENGLES3 (selected at runtime from 4 compiled in)
Active renderer: OPENGLES3
Fallback history (1 renderer(s) passed over):
  - LLGL (InitializationFailed): LLGL renderer: the SDL window exposes no X11 handles ...
```

`DILIGENT` behaves the same way on Wayland, and shows that a renderer's *own* internal dispatch
survives being wrapped in CNA's:

```
  - DILIGENT (InitializationFailed): CNA Diligent: no device type could be created --
    tried Vulkan (unsupported SDL video driver for Diligent: wayland),
    OpenGL (unsupported SDL video driver for Diligent: wayland)
```

Note what survives: `GetSelected()` still reports what was **asked for**, `GetActive()` reports what
was **created**, and the renderer's own diagnostic reaches the history verbatim rather than being
reduced to "it did not work". Without the chain argument the same command fails outright, which is
the default.

### Smoke-testing several renderers from one build

`scripts/run-all-renderer-smoke-tests.sh` gained a multi-renderer mode, which is where this build
mode pays for itself in CI time — N renderers cost one build instead of N:

```bash
scripts/run-all-renderer-smoke-tests.sh --multi "HEADLESS;SOFTWARE;STUB"
```

It configures one build, then selects each renderer in turn through the `CNA_GRAPHICS_RENDERER`
environment variable. A renderer with no smoke test registered in that build is reported as
**skipped**, never as a pass — `ctest -L <label that matches nothing>` exits 0, so a wrong label
would otherwise look like success.

### Comparing renderers against the XNA oracle, from one binary

```bash
scripts/run-oracle-corpus-multi.sh <cna_oracle_render_exe> "OPENGLES3;OPENGL33"
```

Renders the 39-scene oracle corpus once per renderer and diffs each scene against the same
checked-in XNA 4.0 reference PNGs. Every renderer is measured against the *same* fixed point, so
agreement with XNA implies agreement with each other — the cross-renderer question that previously
needed one build per renderer and a comparison done by hand.

A renderer that is not compiled into that binary, or cannot start, is probed once and reported as
skipped rather than as 39 scene failures.

### Verifying a fallback chain

`CNA_DEBUG_UNAVAILABLE_RENDERERS` is a comma-separated list of renderer names to treat as though
their availability probe had failed. It exists so a configured fallback chain can be verified
without breaking a driver to do it:

```bash
CNA_DEBUG_UNAVAILABLE_RENDERERS=HEADLESS ./mygame   # fails the availability probe
CNA_DEBUG_FAIL_RENDERER_INIT=SDL_RENDERER ./mygame  # fails during initialization
```

The two are materially different. A failed probe happens before any window exists; a failed
initialization happens after, so a candidate needing a different window kind forces CNA to destroy
and recreate the window. That second path is otherwise unreachable without a genuinely broken
driver.

It sits alongside the renderer-specific debug variables this project already has
(`CNA_BGFX_TRACE_*`, `CNA_LLGL_DEBUG`) — a named test seam, not something the resolution path does
on its own.

---

## Building for the browser

The Emscripten build needs two things this repository does not currently supply, both pre-existing
and unrelated to renderer selection:

- **zlib.** `sharp-runtime`'s io-compression component calls `find_package(ZLIB)`, which fails under
  Emscripten. Build Emscripten's own port once (`embuilder build zlib`) and point CMake at it:
  `-DZLIB_LIBRARY=$EMSDK/upstream/emscripten/cache/sysroot/lib/wasm32-emscripten/libz.a`
  `-DZLIB_INCLUDE_DIR=$EMSDK/upstream/emscripten/cache/sysroot/include`
- **A `-Werror` unused-function** in `sharp-runtime`'s `System/IO/RandomAccess.cpp`, which Clang
  diagnoses and GCC does not. Until it is fixed upstream, add
  `-DCMAKE_CXX_FLAGS="-Wno-error=unused-function"`.

## Choosing the renderer from JavaScript

A browser build is where runtime selection pays off most: one wasm bundle is downloaded and cached,
and the page picks the renderer before the program starts, instead of shipping one bundle per
renderer.

Build the bundle with several renderers as usual:

```bash
emcmake cmake -S . -B cmake-build-wasm-multi -G Ninja \
      -DCNA_GRAPHICS_RENDERER=WEBGL2 \
      -DCNA_GRAPHICS_RENDERERS="WEBGL2;CANVAS;HTML_DOM;SVG_DOM"
```

Then have the page state its preference on the `Module` object, before the module starts:

```html
<script>
  var Module = {
    // Any public renderer identity, in the CNA_GRAPHICS_RENDERER spelling, case-insensitive.
    cnaPreferredRenderer: "CANVAS",
  };
</script>
<script src="cna_app.js"></script>
```

A page that wants to decide from feature detection can do so in the same place:

```js
var Module = {
  cnaPreferredRenderer:
    document.createElement("canvas").getContext("webgl2") ? "WEBGL2" : "CANVAS",
};
```

There is also a direct export for pages that already drive the module themselves:

```js
Module.ccall("cna_set_preferred_renderer", "number", ["string"], ["SVG_DOM"]);  // 1 = accepted
```

### Where this sits in the resolution order

`Module.cnaPreferredRenderer` is consulted at exactly the point, and with exactly the precedence, a
native build consults the `CNA_GRAPHICS_RENDERER` environment variable:

1. an explicit `GraphicsRendererSelection::SetPreferred()` call in the program
2. `CNA_GRAPHICS_RENDERER` if the shell provides one, otherwise `Module.cnaPreferredRenderer`
3. the compile-time default this bundle was built with

The property is deliberately **not** applied by calling `SetPreferred()` from JS glue. Doing that
would make a page property indistinguishable from an explicit call in the program and let it
silently outrank one.

### Failure behaviour

`cna_set_preferred_renderer` returns 1 on success and 0 when the name is not a renderer identity,
is not compiled into this bundle (with no fallback chain configured), or the selection has already
latched — CNA logs the reason in each case. It never throws across the wasm boundary, because a
browser has no useful place to catch that and aborting the module would be a worse answer than a
page that can see it was refused.

The same latch applies as everywhere else: once the first `GraphicsDevice` exists, the renderer
cannot be changed, from JS or from C++.

## What a multi-renderer build costs

Measured on 2026-08-15, Debug, GCC, on this project's own build trees. Sizes are of the `CnaTests`
executable, which links every renderer in the build and is therefore the widest binary the project
produces — a game linking one renderer's own library pays far less.

**Stripped size is the honest column.** A Debug build's symbol tables dwarf the code: the same
executable is 232 MB unstripped and 32 MB stripped, so an unstripped comparison mostly measures
debug info, not renderers.

| Build | Renderers | `CnaTests`, stripped | vs. single HEADLESS |
|---|---|---|---|
| single | `HEADLESS` | 32.1 MB | — |
| single | `SOFTWARE` | 32.1 MB | +0.0 MB |
| single | `OPENGLES3` | 32.6 MB | +0.5 MB |
| multi | `HEADLESS;LLGL;SOFTWARE;STUB` | 36.2 MB | **+4.1 MB** |

Four renderers in one binary, including a large third-party one, cost about **13 %** over a
single-renderer build of the same executable. That is the number to weigh against the convenience
of choosing a renderer at startup.

### Where the size goes, per renderer

CNA's own renderer archives in that multi build:

| Renderer archive | Size |
|---|---|
| `libcna_renderer_llgl.a` | 7.19 MB |
| `libcna_renderer_software.a` | 6.19 MB |
| `libcna_renderer_headless.a` | 3.97 MB |
| `libcna_renderer_stub.a` | 1.65 MB |

The third-party archives behind LLGL are much larger than CNA's own wrapper — `libLLGL_VulkanD.a`
40.2 MB, `libLLGL_OpenGLD.a` 34.3 MB, `libLLGLD.a` 21.4 MB, `libLLGL_NullD.a` 7.2 MB — yet the
final executable grows by only ~4 MB, because the linker takes what is referenced rather than whole
archives. **A renderer's cost in the binary is not its library's size on disk**, and estimating from
archive sizes overstates it by an order of magnitude here.

### What is NOT measured here, and why

- **Build time.** A trustworthy figure needs from-scratch builds of each set, and this project's
  build rules treat repeated clean rebuilds as real SSD wear to be avoided
  (`../CLAUDE.md`). Timing several full builds for a table was not judged worth that cost. The
  incremental cost is the one developers actually pay, and it is dominated by how many renderer
  archives must relink, which the per-renderer table above already indicates.
- **Sets containing bgfx, FNA3D, WebGPU or the Windows/macOS families.** Those need dependencies or
  operating systems not available on the machine these numbers come from. Their rows are absent
  rather than estimated.
- The `OPENGLES3` tree uses the Makefiles generator where the others use Ninja. That affects build
  time, not binary size, so it is left in the size table and out of any timing claim.

## See also

- `plans/plan_runtimerenderer.md` — the design decisions and the full task breakdown.
- `modules/core/include/CNA/GraphicsRendererType.hpp` — the public renderer identities.
- `cmake/RendererSelection.cmake` — compile-time selection.
