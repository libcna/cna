# plan_cabi.md — cross-binding C-ABI correctness milestone

Work orders: `../_bindings/fixcnacs.md` (CNA.NET evidence) and `../_bindings/fixcnats.md`
(cross-binding evidence). Analysis and effort estimate: `../_bindings/fixcna-analysis.md`.

Branch `feature/bindings`, worktree `../cnabindings`, build `cmake-build-debug`
(Debug / OPENGLES3 / `CNA_BUILD_C_API=ON` / ccache).

Both work orders were written against CNA `1bb2145d99ed572dd4eb15009c34e2e5f410fcf0`
(C ABI 0.7.0). This milestone starts at `6319f30c5`, **307 commits later**, at C ABI 0.8.0.
Several of their premises no longer hold; each is re-measured rather than assumed, and a row
that turns out to be already-resolved says so with the measurement that showed it.

Downstream repositories are read-only evidence. They are not modified by this milestone.

## Status

| Task | Subject | Source | Status |
| --- | --- | --- | --- |
| CABI-1 | Baseline: HEAD, ABI version, C-API build, test inventory | both P1 | DONE |
| CABI-2 | Bound-render-target destroy refusal, pinned from C | fixcnats P2 | DONE |
| CABI-3 | Renderer C/C++ identity parity | fixcnacs P1-2, fixcnats P1 | DONE (upstream) |
| CABI-4 | Triage the 6 red C-API tests | prerequisite | OPEN |
| CABI-5 | StorageContainer disposing: enumerated edge cases | fixcnacs P3 | OPEN |
| CABI-6 | Apply3D multi-listener adjudication | fixcnacs P4 | BLOCKED |
| CABI-7 | SpriteBatch unknown sort mode / non-finite values | fixcnacs P5 | OPEN |
| CABI-8 | Resource-loss model: investigate, design or document | fixcnats P3 | OPEN |
| CABI-9 | VideoPlayer frame identity/generation contract | fixcnats P4 | OPEN |
| CABI-10 | Standalone GraphicsDevice feasibility | fixcnats P5 | OPEN |
| CABI-11 | Reproducible qualified artifacts + provenance manifest | fixcnats P6 | OPEN |
| CABI-12 | Emscripten C-ABI ESM/Wasm artifact | fixcnats P7 | OPEN |

## CABI-1 — Baseline (DONE)

- `HEAD_BEFORE` = `6319f30c5e417417e6da23269072b379d8a3a4f5` on `next`.
- `CNA_ABI_VERSION` = 0.8.0 (`modules/c-api/include/CNA/C/abi.h`).
- `cmake --build cmake-build-debug --target cna_c_api` succeeds unmodified.
- C-API ctest inventory: 82 tests, 76 passing, 6 failing before any change here — see CABI-4.

## CABI-2 — Bound render target destroy (DONE)

**Reported:** destroying a bound RenderTarget escapes the error boundary and aborts the process.

**Measured:** it does not. `RenderTarget2D::Dispose` (`RenderTarget2D.cpp:139`) and
`RenderTargetCube::Dispose` (`RenderTargetCube.cpp:86`) each scan
`GraphicsDevice::GetRenderTargets()` for themselves and throw `InvalidOperationException`;
`CallWithExceptionBarrier` maps it to `CNA_RESULT_INVALID_STATE`, which is exactly what
`render_target.h` documents for `cna_render_target_destroy` ("while bound"). Confirmed by
neutralising a candidate C-API guard and watching the refusal hold without it — so the guard was
redundant and was not kept.

**Gap that was real:** coverage. `GraphicsSurfaceSmoke.c:557` asserts the 2D refusal only inside
`renderer_available == CNA_TRUE`, where a silent skip reads exactly like a pass, and the cube
destroy route was not covered at all.

**Change:** `modules/c-api/tests/pure_c/RenderTargetLifetimeSmoke.c` (`CApi_RenderTargetLifetimeSmoke`).
Both routes, both arms reported. ABI classification: **none**.

## CABI-3 — Renderer identity parity (DONE upstream)

The `49 == 50` build blocker both work orders lead with was fixed by `6bff702a1` (CBIND-052A).
`RendererIdentities` carries 50 explicit pairs including `NANOVG`;
`CNA_GRAPHICS_RENDERER_MAXIMUM == CNA_GRAPHICS_RENDERER_NANOVG`; both `static_assert`s at
`CnaCApiCoreExt.cpp:251-254` hold, and the canonical C-API build succeeds. No work remains.

Note for downstream: `CNA_GRAPHICS_RENDERER_MAXIMUM` moved when NANOVG was added. That is the
sentinel `fixcnacs.md` Phase 2 warns about, and it is why 0.8 is not ABI-compatible with 0.7.

## CABI-6 — Apply3D multi-listener (BLOCKED, needs XNA adjudication)

`fixcnacs.md` Phase 4 asks for real multi-listener mixing, calling the current refusal a defect.

CNA's `SoundEffectInstance::Apply3D(const AudioListener*, int, const AudioEmitter&)`
(`SoundEffectInstance.cpp:1082-1094`) forwards a count of one and otherwise throws
`NotSupportedException("Only one listener is supported.")`. That is a line-for-line match of FNA
(`FNA/src/Audio/SoundEffectInstance.cs:266-278`). The C wrapper deliberately routes even an empty
array through the canonical overload so the refusal is FNA's rather than one invented at the C
boundary.

The two available reimplementations disagree:

- **FNA** throws `NotSupportedException` for any count but one.
- **MonoGame** (`MonoGame.Framework/Audio/SoundEffectInstance.cs:139-144`) applies each listener
  in turn: `foreach (var l in listeners) PlatformApply3D(l, emitter);`.

Neither is XNA. `CLAUDE.md` makes FNA the behavioural reference, and CNA currently matches it;
the work order makes XNA IL the authority, and no XNA IL or reference metadata is available in
this workspace (`/rv/data/library/github.com/` holds FNA, MonoGame and the XNAGameStudio archive,
no reference assemblies).

**This cannot be settled here.** Implementing MonoGame's loop would be a guess that diverges from
the stated behavioural reference; implementing nothing leaves the downstream row open. The row
stays `STILL_BLOCKED` pending XNA adjudication, which is the outcome
`fixcnacs.md` Phase 4 explicitly allows ("document the exact backend limitation instead of
pretending support").

Whoever adjudicates: the question is only what XNA 4.0's
`SoundEffectInstance.Apply3D(AudioListener[], AudioEmitter)` does with a count other than one.
Everything needed to implement either answer is already in place, and the ABI shape
(`cna_sound_effect_instance_apply_3d_multi_ext`) is sufficient for both.
