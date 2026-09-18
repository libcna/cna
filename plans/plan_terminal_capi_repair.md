# plan_terminal_capi_repair.md — two repairs the renderer curation left behind

Branch `terminal-capi-repair`, cut from `30389564c` (the renderer-curation tip, tree-identical to
`next` at `113be1520`).

This is a **repair** branch, not a design branch. Both workstreams restore intended behaviour that
already has an architecture; neither invents one. The renderer set stays curated at 25 identities and
is not reopened.

Build trees: `cmake-build-debug` (HEADLESS/SDL3, Debug) for the C API and the corpus,
`build-probe/cfg-TERMINAL-SOFTWARE` for the terminal end-to-end path.

## Status

| Task | Subject | Status |
| --- | --- | --- |
| TCR-1 | Baseline audit: HEAD, tree, siblings, renderer invariants, both reproductions | ✅ |
| TCR-2 | Reconnect a CPU renderer to `IPlatformSurfacePresenter` | ✅ |
| TCR-3 | Terminal regression coverage + re-enable `TerminalSoftwareDemoIntegration` | ✅ |
| TCR-4 | `CnaCApiEffects.cpp`: effect collections return `T*`, not `T&` | ✅ |
| TCR-5 | The other C API build breaks the same configure exposes | ✅ |
| TCR-6 | Canonical ABI measurement against the real built library | ✅ |
| TCR-7 | `RELEASE_GATE.md` regenerates; the limitations generator does not, for a reason | 🔶 |
| TCR-8 | Combined validation: invariants, corpus, sanitizers, stale-reference audit | ✅ |

`TCR-7` is the one row that is not green, and deliberately: its own defect is fixed and the release
gate regenerates, but the generator still cannot complete because of a separate, far larger
pre-existing gap that needs an owner decision rather than a repair. The section below states it with
the numbers.

Two things this branch found and did **not** fix, both proven pre-existing and both recorded rather
than folded in: ~3 000 unmapped public symbols owned by completed `CBIND` tasks (`TCR-7`), and 13 red
C API smoke tests, seven of them a single static-destruction-order crash in shared platform/runtime
teardown (`TCR-6`).

---

## TCR-2 — Reconnecting the producer

Three changes, no new abstraction, and no terminal-specific code in either the renderer or the
device:

1. **`modules/renderers/software/src/SoftwareRendererDescriptor.cpp`** sets
   `needsSurfacePresenter = true`. `needsWindow` stays `false` and `windowKind` stays `None`.
2. **`SoftwareRenderer::Present()`** (`SoftwareRenderer2DState.cpp`), which was `{}`, resolves the
   backbuffer and hands it over as a `SurfaceFrame`. The factory passes
   `args.surfacePresenter` through a new `AttachSurfacePresenter()`; a null presenter — every
   configuration that cannot display — leaves `Present()` the no-op it was.
3. **`GraphicsDevice::createOrAttachWindow()`** creates a window for a CPU family when the platform
   reports `surfacePresentation` **and not** `nativeWindowHandle`.

That third condition is the whole design decision, so it is worth stating plainly. `needsWindow`
alone could not work: setting it would give SOFTWARE a window on every platform, which is a feature
change, not a repair. Asking the *platform* instead reads as "can this host show pixels, and is the
presenter the only way there?" — true for `TERMINAL` on a TTY, where the window is the terminal
viewport and no native handle exists; false for SDL3, X11, Wayland and Win32, which all offer a
native handle, so a CPU renderer stays off-screen there exactly as before; false for HEADLESS, SDL2
and a `TERMINAL` whose stdout is a pipe, which report no presentation at all.

**No conversion and no copy.** `SoftwareFramebuffer::color` is already what `SurfaceFrame`
documents — RGBA8, four bytes per pixel, row-major, top row first, rows tightly packed at
`width * 4`, which is what `strideBytes == 0` means. `ResolveColor()` is called first for the same
reason `ReadBackbuffer()` calls it: with MSAA on, the displayable pixels live in a cache that is
only refreshed on demand. The backbuffer is presented explicitly rather than
`CurrentFramebuffer()`, so a render target left bound at end of frame is never what gets displayed.

One consequence of the acquire path is recorded here because it changes a line that looked
unconditional: `setVideoSubsystemAcquired(true)` is now guarded by `descriptor.needsVideoSubsystem`.
Every renderer that reached it before has that flag true, so their behaviour is byte-for-byte
unchanged; the presenter-only arrival is the first that does not, and it has no video subsystem to
raise.

## TCR-4 / TCR-5 — the three C API build failures, each fixed where it is wrong

| Failure | Root cause | Fix |
|---|---|---|
| 8 errors in `CnaCApiEffects.cpp` | `e55945cd7` changed the four effect collections' `operator[](int)` from `T&` to `T*` (XNA answers null for an index it does not hold) and touched no C API file. The wrapper still took `&` of a pointer prvalue. | Drop the `&` at the seven address-taking sites and dereference at the one reference-passing site. Each site already range-checks, so the pointer is non-null there by construction, which is stated in a comment at the first of them. |
| Every C API TU fails with `CNA_ENABLE_NET=OFF` | `gamer-services` is added only under `CNA_ENABLE_NET`, while `modules/c-api` links `CNA_GamerServices` and includes its headers unconditionally. | Refuse the combination at configure time, naming the option to change. Nothing is substituted and no published route is dropped. |
| Both ABI walls assert `0.27.0` | The literal duplicated a check `generate_abi_baseline.py` already performs from the headers — and, compiling only under `CNA_BUILD_C_API=ON`, went two bumps stale unnoticed. | Assert the *encoding* (`ENCODE(1,2,3) == 0x00010203`), which nothing else checks and which no bump invalidates. The version value keeps its real gate. |

## TCR-6 — the canonical ABI measurement, against a real library at last

`generate_abi_baseline.py --check --library cmake-build-debug/modules/c-api/libcna_c_api.so`, the
normal supported path, run for the first time since the build broke. It reports **one** difference:

```
1 addition(s), which the evolution policy permits:
  add    ABI version field added: runtime
```

That is exactly what `plans/plan_renderer_cleanup.md` predicted it would report, for the reason it
gave: the previous baseline was recorded without a library, so it could not carry the `runtime`
field, and the checker classifies a field present in the measurement but absent from the baseline as
an addition rather than a break.

**Nothing else moved.** No removed symbol, no new symbol, no struct layout change, no constant
change. `--write` then re-recorded it through the official path:

```
wrote tools/c-api/abi_baseline.json: 221 structs, 350 scalar types, 1558 constants,
                                     14 string constants, 141 color constants, 4055 exports
```

and the whole file diff is two lines — `"runtime": 7424` added to the `abi_version` object, where
7424 is `0.29.0` encoded, i.e. the library's `cna_get_abi_version()` agreeing with the header. The
re-run then passes: *"C API ABI baseline is current"*.

So the expected values are not merely met, they are **confirmed by the generator rather than
carried by hand**: 221 structs and 4055 exports measured from the real `.so`, identical to the
values the previous pass recorded without one. The ABI stays **`0.29.0`**; nothing in this branch is
an externally observable public change, so nothing required a bump.

### What building the C API uncovered: 13 red smoke tests, none of them this branch's

With the library buildable, its 104 `CApi*` tests ran for the first time in a long while. 88 pass;
**13 pure-C smoke tests fail** (plus the 3 generator gates above). Every one of them is
**pre-existing, and that is measured rather than argued**: rebuilding the same tree with
`GraphicsDevice.cpp` restored byte-for-byte from `30389564c` and re-running `^CApi` produces the
**identical 16-name failure list**, so nothing in this branch causes any of them.

They fall into two shapes, and both are the *same underlying story as the Effects break* — a
deliberate C++ change landing while the C API was unbuildable, with nothing to notice its C-side
mirror going stale:

- **Six stale expectations.** `CApi_GraphicsDeviceSmoke` is the clearest: it asserts a default
  `CNA_Viewport` of `{0,0,0,0, 0.0f, 1.0f}`, while `a0a3bc8d4` (*fix(SOFTWARE-201): restore XNA
  viewport value semantics*) deliberately changed `Viewport()`'s `MaxDepth_` from `1.0f` to `0.0f`
  because an XNA struct's parameterless default is all-zero. The implementation is XNA-correct and
  the C test is stale. Also `CApi_VertexValueSmoke`, `CApi_Draw3DSmoke`, `CApi_MorphTargetSmoke`,
  `CApi_TextureSmoke`, `CApi_GameSecondaryGraphicsDeviceContext`.
- **Seven crashes at process exit, all one root cause.** `CApi_VertexBufferSmoke`,
  `CApi_IndexBufferSmoke`, `CApi_EffectSmoke`, `CApi_ModelMeshPartSmoke`,
  `CApi_SkinnedModelSmoke`, `CApi_ContentSmoke`, `CApi_DevicesSmoke` segfault in
  `CurrentPlatform.cpp:103`, reached identically in each:

  ```
  HandleRegistry::Slot::~Slot -> CGame::~CGame -> Game::~Game
    -> UninstallPlatform (Game.cpp:222) -> SetCurrentPlatform(platform=0x5550000d4e26)
    -> TransferPins -> replacement->AcquireSubsystem(...)
  ```

  The successor pointer is already garbage on the way in. This is a **static-destruction-order**
  defect: the C API's handle registry is a static, so at process exit it destroys a `CGame` — and
  therefore a `Game`, and therefore the ambient-platform bookkeeping — after the platform and
  runtime statics it reaches into have themselves been destroyed.

Neither shape is a build failure, neither is caused by either workstream, and the crash fix lives in
`modules/platform` and `modules/runtime` — shared code every configuration links, which deserves its
own change and its own full revalidation rather than being folded into a repair branch. Recorded
here, fixed nowhere in this branch.

## TCR-7 — the limitations generator: one defect fixed, a larger one exposed

`generate_limitations.py` raised `Explicit coverage rules matched no symbols:
sprite-batch-begin-explicit-state`. The cause is mechanical: that rule and `spritebatch-begin-partial`
carry **identical** `qualified_name_regex` and `kinds`, so they can only ever be told apart by their
hand-maintained `approved_symbols`, and `SpriteBatch::Begin`'s declarations have since changed —
of the eight overloads that exist today, the first rule still named two, the second named three that
no longer exist, and **six were claimed by neither**. Two rules that can never be distinguished by
pattern are one rule, so they are now one: `spritebatch-begin`, approved against all eight current
overloads. That is justified rather than asserted — `cna_sprite_batch_begin_with_effect` takes all
four state descriptors, the effect handle and the transform as nullable arguments, so every overload
shape reaches an existing route, and the by-value and by-pointer state overloads pass a descriptor
or null either way.

Fixing it exposed a **second, far larger pre-existing failure** that the first had been masking:

```
A planned row names a task that cannot own it.
  CBIND-044 is recorded complete in plans/plan_binding.md but owns 2611 planned row(s)
  CBIND-035 ... 198   CBIND-080 ... 44   CBIND-084 ... 38   CBIND-036 ... 32
  CBIND-093 ... 20    CBIND-037 ... 19   CBIND-034 ... 17   CBIND-104 ... 15
  ... 14 completed tasks, ~3 000 rows in total
```

**Proven pre-existing, not introduced here.** Running the same check at `30389564c` against that
commit's own `coverage_mappings.json`, with only the unused-rule raise suppressed so the next
failure becomes reachable, produces the identical list — and `CBIND-080` owns **50** rows there
against **44** here, the difference being exactly the six `Begin` overloads this pass mapped. Every
other count is identical, so this workstream strictly reduced the problem and created none of it.

One genuine tool defect **was** fixed along the way, and it is the one that made `RELEASE_GATE.md`
unregenerable: `check_release_gate.py` publishes a tool's **first output line** as the criterion's
measurement, and `generate_limitations.py` let its exception escape, so the document's reason for
the limitations criterion being unmet was the literal text `Traceback (most recent call last):`.
`RRC-008` refused to bake that in, correctly. It now fails the way its sibling
`generate_coverage_inventory.py` already did — one clean line naming the actual defect — so the
document can be regenerated without publishing a stack trace. The failure itself is unchanged and
still exits non-zero.

This is not a stale rule and not a tool defect: it is ~3 000 public C++ symbols with no C mapping,
whose rows name tasks `plans/plan_binding.md` records as finished, while that plan has **zero** open
`CBIND` tasks. The generator names the only two repairs — bind the symbols, or open a task that owns
them — and both are owner decisions about project status rather than mechanical repairs, so neither
is taken here.

It keeps `CApiLimitations`, `CApiCoverageMatrix` and `CApiReleaseGate` red, and leaves
`docs/c-api/LIMITATIONS.md` as this branch found it, because its generator cannot complete.
`docs/c-api/RELEASE_GATE.md` **is** regenerated, because the release gate does not need the
limitations document to exist — only to know whether it is current — so the one thing standing in
the way was the traceback, and that is fixed. The regenerated document is current and honest rather
than stale and flattering: `0.29.0` instead of `0.21.0`, the generator-measured 221 struct layouts
and 4055 exports, and **two** criteria unmet rather than one — the limitations row now reads ❌ with
the real reason, where a document last written at `0.21.0` was still showing it ✅. Regeneration is
deterministic (a second `--write` changes nothing) and `check_doc_export_counts.py` agrees with all
six prose counts.

---
## TCR-8 — combined validation

### The full corpus: 36 failures, every one of them pre-existing

`cmake-build-debug` (HEADLESS/SDL3, Debug, **`CNA_BUILD_C_API=ON`, `CNA_ENABLE_NET=ON`**), `-j8`:
**9 926 tests, 36 failed, 631 s**. The tree is deliberately not the configuration
`plans/plan_renderer_cleanup.md` measured 9 146/18 in — that one had both options off, and the C
API cannot be tested without the first — so the honest comparison is not a count but a per-name
attribution. Every one of the 36 was reproduced at the starting commit rather than assumed:

| Group | Count | How it was attributed |
|---|---:|---|
| `CApi*` gates and pure-C smokes | 16 | The whole `^CApi` set was re-run after rebuilding this tree with `GraphicsDevice.cpp` restored byte-for-byte from `30389564c`: the **identical 16-name list**. |
| Audio timing (`CueTest`, `SoundBankTest`, `WaveBankTest`) | 3 | Fails with baseline sources. The documented flaky family that also passes on individual rerun. |
| Content (`CnbTexture*`, `Cnj*`) | 5 | Fails with baseline sources. |
| `XnaPipelineGenuineRuntimeBuiltFamilies`, `CnaXnbModelCorpusSweep`, `CnaInputTests`, `CNAEXT_NoPosixSetenv`, `Headless_Smoke` | 5 | Fails with baseline sources. All five are in the standing set `plans/plan_x11.md` already records. `Headless_Smoke` was checked with particular care, because it asserts that no window and no video subsystem exist and this pass edits the code that decides both — it fails identically without the edit. |
| ENet discovery/backend | 7 | Five fail with baseline sources; the other two are from the same UDP-discovery family and pass under lower parallelism, which is the "parallel ENet" flakiness `plans/plan_x11.md` records. |

So the branch's own contribution to the failure list is **zero**, and that is measured twice — once
for the C API half by reverting the one file that could plausibly reach it, and once for the rest by
running each of them against baseline sources in the same tree.

### The invariants the renderer curation must keep

`scripts/check_renderer_identities.py`: *"25 public renderer identities preserved in the enum, the
cmake selection list and the runtime registry, over 21 implementation families; every documented
count agrees; 26 retired identities stay retired with their C ABI values reserved (next free value
52)"*. `scripts/check_removed_renderer_api.py`: 4/4, including `RRC-010`'s own justification, which
this pass changes the wording of and is careful not to weaken. The five platform boundary gates
(`sdl_inventory`, `sdl_classify`, `renderer_sdl_audit`, `sdl_ratchet`, `hot_path_lint`) all pass, so
nothing terminal-specific or SDL-shaped leaked into the renderer through the new include.

### The TERMINAL tree's own corpus

`build-probe/cfg-TERMINAL-SOFTWARE` (TERMINAL/SOFTWARE/NULL audio, Debug), `-j4`:
**9 311 tests, 9 failed.** Not one is a terminal, presentation or software failure — they are three
parallel-flaky ENet cases, `CnaXnbModelCorpusSweep`, `CNAEXT_NoPosixSetenv`, one content cache case
and the three `CApi*` generator gates, i.e. the same standing set as everywhere else. Focused:
**328/328** for `SoftwarePresentation` + `Terminal*` + `Software*`, including the re-enabled
`TerminalSoftwareDemoIntegration`.

### Sanitizers — ASan + UBSan, actually run

`build-probe/cfg-TERMINAL-SOFTWARE-asan` (`-DCNA_SANITIZE=address,undefined`,
`-DCNA_SANITIZE_OPTIMIZATION=O1`). The tree needed `-Wno-error=maybe-uninitialized`: at `-O1` with
sanitizers, GCC 14 raises that warning inside libstdc++'s own `<regex>`/`std::function` headers from
a **sharp-runtime** translation unit, which this branch must not modify and which has nothing to do
with either repair.

- **50/50** `SoftwarePresentation` + `TerminalPresenter` + `TerminalFrameGrid` +
  `TerminalAnsiWriter` + `TerminalBudget`, with **no** ASan or UBSan diagnostic of any kind.
- The **whole end-to-end path** under a real pseudo-TTY with `detect_leaks=1`: the demo renders,
  presents repeatedly, takes a `SIGWINCH` resize, handles keyboard input and shuts down — clean,
  exit 0, no leak report. That is the run that matters here, because it is the one that exercises
  raw framebuffer memory, row pitch and presenter lifetime rather than a fixture's.

A wrong result on the first attempt is worth recording, because it was mine and not the code's: the
first sanitized run failed every presenting test. The build had compiled while the sources were
temporarily reverted for the attribution experiment above, so it contained the old empty
`Present()`. Rebuilt against the real sources, it is the 50/50 and the clean end-to-end run above.

### Performance sanity

The producer allocates nothing and copies nothing per frame: the `SurfaceFrame` is a four-field
value pointing straight at `SoftwareFramebuffer::color`, and `ResolveColor()` returns immediately
when multisampling is off and otherwise writes into storage that already exists. On the consumer
side `TerminalSurfacePresenter` diffs against the previous grid and *swaps* the two grids rather
than copying, so a steady-state frame allocates nothing there either, and its frame budget drops
frames instead of growing a backlog. The integration run reports `dropped_frames=0` at 120x40.


## TCR-1 — Baseline audit

**Starting SHA `30389564c`**, tree clean, branch `terminal-capi-repair` created from it. `git diff
30389564c 113be1520` is empty, so this branch starts from exactly the tree `next` carries.

**Sibling repositories** recorded before any work. Four carry pre-existing local modifications that
are none of this branch's business and must be unchanged at the end: `cna-car-simulator` (2 modified),
`mesh-craft` (1 modified, 2 untracked), `myra-cna` (1 modified, 1 untracked), `cna-java` (untracked
`out/`). `sharp-runtime`, `easy-gl` and `house-simulator` are clean and stay clean.

**Renderer invariants at the start**, read from the canonical registry
(`cmake/RendererIdentities.cmake`): 25 public identities, 26 retired identities with reserved C ABI
values, next free value 52. ABI `0.29.0`; `tools/c-api/abi_baseline.json` records 221 structs and
4055 exports.

### The TERMINAL disconnection, traced end to end

The presenter seam is **complete on the consumer side and absent on the producer side**. Nothing is
missing architecturally:

- `CNA::Platform::SurfaceFrame` (`modules/platform/include/CNA/Platform/IPlatformSurfacePresenter.hpp:35`)
  is the CPU-framebuffer contract: RGBA8, row-major, top row first, `strideBytes == 0` meaning
  tightly packed. `SoftwareFramebuffer::color` is **byte-for-byte that layout already**.
- `IPlatform::CreateSurfacePresenter(IPlatformWindow&)` is pure virtual and implemented by five
  retained backends; `TerminalSurfacePresenter` passes 36 pseudo-TTY tests.
- `GraphicsRendererCreateArgs::surfacePresenter` (`IGraphicsRenderer.hpp:4000`) is the wire.
- `GraphicsDevice::createRenderer()` populates it (`GraphicsDevice.cpp:3985`).

Two links are missing, and both are recorded in the tree rather than discovered here
(`docs/platform-terminal-analysis.md:83-86`):

1. **No renderer sets `needsSurfacePresenter`.** BLEND2D was the only one that did and was retired
   by `RRC-002`.
2. **`SoftwareRenderer::Present()` is empty** (`SoftwareRenderer2DState.cpp:58`), and the descriptor
   sets `needsWindow = false`, so `createOrAttachWindow()` drops the window at
   `GraphicsDevice.cpp:3555` before a presenter could be created against it.

`TerminalSoftwareDemoIntegration` is registered `DISABLED TRUE`
(`modules/graphics/examples/CMakeLists.txt:138`) with the one-line re-enable condition stated at the
registration site.

### The C API build failure — reproduced, and it is not one failure but three

`-DCNA_BUILD_C_API=ON`, built to completion with `-k` so every error is seen rather than the first
eight:

1. **The recorded one.** `modules/c-api/src/CnaCApiEffects.cpp` fails with exactly **8 errors** —
   one `invalid initialization of reference ... from expression of type EffectAnnotation*` at 1691,
   and seven `lvalue required as unary '&' operand` at 2630, 2658, 2963, 2992, 3302, 3332, 3360.
   **Root cause, proven by history rather than inferred:** `e55945cd7`
   (*fix(SOFTWARE-255): restore effect index null semantics*, 2026-09-09) changed
   `operator[]` on `EffectAnnotationCollection`, `EffectParameterCollection`, `EffectPassCollection`
   and `EffectTechniqueCollection` from `T&` to `T*`, because XNA returns null for a lookup that
   finds nothing. That commit touched **zero** files under `modules/c-api/`. The wrapper still writes
   `&(*collection)[i]` — the address of a pointer prvalue — and passes `(*collection)[i]` to a
   `const T&` parameter. The C API is OFF by default, so nothing caught it for nine days.

2. **A configuration break the recorded audit never reached.** With `CNA_ENABLE_NET=OFF` the C API
   does not compile at all: `modules/CMakeLists.txt:399` adds `gamer-services` only under
   `CNA_ENABLE_NET`, while `modules/c-api` links `CNA_GamerServices` unconditionally and
   `CnaCApiDetail.hpp:29` includes a GamerServices header, so every C API TU fails with
   `GameUpdateRequiredException.hpp: No such file or directory`. `CNA_ENABLE_NET` defaults `ON`; the
   shared debug tree had it off, which is how this surfaced. Restoring the default reaches failure 1.

3. **Two stale ABI assertions.** `modules/c-api/tests/pure_c/AbiHeaderC.c:11` and
   `modules/c-api/tests/cpp/AbiHeaderCpp.cpp:11` still assert `CNA_ABI_VERSION_ENCODE(0, 27, 0)`
   while `abi.h` declares `0.29.0`. Neither the `0.28.0` commit (`fc1b6a537`) nor the `0.29.0` commit
   (`dea94ad85`) updated them. These are the two ABI walls, so they fail the moment the library
   builds.

None of the three is caused by this branch, and none is worked around: each is fixed where it is
wrong.
