# Lane card — `glide` · ✅ **INTEGRATED 2026-08-08** · merge `677f4c59` — the sixteenth lane

> **Outcome.** Glide's technical integration gates pass. The original 32-commit history was
> preserved, replayed chronologically with unchanged author/date/subject metadata, adapted to the
> current graphics contracts, and merged signed `--no-ff`. The merge tree equals the validated
> adaptation tree. Production rendering is build-only/runtime-unavailable on this host; no native
> hardware or emulator result is claimed. Batch 5 remains open at 1 of 3 and no checkpoint exists.
>
> **Procedural reconciliation — accepted with recorded parallelism deviation.** The first
> historical-baseline configure reached a repository helper that violated the explicit
> bounded-parallelism rule by invoking `cmake --build … --parallel` without a job count. Retained
> evidence cannot prove that operation stayed at or below eight jobs. This is a process deviation,
> classified **B** below: it built only the original worktree's pinned SDL prerequisites, while
> later monitored `-j4` work independently supplied every final engineering gate. Glide therefore
> remains technically accepted. Batch 5 stays open and no checkpoint exists.

| Field | Value |
|---|---|
| Logical lane | `glide` |
| Original ref / head | `feature/glide` → `2f9b47e1281590e6735b5f76ef1e13dd781d8981` — unchanged |
| Fork/base | `a7a49e3dc135cd3394b04dbc761123584b4e1d45` |
| Unique history / files | 32 commits / 46 files; no merges |
| Archive | annotated, signed `archive/preintegration/glide-20260804` (tag object `8e96bb56`) → original head |
| Integration base | `integration/post-audit-phase1` @ `0a51f8647eb4ddf2fdcd2102756ea79bb49625b7` |
| Adaptation | `adapt/glide` → `e891e105dd2567dd5bd8397996cb8c830c127b18`; 33 signed commits (32 replay + 1 stabilization) |
| Merge | `677f4c59e066fc9a7ed79430d0fee5ffd69b531c`; signed, `--no-ff`, parents `0a51f8647` + `e891e105d` |
| Conflict class | HIGH / Group F — confirmed |
| Conflict count | 3 stops; 10 file-conflict events; 9 unique files |
| Public identity | `CNA::GraphicsBackendType::Glide` / `"GLIDE"` |
| Selector / option / define | `CNA_GRAPHICS_BACKEND=GLIDE` / `CNA_BACKEND_GLIDE` / `CNA_BACKEND_GLIDE` |
| Backend target / factory | `cna_backend_graphics_glide`; normal `CreateGraphicsBackend` factory |
| Platforms | 32-bit Windows/x86 only; x64 and non-Windows configurations reject |

## 1. What Glide means here

This is CNA's own native **3dfx Glide 3.x API submission path**. It opens a context with
`grSstWinOpen`, uploads through the TMU API, submits fixed-function primitives, swaps with
`grBufferSwap`, and reads RGB565 through `grLfbReadRegion`. It is not OpenGL, OpenGlide, EasyGL,
SDL_Renderer, software rendering, or a disguised second CNA backend.

The code dynamically resolves a caller-supplied 32-bit `glide3x.dll` from `CNA_GLIDE3X_DLL` or
normal DLL lookup. Missing/incompatible exports fail startup deterministically. dgVoodoo2 is named
only as a possible external compatibility runtime; CNA did not select or run one here.

## 2. Dependency, acquisition, provenance, and license

| Field | Result |
|---|---|
| Build/link dependency | **None for Glide** — only SDL is linked to obtain the existing Win32 HWND |
| ABI | Hand-declared 32-bit Glide 3.x surface, 39 required exports |
| Runtime acquisition | Caller supplies binary `glide3x.dll`; optional path via `CNA_GLIDE3X_DLL` |
| Runtime project/version/commit | **None selected or pinned by CNA**; dgVoodoo2 is an example, not a build dependency |
| Source-built vs binary | CNA backend source-built; external runtime, if chosen, is caller-managed binary |
| Patches | None; no system or external runtime was patched |
| CNA license | Microsoft Public License (Ms-PL) |
| External runtime license | Not inherited or asserted by CNA; caller must use the selected runtime's exact license/redistribution terms |
| Reproducible part | Toolchain gate + 39-export fake-DLL ABI contract; production rendering needs the separately chosen runtime |

No exact emulator version/commit/license can honestly be recorded because none was acquired. That
is an explicit deployment boundary, not an implicit fallback or an unrecorded local dependency.

## 3. Pre-adaptation baseline

The baseline was attempted at the original `feature/glide` head, whose base is the recorded fork,
after initializing only the exact pinned SDL and googletest submodules the branch requires.

| Instrument | Result | Classification |
|---|---|---|
| Portable historical Glide suite | **65/65**, 9 suites | Native host CPU path |
| x86 fake-DLL ABI contract | exit 0 under Wine; complete 39-export surface | Test-double wrapper contract, not rendering |
| Full i686 CNA configure/build | stopped before backend linkage: i686 ZLIB absent in sibling dependency discovery; direct compiler probe also proves accepted `sharp-runtime` `__int128` use cannot compile for i686 | B — dependency absence; D — target/toolchain limitation |
| First configure before submodule init | missing pinned submodule content; corrected by exact submodule init | E — stale checkout assumption |
| Native Voodoo / emulator smoke | 0 registered/executed; no runtime present | Runtime unavailable / host limitation |

No historical lane defect was inferred merely from the unavailable full executable, and the stale
branch was not required to pass the modern corpus.

## 4. Conflict and shared-interface classification

The replay stopped three times. The first stop conflicted in eight files (`CMakeLists.txt`,
`README.md`, `BackendLibraries.cmake`, `BackendSelection.cmake`, `docs/README.md`,
`GraphicsBackendType.hpp`, `GraphicsCapability.hpp`, `GraphicsBackendTypeTests.cpp`); README
conflicted again later; the historical `Texture2D.cpp` rewrite conflicted once. This is 10
file-conflict events across 9 unique paths.

| Historical change class | Resolution |
|---|---|
| A — required | GLIDE registration, factory/build gate, depth-only default-backbuffer truth, sampler mip hook, backend-local implementation |
| B — independently present | Current capability enumeration and most declaration/draw contracts retained from integration |
| C — superseded | Historical `Texture2D` replacement/update rewrite and its test omitted; current REMED-GFX-223 authority model retained byte-for-byte |
| D — semantic adaptation | Registration union, current stream arrays, separate depth/stencil routing, current test switch shape |
| E — test-only | Fake ABI and portable backend-local tests replayed/adapted; modern stream/capability/pitch tests added |
| F — docs/build-only | CMake selector/target/toolchain, README/docs unioned with current accepted identities |
| G — unrelated, not carried | Historical duplicate D3D9 README token and accidental replacement of the general MinGW toolchain example |

Current integration remained authoritative. No historical copy of a shared interface was accepted
wholesale.

## 5. Exact shared production adaptation

- `IGraphicsBackend::ApplySamplerMipState(slot, MaxMipLevel, lodBias)`: additive default no-op;
  Glide overrides it. Existing backends retain their old behavior.
- `SupportsDepthBuffer()` / `SupportsStencilBuffer()`: additive defaults delegate to the existing
  aggregate `SupportsDepthStencil()`. Glide overrides true/false respectively.
- `GraphicsDevice` forwards sampler mip controls and masks impossible depth and stencil clear bits
  independently.
- `ClearOptions::operator~` supports precise flag removal.

No other shared production behavior changed. A whole-tree name-collision scan found no existing
backend member with any of the three new virtual names. `Texture2D.cpp`, its header, and its tests
are byte-identical to integration head `0a51f8647`; therefore REMED-GFX-223/CnjCacheIsolation did
not require a new run. The principal OPENGLES control exercised the additive defaults and
`GraphicsDevice` routing. Sokol, Diligent, and Skia required no separate rebuild: none has a name
collision or override, no backend-local file changed, and all inherit the exact old default path.

## 6. Registration identity

Enum entries and public-name switch arms both count **35 before / 36 after**. A sorted token diff
contains exactly one addition: `Glide`. Selector, option, compile define, backend target, factory,
public enum, and public name all agree. The non-Windows and non-x86 gates reject at configure time;
a missing runtime rejects at startup. No silent fallback path exists.

## 7. Draw, offset, declaration, and stream contract

| Route / field | GLIDE behavior |
|---|---|
| `DrawPrimitives` | Supported fixed-function subset; uses folded `params.vertexStart` |
| `DrawIndexedPrimitives` | Supported; `startIndex` selects index elements, `baseVertex` added once, validated indices expanded while preserving the resolved declaration |
| Geometry `VertexOffset` | Supported on ordinary routes through the shared fold into `vertexStart`/`baseVertex`; residual stream offset must be zero |
| `DrawInstancedPrimitives` | Unsupported; capability false and deterministic rejection |
| Instance `VertexOffset` / `InstanceFrequency` | Unsupported; any per-instance stream rejected, never ignored |
| Ordinary streams | Exactly one per-vertex stream matching the primary buffer; multi-stream capability false |
| Classic instanced 1+1 | Unsupported because all instancing is unsupported |
| Declaration interpretation | Position0 Vector3 required; optional Color0/Normal0/TextureCoordinate0 in supported formats and arbitrary valid offsets |
| Semantic ownership / duplicates | Unsupported usage/index/format and duplicate semantics reject before submission |

The removed `instanceVb`, `instanceVertexOffset`, `instanceFrequency`, and `vertexBufferOffset`
fields were not restored.

## 8. Capability truth

| Capability | Result |
|---|---|
| `ThreeD` | **true** — supported fixed-function subset, build-covered on this host |
| `DepthStencilBuffer` | **false** — default surface has real 16-bit depth, no stencil |
| `MultiSampleAntiAliasing` | false / unsupported |
| `MultipleRenderTargets` | false / render targets unsupported |
| `AnisotropicFiltering` | false; anisotropic filter values reject |
| `WireFrame` | false; non-solid fill rejects |
| `OcclusionQuery` | false / unsupported |
| `CustomEffects` | false; arbitrary effects reject |
| `Texture3D` | false / unsupported |
| `MultiStreamVertexInput` | false / rejected |
| `Instancing` | false / rejected |
| PBR | Not a current enum member; `GpuDrawParams::pbr` rejects explicitly |

The switch names every current capability. No theoretical feature of a compatibility runtime is
claimed as CNA support.

## 9. Fixed-function and state behavior

- TMU0 supports textured primitives and SpriteBatch; a bounded DualTextureEffect subset uses a real
  TMU1 only when the runtime reports two TMUs. Both textures must be same-sized single-tile triangle
  textures sharing one coordinate channel/address mode; filters and LOD bias remain per-slot.
- `BasicEffect::VertexColorEnabled` is honored. Supported BasicEffect lighting and fog are evaluated
  per vertex on the CPU, then native Glide performs texture, alpha test, depth, blend, cull, and
  rasterization. Per-pixel lighting rejects.
- AlphaTestEffect's discrete comparisons, additive blend equations with representable factors,
  RGB/alpha group write masks, depth test/write/compare, culling, viewport, scissor, point/line/
  triangle topology, filtering, and Clamp/Wrap/Mirror are implemented. Unsupported blend equations,
  per-channel RGB masks, sample masks, depth bias, stencil, render targets, MSAA, and wireframe
  reject rather than degrade silently.
- Presentation is the native front/back-buffer model. Only `NativeBackBuffer` and swap intervals
  0/1 are accepted.

## 10. Texture, pitch, format, and lifetime result

The retained source is RGBA8. Width/height are texels; row length, pitch, retained size, and native
allocation are bytes. Row copies validate positive dimensions/data, negative/short stride,
size arithmetic, pitched source span, and destination size. The width-3, height-2 test uses a
16-byte pitch and distinguishes all 24 data bytes from row padding.

The standard native destination is ARGB4444. Full-chain alpha classification can opt in to RGB565
or ARGB1555; conversion bit positions and extrema have portable tests. Logical mips, address-mode
padding, neighbour gutters, tiling, and conversion storage remain CNA-owned for their complete
lifetime. No unconditional shared backend-update path was introduced, so REMED-GFX-223 authority
and the open REMED-GFX-224 boundary are unchanged.

## 11. Findings

| ID | Severity | State | Result |
|---|---|---|---|
| `REMED-GFX-226` | MEDIUM | RESOLVED | Slot 1 sampler state no longer aliases slot 0; unrepresentable address divergence rejects |
| `REMED-GFX-227` | MEDIUM | RESOLVED | Pending SpriteBatch is flushed/fenced before texture range reuse or context teardown |
| `REMED-GFX-228` | MEDIUM | RESOLVED | TMU0 texture restored/revalidated after TMU1 preparation under memory pressure |

Carried state is unchanged: `REMED-GFX-224` MEDIUM/OPEN, `REMED-GFX-225` RESOLVED,
`REMED-CORE-015` LOW/OPEN, `REMED-CONTENT-010` LOW/OPEN. No finding was created for conflicts,
stale API shape, unsupported features, missing hardware/runtime, or the external i686 blocker.

## 12. Validation and rendering-oracle boundary

| Instrument | Result |
|---|---|
| Portable Glide suite | **78/78**, 12 suites |
| Shared identity / clear contracts | **13/13**, 2 suites |
| x86 ABI | PE32 i386 fake DLL + client; all 39 exports; Wine exit 0 |
| Whole backend | `i686-w64-mingw32-g++ -fsyntax-only`, clean |
| Full adapted CMake probe | GLIDE/x86 selector accepted; later fails at missing i686 ZLIB in sibling dependency path |
| ASan/UBSan | linked `libasan.so.8` + `libubsan.so.1`; **78/78**, leak detection on, zero reports |
| OPENGLES control | five serial Xvfb tests, all pass: textured (255,0,0,255), linear (127,128,0,255), depth-write (0,0,255,255), cull-none (255,0,0,255), viewport/scissor 6/6 |
| Glide clear/frame/triangle/texture/state images | **not executed** — runtime unavailable |

The fake DLL proves ABI loading/calling only. It does not prove pixels, native hardware, or a
compatibility runtime. Consequently the requested Glide rendering oracles (clear, frame lifecycle,
primitive/indexed/vertex-color/blend/depth/cull/filter/address/viewport/scissor/repeated frames/
teardown) remain unavailable, not passed or skipped under a false renderer.

## 13. Sanitizer and controls

Focused ASan/UBSan covers CNA-owned pure Glide parsing, conversion, clipping, lighting, capability,
draw-contract, and allocation helpers. It reports zero OOB, UAF, double release, stale pointer, or
conversion overflow in that executable. The production DLL boundary and main backend object cannot
be sanitizer-executed here because the i686 CNA executable cannot link and no runtime is installed.

OPENGLES is the principal control and is green. OPENGL33 was not separately required because the
shared changes are backend-neutral defaults already exercised through the same EasyGL implementation.
Sokol/Diligent/Skia were not required for the collision-free additive-default path. REMED-GFX-223's
CnjCacheIsolation control was not required because the complete shared Texture2D surface is
byte-identical.

## 14. History and losslessness

- Replay metadata comparison: all 32 author names/emails, author timestamps, and subjects match
  exactly; chronological order is unchanged.
- Range-diff: all 32 commits map 1:1; **28 `=`**, four `!` (commits 1, 2, 7, 15) confined to current
  registration/docs context, additive shared hooks, and omission of the superseded Texture2D
  rewrite. No commit was dropped or reordered.
- Attribution/trailer sweep over the 33 adaptation commits: zero prohibited hits and zero trailers.
- Signatures: all 33 adaptation commits report `%G? = U`; adaptation and merge signatures verify
  Good with key `FB9CE8E20AADA55F`.
- Merge shape: `677f4c59` has parents `0a51f8647` and `e891e105d`; merge tree
  `8ebb8b516c46c76121ba7cce5f483f56831d89e7` equals `adapt/glide^{tree}` exactly.

## 15. Batch and safety result

First-parent merge count is exactly **16**; logical inventory is **16/21 integrated, 5 pending**.
Batch 5 membership remains `glide`, `gdi`, `html-dom`; the latter two remain pending. No Batch 5
checkpoint/tag was created. No seventeenth lane began. Original/archive refs and four stash object
IDs remain unchanged; `audit/` is unchanged; touched worktrees are clean; nothing was pushed.

Builds used persistent Ninja/ccache trees on `/media/robertvokac/claude` (external disk, no longer mounted; build trees now live in-repo under `build-probe/` and the shared ccache at `~/.cache/ccache`). Every controlled build
after the baseline helper ran persistent power-saver, `-j4`, and exact process-group monitoring;
`-j6`/`-j8` were never requested. No process was signaled, and no `pkill`/`killall`/name matching was
used. The one retained deviation is the historical helper's unbounded `--parallel`, which prevents
a claim that eight was never exceeded. The start profile was restored after all heavy work.

## 16. Bounded-parallelism reconciliation

The recorded outer command was:

```text
CCACHE_DIR=$HOME/.cache/ccache cmake -S /rv/data/development/github.com/openeggbert/cnaglide -B build-probe/cmake-build-glide-pre -G Ninja -DCMAKE_TOOLCHAIN_FILE=/rv/data/development/github.com/openeggbert/cnaglide/cmake/toolchains/mingw-w64-i686.cmake -DCNA_GRAPHICS_BACKEND=GLIDE -DCNA_BUILD_TESTS=ON -DCNA_USE_CCACHE=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
```

During configure, `_cna_build_sdl_dep` expanded its helper command once for each missing pinned
dependency:

```text
/usr/bin/cmake --build /rv/data/development/github.com/openeggbert/cnaglide/.sdl-prebuilt-Windows-x86/SDL/build --parallel
/usr/bin/cmake --build /rv/data/development/github.com/openeggbert/cnaglide/.sdl-prebuilt-Windows-x86/SDL_image/build --parallel
/usr/bin/cmake --build /rv/data/development/github.com/openeggbert/cnaglide/.sdl-prebuilt-Windows-x86/SDL_mixer/build --parallel
```

The outer CNA generator was Ninja, but all three nested dependency caches record `Unix Makefiles`
and `/usr/bin/gmake`. The recorded invocation assigned only `CCACHE_DIR`: it did not assign
`CMAKE_BUILD_PARALLEL_LEVEL`, `MAKEFLAGS`, or `NINJAFLAGS`, and no retained environment snapshot or
Make log proves an inherited bound. CMake therefore delegated a bare parallel request to GNU Make;
GNU Make's argument-less `-j` permits an unbounded number of simultaneous jobs. The captured output
has no per-process timestamps or concurrency census, so actual maximum parallelism cannot be
reconstructed and **must not be claimed at or below eight**.

All three dependency builds completed and installed `SDL3.dll`, `SDL3_image.dll`, and
`SDL3_mixer.dll`. The outer historical configure then advanced only to the independently recorded
missing-i686-ZLIB barrier. Thus the unbounded operation supplied historical dependency-build and
configure-reachability evidence only. The historical 65/65 portable suite and fake-DLL ABI check
were themselves compiled and run later by separate direct commands; neither is a result of the
helper build.

Final acceptance is independent: monitored Ninja `-j4` built the adapted portable, shared-contract,
and PE32 fake-DLL targets; the adapted portable suite passed 78/78, the shared contracts passed
13/13, and the 39-call fake-DLL ABI client exited zero under Wine. A separate monitored direct i686
compiler command passed whole-backend syntax. Monitored Ninja `-j4` built the sanitizer target and
its linked ASan/UBSan run passed 78/78 with leak detection and zero reports. Monitored Ninja `-j4`
built the five OPENGLES controls and their serial Xvfb executions passed. The tested adaptation was
committed as signed `e891e105`; signed merge `677f4c59` has that commit as its second parent and
their trees are both `8ebb8b516c46c76121ba7cce5f483f56831d89e7`. No final engineering claim
relies uniquely on the unbounded operation. Classification: **B**.

At the preceding close, the unrelated `develop` worktree had a tracked-only edit to
`third_party/enet/CMakeLists.txt` (`cmake_minimum_required` 2.6 → 3.5). Every Glide worktree retained
the committed blob `d3d4aa8def1b2b98beeeb44662b35983efb6a4e7`, the Glide merge does not change
that path, the accepted build graphs name `cnaintegration-glide` as their source root, and both
adapted CMake probes record `CNA_ENABLE_NET=OFF`. Even the direct i686 syntax command used only the
unrelated root worktree's pinned SDL headers, not ENet. The foreign edit therefore did not alter an
object used for accepted Glide evidence and is unrelated and non-blocking. It is no longer present
in the root worktree at reconciliation time; this reconciliation did not modify that worktree.

**Next: Batch 5 / GDI. Do not begin it from this record.**
