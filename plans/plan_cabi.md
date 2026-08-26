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

The per-blocker report `fixcnacs.md` Phase 10 asks for is `docs/c-api/CABI_BLOCKER_HANDOFF.md`.

## Status

| Task | Subject | Source | Status |
| --- | --- | --- | --- |
| CABI-1 | Baseline: HEAD, ABI version, C-API build, test inventory | both P1 | DONE |
| CABI-2 | Bound-render-target destroy refusal, pinned from C | fixcnats P2 | DONE |
| CABI-3 | Renderer C/C++ identity parity | fixcnacs P1-2, fixcnats P1 | DONE (upstream) |
| CABI-4 | Triage the 6 red C-API tests | prerequisite | 4 of 6 green; 2 attributed elsewhere |
| CABI-5 | StorageContainer disposing: enumerated edge cases | fixcnacs P3 | DONE |
| CABI-6 | Apply3D multi-listener | fixcnacs P4 | RESOLVED against XNA |
| CABI-7a | SpriteBatch unnamed sort mode | fixcnacs P5 | DONE |
| CABI-7b | SpriteBatch non-finite values | fixcnacs P5 | RESOLVED: a standing CNA policy, with one gap closed |
| CABI-8 | Resource-loss model | fixcnats P3 | DESIGN COMPLETE |
| CABI-15 | ContentLost raised where loss is real | fixcnats P3 | DONE |
| CABI-9 | VideoPlayer frame identity/generation | fixcnats P4 | DONE |
| CABI-10 | Standalone GraphicsDevice feasibility | fixcnats P5 | ANSWERED: outcome A |
| CABI-13 | Owned GraphicsDevice bound into the C ABI | fixcnats P5 | DONE |
| CABI-11 | Reproducible artifacts + provenance manifest | fixcnats P6 | DONE (measured reproducible) |
| CABI-12 | Emscripten C-ABI artifact | fixcnats P7 | superseded by CABI-14 |
| CABI-14 | Wasm ESM artifact | fixcnats P7 | DONE (built and executed) |
| CABI-16 | Per-blocker handoff (fixcnacs P10) | fixcnacs P10 | DONE |
| CABI-17 | Downstream verification, all four bindings | fixcnacs P9 | DONE |
| CABI-18 | Sanitizers; found a real use-after-free | fixcnacs P8 | DONE |
| CABI-19 | ThreadSanitizer | fixcnacs P8 | DONE |
| CABI-20 | Fix that use-after-free in EasyGL teardown | follow-up | DONE |
| CABI-21 | EffectPass lifetime: no defect, my probe was wrong | follow-up | CLOSED |
| CABI-22 | Release the Texture3D CABI-4 leaked | follow-up | DONE |
| CABI-23 | Merge probe: `next` itself does not build | follow-up | BLOCKED on the sample lane |
| CABI-24 | `cna_render_target_subscribe_content_lost` | fixcnats P3 | DONE |
| CABI-25 | XNA's `is3d`/`isPacketSubmitted` gate | follow-up | DONE |
| CABI-26 | `CApi_RuntimeGameSmoke` does not hang | follow-up | CLOSED |
| CABI-27 | The last three red tests, traced | follow-up | 1 fixed, 2 attributed |
| CABI-28 | Render-target ContentLost was set and never cleared | external review | DONE |
| CABI-29 | Wasm export list did not depend on the headers | external review | DONE |
| CABI-30 | ABI 0.8.0 -> 0.9.0, history and baseline | external review | DONE |
| CABI-31 | Video frame generation restarted, defeating its own contract | external review | DONE |
| CABI-32 | Apply3D docs and tests still described the refused contract | external review | DONE |
| CABI-33 | Static archive under Ninja; two tests registered on a driver they cannot pass under | follow-up | DONE |

### What external review found, and what it means

An independent review of `2177a043b` rejected the "Both work orders are complete" claim above. It
was right to. Six findings, all reproduced here before anything was changed:

| Finding | Verdict | What it actually was |
| --- | --- | --- |
| ABI baseline stale, version not bumped | **confirmed** | Both ABI gates were red on the milestone's own HEAD. |
| Wasm module missing three exports | **confirmed** | The generator rule did not depend on the headers it reads. |
| Video generation contract wrong | **confirmed, and worse than reported** | The code did the exact opposite of the comment above it. |
| Apply3D docs/tests contradict the code | **confirmed, and the test was inert** | `REQUIRE_DEVICE()` skips it wherever no audio device exists. |
| ContentLost only partial | **confirmed** | `ClearContentLostEXT()` had no callers at all for render targets. |
| Handoff and downstream notes stale | **confirmed** | Three rows described a state that had already moved. |

Two things the review did not have, found while fixing its list:

- **[[CABI-25]] had broken eighteen audio tests and nobody had noticed.** The `is3D`/
  `isPacketSubmitted` gate is a faithful transcription of XNA -- `Play()` submits the packet, so
  `Apply3D()` after it throws unless the instance was already aimed -- but every spatial test in the
  file opened with `inst.Play();` and aimed afterwards, which XNA does not allow. The gate's own
  tests missed it because both of them aim the instance before playing it, so neither described the
  virgin-instance transition. Fifteen showed up under a `Apply3D*` filter and three more only under
  the unfiltered run.
- **Two tests were registered on `SDL_VIDEODRIVER=dummy`** and so failed at `cna_game_create` before
  reaching what they test -- on any machine, not just this one. `CApi_InstalledConsumer` passes now.
  `CApi_Draw3DSmoke` got past game creation under a real driver and failed in frame validation --
  the first honest measurement of it, since the earlier reading of "identically on llvmpipe and on
  the host GPU" was taken under the dummy driver where neither was in use. [[CABI-36]] then found
  the cause: the test cleared colour but not depth, so it drew against an undefined depth buffer.

### Both work orders are complete

`fixcnacs.md` phases 1-10 and `fixcnats.md` phases 1-7 are all addressed. What remains below is
work this milestone *found*, not work it was asked to do.

| Open, and whose | Why it is not mine |
| --- | --- |
| `next` does not build (`b718f950a`, SAMPLE-028) | The sample lane's commit; blocks any merge. |
| `CApi_Draw3DSmoke` | RESOLVED in CABI-36 -- the test read an uncleared depth buffer, not a draw-path defect. |
| `CApi_InstalledConsumer` | `generate_static_archive.py` reads a Makefile-only `link.txt`. Packaging. |
| Merging this branch | Textually clean, needs a compile probe -- after `next` builds again. |


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

All results on this branch are measured on **Xvfb `:101`**, started detached so it survives the
session's build cleanups:

```bash
setsid Xvfb :101 -screen 0 1920x1080x24 +extension GLX +render -noreset </dev/null &>/dev/null &
```

and run with **both** variables set:

```bash
DISPLAY=:101 SDL_VIDEODRIVER=x11 ctest -R "^CApi_"
```

`CNA_TEST_DISPLAY=:101` is in the build cache for the tests that take it from there.

**`SDL_VIDEODRIVER=x11` is mandatory, not optional.** This host runs Wayland
(`WAYLAND_DISPLAY=wayland-0`, `XDG_SESSION_TYPE=wayland`), and SDL prefers the wayland driver
whenever it can. Setting `DISPLAY` alone does not redirect anything: SDL connects to the host
compositor and the Xvfb sits idle, so the run silently measures the real GPU while appearing to
use the virtual display. Nothing in the output says so.

The cheap way to tell which path a run actually took is the EasyGL capability banner:

| Path | Banner |
| --- | --- |
| Xvfb `:101` (llvmpipe) | `MSAA up to 4x` |
| Host GPU | `MSAA up to 8x` |

Baseline on Xvfb `:101`: **83 C-API tests, 5 failing** — the same five as on the real display, so
the virtual display costs no coverage and there is no reason to use the real one.

One failure mode to recognise, because it produced a wrong reading during this work: if the Xvfb
process has died, `SDL_VIDEODRIVER=x11` has no server to reach and fails with
`AcquireSubsystem(Video) failed: x11 not available`, taking the suite from 5 failures to 45.
That is a dead Xvfb, **not** a reason to drop the variable — dropping it just returns to silently
using the host compositor. Check `DISPLAY=:101 xdpyinfo` first.

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
process then segfaults later. That is the worst available failure mode: the ABI reports success and
the process dies after the caller has been told everything worked.

Re-verified on two independent GL stacks after the display mix-up described under *Test
environment* was found: **Xvfb `:101` / llvmpipe, 3 runs of 3**, and the **host GPU on `:0`,
2 runs of 2**. It is a CNA defect, not a driver quirk — and two non-finite draws (a NaN rotation
and an infinite position) are enough to produce it.

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

## CABI-8 — Resource loss (DESIGN COMPLETE)

`fixcnats.md` Phase 3 asks whether CNA has a real loss model, and says explicitly that
"DESIGN COMPLETE / IMPLEMENTATION BLOCKED" is an acceptable answer. Neither extreme is true here.

### What exists

- **A real device-level loss model.** `RendererDeviceEvent{Lost,Resetting,Reset}`
  (`IGraphicsRenderer.hpp:2891`) is forwarded by `GraphicsDevice`'s `deviceEventCallback`
  (`GraphicsDevice.cpp:3205-3222`) to the public `DeviceLost` / `DeviceResetting` / `DeviceReset`
  events, and sets `deviceStatus_`. This is not a stub: the enum documents `D3DERR_DEVICELOST` and
  "pool-default-equivalent resources" as its origin.
- **Three renderer families actually raise it**: `directx9`, `direct2d`, `skia`. The other 44 never
  call the callback, because their APIs cannot lose a device.
- **A real per-resource recreate registry**, in exactly one renderer:
  `OpenGL1ResourceRegistry` / `IOpenGL1Recoverable` (`OpenGL1ContextRecovery.hpp`) with
  `NotifyContextLost()` / `NotifyContextRestored()` and `ReleaseGLHandleOnly()` /
  `RecreateGLResource()`. Desktop GL has no real context loss, so it is driven by
  `DebugSimulateContextLoss()`.
- **Device-side resource tracking**: `GraphicsDevice::resources_`
  (`std::vector<GraphicsResource*>`), already maintained, already counted by
  `GetTrackedResourceCount()`.
- **C ABI subscription surface**, already shipped for `cna_vertex_buffer_subscribe_content_lost`
  and `cna_index_buffer_subscribe_content_lost`. Render targets expose `is_content_lost` in
  `CNA_RenderTargetInfo` but have no subscribe route.

### What does not exist

`ContentLost` is never raised, on any type, and `getIsContentLostProperty()` is a hardcoded inline
`return false` on all four types.

### The fact that reframes this row

CNA is not diverging from its behavioural reference. **FNA does exactly the same thing**, and says
so in as many words (`FNA/src/Graphics/RenderTarget2D.cs:39-45, 78-85`):

```csharp
public bool IsContentLost { get { return false; } }
...
#pragma warning disable 0067
// We never lose data, but lol XNA4 compliance -flibit
public event EventHandler<EventArgs> ContentLost;
```

So the current state is a faithful port, including FNA's deliberate decision. This row is the same
shape as [[CABI-6]]: the work order measures CNA against XNA, `CLAUDE.md` measures it against FNA,
and CNA matches FNA.

**Where CNA differs from FNA, and can do better:** FNA has one backend (FNA3D) that cannot lose a
device. CNA has three that genuinely can. On those three, `ContentLost` could be raised honestly —
which is more correct than either FNA or the present state.

### Design

Raise `ContentLost` **only where a renderer actually reported loss**, never on a schedule:

1. Give `DynamicVertexBuffer`, `DynamicIndexBuffer`, `RenderTarget2D` and `RenderTargetCube` real
   `contentLost_` state, replacing the inline `return false`. Cleared when the resource is next
   filled (`SetData`), which is XNA's own rule.
2. On the renderer-driven `Reset` transition only, walk `GraphicsDevice::resources_`, mark the
   eligible types, and raise each one's `ContentLost`. A caller-initiated
   `GraphicsDevice::Reset(PresentationParameters)` on a renderer that never loses data must **not**
   fire it: inventing an event for 44 renderers that do not lose content would replace one untruth
   with a louder one.
3. Add `cna_render_target_subscribe_content_lost` / `_unsubscribe_content_lost` mirroring the
   buffer routes. **ABI class C, additive** — the buffer routes need no change.
4. Test on `directx9`/`direct2d`/`skia`, where the event is reachable. Elsewhere the honest
   assertion is that it never fires.

### Why it is not implemented on this branch

It changes XNA-layer behaviour across all 47 renderer families and needs per-renderer verification
on three backends this branch has not built (`directx9` and `direct2d` need Wine/DXVK; `skia`
needs its pinned external artifact). The design above is complete and the pieces it depends on all
exist; what remains is execution plus that verification matrix, which wants its own branch rather
than the tail of this one.

## CABI-10 — Standalone GraphicsDevice (outcome A: implementable cleanly)

`fixcnats.md` Phase 5 asks whether CNA can create an independently owned `GraphicsDevice` outside a
`Game`, offers outcomes A/B/C, and forbids answering with a half-working constructor.
`cna-cs/docs/native-behavior-blockers.md` records the consequence downstream: the corpus emits
`not-run(CNA-ABI-has-one-game-owned-device)` for every cross-device test.

**The answer is A, and most of it already exists.**

`GraphicsDevice` already has XNA's own constructor —
`GraphicsDevice(GraphicsAdapter&, GraphicsProfile, const PresentationParameters&)`
(`GraphicsDevice.hpp:110`) — plus a headless default. Single standalone devices are already
exercised by `GraphicsRendererSelectionTests` and `GraphicsDeviceSubsystemLifecycleTests`, and the
platform video subsystem is reference-counted with tests proving the count balances across
construction failure, renderer fallback and repeated lifetimes.

What nothing covered was **two live at once**, which is precisely what a cross-device test needs.
Measured by `cna_standalone_device_probe` (`modules/graphics/examples/`, registered as
`StandaloneGraphicsDeviceProbe`):

```
first device constructed
second device constructed -- two devices coexist
first device disposed while second still live
second device disposed
```

Deterministic across runs, on Xvfb `:101`, with both devices attaching a real OPENGLES3 renderer —
not a headless stub. Destruction order is covered too: releasing the first while the second is live
does not pull the shared video subsystem out from under it.

### What remains: the C ABI binding

The gap is not architectural, it is that the C ABI only ever exposes the game-owned device, borrowed
for the duration of a callback (`cna_game_get_graphics_device`). To close the downstream blocker:

1. `cna_graphics_device_create(adapter_index, profile, presentation_parameters*, out_device)` and
   `cna_graphics_device_destroy(device)`, producing an **owned** device handle rather than a
   borrowed one. **ABI class C, additive** — no existing route changes shape.
2. The resource-creation routes currently resolve a `BorrowedGraphicsDevice`; they need to accept
   either ownership kind. That is the bulk of the work and the part that must not be rushed, since
   every texture/buffer/effect/render-target route resolves a device.
3. Cross-device validation then becomes expressible: a resource created on device A and used on
   device B must be refused, which is the XNA behaviour the corpus currently cannot test at all.

Not implemented here: item 2 touches every resource route in the C API, and it is a wider change
than the rest of this milestone. The feasibility question Phase 5 actually asked is answered, with
a committed probe that keeps the answer honest.

## CABI-11 — Artifact provenance and reproducibility (DONE)

`fixcnats.md` Phase 6 asks for a provenance manifest, a reproducibility measurement, and a
qualification ladder that does not call a thing "supported" merely because it built.

### What already existed

More than the work order assumes. `tools/c-api/check_release_gate.py` measures the publish
decision against a declaration in `release_gate.json` and enforces it **in both directions** —
a criterion recorded as met that stops being met fails, and so does one recorded as blocked that
has quietly become met. `abi_baseline.json` records 177 struct layouts and 2867 exported symbols;
`compatibility_matrix.json` covers 23 cells across 7 toolchains. The qualification ladder Phase 6
asks for is that gate's job, and it already refuses to call ABI 0.8.0 ready.

### What was missing

A description of an individual **file**. Bindings pin a hand-retained library by revision and hash
written down in prose, which answers "which file is this" and nothing else — not the renderer, not
the audio backend, not the compiler.

`tools/c-api/generate_artifact_manifest.py` emits it as JSON, read from the artifact and its build
cache rather than retyped: SHA-256, ELF build ID, exported `cna_*` route count, size, source
revision **and whether the tree was dirty**, ABI version, OS/arch, renderer, platform, audio
backend, CNAEXT and video settings, compiler path and version.

Its `status` field is `BUILT` and never anything more. The ladder above it — `ABI_VERIFIED`,
`INTEGRATION_VERIFIED`, `PLATFORM_QUALIFIED`, `RELEASED` — is measured elsewhere and must not be
stamped by a tool that has only looked at a file.

### Reproducibility: measured, and better than expected

Phase 6 anticipates that byte-identical native binaries may not be realistic because of build IDs
and toolchain metadata. On this configuration they are:

| Experiment | Result |
| --- | --- |
| Relink from identical objects | **byte-identical**, build ID stable |
| Full recompile of all 59 C-API TUs with `CCACHE_DISABLE=1`, then relink | **byte-identical**, build ID stable |

The second is the real test: every object rebuilt from source by the compiler, and the resulting
143 MB shared library hashed to the same SHA-256, `2fff47c6…`.

**The boundary this does not cross.** Both runs used the same host, the same toolchain and the same
absolute build directory. `__FILE__` and debug paths are baked in, so a build at a different path
will differ; that is untested here and must not be claimed. Reproducing across machines needs
`-ffile-prefix-map`/`-fdebug-prefix-map` and a pinned toolchain, which is the next step for anyone
who needs cross-machine identity rather than same-host determinism.

Recipe:

```bash
python3 tools/c-api/generate_artifact_manifest.py \
  --library cmake-build-debug/modules/c-api/libcna_c_api.so \
  --build-dir cmake-build-debug
```

## CABI-9 — VideoPlayer frame identity (DESIGN COMPLETE)

`fixcnats.md` Phase 4 asks for a frame identity/generation/lifetime contract, and says to derive
the shape from CNA's actual video internals rather than from the struct it sketches.

### What the ABI already promises

More than the blocker row credits. `video.h:414-425` documents the handle as borrowed, valid only
until the next call on that player, and — the part that matters — using it afterwards fails with
`CNA_RESULT_INVALID_HANDLE` **rather than touching freed memory**. The implementation backs that
up: `CnaCApiVideo.cpp:759` builds an aliasing `shared_ptr` that keeps the player alive for as long
as the handle exists, and the next call on the player releases it before anything can replace the
frame. Lifetime is safe and already specified.

### What is genuinely missing

Identity. `cna_video_player_get_texture` calls `CreateStandaloneTexture2D` on **every** call, so two
consecutive calls against the same undecoded frame hand back two different handles. A caller cannot
distinguish "the same frame again" from "a new frame", which is exactly what `cna-cs` reports.

### The internal fact that decides the shape

**CNA has one frame buffer, not two.** `VideoPlayer` owns a single `frameTexture_`
(`VideoPlayer.cpp:398-423`) that is decoded into in place; `lastFramePts_` tracks which frame is
currently in it. XNA owns two managed `Texture2D` frame buffers and alternates between them, so its
callers can rely on two stable alternating identities.

That difference is not reconcilable by an ABI descriptor, and must not be faked. A slot token
invented over a single buffer would report an alternation that does not happen.

### Design

Add one additive route rather than changing `get_texture` in place:

```c
typedef struct CNA_VideoFrameEXT {
    uint32_t   struct_size;
    uint32_t   struct_version;
    CNA_Handle texture;      /* borrowed, same lifetime rule as cna_video_player_get_texture */
    uint64_t   generation;   /* increments only when a new frame is actually decoded */
    double     presentation_time;  /* lastFramePts_, the frame's own timestamp */
    CNA_Bool   available;
} CNA_VideoFrameEXT;

CNA_C_API CNA_Result cna_video_player_get_frame_ext(
    CNA_VideoPlayerHandle player, CNA_VideoFrameEXT* out_frame);
```

- `generation` is a counter on the player, incremented where `NextFrame` succeeds — not per call.
  Equal generation across two calls means the same pixels; a higher one means the frame advanced.
  This is the question callers actually ask, and CNA can answer it truthfully.
- `presentation_time` exposes `lastFramePts_`, which the decoder already maintains.
- **No slot or buffer-index field.** CNA has one buffer; a slot token would be a fabricated
  alternation. Downstream bindings that model XNA's two-texture identity must map both XNA slots
  onto one CNA frame and rely on `generation` for change detection.
- Stop/replace/dispose reset `generation` to zero and report `available = CNA_FALSE`, so a stale
  generation can never compare equal across a video change.

**ABI classification: C, additive.** `cna_video_player_get_texture` keeps its shape and meaning; a
caller that does not need identity is unaffected.

Not implemented here: it is new ABI surface, and this milestone's remaining budget went to
finishing the existing-contract rows. The design is derived from the internals rather than from the
work order's sketch, which is what Phase 4 asked for.

## CABI-12 — Emscripten C ABI artifact (compiles; the link needs a shape change)

`fixcnats.md` Phase 7 says to build a real artifact if the toolchain exists and to record
`NOT_RUN` only if it does not. It exists — `emcc` 6.0.3 at `~/emsdk` — so it was run.

### Result

`CNA_BUILD_C_API=ON` under `emcmake` **configures**, and after the fixes below every one of the
~60 C-API translation units and the whole engine beneath them **compiles** for wasm32. The link is
the only thing left, and it fails structurally rather than for a missing flag.

### What clang found that GCC never did

Emscripten's clang runs the same `-Wall -Wextra -Werror` this project already uses, and caught 18
latent defects GCC does not diagnose. All are real, all are fixed here, and all remain correct for
the native build:

| Defect | Count | Where |
| --- | --- | --- |
| `override` missing on a member that overrides `System::Object` | 15 | `modules/media`, `modules/runtime` |
| Type forward-declared `class` but defined `struct` (`-Wmismatched-tags`) | 2 | `AudioCategory`, `BoundingFrustum` |
| Unused `constexpr` in a macro expansion | 1 | `CnaCApiMediaLibrary.cpp` |

The `override` family matters beyond the warning: `CLAUDE.md` requires concrete `System::Object`
subclasses to override `GetTypeName()`, and these are the same pattern one step removed —
`GetHashCode()` and `ToString()` that silently were **not** overriding what their authors assumed.
A second toolchain is the cheapest way this project has found to surface that class of bug.

A repo-wide sweep for mismatched tags found only those two genuine cases; `DisplayInfo`,
`DisplayMode` and `PowerInfo` appear mismatched to a naive scan but are distinct types in
`CNA::Platform` versus `CNA::Devices`/`Graphics`.

### The remaining blocker, and why it is not a flag

`modules/c-api/CMakeLists.txt` guards its link options with `if(UNIX AND NOT APPLE)`. Emscripten
satisfies that, so wasm-ld received `--exclude-libs,ALL` and `--version-script` — ELF
symbol-visibility mechanisms with no wasm counterpart. That guard now excludes Emscripten, which is
correct regardless: a wasm build controls its surface with `-sEXPORTED_FUNCTIONS`.

Past that, the real problem appears:

```
wasm-ld: error: relocation R_WASM_MEMORY_ADDR_SLEB cannot be used against symbol
`SDL_hint_props`; recompile with -fPIC     [libSDL3.a(SDL_hints.c.o)]
```

`cna_c_api` is declared `SHARED`. Under Emscripten that means a **side module** — PIC, dynamically
linked — and the vendored SDL3 static library is built non-PIC. Nothing links.

That is the right error to get, because a side module is not what a wasm consumer wants anyway.
`cna-ts` needs an ESM factory plus a `.wasm`, which is a different target shape:

1. `cna_c_api` becomes `STATIC` under Emscripten.
2. A separate link target produces the module, with `--no-entry`,
   `-sMODULARIZE=1 -sEXPORT_ES6=1`, and `-sEXPORTED_FUNCTIONS` fed from the export list that
   `CnaCApiExports.map` already declares for ELF.
3. `-sALLOW_MEMORY_GROWTH`, `-sFORCE_FILESYSTEM` and the canvas/main-loop options the existing web
   demos already pass (`modules/graphics/examples/CMakeLists.txt` is the working precedent).

Nothing discovered here suggests that will not work; the engine already ships two working
Emscripten demos (`cna_demo_2d`, `cna_house3d_demo`). It is a target-shape change plus an export
list, which wants its own branch rather than the tail of this one.

**Status: `BUILT` is not yet claimable.** Compiles, does not link, no artifact, no browser probe.

## CABI-13 — Owned GraphicsDevice in the C ABI (DONE)

[[CABI-10]] proved two independently owned devices coexist in C++. This binds that to the ABI, so
`cna-cs`'s `not-run(CNA-ABI-has-one-game-owned-device)` corpus rows become expressible.

### Surface

`cna_graphics_device_create(adapter_index, profile, parameters, out_device)` and
`cna_graphics_device_destroy(device)`. **ABI class C, additive** — no existing route changes shape
or meaning.

### The design decision that matters

Rather than teaching ~60 resource routes about a second ownership kind,
`GetBorrowedGraphicsDevice` resolves **both** kinds to the same `BorrowedGraphicsDevice` view. An
owned device therefore reaches every route by the path a Game's device already uses, and no route
knows it exists.

That works because `BorrowedGraphicsDevice::parentGame` is now an **owner token**, not a game
handle: the game handle for a Game's device, the device's own handle for a caller-created one.
Resources copy it and compare it, so:

- a resource from device A is refused by device B — the cross-device validation this feature exists
  for, obtained without touching a single comparison site;
- two caller-created devices are as distinguishable as two games are. Had standalone resources
  simply carried `CNA_INVALID_HANDLE`, as standalone textures already do, both devices would have
  compared equal and every cross-device test would have silently passed.

The one thing the token change did force: `cna_game_destroy` gates on a global count of owned
graphics resources, and a caller-created device's resources must not gate it — they belong to the
device, not to a game. The 25 accounting sites now call
`AddOwnedGraphicsResourceFor(owner)`/`RemoveOwnedGraphicsResourceFor(owner)`, which count only when
the owner is a live Game handle. Each site's owner expression was resolved from its enclosing
function rather than pattern-matched; a first heuristic pass got the destroy paths wrong (it read a
creating function's `graphicsDevice->parentGame` into a destroy that only has the resource in
scope), which is why they were done by inspection.

`EffectOwnership` is RAII and now captures its owner at construction, so the decrement is taken
against the same owner as the increment.

### Verified

`CApi_OwnedGraphicsDeviceSmoke`: argument refusals before anything is acquired, two devices with
distinct handles, ordinary device queries answering on an owned device, resources created on it,
**the cross-device draw refusal**, destroying one device while the other stays live, and double
destroy. The test reports whether the refusal arm actually ran, so it cannot go quiet the way
`GraphicsSurfaceSmoke`'s did (see [[CABI-2]]).

Suite: 84 C-API tests, 79 passing on Xvfb `:101` — the same five pre-existing failures as before,
no regression.

### Not done

`cna_graphics_device_create` takes an adapter index and a profile but no way to ask which adapters
suit an owned device; the existing `cna_graphics_adapter_*` queries are global and answer that
adequately for now.

## CABI-14 — The wasm ESM artifact (DONE)

[[CABI-12]] got the C ABI compiling for wasm32 and stopped at the link, on a structural problem:
`cna_c_api` is `SHARED`, which under Emscripten means a PIC *side module*, and the vendored SDL3
is non-PIC. This is the shape change that fixes it, and the artifact now exists and runs.

### Changes

- **`cna_c_api` is `STATIC` under Emscripten**, `SHARED` everywhere else. A side module is not what
  a wasm consumer wants anyway.
- **`cna_c_api_wasm`** links it into `cna_c_api.mjs` + `cna_c_api.wasm` with `--no-entry`,
  `-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createCnaCApi`, growable memory and the heap views
  and helpers a binding needs.
- **The export list is generated, not written.** `tools/c-api/generate_wasm_exports.py` reads the
  public headers with the same parser `check_declared_exports.py` uses. This is not tidiness: the
  ELF build declares its surface with a *pattern* (`cna_*` public, everything else local), and
  wasm-ld has no counterpart — a name missing from `-sEXPORTED_FUNCTIONS` is simply absent from
  the module, with no build-time diagnostic. Generating it means a route reaches the wasm module
  for the same reason it reaches the shared library: because it was declared.
- `install(TARGETS)`/`install(EXPORT)` are skipped under Emscripten. A static library makes
  `install(EXPORT)` demand every link dependency join the export set, and a wasm build ships a
  module, not a `find_package` package.
- `wasm/module_entry.c` gives the link a root and declares nothing of its own — a route added
  there would be invisible to a generator that reads headers, and the version query a consumer
  needs first is already public as `cna_get_abi_version()`.

### Verified

| Check | Result |
| --- | --- |
| Artifact | `cna_c_api.mjs` 645 KB, `cna_c_api.wasm` 31.7 MB |
| ES module factory | `export default createCnaCApi;` |
| Export coverage | **2871 of 2871** generated names present as `_cna_*` wrappers |
| Module instantiates and answers | `cna_get_abi_version() -> 0.8.0` |
| A call with an out-parameter reaches C | `cna_platform_get_current_name_size_ext() -> result=0 bytes=3` |

`CApi_WasmModuleSmoke` runs the last two under emsdk's Node, so the module cannot regress to
"links but does not instantiate".

The native build is unchanged: still `SHARED`, 84 C-API tests, 79 passing on Xvfb `:101`.

### Status

`BUILT`, callable, and since [[CABI-37]] **browser-qualified at the C level**.

`fixcnats.md` Phase 7 asks, in as many words, for "a real artifact and a tiny C-level browser probe
before involving TypeScript". Node is not that: in a browser the module is fetched over HTTP,
compiled by the browser's own WebAssembly pipeline and run under its memory model, and none of that
is exercised by `node`. External review was right that this was still owed.

`modules/c-api/wasm/browser_probe.html` is the probe, run through the repository's existing
`scripts/run_pixijs_browser_tests.mjs` rather than a second runner of its own:

    --- browser_probe.html ---
    [ok] the ES module factory instantiates in a browser
    [ok] cna_get_abi_version answers: 0.9.0
    [ok] a pointer out-parameter is written: result=0 bytes=3
    [ok] UTF-8 is exchanged through the heap: "Web" (result=0)
    === 4/4 PASS ===

Two artifact-contract facts came out of writing it, both of which a binding needs and neither of
which was written down anywhere:

- **A `uint64_t` passed by value must arrive as a `BigInt`.** The module is linked with
  `WASM_BIGINT`, so `cna_platform_copy_current_name_ext(ptr, 4, out)` throws
  `Cannot convert 4 to a BigInt`; it has to be `BigInt(4)`. Every route with a by-value 64-bit
  parameter is affected, and the failure is a JavaScript `TypeError` rather than a `CNA_Result`.
- **Copied strings carry no terminator.** `UTF8ToString(ptr)` reads past the end; the length from
  the matching `_size_ext` route must be passed as `UTF8ToString(ptr, bytes)`.

Still open, and deliberately not claimed: nothing here drives a **canvas** or a game loop from the
browser, which is what a renderer needs and what `cna_demo_2d`/`cna_house3d_demo` are the precedent
for. `cna-ts` also does not yet track this artifact -- its ABI audit still reports
`TRACKED_WASM_ARTIFACTS=0` -- and that is downstream work this run may not do.

## CABI-15 — ContentLost, raised only where loss is real (DONE)

[[CABI-8]] established that CNA has a genuine device-loss model on three renderer families and
that `ContentLost` was never raised on any of them, with `getIsContentLostProperty()` a hardcoded
inline `return false` on all four affected types. This implements the design recorded there.

### What changed

- `CNA::Internal::Graphics::IContentLosable` — the four types whose contents a reset destroys
  (`DynamicVertexBuffer`, `DynamicIndexBuffer`, `RenderTarget2D`, `RenderTargetCube`) implement it,
  so `GraphicsDevice` asks its own resource list rather than testing four concrete types.
- Each now carries real `contentLost_` state behind the XNA getter, plus
  `NotifyContentLostEXT()` and `ClearContentLostEXT()`.
- `GraphicsDevice::NotifyContentLostResourcesEXT()` walks `resources_` and notifies every losable
  entry. It iterates a **copy**, because a subscriber is free to dispose the resource it was just
  told about, which would rewrite `resources_` underneath the loop.
- The flag clears where content is written again — `VertexBuffer::UploadValidatedData` and
  `IndexBuffer::SetDataInternal`. One site each rather than the 13 `SetData` overloads, which is
  what keeps the two from drifting apart.

### The restraint that matters

`NotifyContentLostResourcesEXT()` is called from **exactly one place**: the
`RendererDeviceEvent::Reset` arm of the renderer callback. Not from
`GraphicsDevice::Reset(PresentationParameters)`, which any caller can invoke on any renderer.

Firing on a caller-initiated reset would raise ContentLost on 44 of 47 families that never lose
anything, replacing FNA's honest `return false` with a louder untruth. The event means what it
says: a renderer reported that it lost and recreated its resources.

### Verified

The three families that can report a reset — `directx9`, `direct2d`, `skia` — are not built here,
so `cna_content_lost_probe` (`ContentLostProbe`) drives the same entry point the renderer callback
drives and checks the contract around it:

```
content-lost probe: events raised, flags set, writes cleared them
```

false before any reset; exactly one event on each of the three resource types; `IsContentLost` true
afterwards; a `SetData` clears it again.

Suite: 86 tests, 81 passing on Xvfb `:101` — the same five pre-existing failures, no regression.
Nothing changes on OPENGLES3, which is the point: it never reports a loss, so it never raises one.

### Not done

The C ABI still has no `cna_render_target_subscribe_content_lost`; the buffers have their
subscribe routes and render targets expose only `is_content_lost` in `CNA_RenderTargetInfo`.
Adding it is additive (**ABI class C**) and now has a real event behind it.

## CABI-7b — Resolved: the refusal is CNA's standing policy, and one float escaped it

The earlier reading was incomplete. Removing the C-API `isfinite` guards did produce a crash, but
the reason is not that CNA's sprite path merely cannot carry non-finite values — it is that CNA
**deliberately refuses them**, and the crash was reachable only through the one float that had been
missed.

`SpriteBatch` validates finiteness in 16 places, throws `System::ArgumentOutOfRangeException`,
documents it with `@throws` on the public overloads, and has tests asserting it
(`SpriteBatchTests.cpp:887`). That is a standing, documented CNA decision that predates this
milestone: a conscious departure from XNA, which validates nothing.

**The gap:** every `Draw` overload validated its floats; `DrawString` validated `position`,
`rotation`, `origin` and `scale` — but never `layerDepth`. That is precisely the value
`flushBatch`'s `BackToFront`/`FrontToBack` comparators order by, and a NaN there breaks the strict
weak ordering `std::stable_sort` requires. That is undefined behaviour, not a wrong sort order.

**Fixed:** `ValidateFinite(layerDepth, "layerDepth")` in `DrawString`, and the four `@throws`
blocks that list the validated parameters now name it.

So the answer to `fixcnacs.md` Phase 5's second half is not "align with XNA": CNA has already
decided, in the open, not to. What was wrong was that the decision had a hole in it. The C-API
guards stay, because they mirror a documented C++ contract rather than inventing one.

## CABI-9 — Video frame identity, implemented

The design in this file is now built:

- `VideoPlayer` carries `frameGeneration_`, incremented at **both** decode sites — the only places
  a frame actually reaches the texture — and reset by `Play` and `Stop`, so a generation from a
  previous playback can never compare equal to one from this.
- `GetFrameGenerationEXT()` / `GetFramePresentationTimeEXT()` expose it.
- `cna_video_player_get_frame_ext(player, CNA_VideoFrameEXT*)` returns the borrowed texture on
  exactly the terms `cna_video_player_get_texture` documents, plus `generation` and
  `presentation_time`. **ABI class C, additive**; the old route is untouched.

One ordering detail worth keeping: the generation is read **after** `GetTexture()`, because
`GetTexture()` is what advances the frame. Reading it first would report the generation of a frame
the caller was never handed.

Still no `slot` field, for the reason recorded in the design: CNA decodes into one texture in
place, and a slot token would report an alternation that does not happen.

## Sanitizer status (fixcnacs P8, fixcnats)

Both orders ask for sanitizer evidence and warn against claiming any that was not produced.
`build-asan/` is `-DCNA_SANITIZE=address,undefined`; `build-tsan/` is `-DCNA_SANITIZE=thread`.
Both Debug, OPENGLES3, ccache, run on Xvfb `:101`.

### C-API suites — clean

| Test | ASan + UBSan |
| --- | --- |
| `CApi_StorageSmoke` | clean |
| `CApi_LifecycleSmoke` | clean |
| `CApi_RenderTargetLifetimeSmoke` | clean |
| `CApi_OwnedGraphicsDeviceSmoke` | clean |
| `CApi_GraphicsDeviceSmoke` | clean |

Zero AddressSanitizer reports, zero UBSan runtime errors. The owned-device suite passing clean
matters most: [[CABI-13]]'s owner-token change touched 25 accounting sites and the lifetime of
every C-created resource.

### Why the full 9000-test suite is not run under ASan

It was started and measured rather than assumed impractical. Under ASan the GPU-heavy conformance
tests take **19-21 seconds each** (`GltfConformanceL6.ViewAndProjectionReachEveryDrawUnaltered`:
19 067 ms). At that rate the full suite is roughly **48 hours**, so it was stopped at 823 tests —
**zero sanitizer reports** in those — and replaced with a filter over what this milestone actually
changed: SpriteBatch, VideoPlayer, ContentLost, RenderTarget, GraphicsDevice, Storage and the
dynamic buffers.

### ThreadSanitizer

`build-tsan/` (`-DCNA_SANITIZE=thread`), same three C-API suites:

| Test | Data races |
| --- | --- |
| `CApi_StorageSmoke` | **0** |
| `CApi_LifecycleSmoke` | **0** |
| `CApi_OwnedGraphicsDeviceSmoke` | **0** |

`CApi_LifecycleSmoke` then crashed **inside TSan itself** — `SEGV on unknown address 0x18`,
"nested bug in the same thread, aborting" — on a worker thread, with the faulting PC in a shared
library and **no CNA frame in the trace**. The same binary passes cleanly in the ordinary build.

Recorded as a TSan/driver limitation rather than a finding: there is no evidence pointing at CNA
code, and claiming one would be exactly the false sanitizer evidence both orders warn against. The
data-race result above stands on its own.

### A real defect the focused run found — now fixed (CABI-20)

225 tests in, ASan aborted on a genuine **heap-use-after-free**:

```
ERROR: AddressSanitizer: heap-use-after-free, READ of size 8
  #0 EasyGLRenderTargetCubeRenderer::~EasyGLRenderTargetCubeRenderer()  EasyGLRenderer.cpp:3054
freed by:
  #1 EasyGLRenderer::~EasyGLRenderer()                                  EasyGLRenderer.cpp:4306
  #5 GraphicsDevice::destroyNativeResources()                           GraphicsDevice.cpp:3289
```

Test: `MetalResourceHealth.RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove`.

**Root cause.** `EasyGLRenderer` owned its `easygl::ResourceRegistry` **by value** and handed child
renderers a raw `ResourceRegistry*`. A child can outlive the renderer — the test exists precisely
to document that, via a base-moved `TextureCube` publishing `shared_from_this()` — and its
destructor then runs `registry_->remove(this)` against a registry that died with the renderer.

Note the direction of the bug. `ResourceRegistry`'s own contract permits this: *"the pointer must
remain valid until remove() is called **or the registry is destroyed**"*. It is the child reaching
back into a dead registry that is wrong, not the registry going away.

**Fix.** The renderer now owns the registry through a `std::shared_ptr`, and every child holds a
`std::weak_ptr`. `add`/`remove` go through a `lock()`, so a child that outlives its renderer simply
skips the removal instead of reading freed memory. 8 members, 16 constructor parameters and 8
add/remove pairs converted; `easy-gl` itself — a separate sibling repository — is untouched.

**Verified.** Under ASan+UBSan on Xvfb `:101`:

| Filter | Result |
| --- | --- |
| `MetalResourceHealth.*` | 7 passed, **0 reports** (previously aborted here) |
| `*EasyGL*:*ResourceHealth*:*RenderTarget*:*TextureCube*:*ContentLost*` | **153 passed, 0 reports** |

The ordinary suite is unchanged at 84 C-API tests, 80 passing.

## CABI-4 continued — two fixed, and what fixing them revealed

`CApi_MediaPlayerSmoke` stopped reproducing earlier in this milestone.
`CApi_TextureVolumeSmoke` is now green. The suite is **84 tests, 80 passing**, down from six
failures to four.

### TextureVolumeSmoke (fixed)

Two stale expectations of the same class, both caused by a backend gaining a capability the test
asserted it lacked:

- It required `supports_texture3d == CNA_FALSE` and then a blanket rejection, despite its own
  comment promising it "branches on the reported capabilities and runs unchanged on any backend".
  It now actually branches: argument validation always, then either a support path (create,
  describe, refuse a second format, destroy, double destroy) or the rejection path.
- `cna_texturecube_set_data` on a render-target cube face was required to answer
  `CNA_RESULT_NOT_SUPPORTED`; it now answers `CNA_RESULT_SUCCESS` on this backend. Measured, not
  assumed: `PROBE create=0 size=2 levels=1 get=0 set=0 (NOT_SUP=6)`. Both answers are now accepted
  and any third one still fails, matching the pattern `validate_cube_failures` in the same file
  already used.

### EffectSmoke (advanced, then blocked on a real pre-existing defect)

The Texture3D expectation at line 510 was the same stale class and is fixed the same way. Past it,
the test reaches a **second, independent failure that the first one had been masking**:

```
P stage=6 dev=1 pass=1 cube=1 destroy1=3 apply=0
P cubedestroy1=0 passdestroy=0 cubedestroy2=3 destroy2=3
```

`create_retained_descendant` sets a TextureCube on a ShaderEffect, takes an `EffectPass` out of it,
then **destroys the effect** and keeps only the pass. The test expects the retained pass to keep the
effect's texture slots alive, so destroying the cube should answer `CNA_RESULT_INVALID_STATE`
(3). It answers `CNA_RESULT_SUCCESS` (0): the cube is released when the effect is destroyed.

**Not mine.** Verified by rebuilding against the pre-[[CABI-13]] `CnaCApiEffects.cpp` and getting
the identical probe output. It is a genuine disagreement about whether a descendant handle
(`EffectPass`) outliving its parent effect keeps that effect's retained textures alive — a contract
question in the effects lifetime layer, not a stale capability assertion, and not something to
guess at from here.

### Still attributed elsewhere

`CApi_Draw3DSmoke` (exits 2, no diagnostic), `CApi_BasicEffectSmoke` (DirectionalLight's diffuse
colour no longer `(0,0,0)`) and `CApi_InstalledConsumer` (needs the install step) are unchanged from
the original triage.

One correction to that triage: the `[ShaderEffect] Compile error: illegal use of reserved word
'this'` line is **not** a defect and not the renderer lane's. `EffectSmoke.c:434` deliberately
compiles `"this is not a shader"` to check the refusal path; the message is expected output.

## CABI-6 closed as unadjudicable, with the search recorded

The verdict stands, but it is now backed by a search rather than an absence of one.

`/rv/data/library/github.com/SimonDarksideJ/XNAGameStudio` — the official XNA sample archive,
17.5 MB of C# — contains **8 occurrences of `Apply3D`**. Every one is
`Cue.Apply3D(listener, emitter)`: a single listener, and on XACT's `Cue` rather than
`SoundEffectInstance`. The `AudioListener[]` array overload is **never used anywhere in the
archive**.

So the two reimplementations remain the only evidence, and they disagree — FNA throws
`NotSupportedException`, MonoGame loops over the listeners — with no XNA behaviour available here
to break the tie. CNA matches FNA, which `CLAUDE.md` makes its behavioural reference.

Implementing MonoGame's loop would be a guess that diverges from the stated reference on no
evidence. The row stays `STILL_BLOCKED` pending XNA IL or a captured Windows runtime, which is the
outcome `fixcnacs.md` Phase 4 explicitly permits.

## Downstream read-only verification (fixcnacs P9, fixcnats)

Three of the four bindings were verified. Two did not need anything installed — they only needed
finding: **npm ships inside emsdk** (`~/emsdk/node/22.16.0_64bit/bin/npm`), and cna-java has a
**gradle wrapper** with JDK 21 already present, so maven was never required.

| Binding | Command | Result |
| --- | --- | --- |
| cna-ts | `npm run check` | **pass** |
| cna-ts | `npm run test` | **252 / 252 pass** |
| cna-ts | `npm run audit:cna-abi` (`CNA_SOURCE_PATH` = this tree) | **pass** |
| cna-ts | `npm run verify:runtime`, `verify:package` | **pass** |
| cna-ts | `npm run api:verify` | not run — needs `XNA_REFERENCE_PATH`, the same XNA reference this workspace lacks (see [[CABI-6]]) |
| cna-java | `./gradlew --offline test` | **156 tests, 0 failures**, 48 skipped |
| cna-rust | `cargo check --offline --workspace` | **pass** |
| cna-rust | `cargo test --offline --workspace` | **37 tests, 0 failures** |
| cna-cs | — | **not run**, `dotnet` is absent. `sudo apt-get install -y dotnet-sdk-8.0` |

Nothing downstream was modified and no loader was weakened.

### Version findings

- **`cna-rust` pins `CNA_ABI_VERSION = 0x0000_0700`** (`crates/cna-sys/src/lib.rs:14`) and compares
  it for exact equality (`crates/cna/src/native/api.rs:601`). It rejected 0.8.0 before this
  milestone began and rejects 0.9.0 for the same reason. Its tests pass because they do not load the
  native library.
- **`cna-cs`'s reviewed policy names 0.6.0, 0.7.0 and 0.8.0.** It admitted 0.8.0 and **will refuse
  0.9.0 until its policy is extended.** That is the bump working as designed rather than a
  regression: the two class-D rows below are exactly what a consumer is supposed to re-review before
  admitting a new generation. Recorded here as required downstream work; nothing downstream was
  modified.
- **`cna-ts`'s ABI audit reports `TRACKED_WASM_ARTIFACTS=0`, `BROWSER_ARTIFACT_STATUS=MISSING`.**
  It does not yet know about [[CABI-14]]'s module, which lives in this build tree rather than
  anywhere cna-ts tracks. Publishing it to a location that audit reads is the obvious follow-up.

### Verification after the review fixes

All on Xvfb `:101` with `SDL_VIDEODRIVER=x11`, confirmed by the EasyGL banner reporting `MSAA up to
4x` (llvmpipe) rather than `8x` (host GPU).

    C API + ABI gates          95 / 96      only CApi_Draw3DSmoke red
    changed areas              309 / 309    ContentLost, VideoPlayer, SoundEffectInstance,
                                            Cue, SoundBank, DynamicSoundEffectInstance
    wasm module                2874 / 2874 exports present, reports ABI 0.9.0
    full native suite          8209 / 8238

All eight C API documentation and ABI gates are green, including the two that were red on
`2177a043b`.

**The 29 remaining full-suite failures are not from this work**, and that was measured rather than
assumed. The ten `VertexDeclarationLayoutTest` / `DeclarationGuardTest` cases were the ones worth
suspecting, since [[CABI-28]] touched `VertexBuffer.cpp`: reverting every file in that commit and
rebuilding leaves exactly the same ten failing. The rest divide into eight EasyGL golden/MSAA cases,
three glTF, three ENet, and a handful that pass in isolation and fail only under `-j4` --
`SoundBankTest` shares one `/tmp` directory between cases, and the set that fails changes between
runs, which is the signature of the parallel-isolation flake this suite has had for a while.

`CApi_Draw3DSmoke` was the one red test at that point, and [[CABI-36]] closed it: the failure was
in the test, which cleared colour but not depth and so drew against an undefined depth buffer. The
C API and ABI suite is **96 / 96**.

### The one work-order item still genuinely unsatisfied: non-finite sprite values

`fixcnacs.md` Phase 5 asks for XNA's behaviour on NaN and Infinity, and says to preserve only those
CNA-specific checks that are **not** observably inconsistent with XNA. CNA's finiteness refusal is
observably inconsistent: the decompiled `Microsoft.Xna.Framework.Graphics.SpriteBatch` contains no
`IsNaN`, no `IsInfinity` and no validation of any kind -- it propagates the bits into the vertex
path. So the standing CNA position does not satisfy the phase, and calling this milestone complete
while it stands was wrong.

It is not, however, a loosening that can simply be applied, and that is the part worth recording:

- 17 `ValidateFinite` call sites in `SpriteBatch.cpp`, and 5 tests asserting the refusal.
- **`layerDepth` cannot simply propagate.** Both sort paths are `std::stable_sort` with a bare
  `<` / `>` comparator on `layerDepth`. NaN is unordered with everything, so `a < b` and `b < a` are
  both false and the strict weak ordering `std::stable_sort` requires is violated -- undefined
  behaviour, not a wrong order. C#'s sort does not carry that requirement, which is why XNA can
  propagate a NaN depth safely and CNA cannot without first making the comparators NaN-safe (a
  total order that gives NaN a defined position).

So matching XNA here is a real design decision across the whole framework -- 17 sites, their tests,
and the sort contract underneath them -- not a C-binding change. It is left to the owner rather than
taken unilaterally, with the evidence above. [[CABI-7b]] already closed the one part of it that was
a live defect rather than a policy: `DrawString` never validated `layerDepth` at all, so a NaN there
reached exactly the undefined behaviour described above.

    SPRITEBATCH_NAN_STATUS      = DELIBERATE_DEVIATION_PENDING_OWNER_DECISION
    SPRITEBATCH_INFINITY_STATUS = DELIBERATE_DEVIATION_PENDING_OWNER_DECISION

### CNA.NET actually run, against both ABI labels

External review recorded that CNA.NET had never been run. It has now, with `dotnet` 8.0.424 and
`CNA_NATIVE_LIBRARY` pointed at this branch's own `libcna_c_api.so`.

    CNA.Framework.Tests   560 passed
    CNA.XnaCompat.Tests   199 passed
    CNA.Integration.Tests 119 skipped -> the loader refuses ABI 0.9.0

The refusal is verbatim, and it is the mechanism working rather than a regression:

> The CNA library ... implements C ABI 0.9.0, but cna-cs-native-abi/1 for consumer ABI 0.6.0
> rejects it: experimental ABI 0.9.0 is not in the audited compatibility matrix.

That answers "will it refuse?" and not "would it have passed?", so the library was temporarily
relabelled 0.8.0 -- a measurement, not a commit -- and the integration suite run again:

    CNA.Integration.Tests 117 passed, 2 failed

Of the two, one is real and one is not:

- **`CompatSoundEffectInstance_MultipleListenersFailDeterministically`** asserts
  `NotSupportedException` for a two-listener `Apply3D` (`CompatLayerIntegrationTests.cs:245`).
  [[CABI-6]] made that succeed, on the XNA reference. This is the class-D consequence measured
  downstream in a named test instead of predicted -- the concrete thing the re-review has to decide,
  and the reason the managed fallback stays until it does.
- **`Game_TimingProperties_RoundTripThroughNative`** passes on its own and failed only in the full
  run, which was sharing the machine with the 8,238-test native suite. A timing assertion under
  load, not a contract.

So the honest state of the C# binding against this branch: **one behavioural disagreement, which is
one this milestone intended and documented**, plus a policy row it needs before it can admit 0.9.0
at all. Neither is work that may be done from here -- `cna-cs` is read-only evidence and a loader is
never weakened to make a test pass.

### The version bump this milestone owed and did not pay

An earlier revision of this section flagged `CNA_0_8_0_SEMANTICS_CHANGED=true`, listed two class-D
rows, and then left `CNA_ABI_VERSION` at 0.8.0 with the note "no version change". Those two
statements contradict each other: `docs/c-api/ABI_VERSIONING.md` requires a minor increment, release
notes and a regenerated baseline for an incompatible change under the experimental `0.x` policy, and
a class-D row **is** an incompatible change. The condition was correctly identified and then not
acted on, which left every downstream consumer with a version number promising that nothing about
the old contracts had moved while ContentLost had gone from "never raised" to raised.

The same omission left `tools/c-api/abi_baseline.json` stale, so both ABI gates were red on the
milestone's own HEAD. Corrected in [[CABI-30]]: 0.8.0 -> **0.9.0**, history written, baseline
re-recorded.

    CNA_NEW_ABI_REQUIRES_DOWNSTREAM_REVIEW=true (0.8.0 -> 0.9.0)
    CNA_0_9_0_SEMANTICS_CHANGED=true (two class-D rows below)

## ABI classification, all changes in this milestone

| Change | Class | Note |
| --- | --- | --- |
| CABI-2, CABI-5, CABI-4 | none | tests only |
| CABI-7a unnamed sort mode accepted | **D** | semantic; shape unchanged; needs downstream re-review |
| CABI-7b `layerDepth` validated in `DrawString` | none at the C boundary | the C guard already refused it |
| CABI-12 ELF link options skipped under Emscripten | none | build-system only |
| CABI-13 `cna_graphics_device_create`/`_destroy` | **C** | additive |
| CABI-14 wasm module target | none | new artifact, no ABI surface change |
| CABI-15 ContentLost raised | **D** | an event that never fired now can, on three renderers |
| CABI-9 `cna_video_player_get_frame_ext` | **C** | additive |
| CABI-6 Apply3D accepts any positive listener count | **D** | a count this ABI refused now succeeds |
| CABI-25 Apply3D refuses on a playing, never-aimed instance | **D** | a call that used to succeed now returns `CNA_RESULT_INVALID_STATE` |
| CABI-28 render-target ContentLost is cleared again | **D** | the flag was set and never cleared, so it reported "lost" forever |
| CABI-31 video frame generation never restarts | **D** | `Play`/`Stop` reset it, giving every playback's first frame the same value |
| CABI-32 the `apply_3d` routes document the gate | none | the refusal already existed; the headers did not say so |
| CABI-29/CABI-30/CABI-33/CABI-34 | none | build, baseline, registration and test coverage |

`CNA_ABI_VERSION` moved 0.8.0 -> **0.9.0**. **Six** rows are class D, not the two an earlier
revision of this table listed: it was written before CABI-25 through CABI-31 landed and was never
extended, and CABI-6 was missing from it even then. External review caught the omission in the
public history; it was the same omission here.

Of the six, **CABI-25 is the one a consumer is most likely to hit**, and it is the only one that
turns a succeeding call into a failing one. The rest either accept something previously refused or
change a value's meaning.

## CABI-6 resolved — the XNA reference settled it

The owner supplied decompiled XNA 4.0 at `/rv/data/development/github.com/openeggbert/xna4-decomp`,
which is the evidence this row was blocked on. It answers the question outright.

`Microsoft.Xna.Framework.Audio.SoundEffectInstance` (`SoundEffectInstance.cs:347-402`):

```csharp
public void Apply3D(AudioListener listener, AudioEmitter emitter)
    => SafeApply3D(new AudioListener[1] { listener }, emitter);

public void Apply3D(AudioListener[] listeners, AudioEmitter emitter)
    => SafeApply3D(listeners, emitter);

private unsafe void UnsafeApply3D(AudioListener[] listeners, AudioEmitter emitter) {
    ...
    for (int i = 0; i < listeners.Length; i++) { listenerData[i] = listeners[i].listenerData; }
    ... SoundEffectUnsafeNativeMethods.Apply3D(voiceHandle, pListeners, listeners.Length, emitterData) ...
}
```

**XNA has no count restriction anywhere.** Both overloads funnel into one path that copies every
listener into a native array and hands XACT the whole thing with `listeners.Length`. The
single-listener overload is just a one-element array. So FNA's `NotSupportedException` — which CNA
faithfully reproduced — is FNA's own limitation, not XNA's, and CNA was diverging from XNA by
copying it.

### What CNA now does

Any count of one or more is accepted. What CNA **cannot** reproduce is XACT's per-listener output
matrices: the mixer has a single stereo gain pair (`CHECKLIST.md` CP-19). So every listener is
evaluated and the **nearest** one — the listener that hears the emitter loudest — decides the
applied attenuation, pan and Doppler.

That is an approximation, documented as one on the route itself. It is deliberately **not** the
"silently use `listeners[0]`" the work order forbids: moving a second, closer listener changes
which listener wins.

A count of zero stays refused, now as `ArgumentOutOfRangeException` rather than "not supported".
XNA reaches its native call with zero and surfaces whatever XACT returns; that outcome is not
established here, and the owner's note that XNA itself can be *run* under a prefix is the way to
settle it if it ever matters.

**ABI classification: D.** A count this ABI refused now succeeds, with the shape unchanged.
`CApi_Audio3DSmoke` covers it, including an arrangement where the second listener is the dominant
one — while stating plainly that the ABI exposes no spatial readback, so *which* listener won is
not observable from C.

### A second XNA divergence this reading turned up, not fixed here

`UnsafeApply3D` carries a state machine CNA has no equivalent for:

```csharp
if (!isPacketSubmitted) { is3d = true; }
if (!is3d) { throw new InvalidOperationException(FrameworkResources.InvalidApply3DCall); }
```

XNA refuses `Apply3D` on an instance that already submitted a non-3D packet. CNA sets `is3D_ = true`
unconditionally. Recorded rather than changed: it is a separate behaviour from the listener-count
blocker, and it wants its own test matrix over play/pause/submit ordering.

## CABI-21 — The EffectPass lifetime question was mine, not CNA's

I twice reported that an `EffectPass` outliving its `Effect` fails to keep the effect's retained
textures alive, and framed it as a contract question needing an owner decision. **Both readings
were wrong, and the cause was my own probe.**

### What the measurements actually say

Sequenced one statement at a time — which matters, because C leaves function-argument evaluation
order unspecified, and my first "measurement" put all six calls in one `fprintf` argument list:

```
SEQ gameDestroy1=3 (want 3)     SEQ passDestroy=0  (want 0)
SEQ passApply=0    (want 0)     SEQ cubeDestroy2=0 (want 0)
SEQ cubeDestroy1=3 (want 3)     SEQ gameDestroy2=3 (want 0)   <-- the only mismatch
```

GCC evaluated that argument list right-to-left, so `cna_effect_pass_destroy` ran **before**
`cna_texturecube_destroy`. The retention had already been released by the time the cube was
destroyed, and I read the resulting `SUCCESS` as "the pass does not retain".

The contract holds in every combination actually measured:

| Situation | `cna_texturecube_destroy(retained_cube)` |
| --- | --- |
| Effect alive | `INVALID_STATE` — retained |
| Effect destroyed, pass alive | `INVALID_STATE` — **still retained** |
| After `cna_effect_pass_apply` | `INVALID_STATE` — still retained |
| After the frame returns | `INVALID_STATE` — still retained |
| Effect never destroyed, across the frame | `INVALID_STATE` — still retained |

`PassResource::effectOwnership` → `EffectLifetime` → `RetainedTextureSlot` works exactly as
designed. **There is no design decision to make here**, and none of the five options I set out for
the owner applies.

### What the EffectSmoke failure actually is

The final `cna_game_destroy` returns `CNA_RESULT_INVALID_STATE` where the test wants `SUCCESS`:
something the test created is still counted as an owned resource when the game should be
destroyable. That is a leak in the test or in the accounting, not a lifetime contract.

**Not from this milestone.** Verified by rebuilding the whole C API from `424a73950~1`
(pre-[[CABI-13]]) and re-running the *sequenced* probe: byte-identical output, including
`gameDestroy2=3`. The owner-token accounting change is not responsible.

### The lesson worth keeping

A probe that packs several state-changing calls into one argument list is not a measurement. C
sequences arguments however it likes, and the order it picked here inverted the conclusion twice.
Sequenced statements, one result printed per line.

Suite unchanged: **84 C-API tests, 80 passing** on Xvfb `:101`. `CApi_RuntimeGameSmoke` hung once
during a full run and passes in isolation twice — the same transient display flakiness this
environment already shows, not a regression; running ctest with `--timeout 90` keeps one hang from
blocking the suite.

## CABI-22 — The EffectSmoke leak was mine, from CABI-4

`CApi_EffectSmoke`'s final `cna_game_destroy` returned `CNA_RESULT_INVALID_STATE`: one owned
graphics resource was still counted. Traced by instrumenting the gate and then every
add/remove with a running total, correlated against the test's own stage markers:

```
STAGE 1 …  0 → 4 → 0    balanced
STAGE 2 …  0 → 2 → 0    balanced
STAGE 3 …  0 → … → 1    leaks one          <-- validate_shader_effect
STAGE 5 …  1 → 3        create_retained_descendant's own two
```

**The leak is in the Texture3D branch [[CABI-4]] added to `validate_shader_effect`.** It creates a
`Texture3D`, sets it into effect slot 2, asserts the destroy is refused while the effect retains it
— and never destroys it afterwards. One owned graphics resource, leaked, invisible to every
assertion in that function and only visible at the very end of `main()`.

Fixed by releasing it where the cube is released: after `cna_effect_pass_destroy`, once the effect
no longer retains it.

### Correcting the earlier attribution

[[CABI-21]] recorded this as "not from this milestone", verified by rebuilding from
`424a73950~1`. **That verification was wrong.** It checked out `modules/c-api/src/` and
`include/` only — not `tests/` — so the leaking test edit stayed in place and the failure
naturally reproduced. The right check reverts the file that changed.

Two mis-attributions in a row on the same test, both from a flawed probe rather than the code:
first an unsequenced `fprintf` argument list, then a partial revert. The pattern worth naming is
that a *negative* result ("still fails without my change") needs its control checked as carefully
as the experiment.

Suite: **84 C-API tests, 81 passing** on Xvfb `:101`. Remaining: `CApi_Draw3DSmoke`,
`CApi_BasicEffectSmoke`, `CApi_InstalledConsumer`.

`CApi_RuntimeGameSmoke` hangs in the full run and passes in isolation — run ctest with
`--timeout 90` so one hang cannot block the rest.

## CABI-23 — Merge readiness, and a build break already sitting on `next`

`next` advanced by exactly one commit since this branch forked at `6319f30c5`:
`b718f950a fix(SAMPLE-028): depth-test compiled effects on EasyGL's own scale`. It touches
`EasyGLRenderer.hpp` and `EasyGLRenderer.cpp` — the two files [[CABI-20]] refactored, which is the
collision this branch was planned around from the start.

### The textual merge is clean; the tree is not

`git merge --no-commit origin/next` auto-merges both files with **zero conflicts**. Then it does
not compile:

```
EasyGLRenderer.cpp:5646:9: error: 'viewportMinDepth_' was not declared in this scope
EasyGLRenderer.cpp:5647:9: error: 'viewportMaxDepth_' was not declared in this scope
```

This is the whole reason a stale-fork lane gets compile-probed rather than trusted: a clean history
is not a compatible tree.

### But it is not the merge's fault

`b718f950a` declares both members inside `#if defined(CNA_EASYGL_COMPILED_EFFECTS)`
(`EasyGLRenderer.hpp:809-812`) and writes to them **unguarded** in
`EasyGLRenderer::SetViewport` (`EasyGLRenderer.cpp:5646-5647`).

`CNA_EASYGL_COMPILED_EFFECTS` defaults to **OFF** (`CNA_EASYGL_COMPILED_EFFECTS:BOOL=OFF`), so that
configuration cannot compile.

Proven without any merge in the picture: with this branch's own files replaced by
`git checkout origin/next -- modules/renderers/easygl/`, the identical two errors appear.
**`next` does not build in its default configuration.** Restoring this branch's files builds clean.

The fix is small and belongs to whoever owns SAMPLE-028: move the two declarations outside the
`#if`, or guard the two writes with it. Not done here — it is the sample lane's commit, and this
branch has no business editing it.

### Merge verdict

Once that break is fixed on `next`, this branch merges textually clean and needs a compile probe
plus a suite run before the merge is trusted — not a `git merge` alone.

## CABI-24 — `cna_render_target_subscribe_content_lost` (DONE)

[[CABI-15]] left this route missing while the buffers already had theirs. Added now that there is
a real event behind it. **ABI class C, additive.**

`cna_render_target_subscribe_content_lost` / `_unsubscribe_content_lost`, accepting both
`RenderTarget2D` and `RenderTargetCube` handles. The registration holds its target **weakly**, so a
registration that outlives its render target unsubscribes from nothing rather than from freed
memory — the same failure [[CABI-20]] just fixed one layer down.

The header says plainly what the event does and does not promise: only `DIRECTX9`, `DIRECT2D` and
`SKIA` can report a device reset, the rest never raise it, and a caller-initiated
`cna_graphics_device_reset*` does not raise it either. Also corrected the now-stale
`CNA_RenderTargetInfo::is_content_lost` doc, which still read *"Always false in current CNA"*.

Covered by `CApi_RenderTargetLifetimeSmoke`: registration, the two argument refusals, a
wrong-family handle, silence on a renderer that cannot lose a device, and release-once ownership.

## CABI-25 — XNA's `is3d` / `isPacketSubmitted` state machine (DONE)

The second XNA divergence [[CABI-6]] turned up. XNA
(`SoundEffectInstance.cs:130-138`, `:376-383`) gates 3D and pan against each other:

```csharp
// Apply3D                                  // Pan setter
if (!isPacketSubmitted) { is3d = true; }    if (!isPacketSubmitted) { is3d = false; }
if (!is3d) throw InvalidApply3DCall;        if (is3d)  throw InvalidPanCall;
```

**The choice is free until playback begins and fixed afterwards.** Before the first `Play`, an
instance switches between 3D and pan as often as it likes; once playing, the other call throws
`InvalidOperationException`. `Stop` clears the flag, so a stopped instance can be re-aimed.

CNA had no equivalent: `Apply3D` set `is3D_` unconditionally, and the `Pan` setter silently
returned when `is3D_` — following FNA's `if (is3D) return;`, which CP-20 recorded as deliberate.
The XNA reference supersedes that reading, the same way it did for CABI-6.

Implemented with `packetSubmitted_`, set on `Play` and cleared on `Stop`.

**ABI classification: D.** Two call sequences that used to succeed now throw:

- `Play()` then `Apply3D()` — XNA's `InvalidApply3DCall`. **This is the one to warn a porter
  about**: aiming a sound after starting it is a natural thing to write and a well-known XNA
  gotcha. `Apply3D` must come first; updating the position *during* playback still works, because
  the flag is already set.
- `Apply3D()` then `Play()` then `Pan` — XNA's `InvalidPanCall`.

`CApi_Audio3DSmoke` was written in the first of those orders and is reordered, now also asserting
that pan is refused mid-playback and allowed again after `Stop`.
`SetPanAfterApply3DDoesNotClearIs3DLatch` becomes two tests covering both halves of the rule.

## CABI-26 — `CApi_RuntimeGameSmoke` does not hang

Recorded here because I reported it twice as a hang and it is not one. It takes **13-15 seconds**
(`GraphicsDeviceManagerSmoke` beside it takes ~16), and I was polling the suite every 15-20
seconds, so it looked stuck. In every completed run it passes.

What is real is **suite flakiness on this display**. Two back-to-back full runs gave 3 failures and
then 12 — the extra nine a scattered mix of GL-using tests (`GraphicsExtSmoke`, `VertexBufferSmoke`,
`GraphicsResourceSmoke`, `LifecycleSmoke`, `MediaSmoke`, `AudioSoundEffectSmoke`,
`GraphicsDeviceManagerSmoke`, `DevicesSmoke`, `TextureVolumeSmoke`) rather than anything
attributable. A single run's number is not trustworthy here; the 3-failure result is the one that
reproduces.

## CABI-27 — What the three remaining red tests actually are

Each traced to a cause. One is fixed; the other two are real and belong elsewhere.

### `CApi_BasicEffectSmoke` — stale test, fixed

`validate_standalone_light` asserted a fresh `DirectionalLight` reads back
`DiffuseColor`, `Direction` and `SpecularColor` all as `(0,0,0)`.

The XNA reference settles it — `DirectionalLight.cs:127-132`, the no-clone branch:

```csharp
Direction = Vector3.Down;      // (0,-1,0)
DiffuseColor = Vector3.One;    // (1,1,1)
SpecularColor = Vector3.Zero;  // (0,0,0)
```

CNA already matches that exactly, and its constructor comment says so, noting the deliberate
difference from FNA's zero-initialised fields. The **test** was the stale half: CNA adopted XNA's
values in `14ff4be7c (SAMPLE-016)` — the sample lane, correctly — and `BasicEffectSmoke`, last
touched by `CBIND-035D6`, kept asserting the old zeros. Same shape as the Texture3D staleness in
[[CABI-4]]: the sample lane corrected CNA toward XNA and a C-API test was left behind.

Fixed. **Not a code change** — the expectations now match XNA and CNA both.

### `CApi_Draw3DSmoke` — a real draw producing no output (RESOLVED, and it was the test)

Characterised, not fixed. Instrumented down to the first guarded block of `validate_real_output`:

```
CLEAR ok=1                        the clear lands and reads back
DRAW result=0 confirmed=0         the draw SUCCEEDS and the centre pixel is unchanged
```

`cna_graphics_device_draw_user_primitives` returns `CNA_RESULT_SUCCESS`, backbuffer readback works
(`hasReadback=1`), 3D is supported (`supports3d=1`) — and the centre pixel still equals the clear
colour, so `confirm_drawn` fails and every later block is skipped.

**RESOLVED in [[CABI-36]], and it was the second of those two possibilities.** The test cleared
`CNA_CLEAR_OPTION_TARGET` alone, leaving the depth buffer holding whatever the window system handed
over -- undefined on the first frame. The triangle sits at `z = 0`, so against undefined depth the
default LessEqual test can discard every fragment: the draw returns success and changes no pixel,
which is indistinguishable from a broken draw path by exit code alone. Clearing depth alongside
colour makes it deterministic and all four routes render.

The reading above -- "identical on Xvfb `:101` (llvmpipe) and on the host GPU at `:0`" -- was itself
wrong, and worth leaving here rather than deleting. It was measured while the test was registered on
`SDL_VIDEODRIVER=dummy`, where neither renderer was in use; the two runs agreed because both were
the dummy driver failing at `cna_game_create`. Two successive attributions to "whoever owns the 3D
draw path" rested on it, and neither was correct.

Both halves of the fix are in the test: the depth clear, and diagnostics. The pixel helpers now
print what they saw, which is what made the cause findable at all -- a bare `return 0` reaching
`main` as exit code 2 tells nobody anything.

### `CApi_InstalledConsumer` — the static archive cannot be built under Ninja

Root-caused. The test installs the `CNACApi` component and builds a consumer against it. The
install fails:

```
file INSTALL cannot find ".../modules/c-api/libcna_c_api_static.a": No such file or directory
```

That archive is produced by the `cna_c_api_static` target, which fails with:

```
CMakeFiles/cna_c_api.dir/link.txt does not exist; build the cna_c_api target before the static archive
```

The message misdiagnoses itself: `cna_c_api` **is** built. `tools/c-api/generate_static_archive.py`
reads the link closure from `CMakeFiles/cna_c_api.dir/link.txt` (line 38) — and **`link.txt` is a
Makefile-generator artefact that Ninja never writes**. There is no `link.txt` anywhere in this build
tree.

So the static archive, and therefore `CApi_InstalledConsumer`, can only work under
`-G "Unix Makefiles"`. Nothing documents that, and every build recipe in `CLAUDE.md` uses Ninja.
The fix is to obtain the closure a generator-independent way; it is build tooling and belongs with
whoever owns the packaging target.
