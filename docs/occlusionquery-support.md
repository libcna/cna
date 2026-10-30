# OcclusionQuery: support and limitations

This document covers `Microsoft::Xna::Framework::Graphics::OcclusionQuery` across renderers. The
original Phase 50 audit (Tasks 441-450) used FNA as its only behavioral source. SOFTWARE-199's
adversarial pass checked the recovered Microsoft XNA 4.0 implementation itself and overturned the
old lifecycle conclusion.

## Microsoft XNA state machine (SOFTWARE-199)

The recovered Windows XNA 4.0 implementation at
`xna4-decomp/reference/xna4/decompiled/windows/Microsoft.Xna.Framework.Graphics/.../OcclusionQuery.cs`
contains five managed fields (`_pixelCount`, `_isAvailable`, `_isInBeginEndPair`,
`_hasCalledBegin`, `_hasIsCompleteBeenQueried`) above the Direct3D query. Its observable rules are:

- construction rejects a null device and a profile whose capability table excludes queries;
  Reach excludes them;
- `PixelCount` calls `IsComplete` and throws `InvalidOperationException` when no result is ready;
- `End` before `Begin`, a nested `Begin`, and a repeated `End` throw;
- after `Begin`/`End`, another `Begin` throws until `IsComplete` (directly or through
  `PixelCount`) has been queried; XNA records that observation even if the result returned false;
- a successful `Begin` clears the previous availability state, making the cached count
  inaccessible until a new result completes.

FNA's `OcclusionQuery.cs` omits this managed state machine and forwards directly to FNA3D. Under
the project's authority hierarchy, that omission cannot override recovered XNA behavior. CNA now
implements the XNA state machine once in its renderer-neutral public object and keeps renderer
objects responsible only for native begin/end/completion/count work. A HiDef construction test,
fresh/unavailable result checks, all invalid sequences, reuse ordering and Software/EasyGL pixel
queries cover the contract. Public operations on a disposed CNA query additionally follow the
project-wide resource rule and throw `ObjectDisposedException`.

**`Dispose()` note** (Task 449 / SOFTWARE-199): `OcclusionQuery::Dispose(bool)` releases the native
query before the base resource is marked disposed. Subsequent Begin/End/result operations throw
`ObjectDisposedException`. Destroying a query that's still active (`Begin()` with no matching
`End()`) remains covered by a 50-iteration stress test: the native destructor releases any shared
active slot, no crash occurs, and resource tracking returns to baseline. `SOFTWARE-309` removed an
unconditional final assertion from that test; it now also forces command progress and proves that a
fresh query completes and publishes `PixelCount` after the stress on Software and both EasyGL
profiles.

## Per-renderer support matrix

| Renderer | Attaches to real GPU work? | Sequence validation | Pixel/query correctness | Status |
|---|---|---|---|---|
| **EasyGL** | ✅ Yes — `glBeginQuery`/`glEndQuery`, asking for `GL_SAMPLES_PASSED` and falling back to the boolean `GL_ANY_SAMPLES_PASSED` | Shared XNA validation (SOFTWARE-199) | ✅ Verified both directions (Tasks 445/446) **and count-vs-flag** (SAMPLE-041) | **Desktop GL is XNA-equivalent; GLES3 is truthfully boolean-only and cannot run coverage-ratio consumers faithfully** |
| **WebGPU** | ✅ Yes — draw tagging, a real `WGPUQuerySet`, resolve and asynchronous readback | Shared XNA validation (SOFTWARE-199) | ✅ Exact count verified by `WebGPU_OcclusionQuery`; representative LensFlare visually matches OPENGL33 | **Correct for a contiguous render-pass segment; a query spanning segments records only its first segment** |
| **SDLGPU** | N/A — construction throws the documented SDL_gpu API limitation | N/A | N/A | **Correctly unsupported: SDL_gpu 3.5.0 exposes fences but no occlusion/query-pool commands** |
| **FNA3D** | Driver-dependent — the selected internal driver must create a real FNA3D query | Shared XNA validation (SOFTWARE-199) | ✅ OpenGL driver verified by capability tests and LensFlare; default SDLGPU driver has no query | **Truthfully selected at device creation; not a renderer-wide unconditional capability** |

### EasyGL — correct, with a precision boundary that depends on the profile

`EasyGLOcclusionQueryRenderer` is a thin wrapper over `easygl::Query`'s own
`glBeginQuery`/`glEndQuery(GL_ANY_SAMPLES_PASSED)` calls. SOFTWARE-199 now rejects invalid public
sequences before GL, so unchecked driver errors are no longer the API contract. Two real pixel/
query correctness tests prove the native query genuinely reports the right answer, not just "doesn't
crash": `EasyGL_OcclusionQuery_VisibleQuad` (Task 445, a fully visible quad reports `PixelCount() >
0`) and `EasyGL_OcclusionQuery_OccludedQuad` (Task 446, a quad hidden behind a nearer opaque
occluder — rejected by `DepthStencilState::Default`'s `LessEqual` compare — reports `PixelCount()
<= 0`). Both independently confirmed via sabotage-and-revert. This is the only renderer where
occlusion queries are both wired up AND pixel-verified correct.

#### The count is a count only where the driver has `GL_SAMPLES_PASSED` (SAMPLE-041, 2026-08-27)

XNA's `PixelCount` is a **tally of the fragments that passed**, which is what Direct3D 9 returns.
**OpenGL ES 3.0 and WebGL 2 have no query target that produces one**: their core occlusion target
is the boolean `GL_ANY_SAMPLES_PASSED`, whose result is 0 or 1 whatever the geometry covered.

Both tests above assert `PixelCount() > 0` and `PixelCount() <= 0`, and **a boolean passes both**.
That is why this row said "fully correct" while a game dividing `PixelCount()` by an area — the
lensflare idiom, and the whole subject of SAMPLE-041 — got `1/area` instead of a coverage fraction
and faded its effect to nothing. Measured there: the query rectangle covered **9788** pixels of
the frame and `PixelCount()` answered **1**.

`EasyGLOcclusionQueryRenderer` now resolves its target from the driver on the first query: it
begins `GL_SAMPLES_PASSED` and keeps it if GL accepts the enum, otherwise it falls back to the
boolean for the life of the process. The distinction is askable rather than folklore —
`OcclusionQuery::isPixelCountPreciseEXT()` (CNAEXT), forwarding
`IOcclusionQueryRenderer::PixelCountIsPreciseEXT()`, which defaults to `true` so no other backend
had to change.

FNA is **worse** here rather than equivalent: `FNA3D_Driver_OpenGL.c` uses `GL_SAMPLES_PASSED`
unconditionally and asserts `supports_ARB_occlusion_query`; on its ES3 path a missing extension
only logs *"Occlusion queries unsupported, beware..."*, and off ES it is a fatal device-creation
error. FNA therefore declares occlusion queries unsupported on ES rather than degrading them to a
flag. CNA keeps them working as a flag and upgrades to a real count wherever the driver has the
precise target.

The asymmetry is real and driver-side, not a CNA choice. Listing what each profile exposes on the
**same Mesa 25.0.7** settles it: the ES 3.2 profile offers `GL_EXT_occlusion_query_boolean` and
nothing else, while the desktop profile offers `GL_ARB_occlusion_query` (a count),
`GL_ARB_occlusion_query2` and the EXT boolean. No ES extension anywhere adds a precise count, and
the WebGL registry has none either. Asking the driver for the enum answers the same way:

| Context | `GL_SAMPLES_PASSED` accepted | Fragments reported for a fully covered viewport |
|---|---|---|
| OpenGL 4.5 compatibility (`spikes/occlusion-count-spike/`) | yes | **4096** — the 64x64 viewport's own area |
| OpenGL ES 3.2, which CNA's `OPENGLES3` profile creates | no | 1 |

`modules/graphics/tests/.../OcclusionQueryPixelCountPrecisionTests.cpp` pins the pair: a
full-viewport quad must report a real tally when the query claims precision and at most 1 when it
does not, with back-buffer readbacks proving the quad genuinely covered the frame. Confirmed to
fail when the precision claim is falsified.

The native sample campaign supplies the visual consequence rather than treating the truthful flag
as full XNA parity.  The unchanged `LensFlare` sample divides `PixelCount` by its 100x100 query
area.  OPENGL33 renders its sun, glow and flare chain, while OPENGLES3 receives the valid boolean
`1` and the chain is effectively invisible.  A faithful general emulation is not available in
GLES3: counting arbitrary fragments after shader discard, depth/stencil and multisampling would
require shader instrumentation or replay of arbitrary application draws, neither of which retains
the query's semantics.  CNA therefore records OPENGLES3 LensFlare as an upstream API
limitation/visual mismatch and does not multiply a boolean into a fabricated count.  Evidence is
in the cna-samples result `matrix-results/msr-046-postfix-visual-representative`.

## Summary

| Area | Status |
|---|---|
| XNA API surface + lifecycle/result state machine | ✅ Recovered XNA implementation audited and implemented renderer-neutrally; the prior FNA-only no-validation conclusion is superseded (SOFTWARE-199) |
| `Dispose()`/active-query-destruction safety | ✅ Verified safe on EasyGL via 50-iteration stress test (Task 449) |
| EasyGL pixel/query correctness | ✅ Both directions (visible → positive, occluded → zero) pixel-verified (Tasks 445-446) |
| WebGPU | ✅ Real exact-count query and representative LensFlare visual pass; cross-render-pass-span limitation remains explicit |
| SDLGPU | ⛔ Truthfully unsupported by the current public SDL_gpu API; no fake result |
| FNA3D | ✅/⛔ Selected-driver dependent: OpenGL query and LensFlare pass; default SDLGPU driver is truthfully unsupported |
