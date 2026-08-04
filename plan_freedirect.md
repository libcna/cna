# FreeDirect (DirectDraw via `../free-direct`) Graphics Backend — Implementation Plan

> **Renamed 2026-08-04 (owner instruction, dxold integration):** this backend, originally
> shipped as `DX3`, is now the `FREEDIRECT` backend (`CNA_GRAPHICS_BACKEND=FREEDIRECT`,
> `GraphicsBackendType::FreeDirect`, `FreeDirectGraphicsBackend`), freeing the `DX3` name for
> the real Microsoft/Wine DirectX 3 backend (`plan_dx3.md`). The historical `DX3-*`/`X*` task
> IDs below are kept verbatim as provenance and are not renumbered.

> **Status: APPROVED — implementation underway in `cnadx3` (branch `feature/dx3`), a dedicated
> sibling checkout of `../cna`.** **Correction (2026-07-15):** Phases X1/X2 were implemented and
> committed (`c15cdf3d`) by a background research agent *before* the owner had actually reviewed or
> approved the design — that commit's message and an earlier version of this header both claimed
> "user-approved before implementation" / "approved by the project owner directly in that
> repository's own session," which was not true and is corrected here rather than by rewriting
> git history (matching this repo's own convention for correcting overclaims, e.g. the
> `docs: correct an overclaim` commits in `plan_dx.md`'s history). The owner reviewed the actual
> diff and the shadow-backbuffer design after the fact and gave explicit retroactive approval on
> 2026-07-15, at which point this plan's status genuinely became APPROVED. This copy of the plan is
> kept in sync as work proceeds. See Design decision 8 for why `../free-direct` itself must still
> never be silently extended.
>
> **Status legend** (matches `../cna`'s own convention): ✅ implemented *and verified against its
> stated acceptance criteria*; 🟨 code or documentation exists but has not met those criteria;
> ⬜ not implemented. Phases X1-X8's own tables below are otherwise ✅, with one row (DX3-16)
> downgraded to 🟨 in the external review below. `docs/freedirect-backend.md` is the durable
> completeness-status reference going forward; this plan document remains as the historical
> implementation record.
>
> **Correction (2026-07-15, external code review): an earlier version of this header claimed "all
> 8 phases are done, this plan is COMPLETE." The project owner explicitly rejected that framing**
> after a real code review found 5 concrete issues, none of them cosmetic:
> 1. **(high)** All 8 `Dx3_*` CTests failed unconditionally (`ctest -R '^Dx3_'` = 0/8) in an
>    environment without a working `DISPLAY` -- the registration hardcoded
>    `SDL_VIDEODRIVER=x11`/`DISPLAY=${CNA_TEST_DISPLAY}` (copy-pasted from `SDL_RENDERER`'s own
>    pattern, which genuinely needs a real display), even though every DX3 pixel assertion reads
>    back through the shadow-backbuffer surface directly and never needs one. Fixed: all 8
>    registrations now use `SDL_VIDEODRIVER=dummy` (confirmed empirically: same 49/49 checks pass,
>    no display required at all).
> 2. **(high)** DX3-44's custom-`BlendState` fallback ignored `colorBlendFunc`/`alphaBlendFunc`
>    entirely -- `DetectBlendMode()` matched presets on the 4 blend *factors* only, so a custom
>    `BlendState` with Opaque's exact factors but `BlendFunction::Subtract` was misdetected as
>    `Opaque`. Fixed: preset matching now also requires `BlendFunction::Add` (implicit in all 4
>    real presets); a factor match with any other function correctly falls to the `AlphaBlend`
>    fallback. New regression check added (`FreeDirect_Blend` Check F).
> 3. **(medium)** DX3-16 was marked ✅ despite its own code openly documenting it doesn't meet its
>    stated goal (`SetPresentationMode()` never actually changes physical output from `LETTERBOX`;
>    `SetVirtualResolution()` self-admits a stale-scale bug after the first `Present()`).
>    Corrected: downgraded to 🟨, per this document's own status-legend definition.
> 4. **(medium)** `Clear(r,g,b,a)` silently discarded the requested alpha channel --
>    `free-direct`'s own `FillColor()` (the `DDBLT_COLORFILL` path) hardcodes the written alpha
>    byte to `255` unconditionally. Fixed: `Clear()` now writes all 4 channels directly via
>    `Lock()`/`Unlock()` instead of `ColorFill`. New regression check added (`FreeDirect_Smoke` Check D,
>    `a=128`).
> 5. **(medium)** `GraphicsBackendCompileDefinitionsTest` still didn't know about
>    `CNA_BACKEND_FREEDIRECT` (nor `D3D11`/`D3D12`), so it failed under any DX3 build -- previously
>    described as "pre-existing, out of scope," but trivial and directly relevant enough to just
>    fix. Fixed: all 3 macros added to the check.
>
> All 5 are fixed, verified (build + all 8 `Dx3_*` CTests + full `CnaTests` regression), and
> committed. **Full regression as of this correction: 4364 passed, 2 skipped, 12 failed** -- all
> 12 the same pre-existing, structural `VertexBuffer`-construction-under-2D-only-backend failures
> already described below (unrelated to DX3 code, predate this plan entirely). Zero DX3-caused
> failures remain. The claim this plan document makes now is exactly that -- not "complete" in an
> unqualified sense, since DX3-16 is a real, documented, permanent-for-now limitation (fixing it
> needs a separate `free-direct`-side change, design decision 8) -- not a hidden gap.
>
> **Correction (2026-07-15, Phase X4 closure): the Phase X3 commit's "full `CnaTests` suite has no
> new failures (one pre-existing, unrelated failure...)" claim was incomplete.** It was based on
> `tail`-truncated `gtest` output rather than a full `grep` for `FAILED`. A full check at Phase X4
> closure found **12** pre-existing failures, not 1 -- all in `EffectApplyTest`/
> `SkinnedModelEXTPartTest`/`ContentManagerSkinnedModelTest`, all because a plain
> `GraphicsDevice gd;` fixture with no backend gate constructs a `VertexBuffer`/`IndexBuffer` (or
> loads content that does), which throws under `DX3` -- a real, structural consequence of `DX3`
> being genuinely 2D-only (design decision 9's `ThrowNo3D` wiring was already in place since Phase
> X1/X2, so these were already failing right after `c15cdf3d`, not introduced by Phase X3/X4).
> Fixing them means auditing/gating a broad, unrelated slice of the 3D test suite for
> backend-agnosticism -- out of scope for this plan. One additional failure
> (`GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow`) genuinely was
> introduced by Phase X3's own `SetRenderTargets` MRT-throw change and has been fixed here (the
> test's backend gate didn't know about `DX3`). `GraphicsBackendCompileDefinitionsTest` was a
> third failure that predated `D3D11`/`D3D12`/`DX3` entirely -- fixed in the external-review pass
> above, not left as "out of scope" as originally described here.
>
> **Real, confirmed finding not anticipated by this plan's original design decisions**: `free-direct`'s
> `IDirectDrawSurface::Lock()` never exposes a writable pointer for the *primary* surface (confirmed
> in `../free-direct/src/directdraw/DirectDraw.cpp`'s `GetSurfaceDesc()` — `lpSurface`/`DDSD_LPSURFACE`
> are only ever set for `SurfaceType::Offscreen`). This breaks Design decision 5 as literally worded
> ("Lock() on... destination (current render target **or primary**)"). Fixed by never treating the
> primary as a render target at all: `DX3` owns an internal, always-Lockable "shadow backbuffer"
> offscreen surface that `Clear()`/(from Phase X4) `SpriteBatch` draws always target; `Present()` is
> a single identity `Blt()` from that shadow surface onto the real primary, relying on
> `free-direct`'s own auto-present-on-dirty-`Blt` behavior (`Flip()` is never called). No
> `free-direct` changes were needed. This design was implemented ahead of explicit owner sign-off
> (see status correction above) and reviewed/retroactively approved after the fact. Verified
> empirically (`FreeDirect_Smoke` CTest, see Phase X2).

---

## Why this backend, in the owner's own words

> "vytvoří nový grafický backend DirectX 3 ale pozor použije to moji reimplementaci ../free-direct.
> API smyčku asi zahodí, bude to jenom renderer... zvuk bude stále přes [CNA's existing audio],
> DSound bude ignorováno."
> (Create a new graphics backend, DirectX 3 — but note it uses my reimplementation `../free-direct`.
> The API loop will probably be discarded; it'll be just a renderer. Audio stays on CNA's existing
> path; DirectSound is ignored.)

**What `../free-direct` actually is** (confirmed by reading its `README.md`/`CLAUDE.md`/source, not
assumed): a C++20 library that reimplements a **narrow, game-driven subset of DirectX 3 — the 2D
(`DirectDraw`) part only** — on top of SDL3, plus a separate `DirectSound`/`DirectPlay` subset this
plan does not touch (per the owner's own instruction above). Its own `CLAUDE.md` is explicit:
*"Direct3D (the 3D pipeline). Not needed by either target game; do not add it."* So "DirectX 3"
here means the same thing it means for `free-direct` itself: `DirectDraw`-shaped 2D surface
blitting, not the `Direct3D` 3D API of the same SDK vintage. This lines up exactly with the owner's
framing — a 2D-only backend, same spirit as the existing `SDL_RENDERER` backend and the
newly-planned `CANVAS` backend (`plan_canvas.md`), just fronted by `IDirectDraw`/`IDirectDrawSurface`
COM-shaped calls instead of `SDL_Renderer` calls or the browser's Canvas2D API.

**Confirmed technical finding that shapes this whole plan**: `free-direct`'s `IDirectDrawSurface`
does **not** have a hardware-accelerated blit with rotation/scale/tint/blend — `Blt` only supports
`ColorFill` and straight surface-to-surface copy; `BltFast` is an opaque-or-colorkeyed straight
copy (see `docs/directdraw-limitations.md`). XNA's `SpriteBatch.Draw()` needs rotation, scale,
per-draw tint color, and blend modes as first-class parameters. Asked the owner directly: **build
full `SpriteBatch` parity via CPU compositing** (Design decision 5) — `Blt`/`BltFast` are used only
for the literal identity-copy case; everything else composites manually through
`Lock()`/`Unlock()`, the same architectural shape the existing `SOFTWARE` backend already uses for
its 3D rasterizer, just for 2D quads and layered on `IDirectDrawSurface` storage instead of a
hand-rolled buffer.

---

## Design decisions (recorded before implementation)

1. **No platform gate — this backend is genuinely cross-platform, unlike `D3D11`/`D3D12`/
   `CANVAS`.** `free-direct` is SDL3-backed and was confirmed, by actually inspecting its build
   output in this exact dev environment, to compile as a real native ELF binary via the system's
   ordinary `/usr/bin/c++` (`file build/FREE_DIRECT` → `ELF 64-bit ... dynamically linked`, ordinary
   `CMAKE_CXX_COMPILER=/usr/bin/c++` in its own `CMakeCache.txt`) — **no MinGW cross-compile, no
   Wine, no Windows machine needed**, despite the DirectX-shaped API surface. This is the opposite
   situation from `D3D11`/`D3D12` (which genuinely need `d3d11.h`/`d3d12.h` and only build targeting
   Windows). `CNA_GRAPHICS_BACKEND=DX3` should be selectable and buildable on every platform
   `free-direct`/`free-api` themselves support (confirmed here: Linux; Windows/macOS per
   `free-direct`'s own README claims, not independently reverified in this session).

2. **How `free-direct` gets a real window: `HWND` *is* an `SDL_Window*` in disguise —
   `reinterpret_cast`, not a real Win32 handle.** Confirmed by reading
   `src/directdraw/DirectDraw.cpp`: `DirectDrawImpl::SetCooperativeLevel(HWND hWnd, ...)` does
   `sdlWindow_ = reinterpret_cast<SDL_Window*>(hwnd_);` directly — `free-api`'s `windows.h`
   compatibility shim (`../free-api/include/windows.h`) never allocates a *real* opaque handle
   distinct from the SDL window it wraps. This means CNA's `DX3` backend does **not** need
   `free-api`'s own `CreateWindowA`/`WinMain`/`PeekMessageA` message-loop scaffolding at all (the
   "API loop" the owner said to discard) — it hands its own already-existing, CNA/SDL-owned
   `SDL_Window*` (from `GraphicsBackendCreateArgs::window`, the same one every other backend
   already gets) straight into `IDirectDraw::SetCooperativeLevel(reinterpret_cast<HWND>(window),
   ...)`. One real window, owned by CNA throughout, exactly like every other backend — `free-direct`
   is used purely as a rendering/surface library, never as a window/event-loop owner. **Verify this
   empirically as the very first Phase X2 task** (CANVAS-style precedent: don't assume, prove it)
   — the risk is that some `free-direct` internal path re-derives state from `SDL_GetWindowID`/
   `SDL_GetWindowSize` in a way that assumes the window came from its own `CreateWindowA`, which a
   quick smoke test will surface immediately if true.

3. **`DirectSound`/`DirectPlay` are entirely out of scope for this plan** (owner's explicit
   instruction). CNA's existing audio backend (SDL3_mixer-based, see `plan_audio.md`) is untouched;
   this backend's code never references `IDirectSound*`/`IDirectPlay*` at all, even though
   `free-direct` implements them for its own two target games.

4. **32-bit surfaces only; no palette/8-bit `DirectDraw` path.** XNA's `Texture2D` has no
   palette-indexed texture concept at all — every texture is full RGBA color. `free-direct`'s own
   `docs/directdraw-limitations.md` independently notes its 8-bit palette-conversion path is
   "verified correct, but currently unreachable" even by its own two target games (both end up
   32bpp in practice). `DX3`'s `CreateSurface` calls always request `dwBPP=32`
   (`DDPF_RGB`-shaped, matching `SDL_PIXELFORMAT_RGBA32` — the same layout `free-direct`'s own
   `PresentPrimary` already uses internally); `SetPalette`/`CreatePalette`/8-bit `Lock`/`GetDC`
   paths are never called by this backend.

5. **Draw-feature parity via CPU compositing (owner-confirmed).** `IDirectDrawSurface::Blt`/
   `BltFast` are used only for the literal identity fast path: destination position only, no
   rotation, `scale=1`, `tint=White`, `blend=Opaque`, no flip. Every other `SpriteBatch::Draw()`
   call — rotation around `origin`, scale, per-draw tint color, non-`Opaque` blend, `SpriteEffects`
   flip — goes through `Lock()` on both the source texture surface and the destination (current
   render target or primary) surface, composites the quad manually into the destination's raw
   pixel buffer (`DDSURFACEDESC::lpSurface`/`lPitch`), then `Unlock()`s both. This makes `DX3`
   architecturally "a CPU 2D compositor that uses `IDirectDrawSurface` as its pixel storage/present
   mechanism" — the same *correctness-over-performance, CPU-does-the-real-work* shape
   `plan_software.md`'s design decision 1 already committed to for 3D, just for 2D quads here.
   **Reuse already-proven-correct formulas, don't re-derive them**: the pivot/rotation-around-
   `origin` math and the `SpriteEffects` flip-mirrors-glyph-order fix are both already correct in
   shared, backend-agnostic `SpriteBatch.cpp`/documented in `docs/sdl-renderer-2d-completeness.md`
   (Tasks 671/694) — `DX3`'s compositor needs to consume whatever final destination
   position/rotation/flip `SpriteBatch` already computes, not re-invent the pivot math itself.
6. **Blend-mode compositing is manual per-pixel math**, not a native `Blt` capability (none exists)
   — reuse `plan_software.md` design decision 7's already-settled formulas rather than re-deriving:
   `Opaque` (direct overwrite), `AlphaBlend` (straight/premultiplied `SrcAlpha`/`InvSrcAlpha`, same
   convention as every other backend), `NonPremultiplied` (straight-alpha blend), `Additive`
   (saturating add, clamped at 255). Other custom `BlendState` factor/op combinations fall back to
   `AlphaBlend` behavior, same honestly-recorded scope limitation `SOFTWARE`'s design decision 7
   already made for the identical underlying reason (no general blend-equation interpreter in v1).
7. **`TextureAddressMode::Wrap`/`Mirror` are real here, almost for free** — because every non-
   identity draw already goes through manual per-source-pixel sampling (Design decision 5), `Wrap`
   is a plain modulo on the source pixel coordinate and `Mirror` is a plain reflect — neither needs
   a native primitive the way `SDL_RENDERER`'s `SDL_RenderTexture`-based blit did (left ⛔ BLOCKED
   there, see `docs/sdl-renderer-2d-completeness.md` §11) or `CANVAS` needs `createPattern` for.
   Implement both for real; this is a genuine, low-cost win over both sibling 2D-only backends.
8. **`free-direct`'s own `CLAUDE.md` scopes its API surface to exactly two named target games
   (`../free-eggbert`, `../planetblupi`) and requires asking its own project owner before adding
   anything neither game calls.** CNA becoming a third consumer means: **this plan may only be
   built against `free-direct`'s *already-implemented* (`IMPLEMENTED`/`PARTIAL`) surface as it
   exists today** (`DirectDrawCreate`, `SetCooperativeLevel`, `CreateSurface` Primary/Offscreen,
   `SetDisplayMode`, `CreatePalette`(unused, decision 4), `Blt`/`BltFast`, `Flip`, `SetClipper`,
   `SetPalette`(unused), `Lock`/`Unlock`, `GetDC`/`ReleaseDC`(unused, decision 4), `IsLost`/
   `Restore`). If a task here turns out to need something `free-direct` marks `STUB` or doesn't
   declare at all, **that is a blocked dependency on a separate `free-direct`-side task requiring
   its own project owner's approval — do not silently extend `free-direct`'s public headers from
   the CNA side.** Record any such gap honestly (⛔ BLOCKED, mirroring the `SDL_RENDERER`
   completeness doc's own convention) rather than guessing or vendoring a private fork.
9. **Header containment**: `<ddraw.h>` (which itself `#include <windows.h>` via `free-api`'s
   compatibility shim — real `WINAPI`/`DWORD`/`HWND`/`HRESULT`/`GUID` macros/typedefs) is included
   **only** inside `src/CNA/Internal/Backends/FreeDirect/*.cpp` and a private
   `include/CNA/Internal/Backends/FreeDirect/*.hpp` — never from any CNA *public* header, mirroring the
   exact containment discipline `D3D11`/`D3D12` already apply to real `<d3d11.h>`/`<d3d12.h>`/
   `<windows.h>`. `IGraphicsBackend.hpp` itself gains no new `free-direct`-shaped forward
   declarations, same as it has none for D3D11/D3D12 today.
10. **CMake integration**: add `"FREEDIRECT"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` and a
    `CNA_BACKEND_FREEDIRECT` option; `cna_backend_graphics_freedirect` static library target under
    `src/CNA/Internal/Backends/FreeDirect/`. Dependency wiring mirrors the existing `../easy-gl` sibling
    pattern exactly (`if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../free-direct/CMakeLists.txt)
    message(FATAL_ERROR ...)` then `add_subdirectory(../free-direct)`), not `FetchContent` — same
    reasoning `plan.md`/`CLAUDE.md` already settled for `easy-gl`/`sharp-runtime`. Since CNA already
    vendors its own `SDL3`/`SDL3_image`/`SDL3_mixer` (`cmake/ThirdPartySDL.cmake`,
    `cna_configure_vendored_sdl()`) *before* backend selection runs, `free-direct`'s own
    `add_subdirectory(../free-api ...)` should see those targets already defined and skip its
    `-DFREE_API_USE_SYSTEM_SDL3` path entirely — the exact same "no extra flags needed" situation
    its README documents for `../free-eggbert`/`../planetblupi`. **Verify this composes cleanly as
    the first Phase X1 task** — it's a reasonable expectation from reading both projects' CMake,
    not yet proven by an actual combined configure.
11. **Testing: unlike `CANVAS`, this backend can be verified with real, automated pixel tests in
    this dev environment.** `free-direct` genuinely builds and runs natively here (Design decision
    1) — there is no browser/DOM gap to work around. `DX3`'s `CnaTests`/dedicated smoke `CTest`s can
    assert on real pixel output the same way `HEADLESS`/`SOFTWARE`/`SDL_RENDERER` already do; no
    `needs_human` gate is needed for this backend's own correctness (only true cross-platform
    Windows/macOS parity, if ever claimed, would need separate real-machine verification — same
    caveat every backend already carries).

---

## Active execution order — do this one phase at a time

1. Phase X1 (CMake integration + skeleton) unblocks everything else. Verify Design decisions 1/10
   (native build, sibling SDL3 target reuse) empirically here before anything else.
2. Phase X2 (DirectDraw device/window bring-up) must land before any surface work — get
   `DirectDrawCreate`→`SetCooperativeLevel`(Design decision 2)→`SetDisplayMode`→primary
   `CreateSurface`→`Clear`(`ColorFill` `Blt`)→`Flip`(present) working and pixel-verified first, the
   same "prove the foundation before building on it" order `plan_software.md` Phase S1→S4 used.
3. Phase X3 (texture/render-target backends: offscreen surfaces + `Lock`/`Unlock`) is the storage
   layer everything else composites into/out of.
4. Phase X4 (the CPU compositor / `SpriteBatch` draw path) is the architectural core and the actual
   point of this backend (Design decision 5) — verify continuously against Phase X3, not left to
   the end, same as `plan_software.md` Phase S6's own instruction.
5. Phase X5 (blend-mode math) builds directly on X4's per-pixel compositor.
6. Phase X6 (`SpriteFont`) should fall out of X3+X4 almost for free — confirm, don't assume.
7. Phase X7 (`ThrowNo3D` wiring) can happen any time after X1 but must be complete before this
   backend is feature-complete.
8. Phase X8 (tests + `docs/freedirect-backend.md`) — add test coverage in the same task that implements
   each capability (this family of repos' standing convention), not bolted on afterward. Since
   Design decision 11 means real pixel tests are possible here, hold this backend to the same bar
   `SOFTWARE`/`HEADLESS` met, not a lesser one.

For every task: build the affected target(s) (`-DCNA_GRAPHICS_BACKEND=DX3`), run the relevant
`CnaTests`/dedicated CTest, and do not mark a task ✅ without both actually passing.

---

## Phase X1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-1 | Add `"FREEDIRECT"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property and a matching `CNA_BACKEND_FREEDIRECT` option flag | ✅ | |
| DX3-2 | Sibling-repo dependency wiring for `../free-direct` (`FATAL_ERROR` if missing, `add_subdirectory`), mirroring `../easy-gl`'s exact pattern | ✅ | |
| DX3-3 | Empirically verify `free-direct`'s `add_subdirectory(../free-api)` resolves `SDL3::SDL3`/`SDL3_image::SDL3_image`/`SDL3_mixer::SDL3_mixer` from CNA's own already-vendored targets with no extra flags (Design decision 10) | ✅ | Confirmed: configure log shows `free-api`'s generated-strings step running cleanly with zero extra flags, right after CNA's own vendored SDL3/SDL3_image/SDL3_mixer are configured. |
| DX3-4 | `cna_backend_graphics_freedirect` static library target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "FREEDIRECT")` block, mirrors `HEADLESS`/`SOFTWARE`/`CANVAS`) | ✅ | |
| DX3-5 | `include/CNA/Internal/Backends/FreeDirect/FreeDirectGraphicsBackend.hpp` (no `<ddraw.h>` visible here — opaque pointer/pimpl only, Design decision 9) + `src/CNA/Internal/Backends/FreeDirect/FreeDirectGraphicsBackend.cpp` (real `<ddraw.h>` usage lives here): class implementing every `IGraphicsBackend` pure virtual — real where X1 can make it real, honest throwing stubs elsewhere | ✅ | Went further than D3D11/D3D12's own precedent for design decision 9: even `FreeDirectGraphicsBackend.hpp` itself stays free of `<ddraw.h>` (true pimpl, `struct Impl`), because `<ddraw.h>` pulls in `free-api`'s `<windows.h>` shim, which globally `#define`s `fopen` — a real macro-leak risk D3D11's `<d3d11.h>` doesn't have. |
| DX3-6 | Factory dispatch for `DX3` in `CreateGraphicsBackend()` | ✅ | |
| DX3-7 | Confirm `CnaTests` links cleanly against the new backend target | ✅ | Full `CnaTests` builds and links; `--gtest_list_tests` runs clean (4736 lines, no crash). |

## Phase X2 — DirectDraw device/window bring-up

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-10 | `DirectDrawCreate(nullptr, &dd, nullptr)` at backend construction; verify the returned `IDirectDraw*` is real and `Release()`d correctly on backend destruction (COM refcounting, no leak) | ✅ | |
| DX3-11 | `SetCooperativeLevel(reinterpret_cast<HWND>(args.window), ...)` using CNA's own already-existing `SDL_Window*` — empirically verify Design decision 2's assumption (no second window gets created, no crash, `free-direct`'s internal `sdlWindow_` genuinely equals CNA's window) | ✅ | Verified via `FreeDirect_Smoke`: real window created, no crash, `free-direct`'s own log shows its internal `opengl`-backed SDL renderer bound against that exact window. |
| DX3-12 | `SetDisplayMode(width, height, 32)` sized to the game's requested backbuffer/`PresentationParameters` | ✅ | Design decision 4 (32bpp only). |
| DX3-13 | `CreateSurface` for the primary surface (`DDSCAPS_PRIMARYSURFACE`) — this becomes the backbuffer `IDirectDrawSurface*` | ✅ | |
| DX3-14 | `Clear(r,g,b,a)`: real `Blt` with `DDBLT_COLORFILL`/`DDBLTFX.dwFillColor` against the primary (or bound render target) surface | ✅ | Targets the shadow-backbuffer surface, not the primary directly — see this file's own top-of-document note on the `Lock()`-on-primary gap. **Corrected during external code review (2026-07-15)**: the original `DDBLT_COLORFILL` implementation silently discarded the requested alpha channel — `free-direct`'s own `FillColor()` hardcodes the written alpha byte to `255` unconditionally, so `Clear(..., a)` for any `a != 255` produced the wrong result with no error. Fixed by writing all 4 channels directly via `Lock()`/`Unlock()` (`FillSurfaceColor`) instead of `ColorFill`. Verified via `FreeDirect_Smoke` Check D (`Clear()` with `a=128` reads back exactly `128`, not `255`). |
| DX3-15 | `Present()`: `Flip()` on the primary surface (Design decision 11: real, pixel-verifiable — read back via `Lock()` after present, same as `SDL_RENDERER`'s own `GetBackBufferData` proof pattern) | ✅ | Deviates intentionally from the literal task wording: a single identity `Blt()` from the shadow backbuffer onto the primary, relying on `free-direct`'s auto-present-on-dirty-`Blt`. `Flip()` is never called — confirmed in `free-direct` source that it only sets an internal `usesFlip_` flag that *disables* that auto-present path, with no compensating benefit (`Flip()` copies no pixels itself). |
| DX3-16 | `GetViewportSize()`/`SetVirtualResolution()`/`SetPresentationMode()`: reuse the same backend-agnostic logical-resolution/letterbox math every other backend shares | 🟨 | **Corrected from ✅ during external code review (2026-07-15) — this task's own stated goal is not actually met.** `GetViewportSize()` is genuinely correct. But `SetPresentationMode()` stores the requested mode and then honestly does nothing further with it — physical output is *always* `LETTERBOX` regardless of what's requested (`Stretch`/`Overscan`/`NativeBackBuffer` are silently no-ops), and `SetVirtualResolution()`'s own in-code comment already admits a real bug: a resolution change *after* the first `Present()` keeps presenting at the stale old physical scale (`free-direct`'s `logicalPresentationSet_` flag is only ever applied once). Per this plan's own status legend ("✅ = verified against its stated acceptance criteria"), a task whose own code documents it doesn't do what it says cannot be ✅. Not fixed: doing so requires changing `free-direct` itself (its `PresentPrimary` would need to re-apply logical presentation on resize and support non-`LETTERBOX` modes), which design decision 8 explicitly reserves for a separate, `free-direct`-side, project-owner-approved task — not something this plan can do unilaterally. |
| DX3-17 | `GetWindowInternal()` returns the real `SDL_Window*`; `GetRendererInternal()` returns `nullptr` (no `SDL_Renderer*` — `free-direct` manages its own internal SDL renderer/texture privately, never exposed) | ✅ | |
| DX3-18 | Smoke CTest (`FreeDirect_Smoke`): construct backend, clear to a known color, present, read back via `Lock()`, assert exact pixel match — the real, automated equivalent of `Software_Smoke`/`Headless_Smoke` (Design decision 11) | ✅ | Reads back via `Lock()` on the shadow backbuffer (not the primary, per the `Lock()`-on-primary gap). 3/3 checks pass; registered and passing via `ctest -R FreeDirect_Smoke`. |

## Phase X3 — Texture and render-target backends

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-20 | `FreeDirectTextureBackend : ITextureBackend` — owns a private offscreen `IDirectDrawSurface*` (`DDSCAPS_OFFSCREENPLAIN`, 32bpp) sized to the texture; `UpdatePixels` writes via `Lock()`/`memcpy`/`Unlock()` | ✅ | Defined entirely inside `FreeDirectGraphicsBackend.cpp` (never named in the header) — no external code needs to name it, and this keeps `<ddraw.h>` fully contained (design decision 9) without a pimpl. Shares `CreateOffscreenSurface`/`WriteSurfacePixels`/`ReadSurfacePixels` helpers with `FreeDirectRenderTargetBackend`. |
| DX3-21 | `SetData`/`GetData` full round-trip via `Lock`/`Unlock` — genuinely synchronous, no async concerns at all (an advantage over `CANVAS`'s Design decision 3 workaround, since COM calls are already synchronous by nature) | ✅ | `Texture2D::GetData` reads from CPU-side `cpuPixels_`, never the backend (confirmed in `Texture2D.cpp`), so the meaningful round-trip proof is via `RenderTarget2D` bind+`Clear`+`GetBackBufferData` (`FreeDirect_TextureRenderTarget` CTest, Checks D/E) rather than `Texture2D::GetData` itself. |
| DX3-22 | Mip levels (`level>0` `SetData`): decide the same way `SDL_RENDERER` (Task 681) and `CANVAS` (CANVAS-21) did — no native mip chain in `DirectDrawSurface` either; likely throws for `level>0`, `level=0` unaffected | ✅ | Throws (matches `SDL_RENDERER` Task 681's message style); `level=0` always routes through `UpdatePixels`, confirmed in `Texture2D::SetData`. |
| DX3-23 | `FreeDirectRenderTargetBackend : IRenderTargetBackend` — same offscreen-surface mechanism; `BindAsRenderTarget()`/`UnbindAsRenderTarget()` switch which surface subsequent `Clear`/compositor writes/`Blt` calls target | ✅ | `BindAsRenderTarget`/`UnbindAsRenderTarget` write/clear a `LPDIRECTDRAWSURFACE*` slot living in `FreeDirectGraphicsBackend::Impl` (`currentTargetSurface`), passed in at construction — `FreeDirectRenderTargetBackend` never needs to name the private `Impl` type itself. |
| DX3-24 | `HasRealDepthBuffer()` → always `false` (no depth buffer concept in `DirectDrawSurface` at all) | ✅ | |
| DX3-25 | `RenderTargetUsage::DiscardContents` vs `PreserveContents` — same observable contract `SDL_RENDERER` Task 706/`CANVAS`-24 already established | ✅ | Confirmed: this is entirely shared `GraphicsDevice.cpp` logic (Task 704's gating), not backend-specific — came for free once `SetRenderTarget2D`+`Clear()` were wired correctly. `FreeDirect_TextureRenderTarget` CTest Check F confirms `DiscardContents` really auto-clears to black on rebind. |
| DX3-26 | `ReadBackbuffer()`/`GetBackBufferData()`: real `Lock()` + `memcpy` from the currently-bound surface | ✅ | Added `Impl::ActiveSurface()` (`currentTargetSurface ? currentTargetSurface : backBuffer`); `Clear()`/`ReadBackbuffer()` both go through it. `Present()` deliberately does not — it always Blt()s the real shadow backbuffer, matching FNA's own backbuffer/render-target separation. |
| DX3-27 | `SetRenderTargets` with 2+ bindings (MRT): throw — single-surface-target reality, same conclusion `SDL_RENDERER` Task 709/`CANVAS`-26 already reached | ✅ | `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow`'s backend gate didn't know about `DX3` (only `SDL_RENDERER`) — fixed at Phase X4 closure, see the correction note above. |
| DX3-28 | 4096×4096 dimension cap: `free-direct`'s own `CreateSurface` already enforces this (`docs/directdraw-limitations.md`) — confirm CNA's texture-size validation doesn't contradict it (e.g. surface up to XNA's own larger limits silently truncating instead of throwing) | ✅ | Confirmed empirically (`FreeDirect_TextureRenderTarget` CTest Check H): `Texture2D(5000, 5000)` throws (free-direct's `CreateSurface` returns `DDERR_INVALIDPARAMS`, propagated via `ThrowHr`); CNA's own `Texture2D` constructor performs no silent width/height clamping (confirmed by reading `Texture2D.cpp`). |

## Phase X4 — CPU compositor / `SpriteBatch` draw path

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-30 | `FreeDirectSpriteBatchBackend : ISpriteBatchBackend` skeleton; `Begin()`/`End()` — no explicit flush needed (each `Draw()` can composite immediately, same reasoning `CANVAS`-30 used) | ✅ | Defined entirely inside `FreeDirectGraphicsBackend.cpp` (never named outside it), same containment reasoning as `FreeDirectTextureBackend`/`FreeDirectRenderTargetBackend`. `Begin()`/`End()` throw on mismatched calls (`FreeDirect_SpriteBatch` CTest Check A). |
| DX3-31 | Identity fast path: destination position only, `scale=1`, `tint=White`, `blend=Opaque`, no rotation/flip → real `BltFast`/`Blt` straight copy, no CPU compositing needed | ✅ | Gate also checks `SetTransformMatrix()` is identity (a non-identity transform forces the general path even if otherwise identity-shaped) and `ApplyBlendState`'s new Opaque-preset detection (see Phase X5 note below). Verified exact-pixel via `FreeDirect_SpriteBatch` Check B. |
| DX3-32 | General path: `Lock()` source texture surface + destination surface, composite the quad pixel-by-pixel (position, scale, rotation about `origin`, tint multiply, `SpriteEffects` flip), `Unlock()` both | ✅ | `CompositeQuad` (2-triangle edge-function rasterizer, winding-agnostic). Verified via `FreeDirect_SpriteBatch` Checks C/D (full-opacity and zero-alpha edge cases under `AlphaBlend`, chosen to be exact-float-safe rather than asserting a partial-alpha blended byte value). A real bug was caught and fixed before any test ran: the initial straight-alpha "over" formula multiplied the destination term by `(1-srcAlpha)` but forgot to multiply the source term by `srcAlpha` — fixed to `out = src*srcAlpha + dst*(1-srcAlpha)`. |
| DX3-33 | Rotation around `origin` — verify against the same known-correct formula `SDL_RENDERER` Task 671 fixed | ✅ | Quad-corner placement math ported verbatim from `SoftwareSpriteBatchBackend::Draw()` (design decision 5). Verified via `FreeDirect_SpriteBatch` Check G (pi rotation swaps opposite quadrants) — first test run caught a *test* bug (wrong expected sample coordinates, not a rasterizer bug): a rotated quad centered on `origin` is centered ON the destination position, not offset from it. |
| DX3-34 | `SpriteEffects::FlipHorizontally`/`FlipVertically` | ✅ | Verified via `FreeDirect_SpriteBatch` Check E. |
| DX3-35 | Scalar / `Vector2` scale overloads | ✅ | Both resolve to the same `destinationRectangle`-vs-`sourceRectangle` ratio at the `ISpriteBatchBackend::Draw` boundary (shared `SpriteBatch.cpp` converts scalar/`Vector2` scale to a `destinationRectangle` before calling the backend) — no backend-specific overload needed. Verified via `FreeDirect_SpriteBatch` Check F. |
| DX3-36 | `SetTransformMatrix()` (`Begin(transformMatrix)`): apply the full affine matrix per-source-pixel in the compositor (straightforward once per-pixel sampling already exists for the general path) | ✅ | Applied as a point transform (z=0) on the already-screen-space quad corners, same convention `SOFTWARE`'s own `SetTransformMatrix` uses. Verified via `FreeDirect_SpriteBatch` Check H (translation). |
| DX3-37 | `SpriteSortMode` handling: confirm fully covered by shared, backend-agnostic `SpriteBatch` code — expect no backend-specific code needed (same finding as `SDL_RENDERER` Task 677/`CANVAS`-37) | ✅ | Confirmed by reading `SpriteBatch.cpp`: `sortMode_`/sorting are resolved entirely there before `backend_->Draw(...)` is ever called. Verified via `FreeDirect_SpriteBatch` Check I (`BackToFront` draws without throwing). |
| DX3-38 | Custom `Effect` via `Begin(effect)`: throws for non-null custom effects (no shader stage exists here either) | ✅ | Verified via `FreeDirect_SpriteBatch` Check J. |
| DX3-39 | Source-rectangle cropping | ✅ | Exercised implicitly by every `Draw()` call in `FreeDirect_SpriteBatch` (all use an explicit `sourceRectangle`). |

**Phase X4/X5 blend-state note**: `FreeDirectGraphicsBackend::ApplyBlendState()` started in Phase X4 as
only a boolean Opaque-vs-"blend" gate; Phase X5 replaced it with `DetectBlendMode()`, which
matches the raw factors against all 4 `BlendState.cpp` presets by exact value (falling back to
`AlphaBlend` for any other/custom combination, DX3-44) and drives `CompositeQuad`'s real,
per-formula math (DX3-40..43) below.

## Phase X5 — Blend-mode compositing math

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-40 | `Opaque`: direct overwrite, ignore source alpha | ✅ | Verified via `FreeDirect_Blend` CTest Check A. |
| DX3-41 | `AlphaBlend` (premultiplied): straight `SrcAlpha`/`InvSrcAlpha` per-pixel formula | ✅ | Premultiplied convention: `out = src + dst*(1-srcAlpha)` — the source color is used as-is (not multiplied by `srcAlpha` again), since this preset assumes an already-premultiplied source pixel. Verified via `FreeDirect_Blend` CTest Check B. |
| DX3-42 | `NonPremultiplied` (straight alpha): correct textbook blend | ✅ | `out = src*srcAlpha + dst*(1-srcAlpha)`. Verified via `FreeDirect_Blend` CTest Check C. |
| DX3-43 | `Additive`: saturating add, clamp at 255 | ✅ | `out = src*srcAlpha + dst` (no destination attenuation at all — `ColorDestinationBlend=One`). Verified via `FreeDirect_Blend` CTest Check D. |
| DX3-44 | Custom `BlendState` (non-preset factor/op combos): falls back to `AlphaBlend` behavior — same recorded scope limitation as `SOFTWARE` design decision 7 | ✅ | Verified via `FreeDirect_Blend` CTest Check E (a `BlendState` with `SourceColor`/`One` factors, matching none of the 4 presets, produces the exact `AlphaBlend` result). **Real bug found in external code review and fixed (2026-07-15)**: `DetectBlendMode()` originally matched presets on the 4 blend *factors* only, ignoring `colorBlendFunc`/`alphaBlendFunc` entirely — a custom `BlendState` with Opaque's exact factors (`One,One,Zero,Zero`) but `BlendFunction::Subtract` was misdetected as `Opaque` (wrong: the blend *equation*, not just the factors, differs). Fixed by requiring both factors AND `BlendFunction::Add` (all 4 real presets use `Add` implicitly) to match a preset; any other function falls to the `AlphaBlend` fallback like any other custom combination. Verified via `FreeDirect_Blend` CTest Check F (Opaque-matching factors + `Subtract` correctly falls back to `AlphaBlend`, not `Opaque`). |
| DX3-45 | `TextureFilter` → nearest-neighbor vs. bilinear sampling in the compositor (Design decision 7's per-pixel sampling makes both genuinely implementable, unlike a native `SDL_ScaleMode`-style enum mapping) | ✅ | `ISpriteBatchBackend::SetSamplerFilter`'s raw `TextureFilter` ordinal is used directly (0=`Linear`→bilinear, everything else→nearest, matching the interface's own documented convention and `TextureFilter.hpp`'s real enum values). Verified via `FreeDirect_AddressMode` CTest Checks A/B (`Point` samples a pure endpoint at a texel boundary; `Linear` blends). |
| DX3-46 | `TextureAddressMode::Wrap` (modulo) / `Mirror` (reflect) in the per-source-pixel sampler | ✅ | `WrapCoord()` handles `Wrap`(0)/`Clamp`(1)/`Mirror`(2) — ordinals confirmed to match `TextureAddressMode.hpp` exactly. Verified via `FreeDirect_AddressMode` CTest Checks C/D/E (an oversized source rectangle produces 3 distinct, predictable Red/Green tiling patterns per mode) — a real win over `SDL_RENDERER`'s ⛔ BLOCKED status for the same modes. |

## Phase X6 — `SpriteFont`

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-50 | Single glyph at a known position/size — confirm needs no code beyond Phase X4's `Draw()` path | ✅ | Confirmed by reading `SpriteBatch.cpp`: `DrawString` lays out a destination rect per glyph and calls `pushSprite()` -> `backend_->Draw()`, the exact same `ISpriteBatchBackend::Draw()` overload Phase X4/X5 already implemented. Zero new backend code -- this phase adds only `FreeDirect_SpriteFont`, a CTest proving the claim empirically rather than merely asserting it. Verified via Check A (exact-pixel, no color-match tolerance needed, unlike `SDL_RENDERER`'s equivalent test). |
| DX3-51 | Multiple glyphs with spacing/kerning | ✅ | Expect ✅ with no new code, per `SDL_RENDERER` Task 691's own finding — confirmed. Verified via `FreeDirect_SpriteFont` Check B (two distinctly-colored glyphs land at their correctly kerning-advanced positions; the gap between them stays background). |
| DX3-52 | `\n` newline advance | ✅ | Verified via `FreeDirect_SpriteFont` Check C. |
| DX3-53 | Unknown-character fallback (`defaultCharacter`) | ✅ | Verified via `FreeDirect_SpriteFont` Check D (both the fallback-glyph-used case and the throws-when-unset case). |
| DX3-54 | `SpriteEffects` flip + rotation/origin/scale with `DrawString` | ✅ | The shared `SpriteBatch.cpp` fix from `SDL_RENDERER` Task 694 is backend-agnostic and already applies here — confirmed. Verified via `FreeDirect_SpriteFont` Check E (`FlipHorizontally` mirrors the whole glyph sequence, not just each glyph individually). |

**Phase X6 CTest**: `FreeDirect_SpriteFont` (5 checks, all exact-pixel) — no `FreeDirectGraphicsBackend.cpp`/`.hpp` changes were needed for this phase at all, only the new test file + its `CMakeLists.txt` registration.

## Phase X7 — `ThrowNo3D` wiring and remaining defaults

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-60 | `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` → `ThrowNo3D` | ✅ | Already implemented since Phase X1/X2. **Real finding**: these are provably unreachable from the public `GraphicsDevice::Clear(ClearOptions, ...)` API — shared `GraphicsDevice.cpp` masks `DepthBuffer`/`Stencil` out of the request before it ever reaches the backend, because `SupportsDepthStencil()` is `false` (DX3-65). Verified both the graceful-degrade behavior (`FreeDirect_No3D` Check D, `Clear()` doesn't throw) and that the methods still throw if ever reached directly (same check, direct backend calls). |
| DX3-61 | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` → `ThrowNo3D` | ✅ | Already implemented since Phase X1/X2; unlike DX3-60, these ARE directly, unconditionally reachable from `GraphicsDevice` (no masking). Verified via `FreeDirect_No3D` Check E. |
| DX3-62 | `CreateVertexBuffer`/`CreateIndexBuffer16`/`CreateIndexBuffer32` → `ThrowNo3D` | ✅ | `CreateVertexBuffer`/`CreateIndexBuffer16` already implemented since Phase X1/X2; `CreateIndexBuffer32` needed no override at all — `IGraphicsBackend`'s own default delegates to `CreateIndexBuffer16`, which already throws, so the composition is correct for free. Verified via `FreeDirect_No3D` Checks A/B/C (`VertexBuffer`/`IndexBuffer` construction throws for both 16- and 32-bit). |
| DX3-63 | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`DrawInstancedPrimitivesEx` → `ThrowNo3D` | ✅ | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` already implemented since Phase X1/X2; the `*Ex` variants needed no override — `IGraphicsBackend`'s own defaults delegate to the non-`Ex` methods (or throw directly for `DrawInstancedPrimitivesEx`). **Real finding**: since DX3-62 already makes it impossible to construct a `VertexBuffer` at all, every method in this row is provably unreachable from the public API — not directly tested via a synthetic backend call (would need a fabricated `IVertexBufferBackend`, adding artificial complexity to re-prove something DX3-62 already establishes). |
| DX3-64 | `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` → `nullptr` | ✅ | No override needed — `IGraphicsBackend`'s own defaults already return `nullptr`, and `Texture3D`/`TextureCube`/`RenderTargetCube` are all designed to degrade gracefully against a null backend (confirmed by reading their `.cpp` constructors). Verified via `FreeDirect_No3D` Check F. |
| DX3-65 | `SupportsDepthStencil()` → `false` | ✅ | Already implemented since Phase X1/X2 (inline in the header). Verified both directly and via its real consequence for DX3-60 (`FreeDirect_No3D` Check D). |
| DX3-66 | `CreateOcclusionQuery()` → `nullptr` | ✅ | **Real bug found and fixed**: the Phase X1/X2 skeleton had this throwing (`ThrowNo3D`), inconsistent with this row's own spec (`-> nullptr`) and with `OcclusionQuery`'s own null-safe design (its constructor/`Begin`/`End`/getters all check for a null backend and degrade gracefully — confirmed by reading `OcclusionQuery.cpp`). Fixed by removing the override entirely, letting `IGraphicsBackend`'s own default (`return nullptr`) apply, matching DX3-64/67's same pattern. Verified via `FreeDirect_No3D` Check G. |
| DX3-67 | `CreateEffectBackend()` → `nullptr` | ✅ | No override needed — same reasoning as DX3-64; `ShaderEffect` (the NOXNA class that calls `CreateEffectBackend`) is null-safe throughout (confirmed by reading `ShaderEffect.cpp`). Verified via `FreeDirect_No3D` Check H. |
| DX3-68 | `TransformWindowToLogical`/`TransformLogicalToWindow`: implement for real (needed for correct mouse-coordinate mapping under letterboxing) | ✅ | Real, new code (the only genuinely new implementation this phase needed): an independent letterbox scale+offset computation from the real physical `SDL_Window` size (queried directly) vs `Impl::logicalWidth/Height` — matches `free-direct`'s own hardcoded `SDL_LOGICAL_PRESENTATION_LETTERBOX` behavior without ever needing access to its internal (never-exposed) `SDL_Renderer`. `FreeDirect_LogicalTransform` CTest verifies round-trip correctness, centering, uniform (non-anisotropic) scale, and the actual "fit-largest-without-overflow" formula against the real physical window size in this dev environment (2048x1152, a genuinely non-trivial letterbox against the 64x64 logical size) — `SDL_SetWindowSize` was tried first but is not reliably honored by the window manager in this sandboxed environment, so the test queries whatever the real size actually is instead of forcing one. |
| DX3-69 | `DebugSimulateContextLoss`/`DebugRestoreContext`: likely no-op (`IsLost`/`Restore` are inert stubs in `free-direct` itself, per its own `docs/directdraw-limitations.md`) — confirm rather than assume | ✅ | Confirmed: no override needed. No other 2D-only CNA backend (`SDL_RENDERER`/`SOFTWARE`/`HEADLESS`) overrides these either — only `EasyGL` does, since it is the only backend with a real GL context that can genuinely be lost/restored. DX3's CPU/`DirectDraw` compositor has no equivalent concept, so the inherited no-op default is correct. Verified via `FreeDirect_No3D` Check I. |

## Phase X8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| DX3-80 | `FreeDirect_Smoke` CTest (see DX3-18) | ✅ | Landed in Phase X2. |
| DX3-81 | `FreeDirect_Compositor` CTest: rotation/scale/tint/flip pixel-verified, same rigor `Software_Rasterizer` applied | ✅ | Landed in Phase X4 as `FreeDirect_SpriteBatch` (10 checks) — same content/rigor this task asks for, under the name that matched this plan's own phase structure rather than the originally-guessed CTest name. |
| DX3-82 | `FreeDirect_Blend` CTest: all 4 supported blend modes pixel-verified | ✅ | Landed in Phase X5 (5 checks, including the custom-`BlendState`-fallback case). |
| DX3-83 | `FreeDirect_AddressMode` CTest: `Wrap`/`Mirror` sampling pixel-verified (Design decision 7's real win) | ✅ | Landed in Phase X5 (5 checks — also covers `TextureFilter`/DX3-45, since both are "sampling" concerns tested together). |
| DX3-84 | `docs/freedirect-backend.md`: mirror `docs/sdl-renderer-2d-completeness.md`'s table/status-legend structure | ✅ | Written this phase — per-phase tables (X1/X2 through X7), a status legend (✅/❌-throws-by-design/⚪-degrades-to-nullptr), a "known permanent limitations" section, and a "what actually works today" summary table, mirroring the reference doc's structure at DX3's own (much narrower) scope. |
| DX3-85 | Update `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` STRINGS docstring and `../cna/plan.md`/`README.md` to list `DX3` | ✅ | `CMakeLists.txt`'s STRINGS docstring already included `DX3` since Phase X1 (DX3-1). `../cna/plan.md` doesn't exist relative to this repo — `cnadx3` is its own checkout with its own top-level `plan.md`, which is a cross-cutting *deferred-task* tracker, not a backend index; added a one-line update to its `a-0001` entry instead (DX3 now also implements `TransformLogicalToWindow`, DX3-68), which is genuinely relevant there. `README.md` updated: a new `DX3` bullet in §1 Project Status, and `DX3` (plus the previously-missing `WEBGPU`/`HEADLESS`/`SOFTWARE`/`D3D11`/`D3D12`) added to §6's backend list. `docs/README.md`'s file index updated too (58 files, new `freedirect-backend.md` entry). |
| DX3-86 | Full `CnaTests` regression run under `-DCNA_GRAPHICS_BACKEND=DX3` — confirm no unrelated suite breaks, same bar every other backend's Phase 1 closure already met | ✅ | Final run (after the external-review fixes above): **4364 passed, 2 skipped, 12 failed** — all 12 the same pre-existing, structural `VertexBuffer`-construction-under-2D-only-backend failures, unrelated to DX3 code (see the status-header correction note). `GraphicsBackendCompileDefinitionsTest` — previously left as "out of scope" — is now fixed too, so the failure count actually dropped from 13 to 12 in this pass. Zero failures caused by DX3 itself remain unaccounted for. |

---

## Boundaries — explicitly out of scope for v1

- **`DirectSound`/`DirectPlay`** — owner's explicit instruction (Design decision 3); CNA's audio
  stays exactly as it is today.
- **No 3D pipeline** (Design decision 9's `ThrowNo3D` wiring) — permanent, not a v1-only gap;
  matches `free-direct`'s own "Direct3D not implemented" stance.
- **8-bit/palette surfaces, `GetDC`/`ReleaseDC`, `SetPalette`/`CreatePalette`** — XNA has no
  palette-texture concept; `free-direct` itself calls this path "currently unreachable" even by its
  own two target games (Design decision 4).
- **Mip levels (`level>0` `SetData`)** — expected throw, pending DX3-22 confirming no cheap real
  option exists.
- **Any `free-direct` API surface not already `IMPLEMENTED`/`PARTIAL` today** — Design decision 8;
  extending `free-direct` itself is a separate cross-repo ask, not something this plan can do
  unilaterally.
- **Real Windows/macOS verification** — this plan proves the backend on Linux in this dev
  environment (Design decision 11); cross-platform parity claims for Windows/macOS remain
  unverified here, same caveat every CNA backend already carries for platforms this session can't
  reach.
