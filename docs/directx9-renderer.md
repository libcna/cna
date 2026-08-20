# Direct3D 9 graphics renderer

## What this renderer is for (and isn't)

XNA 4.0 ran on Direct3D 9. Every other CNA graphics renderer (`EasyGL`, `Vulkan`, `Bgfx`, `D3D11`,
`D3D12`, `SDL_Renderer`, `WebGPU`, `Headless`, `Software`) targets **feature parity** — the same
XNA-shaped surface, reimplemented against a modern API. `D3D9` targets something narrower and
harder: **pixel-for-pixel indistinguishability from the original XNA 4.0 runtime itself**, not
just "renders plausibly." It runs **Microsoft's own XNA 4.0 Stock Effects HLSL** (`BasicEffect.fx`
and 5 siblings), vendored verbatim from the FNA tree and compiled by CNA itself via the real
`d3dcompiler_47.dll` — not reimplemented, not ported, not approximated. Where every other renderer
asks "does this feature work," this one asks "does this pixel match the pixel real XNA 4.0
produced for the identical scene."

That measurement is not rhetorical. `tools/xna-oracle/` stands up the **real XNA 4.0 runtime**
under Wine (the actual GAC assemblies, compiled by the real in-prefix `csc.exe` — no
reimplementation of XNA anywhere in the oracle itself) and renders the same declarative `.scene`
file through both the real XNA runtime and CNA's own D3D9 renderer, then diffs the two PNGs
pixel-for-pixel at `--tolerance 0` (exact match, not "close"). Both sides execute through the same
DXVK Direct3D-9-over-Vulkan implementation on this dev machine, which is what makes the diff
meaningful — without that, it would silently measure a driver difference and misattribute it to
CNA.

**Current result: 0/31 scenes diverge.** See `docs/d3d9-divergence-report.md` for the full,
current measurement — what's covered, what isn't yet, and the honest DXVK-authenticity caveat
every result here inherits (below).

Select this renderer with:

```bash
cmake -S . -B cmake-build-d3d9 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_RENDERER=D3D9 \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-d3d9 -j
```

`D3D9`, like `D3D11`/`D3D12`, is hard-gated to Windows cross-compilation (or native Windows) at
configure time.

## What's real

- **Device lifecycle**: plain `Direct3DCreate9` (not D3D9Ex — design decision 2), the real
  `DeviceLost`/`DeviceResetting`/`DeviceReset` XNA event sequence, `D3DPOOL_MANAGED` resources that
  genuinely survive `Reset()` with no re-upload, `D3DPOOL_DEFAULT` resources (dynamic
  buffers/render targets) that correctly release-then-recreate around a device-lost cycle.
- **All 5 XNA Stock Effects** (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
  `EnvironmentMapEffect`, `SkinnedEffect`), dispatched through CNA's own real
  `ComputeXShaderIndex()` logic (transcribed from FNA's own `.cs` sources + the vendored `.fx`
  files' own tables) driving Microsoft's own compiled bytecode — 61 of 66 compiled shader
  variants are byte-identical to Microsoft's own shipped `.fxb`; the other 5 (`PixelLighting`
  variants) differ only by compiler version, not logic, and are separately oracle-proven
  equivalent.
- **`SpriteBatch`**: the real `SpriteEffect.fx` (Microsoft's own, vendored verbatim), the D3D9
  half-pixel offset baked into `MatrixTransform` on the CPU (the classic D3D9-era texel-convention
  fix, oracle- and mutation-verified — the fix is genuinely invisible to a 1×1-texture test, a
  real finding this project's own methodology caught before it became a false-positive "closed"
  claim), all sampler address modes, 3 of 5 `SpriteSortMode` values, multi-texture batching.
- **`GraphicsProfile.Reach`/`.HiDef`**: this is the **only** CNA renderer where this distinction is
  real, because it's the only one with a real `D3DCAPS9` to consult. `IsProfileSupported()`,
  `QueryRenderTargetFormat()`/`QueryBackBufferFormat()`, and resource-creation-time enforcement
  (`Texture2D`/`TextureCube`/`Texture3D` size ceilings, `MaxRenderTargets`) are all real,
  reachable through the actual public API — not a hardcoded table pretending to be a capability
  query, which is exactly what every other renderer's `IsProfileSupported() { return true; }`
  honestly is (and correctly stays).
- **Buffers, textures, render targets, MRT, MSAA, occlusion queries, NPOT, hardware instancing,
  all render/blend/depth-stencil/rasterizer state** — same shape as `D3D11`'s own established
  coverage, real `SetRenderState`/`SetSamplerState` sequences (D3D9 has no state objects to
  cache — design decision 11).

## What's not (yet)

- **Render targets sampled back as a texture**: blocked on a real, reproducible crash
  (`dxvk::DxvkError`, uncaught, apparently on a DXVK async shader-compiler thread) the moment a
  `D3DUSAGE_RENDERTARGET`-flagged texture exists in-process alongside any subsequent draw call.
  Documented in `NEXT.md` §4 with a full reproduction record; needs Vulkan validation layers or
  DXVK-internals debugging to root-cause, not another oracle-scene attempt.
- **Every `SurfaceFormat` besides `Color`**: CNA's own `Texture2D::SetData`/`GetData` C++ API is
  `Color`-only (no generic `SetData<T>` matching real XNA's own generic API) — a shared,
  cross-renderer limitation (see `docs/graphics-renderer-feature-matrix.md`'s own `Texture2D` row),
  not specific to D3D9.
- **`EnvironmentMapEffect`'s specular variants, `PreferPerPixelLighting`**
  (`BasicEffect`/`EnvironmentMapEffect`/`SkinnedEffect`): blocked on a confirmed, cross-cutting
  `GpuDrawParams` gap present on **every** CNA renderer (Divergence 1/4 in `plans/plan_dx9.md`'s own
  "CNA's divergences from XNA 4.0" section) — out of this renderer's authority to fix; measured
  and reported, not silently worked around.
- **`SpriteSortMode.Immediate`/`.Texture`**: `Immediate`'s only real behavioral difference from
  `Deferred` isn't pixel-observable by a raster-diff methodology at all; `Texture` sorts by
  `Texture.GetHashCode()` in real FNA, an implementation-defined identity hash with no
  predictable ordering — confirmed not viable as a deterministic oracle scene, not merely
  deferred.
- **NPOT-wrap-on-`Reach`, hardware-instancing's `HiDef`-only gate**: real XNA's own enforcement
  behavior here is undocumented and FNA implements neither — inventing enforcement without a
  reference to verify against would be asserting behavior this project cannot actually check.
- **`CnaTests` does not build under `CNA_GRAPHICS_RENDERER=D3D9`** — ~10 test files call
  POSIX-only `::setenv()`, the same wall `D3D11` already hits. A known, pre-existing,
  cross-renderer gap, not something this renderer introduced.
- **Real Windows hardware verification** — see the DXVK-authenticity caveat immediately below.

## The one caveat every result above inherits

Every check in this renderer — the oracle's own `0/31` result included — ran under Wine+DXVK, on
this dev machine's own GPU (AMD Radeon 780M, RADV). `D3DCAPS9` in this loop is **synthesized by
DXVK**, not reported by an authentic XNA-era (~2006–2013) Direct3D 9 driver.
`IsProfileSupported(HiDef)` returning `true` here proves the comparison *logic* is correct, not
that a real `HiDef`-class GPU exists in this loop (it doesn't). This renderer's real claim is
precisely: **indistinguishable from real XNA 4.0 running through the same DXVK
Direct3D-9-over-Vulkan path, on this machine** — a real, strong, reproducible result, and a
narrower one than "authentically indistinguishable from XNA on real hardware." That gap is
`plans/plan_dx9.md`'s own `D9-140`, `needs_human`, still open.

## Development environment: Wine + DXVK dev-loop

Same shape as `D3D11`'s own dev loop (`docs/directx11-renderer.md`), plus a SECOND, separate Wine
prefix for the real XNA 4.0 oracle:

```text
Debian (this repo's actual dev machine)
└── Windows cross-build (cmake/toolchains/mingw-w64.cmake)
    └── D3D9
         ├── compile: MinGW-w64 (x86_64-w64-mingw32-{gcc,g++})
         ├── CNA-side dev-loop test: Wine + DXVK (~/.wine-cna-d3d9-spike or CNA_D3D9_WINEPREFIX)
         ├── XNA-side oracle: a SEPARATE Wine prefix (~/.wine-cna-xna40) with the real XNA 4.0 GAC
         │   assemblies + in-prefix csc.exe + DXVK also installed (so both sides hit the same
         │   Direct3D-9-over-Vulkan path)
         └── final verification: a real Windows machine (still open, D9-140)
```

```bash
scripts/run-wine-dxvk9.sh cmake-build-d3d9/examples/directx9_smoke_test.exe
```

`CNA_D3D9_WINEPREFIX` overrides the prefix; the D9-5 gate fails loudly (exit 3) if a run silently
fell back to WineD3D instead of DXVK. `ctest --test-dir cmake-build-d3d9 -L D3D9` runs every D3D9
test — including `D3D9_XNA_Diff` (below) — through this same wrapper automatically; none of them
need the separate XNA prefix to run.

## The oracle harness (`tools/xna-oracle/`)

- `tools/xna-oracle/scenes/*.scene` — a shared, declarative `key=value` text scene format, parsed
  identically by both sides. Extending it (a new key, a new effect combination) is the normal way
  to grow coverage — see `tools/xna-oracle/README.md` for the full key reference and current
  31-scene corpus manifest.
- `tools/xna-oracle/Oracle.cs` — the real-XNA-4.0 side, compiled by the in-prefix `csc.exe`
  (targets .NET Framework 4.0-era C# — no expression-bodied members, no string interpolation, no
  C# 6+ syntax at all).
- `tools/xna-oracle/CnaOracleRender.cpp` — the CNA side, through the real public
  `Game`/`GraphicsDeviceManager`/`GraphicsDevice`/effect/`SpriteBatch`/`Texture2D` API, never the
  raw renderer interface.
- `tools/xna-oracle/reference/*.png` — the 31 real-XNA-4.0 renders, captured once and checked in.
  `D3D9_XNA_Diff` (a real CTest, `D9-120`) diffs every scene against these on every run — it needs
  only the D3D9 Wine prefix, never the XNA one, to keep validating against them.
- `scripts/xna-diff.py` — the pixel-diff tool itself (`--tolerance` defaults to `0`; **never**
  widen it to turn a red comparison green without a documented, per-scene reason — that is exactly
  how an authenticity project quietly becomes a parity project).

## Writing a D3D9 test

Two established patterns, matching `D3D11`'s own precedent:

1. **`Game`-subclass CTests** (`examples/directx9_*_test.cpp`) — the normal path for anything
   exercising the real public XNA API (`DirectX9_Smoke`, `DirectX9_Draw`, `DirectX9_DrawEx`, `DirectX9_SpriteBatch`,
   `DirectX9_GraphicsProfile`, ...). Most draw a known scene, read back specific pixels via
   `GraphicsDevice::GetBackBufferData()`, and assert exact or discriminating-expected colors — not
   just "the call returned `S_OK`."
2. **Pure-function checks** (`modules/renderers/directx9/examples/directx9_common_test.cpp`, `DirectX9_Common`) — format/state/
   vertex-layout mapping-table checks needing no device/GPU at all.
3. **The oracle** (`tools/xna-oracle/`) — for anything where "matches XNA" is the actual
   question, not just "internally consistent." This is the authoritative layer; the CTests above
   are fast, offline regression guards for findings the oracle already proved once.

Every check added to this renderer that claims to test something is expected to be
**mutation-verified**: deliberately break the implementation it exercises, confirm the check (and
only the checks that should be affected) goes red, then revert and reconfirm green. See
`plans/plan_dx9.md`'s own `D9-91`/`D9-93`/`D9-103`/`D9-122` closure notes for real examples of this
discipline catching genuine false-positive traps (a check that stays green even with its own
target feature disabled is worse than no check at all).

## Known limitations (2026-07-15)

- **Not verified on real Windows hardware** (`D9-140`, `needs_human`) — see the DXVK-authenticity
  caveat above.
- **`CnaTests` does not build under D3D9** (`D9-123`) — same POSIX `::setenv()` wall `D3D11`
  already documents.
- **`DirectX9_Common`'s lookup-table checks are mostly not individually mutation-verified** — `D9-122`
  closed the highest-value gaps (blend state, device-lost recovery, MRT, render-target bind,
  vertex-buffer upload, cube-texture upload) but explicitly left most of `DirectX9_Common`'s ~30
  homogeneous mapping-table checks and a handful of `DirectX9_Smoke` checks (Clear/Clear* combo
  variants, resize) unverified this pass — documented, not silently assumed covered.
- **Render targets cannot be sampled as textures at all** — see "What's not (yet)" above.
- **`D9-11` (custom `ShaderEffect`)** is explicitly ask-first in `plans/plan_dx9.md`'s own execution
  order and has not been started.

See `plans/plan_dx9.md` for the full task-by-task status (`D9-0` through `D9-140`) and design rationale,
`docs/d3d9-divergence-report.md` for the current oracle measurement, and
`docs/graphics-renderer-feature-matrix.md` for a row-by-row comparison against the other
established renderers (with the caveat that D3D9's own goal — XNA-authenticity, not feature parity
— makes some of that matrix's rows less meaningful for this renderer than for the others; the
oracle/divergence report is the more relevant measurement for D3D9 specifically).
