# Runtime graphics renderer selection

**Status: reconciled with the active registry, selection implementation, browser contract and
known-defect ledger on 2026-11-01.** Compile-time selection remains CNA's default and recommended
mode. Multi-renderer selection is a second, explicit build mode: several renderer identities are
linked into one binary and one is chosen before the first graphics device successfully starts.

This page owns that selection and fallback contract. CNA has **10 public renderer identities** over
**8 implementation families**; their canonical inventory is
[`renderer-registry.md`](renderer-registry.md). Renderer capabilities belong in the live
`RendererCapabilityProfile` and the renderer-specific pages; current defects belong only in
[`handoff.md` section 5](../handoff.md#5-known-bugs-and-limitations). The completed `RTR-P0`–`P14`
campaign, retired-renderer transcripts and old binary-size measurements are historical evidence in
Git, not current configuration guidance.

## Sources of truth

| Question | Authoritative source |
|---|---|
| Which renderer names exist or are retired? | `cmake/RendererIdentities.cmake` and `docs/renderer-registry.md` |
| What is the host's default identity? | `cmake/RendererIdentityDefault.cmake` |
| How are the default and compiled set resolved? | `cmake/RendererDefaultSelection.cmake` |
| Which identity maps to which descriptor/factory? | `cmake/RendererRegistry.cmake` |
| Which combinations are impossible? | `cmake/RendererCombinations.cmake` |
| What may an application select and query? | `CNA/GraphicsRendererSelection.hpp` |
| How are candidates, windows and fallback resolved? | `GraphicsDevice::resolveRenderer()` |

Do not copy the identity list into another selection table. The registry checks deliberately keep
the CMake list, C++ enum, generated descriptor table, C ABI values and retired-name diagnostics in
lockstep.

## The two build modes

### Single renderer

`CNA_GRAPHICS_RENDERER` selects one public identity at configure time:

```bash
cmake -S . -B cmake-build-sdlgpu -G Ninja \
  -DCNA_GRAPHICS_RENDERER=SDLGPU
```

If the option is omitted, the host default is `WEBGL2` under Emscripten, `SDLGPU` on Windows,
macOS and iOS, and `OPENGLES3` elsewhere. A single-renderer build publishes one registry entry and
the runtime API can only validate or report that choice; it cannot manufacture another renderer.

### Building with several renderers

`CNA_GRAPHICS_RENDERERS` opts into the second mode. `CNA_GRAPHICS_RENDERER` still names the
default and must be a member of the list:

```bash
cmake -S . -B cmake-build-multi -G Ninja \
  -DCNA_GRAPHICS_RENDERER=OPENGLES3 \
  -DCNA_GRAPHICS_RENDERERS="OPENGLES3;OPENGL33;WEBGPU;SDLGPU;FNA3D;SOFTWARE;HEADLESS;STUB" \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON
```

The resolver removes duplicate identities and stores the default first. A default outside the
compiled set is a configure error, not a request to substitute the list's first entry. Unknown,
renamed and retired identities are likewise refused explicitly on every CMake selection route.

Only the default renderer's `CNA_RENDERER_<X>`/profile definition is project-wide. Each
non-default family's own definitions are private to its target. Test targets additionally receive
`CNA_RENDERER_PRESENT_<IDENTITY>` for every linked identity, so they can distinguish “compiled in”
from “the build default”. Code deciding what a live device does must ask the active renderer,
never a build-default macro.

## Application API

Select a renderer before constructing the first successful `GraphicsDevice`:

```cpp
#include <array>

#include "CNA/GraphicsRendererSelection.hpp"

using CNA::GraphicsRendererSelection;
using CNA::GraphicsRendererType;

GraphicsRendererSelection::SetPreferred(GraphicsRendererType::SdlGpu);
const std::array fallbackChain{
    GraphicsRendererType::OpenGL33,
    GraphicsRendererType::Software,
};
GraphicsRendererSelection::SetFallbackChain(fallbackChain);

MyGame game;
game.Run();
```

The selection precedence is:

1. `GraphicsRendererSelection::SetPreferred()`;
2. the native `CNA_GRAPHICS_RENDERER` environment variable, or under Emscripten
   `Module.cnaPreferredRenderer` when no real environment value is present;
3. the build's compile-time default.

The important queries are:

| API | Answer |
|---|---|
| `GetAvailable()` / `IsAvailable()` | Identities compiled into this binary |
| `GetSelected()` | The identity CNA will attempt first; querying it does not latch selection |
| `GetActive()` | The renderer actually created; throws before a successful resolution |
| `IsLatched()` | Whether a renderer has been created and selection is now immutable |
| `GetFallbackHistory()` | Every rejected candidate before the active renderer, in order |

Names use the canonical selectors case-insensitively. A name that is not a public identity throws
`ArgumentException`; a known identity absent from this build throws `InvalidOperationException`
unless fallback was explicitly enabled. Retired names are diagnosed as retired, including their
reserved C ABI value, rather than being treated as unknown or aliased to a survivor.

## Latch and device lifetime

Selection latches **after a renderer is successfully resolved and created**. A failed availability
probe or construction attempt does not latch the process; a caller may catch the error, change the
selection and try again. This is covered by the failed-resolution and recovery tests.

After success, `SetPreferred()`, `SetFallbackChain()` and `EnableAutomaticFallback()` throw
`InvalidOperationException`. Rebuilding the active renderer during `GraphicsDevice::Reset()` or an
MSAA reconstruction is still legal: the latch prevents choosing a different renderer, not
recreating the one already active. A second device uses the same process-wide selection.

Applications must complete selection before starting graphics work on another thread. The state is
process-wide because window creation and renderer construction consume the answer before a game has
a useful per-device configuration surface.

## Failure and fallback policy

Hard failure is the default. An unavailable renderer, a renderer absent from the build or an
initialization exception is surfaced; CNA never silently changes renderer.

Fallback is enabled only by `SetFallbackChain()` or `EnableAutomaticFallback(true)`:

- an explicit chain may contain identities not present in every build; each absent entry is
  recorded rather than silently removed;
- the automatic chain is derived from the published renderer maturity/category metadata, keeping
  picture-producing renderers ahead of `HEADLESS` and `STUB`;
- duplicate candidates are removed while preserving order;
- each rejection is logged and appended to `GetFallbackHistory()` as `NotCompiledIn`,
  `ProbeUnavailable`, `InitializationFailed` or `WindowKindConflict`;
- an exhausted chain throws with the originally requested renderer's first failure as the primary
  cause and the complete attempt history as detail.

Two test seams make both failure stages reproducible without breaking a driver:

```bash
CNA_DEBUG_UNAVAILABLE_RENDERERS=WEBGPU ./mygame
CNA_DEBUG_FAIL_RENDERER_INIT=SDLGPU ./mygame
```

They are diagnostics/testing controls, not automatic fallback configuration.

## Descriptor and window contract

Each compiled identity reaches one `GraphicsRendererDescriptor` through the generated registry.
The descriptor supplies the information needed before an `IGraphicsRenderer` exists: public type
and name, window kind, video/window requirements, optional platform services, a side-effect-free
availability probe, the namespaced factory and adapter-level query hooks.

| Window kind | Current identities | Pre-construction contract |
|---|---|---|
| `None` | `HEADLESS`, `SOFTWARE`, `STUB` | No native window or video subsystem. Software may consume a platform surface presenter where the platform supplies one. |
| `Plain` | `WEBGPU`, `DIRECTX9`, `SDLGPU` | Native window and video subsystem, without an OpenGL render intent. |
| `OpenGL` | `OPENGLES3`, `OPENGL33`, `WEBGL2`, `FNA3D` | Native window and video subsystem with an OpenGL-compatible intent. EasyGL receives the platform GL-context service. FNA3D reserves a 24-bit depth/8-bit stencil, double-buffered GL visual because its runtime OpenGL driver is the restrictive case. |

FNA3D may select OpenGL, Direct3D 11 or SDL_GPU internally; that upstream driver choice does not
create another CNA renderer identity. Its CNA descriptor conservatively prepares a window that can
serve the OpenGL route.

A fallback candidate with a different window kind cannot reuse the current window. CNA destroys
and recreates a window it owns. If the application supplied the window through
`PresentationParameters::DeviceWindowHandle`, CNA may not destroy it, so the candidate is skipped
with `WindowKindConflict`. Video-subsystem ownership is balanced per device across every attempt.

Renderer-specific behavior after construction belongs behind `IGraphicsRenderer` virtuals.
Adapter queries that must run before a device exists use the descriptor's `adapterQueries` hooks.
The runtime-discipline gate refuses a new `CNA_RENDERER_*` branch in `modules/graphics/src`.

## Renderer combinations

Combination rules describe demonstrated incompatibilities, not speculative bans. The current
pairwise rule is the **platform partition**: `DIRECTX9` is Windows-only and `WEBGL2` is
Emscripten-only, so those identities cannot be built into the same binary because one toolchain
cannot target both platforms. Per-identity platform and dependency gates still apply to every
other set.

The three EasyGL profiles share an implementation, but native `OPENGLES3` and `OPENGL33` may
coexist because the selected `GlProfile` is runtime state. `WEBGL2` belongs to the browser
partition. Two representative supported sets are:

| Target | Set | Evidence boundary |
|---|---|---|
| Native Linux | `OPENGLES3;OPENGL33;WEBGPU;SDLGPU;FNA3D;SOFTWARE;HEADLESS;STUB` | Current `cmake-build-multi`; registry, selection, fallback and cross-renderer contract tests exercise all eight identities. |
| Browser | `WEBGL2;WEBGPU` | Current workflow proves configure/build/link/registry consistency. Renderer-specific browser harnesses, not the multi-renderer workflow alone, provide runtime graphics qualification. |

A small display-independent development set such as `HEADLESS;SOFTWARE;STUB` is also valid. Its
members still have different semantics: linking them together does not imply feature parity.

## Browser selection

Configure a browser bundle with both supported browser identities in the usual way:

```bash
emcmake cmake -S . -B cmake-build-wasm-multi -G Ninja \
  -DCNA_GRAPHICS_RENDERER=WEBGL2 \
  -DCNA_GRAPHICS_RENDERERS="WEBGL2;WEBGPU"
```

The page may set its preference before module startup:

```html
<script>
  var Module = { cnaPreferredRenderer: "WEBGPU" };
</script>
```

An embedding that already drives the module can call the Emscripten-only
`cna_set_preferred_renderer` export; it returns 1 when accepted and 0 for an unknown/unavailable
name or a late call, logging the reason instead of throwing through the Wasm boundary. An explicit
C++ `SetPreferred()` remains higher precedence than the page property.

Browser exception ABI, Asyncify, canvas, pthread, storage and runtime-evidence requirements belong
in [`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md). This page does
not carry toolchain workarounds or imply that a successful multi-renderer link is a browser run.

## Validation

The static gates are quick and do not need a display:

```bash
python3 scripts/check_renderer_identities.py
python3 scripts/check_renderer_combinations.py
python3 scripts/check_runtime_renderer_discipline.py
ctest --test-dir cmake-build-multi \
  -R '^(RendererCombinationRegistry|CnaRendererDefaultSelection_)' \
  --output-on-failure
```

Build the focused executable and run live selection through the private GPU runner:

```bash
cmake --build cmake-build-multi --target CnaTests --parallel 8
tools/platform/run_gpu_tests_private.sh cmake-build-multi \
  -R '^(CrossRendererContractTest|GraphicsRendererFallbackTest|GraphicsRendererSelectionTest|GraphicsRendererRegistryTest|MultiRendererFallbackTest|GraphicsDevicePlatformWindowTests)\.' \
  --output-on-failure -j6
```

Never run the live multi-renderer suite on the owner's desktop. Test counts are observations, not
the contract; select the named suites so additions are included automatically.

## Runtime-profile state

Renderer state that differs between EasyGL profiles follows the device's runtime `GlProfile`, not
the build default. EasyGL's `ApplyRasterizerMultiSampleState` toggles `GL_MULTISAMPLE` only on a
desktop core profile, so an `OPENGLES3`-default binary that selects `OPENGL33` toggles it and the
inverse combination never sends the desktop-only state to ES (CNA-FIX-D25). On Linux the
nondefault native GL profiles of a multi-renderer tree register `EasyGL_RuntimeMultiSampleState_*`
and `EasyGL_RuntimeMsaaFragmentContract_*`; see [`opengl33-renderer.md`](opengl33-renderer.md) and
[`rasterizerstate-support.md`](rasterizerstate-support.md).

Capability answers remain properties of the active renderer and live device. A successful runtime
selection does not make two renderers interchangeable and does not widen either renderer's
documented platform qualification.

## Design decisions

Source comments and build diagnostics cite these stable numbers. The implementation tasks
(`RTR-P0`–`P14`) are in Git history.

1. **Compile-time selection remains the default.** Multi-renderer mode is opt-in through
   `CNA_GRAPHICS_RENDERERS`.
2. **Pre-construction decisions live in one descriptor per family.** Window kind, services,
   availability, factory and adapter hooks are data/function pointers, not XNA-layer `#ifdef`s.
3. **The registry is generated, not self-registering.** Static-archive dead stripping cannot make
   a compiled renderer disappear from the table.
4. **Factories are namespaced per family.** Several renderer archives can therefore coexist
   without a shared factory-symbol collision.
5. **Selection latches after successful renderer resolution.** A failed attempt remains
   recoverable; reset/reconstruction keeps the already active renderer.
6. **Failure is hard by default.** CNA never substitutes a renderer without explicit fallback.
7. **Fallback is opt-in, ordered and reported.** Every rejected candidate has a stable reason and
   diagnostic history.
8. **Cross-kind fallback recreates only a CNA-owned window.** A caller-owned incompatible window
   produces `WindowKindConflict`.
9. **Renderer-specific behavior stays out of the XNA layer.** Post-construction behavior uses
   virtuals; pre-device adapter answers use descriptor hooks.
10. **Runtime selection adds no renderer identity.** Both build modes use the canonical registry.
11. **Impossible combinations fail during configuration with a reason.** The combination registry
    and its documentation gate stay in lockstep.
12. **Tests distinguish default, present and active identities.** Default-gated family examples
    remain intentional; shared registry/selection/contract suites exercise the compiled set.

## See also

- [`renderer-registry.md`](renderer-registry.md) — canonical identities, families and defaults.
- [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) — cross-renderer
  feature overview.
- [`renderer-capability-profiles.md`](renderer-capability-profiles.md) — live structured
  capability queries.
- [`web-emscripten-graphics-limitations.md`](web-emscripten-graphics-limitations.md) — browser
  build, event-loop and evidence contract.
- [`removed-renderers.md`](removed-renderers.md) — retired identities and reserved values.
