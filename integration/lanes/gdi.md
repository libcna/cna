# GDI integration lane — accepted 2026-08-08

> **Status: ACCEPTED.** GDI is the seventeenth integrated logical lane and the second of three
> Batch 5 members. Signed merge `ba5fa60166bef2214a4c08b64d50570d1120b7b9` has first parent
> `677f4c59e066fc9a7ed79430d0fee5ffd69b531c`, second parent
> `625f4ad59c9d898eb01a50e22dc872447590989a`, and a tree byte-identical to the second parent.
> Batch 5 remains open at 2 of 3; HTML DOM is pending and no Batch 5 checkpoint/tag exists.
>
> Automated evidence closes every supported-path GDI defect found by this adaptation. It is Linux
> x64 MinGW GCC 14 plus Wine 10/Xvfb and native-Linux Software/control evidence, not a physical
> Windows, native-MSVC, visible lifecycle/DPI, performance, or kernel-object-leak result.

## 1. Identity, implementation, and boundary

`CNA_GRAPHICS_BACKEND=GDI` selects a Windows-only, 2D compatibility backend. It privately composes
the reviewed CPU texture, framebuffer, SpriteBatch, and render-target implementation shared with
Software, then presents the resolved image to the native Win32 `HWND` through classic GDI:

```text
GraphicsDevice / SpriteBatch
        -> GdiGraphicsBackend
        -> private GdiSoftware2DCore
        -> top-down tight RGBA8 resolve
        -> scoped HWND HDC
        -> SetDIBitsToDevice (1:1) / StretchDIBits (scaled)
```

There is no GDI+, Direct2D, Direct3D, OpenGL, SDL Renderer, or other fallback renderer. GDI consumes
alpha in the CPU raster/blend stage; the final window transfer is opaque `SRCCOPY` presentation.
The backend owns no persistent `HDC` or `HBITMAP`: acquisition, selection restoration, release, and
deletion are scoped to the presentation transaction.

The backend archive explicitly names eight reviewed shared CPU-2D translation units and three
GDI-owned translation units. It neither globs the Software backend nor imports Software's 3D,
cube/volume, programmable-effect, or general resource surface. GDI remains under release validation:
the manual native-MSVC workflow and physical-Windows lifecycle/DPI/performance/handle gates are still
external.

## 2. Provenance and history

| Field | Result |
|---|---|
| Original branch | `feature/gdi` at `adc9cc2a2e496d162202733b05ab659199a857b8` |
| Recorded fork | `a7a49e3dc135cd3394b04dbc761123584b4e1d45` |
| Original contribution | 34 linear, meaningful, maintainer-authored PGP-signed commits |
| Archive | sole matching signed annotated tag `archive/preintegration/gdi-20260804`, peeling exactly to `adc9cc2a` |
| Adaptation base | accepted Glide merge `677f4c59e066fc9a7ed79430d0fee5ffd69b531c` |
| Adaptation head | `625f4ad59c9d898eb01a50e22dc872447590989a` |
| Adapted history | 43 linear signed commits: 34 chronological replays, then 9 technical follow-ups |
| Merge | signed `--no-ff` merge `ba5fa60166bef2214a4c08b64d50570d1120b7b9` |
| Merge/adaptation tree | both `70c7cf6621a0ff0f70d5160927005b2b28e7e5b9` |
| Signing identity | Robert Vokac, key `FB9CE8E20AADA55F`; merge and adaptation signatures verify good |

All 34 replayed author names/emails, author dates, subjects, and chronological positions match the
original. Range-diff maps all 34 commits 1:1: **18 `=` and 16 `!`**, with no omission or reorder.
The changed patches reflect current registration/interfaces, semantic adaptation, and the explicit
Software-2D architecture. No meaningful historical commit was omitted. The nine follow-ups close
adaptation findings and record final evidence. Original branch and archive refs remain unchanged.

## 3. Replay and conflict classification

Current integrated interfaces and accepted contracts are authoritative; no historical shared file
was accepted wholesale.

| Historical change class | Resolution |
|---|---|
| A — still required | Genuine GDI public identity/registration, selector/factory and Windows platform gate; the GDI backend/presenter; the reusable private CPU Software-2D implementation GDI needs |
| B — independently present | Current post-audit draw-parameter/stream model, capability enumeration, deterministic unsupported-path guards, cache-isolation/authority structures, and other accepted shared contracts retained in their current forms |
| C — superseded | Historical blanket/default shared-interface forms, stale `GraphicsDevice`/platform APIs, and the monolithic or globbed Software/GDI build architecture were not carried wholesale |
| D — semantic adaptation | Mapped lane draws onto current `GpuDrawParams` without restoring removed fields; aligned separate `StencilBuffer` versus aggregate `DepthStencilBuffer`; fitted current pitch/row/layout contracts; retained only the exact legacy empty-declaration persistent-buffer fallback while declared/multistream/instanced authority remains current |
| E — test-only | Pixel/channel/pitch/orientation/blend/public-API/presentation, handle-fault, allocation-planner, and capability-consistency oracles |
| F — docs/build-only | Windows-only CMake registration, explicit source/target lists, bounded manual Windows workflow, and GDI plans/backend documentation |
| G — unrelated, not carried | Obsolete handoff/process prose and stale broad shared rewrites not required by genuine GDI behavior |

## 4. Exact shared integration surface

This was not a backend-local-only lane. Accepted shared changes include:

- backend identity, public name, selection, factory, platform gates, examples, tests, and explicit
  build/archive registration;
- capability hooks and truthful public routing, including independent stencil-plane questions;
- `GraphicsDevice` 2D/configuration integration and applied presentation-state normalization;
- extraction of the private reusable Software-2D framebuffer/resource/state/SpriteBatch surface;
- pitch-aware texture and render-target uploads, checked allocation planners, and corrected shared
  CPU `SourceAlphaSaturation` blending;
- SpriteBatch and RenderTarget2D integration needed by the composed 2D core;
- fixed CPU `ColorMatrixEffect` and the needed `Blend` enum behavior; and
- the narrow REMED-GFX-233 fallback for the pre-existing legacy ordinary single-buffer shape whose
  declaration is intentionally empty. Declared, multistream, and instanced streams retain the
  current immutable-stream authority.

Shared Texture2D cache ownership/isolation remains unchanged. REMED-GFX-223's authority model is
preserved and its principal cache control passed. The open REMED-GFX-224 EasyGL render-target upload
boundary was neither hidden nor widened.

## 5. Capability truth

The only true `GraphicsCapability` answers are the following three:

| Capability | Answer and supported boundary |
|---|---|
| `StencilBuffer` | **true** — independent 8-bit CPU stencil plane for the supported 2D path |
| `WireFrame` | **true** — crisp CPU DDA SpriteBatch edges; not antialiased geometry |
| `MultiSampleAntiAliasing` | **true** — exactly the implemented 4x CPU backbuffer path; render targets remain single-sampled |
| `ThreeD` | false |
| `DepthStencilBuffer` | false — no complete depth+stencil aggregate; standalone stencil does not imply depth |
| `MultipleRenderTargets` | false |
| `AnisotropicFiltering` | false |
| `OcclusionQuery` | false |
| `CustomEffects` | false — fixed `ColorMatrixEffect` does not advertise arbitrary effects |
| `Texture3D` | false |
| `MultiStreamVertexInput` | false |
| `Instancing` | false |
| PBR | unsupported and not a current capability-enum member |

Every unsupported resource/draw path rejects deterministically rather than invoking private
Software functionality or silently degrading.

## 6. Pixel, presentation, and lifetime result

- Presentation input is tight top-down RGBA8. The 32-bit `BI_BITFIELDS` description uses explicit
  RGB masks and negative height, preserving in-memory channel order without a vertical flip.
- Asymmetric channels, corner/orientation probes, odd widths, padded pitch, dirty-band clipping, and
  transactional short-pitch rejection all pass.
- `SetDIBitsToDevice` is used for native 1:1 full/dirty copies; `StretchDIBits` is used for scaling.
- CPU alpha and corrected blend semantics pass; presentation itself remains opaque `SRCCOPY`.
- Repeated normal construction/destruction and injected `GetDC`, `CreateDIBSection`, and
  `SelectObject` failures pass with selected-object restoration, exact operation counters, retained
  damage, and successful retry.
- `GetGuiResources` comparison is conditional on first observing a stable live delta. It skipped
  under Wine, so physical-Windows kernel-object leak absence is not claimed.

## 7. Validation ledger and boundaries

| Instrument | Result |
|---|---|
| Historical feature-branch GDI matrix | **19/19** Wine/Xvfb |
| Current x64 GDI | MinGW GCC 14 Release; 17 focused correctness executables; **19/19** serial through Wine 10/Xvfb |
| i686 scope | allocation planners only: framebuffer **5/5** + texture **7/7**, both genuine PE32 i386 — **12/12** total; no i686 GDI runtime claim |
| Native Software sanitizer harness | 151 selected: **149 pass + 2 intentional skips**, zero CNA ASan/UBSan reports |
| Standalone Software controls | effects **7/7**, Additive **29/29**, scissor **44/44**, render-target readback **102/102**, viewport **19/19**, Texture2D **40/40** |
| LeakSanitizer boundary | supervising ptrace makes `detect_leaks=1` unusable; valid run used `detect_leaks=0` |
| EasyGL principal control | **8/8** runtime pixel/state tests and `CnjCacheIsolationTest` **2/2** |
| DX3 | x64 MinGW capability runtime **1/1** through Wine/Xvfb |
| Sokol | current native GLCORE runtime **3/3** |
| Diligent / Skia / Glide | current-source compile-only probes pass; no runtime result claimed |
| Physical Windows / MSVC | not run; manual native-MSVC, visible lifecycle/DPI, performance, and handle-observation gates remain external |

The GDI benchmark completed, but quota-constrained timings are not performance evidence. Every
compilation in the integration session used an explicit bound of at most two jobs; final current-tree
runs used one job under `CPUQuota=50%`. No helper was unbounded and compilation never exceeded two.

## 8. Findings

| ID | State | Resolution |
|---|---|---|
| `REMED-GFX-229` | RESOLVED | Reject undersized positive Software texture-upload pitch before mutation; preserve tight default and valid padded rows |
| `REMED-GFX-230` | RESOLVED | Consume Software render-target upload pitch row by row and reject a short positive pitch transactionally |
| `REMED-GFX-231` | RESOLVED | Restore `SourceAlphaSaturation` RGB factor to `min(sourceAlpha, 1-destinationAlpha)` with alpha factor one |
| `REMED-GFX-232` | RESOLVED | Make DX3's standalone stencil hook agree with its real depth-only/no-stencil capability |
| `REMED-GFX-233` | RESOLVED | Preserve the legacy empty-declaration single-buffer stride fallback without weakening declared/multistream/instanced authority |
| `REMED-BUILD-017` | RESOLVED | Native workflow/docs now build all 17 correctness executables before the 19-case matrix |
| `REMED-BUILD-018` | RESOLVED | Capability test includes the complete `IGraphicsBackend` type |
| `GDI-054` lifetime hardening | RESOLVED for automated scope | Repeated cleanup/fault paths and conditional handle-count oracle pass; physical-Windows handle proof remains external |

`REMED-GFX-233` was a pre-existing integration-base defect newly exercised and closed by this lane;
it was not introduced by the GDI replay. No unresolved supported-path GDI finding remains.

Carried state: `REMED-GFX-223` remains preserved/resolved; `REMED-GFX-224` remains MEDIUM/OPEN;
`REMED-GFX-225` through `-228` remain resolved; `REMED-CORE-015` and `REMED-CONTENT-010` remain
LOW/OPEN. GDI touches Content-adjacent tests but no production path-resolution code, so the existing
`REMED-CONTENT-007/-008` checkpoint policy is unchanged.

## 9. Batch and safety result

The logical inventory is now **17/21 integrated, 4 pending**. Batch 5 membership remains exactly
`glide` → `gdi` → `html-dom`: Glide and GDI are accepted, HTML DOM is pending, and Batch 5 is
open at **2/3**. No Batch 5 checkpoint or tag was created. Nothing was pushed. `audit/` remains
unchanged.

**Next: Batch 5 / HTML DOM. Do not start it from this record.**
