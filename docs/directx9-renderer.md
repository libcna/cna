# Direct3D 9 graphics renderer

## What this renderer is for (and isn't)

XNA 4.0 ran on Direct3D 9. Every other CNA graphics renderer (`docs/renderer-registry.md`)
targets **feature parity** — the same XNA-shaped surface, reimplemented against another API.
`DIRECTX9` targets something narrower and
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

**Current result: all 40 active scenes match real XNA 4.0 exactly**
(`D3D9_XNA_Diff`, rerun 2026-10-31). `D3D9_XNA_Manifest` additionally checks exact pairing for
7 auxiliary scene/reference fixtures. See
`docs/d3d9-divergence-report.md` for the measurement — what's covered, what isn't yet, and the
honest DXVK-authenticity caveat every result here inherits (below).

Select this renderer with:

```bash
cmake -S . -B cmake-build-mingw \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_RENDERER=DIRECTX9 \
  -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-mingw --parallel 8
```

`D3D9` is hard-gated to Windows cross-compilation (or native Windows) at configure time.

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
  all render/blend/depth-stencil/rasterizer state** — real `SetRenderState`/`SetSamplerState`
  sequences (D3D9 has no state objects to cache — design decision 11).
- **CNA punctual lighting**: `BasicEffect`, `SkinnedEffect`, `PbrEffect` and `SkinnedPbrEffect`
  consume the single `PunctualLightEXT` point/spot source, including range and spot-cone
  attenuation. The classic effects select CNA-owned SM3 variants only while the extension is
  active, leaving Microsoft's stock bytecode untouched for ordinary XNA draws. Focused public
  BasicEffect and SkinnedEffect regressions cover disabled, point, out-of-range and aimed/away
  spot cases; PBR also covers a skinned draw. The CNA shadow extension samples directional and
  cascaded maps, point-light cubes and spot-light maps in these paths; the focused PBR shadow
  orientation regression passes under DXVK.
- **Custom `ShaderEffect`**: HLSL compiled at run time through `D3DCompile` in the separate
  `cna_renderer_d3d9_effect` library, so the stock pipeline itself never links `d3dcompiler`;
  constants are mapped by CNA's own CTAB parser (`D9-110`..`D9-112`, `DirectX9_CompiledEffects`).
- **Render targets sampled as textures**, single and MRT: the DXVK crash once blamed for blocking
  them was a wrong `static_cast` in `D3D9EffectDraw.cpp` (`D9-84`, fixed 2026-07-16), and
  `DirectX9_RenderTarget_SamplingOrientation`, `_ProducerConsumer` and `_EffectSource` cover them.
  The `rendertarget_sampled_quad` oracle scene also matches the real XNA 4.0 render exactly.

## What's not (yet)

- **Every non-`Color` texture format in the XNA oracle**: ordinary D3D9 textures have a real,
  device-qualified classic/float format table and byte-exact transfer checks, but dedicated
  real-XNA oracle scenes have not swept those formats. Native sRGB and BC7 formats remain rejected.
  D3D9 `RenderTarget2D`/`RenderTargetCube` storage is currently `Color` only.
- **Cube-target MSAA**: deliberately unsupported by this renderer. Two-dimensional render targets
  use the device-qualified D3D9 multisample/resolve path.
- **Depth bias differential proof**: the states are forwarded as D3D9 float bit patterns, but the
  DXVK experiment did not produce a discriminating oracle result.
- **`SpriteSortMode.Immediate`/`.Texture`**: `Immediate`'s only real behavioral difference from
  `Deferred` isn't pixel-observable by a raster-diff methodology at all; `Texture` sorts by
  `Texture.GetHashCode()` in real FNA, an implementation-defined identity hash with no
  predictable ordering — confirmed not viable as a deterministic oracle scene, not merely
  deferred.
- **NPOT-wrap-on-`Reach`, hardware-instancing's `HiDef`-only gate**: real XNA's own enforcement
  behavior here is undocumented and FNA implements neither — inventing enforcement without a
  reference to verify against would be asserting behavior this project cannot actually check.
- **Real Windows hardware verification** — see the DXVK-authenticity caveat immediately below.

## The one caveat every result above inherits

Every check in this renderer — the oracle's own `40/40` result included — ran under Wine+DXVK, on
this dev machine's own GPU (AMD Radeon 780M, RADV). `D3DCAPS9` in this loop is **synthesized by
DXVK**, not reported by an authentic XNA-era (~2006–2013) Direct3D 9 driver.
`IsProfileSupported(HiDef)` returning `true` here proves the comparison *logic* is correct, not
that a real `HiDef`-class GPU exists in this loop (it doesn't). This renderer's real claim is
precisely: **indistinguishable from real XNA 4.0 running through the same DXVK
Direct3D-9-over-Vulkan path, on this machine** — a real, strong, reproducible result, and a
narrower one than "authentically indistinguishable from XNA on real hardware." That gap is
`D9-140`, `needs_human`, still open.

## Design decisions

The decisions behind the renderer, each binding because "indistinguishable from XNA 4.0" is the
goal (numbered as source comments cite them, "design decision N"):

1. **One renderer identity, `DIRECTX9`**, hard-gated to Windows targets at configure time.
2. **Plain Direct3D 9 (`Direct3DCreate9`), not D3D9Ex.** User resources live in
   `D3DPOOL_MANAGED`, so they survive `Reset()` exactly as XNA 4.0 promised; only render targets
   and dynamic buffers use `D3DPOOL_DEFAULT`. The full device-lost lifecycle is implemented and
   fires `DeviceLost`/`DeviceResetting`/`DeviceReset` at XNA's own points; its pain is the
   deliverable, not a reason to switch to D3D9Ex.
3. **Microsoft's Stock Effects sources are vendored verbatim** (`HLSL/*.fx`/`*.fxh` from the FNA
   tree, with their MS-PL headers and provenance README) and never edited. A compile problem is
   solved in the compile step or an added `#define`, never in Microsoft's source, so a `diff`
   against FNA stays empty.
4. **No D3D9 Effect framework.** Each `.fx` entry point is compiled individually by CNA (with
   the real `d3dcompiler_47.dll` and an `ID3DInclude` for the `.fxh` files); Microsoft's shipped
   `.fxb` is a verification oracle only, never a source of shipped bytes (owner decision,
   2026-07-14). 61 of 66 variants are byte-identical to Microsoft's; the 5 `PixelLighting`
   variants differ by compiler version only and are oracle-proven instead. The
   `VSArray`/`PSArray`/`ShaderIndex` machinery is replicated in C++.
5. **Shader targets are Microsoft's own `vs_2_0`/`ps_2_0`** for every stock effect; upgrading to
   SM3 would change codegen and precision. SM3 appears only as the `HiDef` capability floor and in
   custom `ShaderEffect`s.
6. **Constant registers come from Microsoft's `_vs(c#)`/`_ps(c#)` annotations**, transcribed into
   one `static_assert`-checked table; where a CNA layout disagrees, Microsoft's wins.
7. **XNA's shader-permutation model is replicated exactly**: each effect's `ShaderIndex`
   computation is ported from FNA's `*.cs`, not derived from CNA's own variant selection.
8. **`GpuDrawParams` is that computation's input.** Gaps it exposes are reported and fixed
   across renderers rather than worked around here; `preferPerPixelLighting` was the first
   (graphics Phase 80, 2026-07-16), and `oneLight` is derived losslessly from the light fields.
9. **No D3DX, ever.** `d3dx9_43.dll` is a deprecated redistributable absent from stock Windows, so
   constants are read by CNA's own CTAB parser rather than `ID3DXConstantTable`.
10. **SpriteBatch's half-pixel offset is applied on the CPU in `MatrixTransform`**, where XNA
    applied it (`SpriteEffect.fx` has no offset in the shader), at one site, with a pixel test
    that fails without it. It is never absorbed into a widened tolerance.
11. **Render state, not state objects**: blend/depth-stencil/rasterizer/sampler state become
    `SetRenderState`/`SetSamplerState` sequences; there is nothing to cache.
12. **D3D9 has its own format, state and vertex-declaration mapping** (`D3DFORMAT` is not
    `DXGI_FORMAT`); it shared a vendor with the retired D3D11/D3D12 renderers, not a model.
13. **MRT is capped by `D3DCAPS9::NumSimultaneousRTs` (at most 4)**, all targets the same bit
    depth, no independent blending; over-requesting throws a named error instead of degrading.
14. **The native `HWND`** comes from the platform window (`IPlatformWindow::GetNativeHandle()`)
    into `D3DPRESENT_PARAMETERS::hDeviceWindow`; there is no windowing code of its own.
15. **COM lifetime is `Microsoft::WRL::ComPtr<T>`**, with no bare `Release()` call sites.
16. **The renderer links `d3d9` alone.** `d3dcompiler` is linked only by the offline shader tool
    and the separate custom-`ShaderEffect` library, so the stock pipeline has no runtime
    shader-compiler dependency.
17. **The DXVK gate**: a run that silently fell back to WineD3D fails (exit 3), and that gate was
    seen to fail on a deliberately broken prefix before it was trusted.

## Development environment: Wine + DXVK dev-loop

The cross-built Windows binaries run under Wine + DXVK, plus a SECOND, separate Wine prefix for the
real XNA 4.0 oracle:

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
scripts/run-wine-dxvk9.sh cmake-build-mingw/cna_test_directx9_smoke.exe
```

`CNA_D3D9_WINEPREFIX` overrides the prefix; the D9-5 gate fails loudly (exit 3) if a run silently
fell back to WineD3D instead of DXVK. `ctest --test-dir cmake-build-mingw -L DIRECTX9` runs every
D3D9 test — including `D3D9_XNA_Diff` (below) — through this same wrapper automatically; none of
them need the separate XNA prefix to run.

## The oracle harness (`tools/xna-oracle/`)

- `tools/xna-oracle/scenes/*.scene` — a shared, declarative `key=value` text scene format, parsed
  identically by both sides. Extending it (a new key, a new effect combination) is the normal way
  to grow coverage — see `tools/xna-oracle/README.md` for the full key reference and the corpus
  manifest.
- `tools/xna-oracle/Oracle.cs` — the real-XNA-4.0 side, compiled by the in-prefix `csc.exe`
  (targets .NET Framework 4.0-era C# — no expression-bodied members, no string interpolation, no
  C# 6+ syntax at all).
- `tools/xna-oracle/CnaOracleRender.cpp` — the CNA side, through the real public
  `Game`/`GraphicsDeviceManager`/`GraphicsDevice`/effect/`SpriteBatch`/`Texture2D` API, never the
  raw renderer interface.
- `tools/xna-oracle/reference/*.png` — the real-XNA-4.0 renders (40 active plus 7 auxiliary),
  captured once and checked in. `D3D9_XNA_Manifest` refuses any missing or extra scene/reference
  stem.
  `D3D9_XNA_Diff` (a real CTest, `D9-120`) diffs every scene against these on every run — it needs
  only the D3D9 Wine prefix, never the XNA one, to keep validating against them.
- `scripts/xna-diff.py` — the pixel-diff tool itself (`--tolerance` defaults to `0`; **never**
  widen it to turn a red comparison green without a documented, per-scene reason — that is exactly
  how an authenticity project quietly becomes a parity project).

## Writing a D3D9 test

Two established patterns:

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
only the checks that should be affected) goes red, then revert and reconfirm green. The
`D9-91`/`D9-93`/`D9-103`/`D9-122` closure notes (the DX9 plan, Git history) are real examples of
this discipline catching genuine false-positive traps (a check that stays green even with its own
target feature disabled is worse than no check at all).

## Known limitations (2026-10-31)

- **Not verified on real Windows hardware** (`D9-140`, `needs_human`) — see the DXVK-authenticity
  caveat above. This includes authentic driver caps, exclusive fullscreen and naturally induced
  device loss; the automated reset and fullscreen-fallback checks use Wine+DXVK.
- **The oracle is not an exhaustive resource-format suite** — non-`Color` texture formats,
  render-target mip generation, cube targets and MSAA are covered by focused CNA tests rather than
  all being represented in the real-XNA scene corpus. Render-target storage is `Color` only, cube
  MSAA is unsupported, and native sRGB/BC7 textures are rejected.
- **Depth bias has no discriminating differential result** under the current DXVK setup.
- **`DirectX9_Common`'s lookup-table checks are mostly not individually mutation-verified** — `D9-122`
  closed the highest-value gaps (blend state, device-lost recovery, MRT, render-target bind,
  vertex-buffer upload, cube-texture upload) but explicitly left most of `DirectX9_Common`'s ~30
  homogeneous mapping-table checks and a handful of `DirectX9_Smoke` checks (Clear/Clear* combo
  variants, resize) unverified this pass — documented, not silently assumed covered.

The task-by-task record, `D9-0` through `D9-140` (2026-07/09), was `plans/plan_dx9.md`, deleted on
2026-10-28 (Git history only); source comments cite its rows as "DX9 D9-nn". See
`docs/d3d9-divergence-report.md` for the oracle measurement, and
`docs/graphics-renderer-feature-matrix.md` for a row-by-row comparison against the other
established renderers (with the caveat that D3D9's own goal — XNA-authenticity, not feature parity
— makes some of that matrix's rows less meaningful for this renderer than for the others; the
oracle/divergence report is the more relevant measurement for D3D9 specifically).
