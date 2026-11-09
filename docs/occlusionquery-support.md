# `OcclusionQuery` support and precision

This page is the focused source of truth for
`Microsoft::Xna::Framework::Graphics::OcclusionQuery`. The cross-renderer summary remains
[`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md); current defects belong
only in [KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

## Public XNA contract

The recovered Microsoft XNA 4.0 implementation is authoritative for the public state machine. It
contains managed state above the Direct3D query that FNA's thinner wrapper omits. CNA implements
that state once in the renderer-neutral public object:

- construction rejects `GraphicsProfile::Reach` and a HiDef renderer/device that does not
  advertise `GraphicsCapability::OcclusionQuery`;
- `PixelCount` first observes `IsComplete` and throws `InvalidOperationException` until the current
  result is ready;
- `End` before `Begin`, nested `Begin` on the same object and repeated `End` throw;
- after `Begin`/`End`, the same object cannot begin again until `IsComplete` has been queried,
  directly or through `PixelCount`; XNA records the observation even when it returned `false`;
- a new successful `Begin` makes the previous result unavailable and counts only the new run;
- after `Dispose()` the object follows XNA rather than CNA's general use-after-dispose rule (owner
  decision): no member throws `ObjectDisposedException`. Dispose releases only
  the native query; the state machine above survives. `IsComplete` answers `false` and still
  records the observation, `PixelCount` throws `InvalidOperationException`, and `Begin`/`End` first
  apply their ordinary state checks and otherwise throw `NullReferenceException`, as XNA's call
  through the released `IDirect3DQuery9` does. `isPixelCountPreciseEXT()` answers `false`.

Distinct query objects may overlap. CNA's renderer-neutral interval coordinator partitions the
command stream at logical `Begin`/`End` boundaries because GL and several queued APIs permit only
one native interval at a time. Every active logical query receives the segment result without
replaying draws or changing depth/stencil side effects. Precise segments are summed with XNA's
signed-`DWORD` bit behavior; boolean-only segments are OR-combined. Disposal of one active query
does not end another, completed native slots are retired while longer intervals continue, and
creating unused logical objects does not exhaust native query capacity.

## Current renderer matrix

"Exact" means a raster-sample tally suitable for coverage arithmetic. "Visibility" means only
zero versus non-zero is meaningful. HEADLESS acceptance is a validation facility, not raster
support.

| Implementation family | Availability | Result contract | Evidence and boundary |
|---|---|---|---|
| **EasyGL** (`OPENGL33`, `OPENGLES3`, `WEBGL2`) | HiDef; live GL query | Exact with `GL_SAMPLES_PASSED`; visibility with `GL_ANY_SAMPLES_PASSED` | Visible/occluded pixel tests, precision test, lifecycle and overlap tests. Desktop GL is measured exact; GLES/WebGL cannot provide a coverage count. |
| **WebGPU** | HiDef; real `WGPUQuerySet`, resolve and asynchronous readback | Adapter-dependent: measured exact on Vulkan, visibility-only on Metal; other native/browser backends are unmeasured | Visible/occluded pixel test plus lifecycle and overlap tests. Separate contiguous runs, including runs divided by `Clear`/render-pass boundaries, receive separate slots and are aggregated into the logical result. |
| **SDLGPU** | Unsupported | None | Construction is refused because SDL_gpu 3.5.0 exposes fences but no occlusion-query/query-pool API; limitation and capability tests pin the refusal. |
| **Software** | HiDef | Exact CPU raster-sample count | Exact contract covers clipping, scissor, coverage, sample mask, alpha, depth and stencil; color-write masks do not suppress visibility, MSAA counts samples and deferred `SpriteBatch` draws participate. |
| **DirectX9** | Device-dependent support probe | Exact Direct3D 9 `DWORD` sample count | Real `IDirect3DQuery9(D3DQUERYTYPE_OCCLUSION)` uses `D3DISSUE_BEGIN`/`END` and `GetData(D3DGETDATA_FLUSH)`. Wine+DXVK passes the zero-result control, a full-viewport precise-count discriminator and the shared lifecycle/overlap suite; no separate depth-occluded DirectX9 scene is registered. |
| **FNA3D** | Selected-driver dependent | Native FNA3D query count where available | A real query is probed at device creation. The OpenGL driver is capability/LensFlare verified; FNA3D's SDLGPU driver has no query route. Native D3D11 execution remains unverified here. |
| **Headless** | Accepted deliberately | Fixed positive sentinel (`1`) | Resource lifetime, counters and trace are validated. It has no raster samples and makes no count or pixel-fidelity claim; the sentinel conservatively keeps game-logic visibility branches active. |
| **Stub** | Unsupported | None | Reports no graphics capabilities and creates no query resource. |

The availability column is only a summary. Code that chooses an optional path must ask the
live `GraphicsDevice::SupportsCapability(GraphicsCapability::OcclusionQuery)`. Construction is the
matching enforcement point and will not hand out a public query backed by a null native factory.

## Precision is a runtime fact

XNA's `PixelCount` is a tally of samples that passed the active raster tests. CNA exposes
`OcclusionQuery::isPixelCountPreciseEXT()` so a consumer does not have to infer that guarantee from
the renderer name:

- EasyGL first tries `GL_SAMPLES_PASSED` and otherwise uses
  `GL_ANY_SAMPLES_PASSED`. On the same Mesa 25.0.7 machine, a 64×64 full-viewport probe returned
  `4096` under desktop OpenGL and `1` under OpenGL ES 3.2.
- WebGPU exposes the adapter result. wgpu-native's Vulkan route is measured as a sample count;
  Metal is measured as boolean visibility and reports the extension as imprecise. DX12, GLES and
  browser adapters have not been classified by a discriminating count test.
- Software and supported DirectX9/FNA3D query routes expose counts. HEADLESS's inherited answer is
  not a raster-fidelity statement because its documented result is only a validation sentinel.

For an imprecise query, applications may rely only on `PixelCount == 0` versus `PixelCount != 0`.
Dividing it by an area does not produce coverage. This is visible in the unchanged LensFlare
sample: desktop OpenGL can calculate a coverage fraction, while OPENGLES3 receives a valid positive
visibility result whose numeric magnitude cannot drive the same fade. CNA does not fabricate a
sample count because arbitrary shader discard, depth/stencil and multisample behavior cannot be
reconstructed faithfully from a boolean query.

## Queued rendering and render-pass boundaries

Queued renderers tag each draw with the query run that was active when the draw was submitted.
Polling `IsComplete` must make command progress, matching XNA's Direct3D 9
`GetData(D3DGETDATA_FLUSH)` behavior; a caller does not need to call `Present` between polls.

WebGPU opens a native occlusion query around each contiguous tagged run. A query slot can be used
only once per resolve, so later runs—including the run after a `Clear` opens another render
pass—receive additional slots. Readback aggregates those slots: exact adapters add sample counts,
while a visibility-only adapter promises only the aggregate's zero/non-zero meaning. The lifecycle
regression compares a full-viewport run with two half-viewport runs separated by `Clear`; exact
adapters must match counts and imprecise adapters must remain positive. Every render-pass segment
is recorded, not only the first.

## Executable evidence

The maintained coverage is split by responsibility:

- `OcclusionQueryPixelCountPrecisionTests.cpp` pins Reach rejection, the recovered XNA state
  machine, the after-`Dispose` refusal order and exact-versus-visibility reporting;
- `OcclusionQueryLifecycleTests.cpp` pins polling progress, run reuse, queued-draw disposal and a
  count spanning `Clear`;
- `OcclusionOverlapTests.cpp` and
  `CNA/Internal/Renderers/Common/OcclusionIntervalCoordinatorTests.cpp` pin independent overlapping
  intervals, resource retirement, capacity behavior, boolean combination and 32-bit wrap;
- renderer-specific pixel/contract tests pin EasyGL, WebGPU, Software, SDLGPU, FNA3D and DirectX9
  at the evidence boundaries stated in the table above.

Implementation sources are `modules/graphics/src/Xna/OcclusionQuery.cpp`,
`modules/graphics/src/Internal/OcclusionQueryIntervals.cpp` and each renderer family's
`CreateOcclusionQuery()`/native query class.
