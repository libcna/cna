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
| CABI-8 | Resource-loss model | fixcnats P3 | DESIGN COMPLETE, implementation scoped |
| CABI-9 | VideoPlayer frame identity/generation contract | fixcnats P4 | OPEN |
| CABI-10 | Standalone GraphicsDevice feasibility | fixcnats P5 | ANSWERED: outcome A |
| CABI-11 | Reproducible artifacts + provenance manifest | fixcnats P6 | DONE (measured reproducible) |
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
