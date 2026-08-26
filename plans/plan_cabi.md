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
| CABI-4 | Triage the 6 red C-API tests | prerequisite | TRIAGED, handed off |
| CABI-5 | StorageContainer disposing: enumerated edge cases | fixcnacs P3 | DONE |
| CABI-6 | Apply3D multi-listener adjudication | fixcnacs P4 | BLOCKED |
| CABI-7a | SpriteBatch unnamed sort mode | fixcnacs P5 | DONE |
| CABI-7b | SpriteBatch non-finite values | fixcnacs P5 | BLOCKED by a real crash |
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

## CABI-4 — The six red C-API tests (TRIAGED, not fixed here)

83 C-API ctest cases at this HEAD, 77 passing. The six failures predate this branch — they
reproduce identically in `../cnanext` at `6319f30c5` — and they do **not** share one cause.
None of them is a defect in the contracts these work orders target; every one is drift from
another lane, most of it from the sample-porting lane that is actively working in EasyGL.

| Test | Root cause | Owner |
| --- | --- | --- |
| `CApi_TextureVolumeSmoke` | Asserts `supports_texture3d == CNA_FALSE` (line 393) and then a blanket `validate_texture3d_rejection`. EasyGL gained `EasyGLTexture3DRenderer` in `975156d14` (SAMPLE-014), so the device now reports Texture3D supported and the test fails at stage 1. | renderer/sample lane |
| `CApi_EffectSmoke` | Same drift, line 510: `REQUIRE(cna_texture3d_create(...) == CNA_RESULT_NOT_SUPPORTED)`. | renderer/sample lane |
| `CApi_BasicEffectSmoke` | Unrelated: `cna_directional_light_get_diffuse_color` no longer answers `(0,0,0)` at line 61. Not a Texture3D problem. | graphics lane |
| `CApi_Draw3DSmoke` | Exits 2 with no diagnostic on either stream. Needs instrumenting before it can be attributed. | unattributed |
| `CApi_MediaPlayerSmoke` | **Passes in isolation, fails in the full run.** A test-isolation/ordering defect — shared fixture or audio-device state — not a code defect. | test infrastructure |
| `CApi_InstalledConsumer` | Fails in `modules/c-api/cmake/RunInstalledConsumer.cmake:34`; needs the install step, which building the `cna_c_api_*` targets alone does not produce. | build/packaging |

Worth separating, because the file's own comment claims otherwise: `TextureVolumeSmoke.c` says it
"branches on the reported capabilities instead and runs unchanged on any backend", but line 393
*requires* the capability to be absent. It does not branch; it asserts a negative. That is why a
renderer growing a capability turned it red instead of routing it to a support path, and the fix
is to make it do what its comment already promises.

Not fixed on this branch on purpose. Two of the six turn on whether the C API should now expose
Texture3D support at all, which is the renderer lane's call, and `modules/renderers/easygl/` is
where the sample-porting agent is working right now. Repairing them here would collide with
in-flight work and would decide a capability question that does not belong to this milestone.

`CApi_MediaPlayerSmoke` is the one that should not wait: an ordering-dependent failure that
passes alone is the kind that gets re-diagnosed from scratch every time somebody sees it.

## Test environment

All results on this branch are measured on **Xvfb `:101`** (`-screen 0 1920x1080x24 +extension GLX
+render -noreset`), with `CNA_TEST_DISPLAY=:101` in the build cache. Verified equivalent to the
real display: 83 C-API tests, 5 failing, identical set on both.

One trap worth recording, because it cost a wrong conclusion here. Forcing `SDL_VIDEODRIVER=x11`
on the ctest invocation raises the failure count from 5 to **45** — it overrides the per-test
`SDL_VIDEODRIVER=dummy` that several cases set for themselves. Set `DISPLAY` and let each test
choose its own driver.

## CABI-7b — Non-finite sprite values (BLOCKED by a real crash)

The other half of `fixcnacs.md` Phase 5. XNA does not validate sprite floats — FNA's `SpriteBatch`
raises `ArgumentException` only for unresolvable font characters (`SpriteBatch.cs:782`, `:978`),
and `PushSprite` writes the caller's values straight into the vertex path. CNA's C API refuses
them at three sites in `CnaCApiGraphics.cpp` (`submit_many`, the scaled variant, and
`draw_string`), which is a genuine divergence.

**It is not safe to remove those guards today.** Measured, with the guards removed and a begun
batch drawing NaN, `+INF`, `-INF` and `-0.0`:

```
P1 nan            P5 all-drawn        P8 before-batch-destroy
P2 +inf           P6 before-end       P9 after-batch-destroy
P3 -inf           P7 after-end        -> SIGSEGV
P4 -0
```

Every C API call **succeeds** — all four draws, `End`, the font/atlas/batch destroys — and the
process then segfaults later. Deterministic, 3 runs of 3. That is the worst available failure
mode: the ABI reports success and the process dies after the caller has been told everything
worked.

So the `isfinite` guards are not merely a divergence from XNA; they are holding back a real crash
in CNA's own sprite path. Both work orders' own rules point the same way here —
`fixcnats.md` Phase 2 ("never process abort for ordinary invalid public API state") and
`fixcnacs.md` Phase 5 ("preserve valid CNA-specific safety checks") — so the guards stay, and the
divergence is recorded rather than traded for an abort.

**The real defect is upstream of the C boundary** and needs its own ticket: CNA's sprite/flush
path cannot carry non-finite vertex values without crashing. Until that is fixed, no amount of
C-API work can make this row XNA-faithful.

Reproducer, ~3 lines on top of this HEAD:

1. Delete the `std::isfinite` clauses from the three sprite-command guards in
   `modules/c-api/src/CnaCApiGraphics.cpp` (keep the struct/effects checks).
2. Inside a begun batch in `GraphicsDeviceSmoke.c`, `cna_sprite_batch_draw_string` a command with
   `rotation = NAN`.
3. `DISPLAY=:101 ctest -R "^CApi_GraphicsDeviceSmoke$"` → SEGFAULT, after every call has returned
   success.

Note the C++ layer is not the culprit by itself: `SpriteBatch::Begin`/`Draw` hold the values
without dereferencing anything by them. The crash is downstream of the draw calls and outlives
`End` and the resource destroys, which points at the renderer's vertex submission rather than at
the front end.
