# Lane card — `sokol` (sokol_gfx) · ✅ **INTEGRATED 2026-08-07** · merge `37066e45` — the twelfth lane, Batch 3 opens

> **Outcome.** Adapted, validated and merged in one session. The lane was inventory-classified
> `UNKNOWN` / `NEEDS VALIDATION`, but unlike `wicked` and `magnum` it had genuinely been built and
> run before: a pre-adaptation build at its own fork point reproduced its recorded results exactly,
> so the session started from a proven baseline rather than from an unknown. Interface drift against
> the current head was **two references in one function**. Validation found **three defects, all
> fixed in-lane** — two adaptation-owned (a dropped `#endif` in a conflict resolution, and a missing
> registration-union arm) and one lane-owned (a raw-`new`ed `GraphicsDeviceManager` ASan caught) —
> plus **one capability report corrected on measurement**. **Nothing was pushed; no other lane was
> begun; `audit/` untouched; the Batch 3 checkpoint was NOT taken.**

| Field | Value |
|---|---|
| Logical lane | `sokol` |
| Refs | local **and** remote `feature/sokol` — both unchanged |
| Original head | `261ea70027d04c55519f82f435c28705beb0b8c6` — unchanged locally and on `origin` |
| Archive tag | `archive/preintegration/sokol-20260804` → `261ea700` · GPG **Good** · unchanged |
| Real fork point | **`1eb22c11`** on `feature/audit` — audit-stacked, **376 behind** the integration head at adaptation |
| Own commits / files | **37 / 57** · original `+26586, −53` |
| Adaptation branch / head | `adapt/sokol` → **`9fb83a99`** (worktree `cnaintegration-sokol`, retained) |
| Adapted commits | **44** (37 replayed 1:1 + 1 stream-array adaptation + 2 obligations + 3 validation-driven fixes + 1 docs) |
| Merge commit | **`37066e45`** — signed, `--no-ff`, zero conflicts, merged tree byte-identical to `adapt/sokol` |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED — partial (23/37)**, re-verified at the object level |
| Conflict class | MEDIUM — confirmed |
| Path taken | **ADAPTATION** |

---

## 1. History — re-verified at the object level

Two classes, exactly as the inventory's `23/37` said.

| Class | Count | Detail |
|---|---|---|
| `Claude <noreply@anthropic.com>` author **and** committer | **23** | SSH-signed by the campaign's known non-maintainer key (`%G?` = `N`; `git` reports `gpg.ssh.allowedSignersFile` unconfigured), each carrying `Co-Authored-By: Claude Sonnet 5` **and** `Claude-Session:` — one session id across all 23 |
| `Robert Vokac` author **and** committer | **14** | maintainer-PGP signed (`%G?` = `U`), no trailers, no attribution text |

0 merges, 0 WIP/fixup/squash subjects. Every commit was re-authored under policy A2 to
`Robert Vokac <robertvokac@robertvokac.com>` with its **original author date preserved**, both
trailers stripped 23/23, and re-signed with the maintainer key.

**Message cleanup at replay.** The two AI trailers, and ten per-commit corpus-status paragraphs
(`Full CnaTests suite verified under SOKOL: same 6 pre-existing…`, `All 34 registered Sokol tests
pass…`) — stale figures, re-derived by this session, the same class `magnum` dropped. Commit 1's
*"a fifteenth graphics backend"* was dropped: false at the head, where SOKOL is the **thirtieth**
identity. Its verification paragraph was reworded from a session report into a description of what
the two fixtures assert; its `DX3/HEADLESS` gate list was corrected to `FREEDIRECT/HEADLESS`, the
rename having landed on 2026-08-04.

## 2. What the lane is

| Field | Value |
|---|---|
| Public identity | **`SOKOL`** — `CNA::GraphicsBackendType::Sokol`, name `"SOKOL"`, the **30th** identity |
| Selector | `-DCNA_GRAPHICS_BACKEND=SOKOL`; option `CNA_BACKEND_SOKOL`; define `CNA_BACKEND_SOKOL` |
| Architecture | **sokol_gfx**, a single-header GPU abstraction, on a **desktop OpenGL 4.1-core** context. CNA keeps the SDL3 window and the game loop; the backend creates only the GPU context, through `SDL_GL_CreateContext` — `sokol_app` is deliberately unused |
| Second axis | `CNA_SOKOL_API` ∈ {`GLCORE`, `GLES3`, `D3D11`, `METAL`, `WGPU`} selects which native API sokol_gfx dispatches onto. **`GLCORE` is the default and the only implemented value**; every other value warns at configure time and throws at construction, so backend selection is deterministic |
| Dependency | **one header set**: sokol pinned at `27b49604b19be8cee0dcc6b2bbfe803dd9517585` (2026-07-30), **zlib/libpng**, Andre Weissflog. Fetched at configure time, never vendored and never built — sokol is STB-style single-file headers, so the integration only puts a directory on the include path. `~/deps/sokol` at that exact commit serves offline builds via CMake's own `FETCHCONTENT_SOURCE_DIR_SOKOL` |
| Binaries / patches | **none vendored, none carried** — no upstream patch was needed |
| Shaders | compiled offline by `sokol-shdc` into the checked-in `shaders/sokol_shaders.hpp` (13 726 lines), mirroring the Bgfx convention: an ordinary build needs no `shdc` binary |
| Tests | **37** registered pixel CTests (label `Sokol`) + the shared `CnaTests` corpus under this backend |

## 3. Contract

Supported and pixel-verified: context/present and the whole `Clear` family; back-buffer readback;
`Texture2D` (mips, `GetData`); `SpriteBatch` with real `BlendState`/`SamplerState`; vertex/index
buffers (16- and 32-bit) with `sg_update_buffer` reuse; every `PrimitiveType`; depth test, stencil
test, cull mode, depth bias, viewport/scissor, `Viewport.MinDepth`/`MaxDepth`; `RenderTarget2D` and
`RenderTargetCube` (bind, sample, `GetData`, mip regeneration, MSAA + resolve on the 2D form, MRT ×4);
`TextureCube` and `Texture3D` storage; occlusion queries; runtime-compiled `ShaderEffect`;
`BasicEffect` (textured, lit, fog, alpha test), `DualTextureEffect`, `EnvironmentMapEffect`,
`SkinnedEffect`, instanced draws; and **`RasterizerState.FillMode::WireFrame`**, by CPU-side
triangle-to-`GL_LINES` re-expansion.

Refused deterministically, device left usable: PBR shading (`SOKOL-49`); a lit draw whose
declaration has a Normal but no TextureCoordinate; a declaration this backend would misread
(`RequireFaithfulDeclarationEXT`, §5.2); a multi-stream declaration or a second per-instance stream
(`MultiStreamVertexInput == false`, §5.1); a non-`GLCORE` `CNA_SOKOL_API`. Declared boundaries:
`RenderTargetCube` MSAA (a permanent sokol_gfx API boundary — its validation layer hard-rejects a
cube image with `sample_count > 1`), `BlendState.MultiSampleMask` (no upstream API), 16384 sprite
quads per frame, and Tangent/Binormal elements ignored until PBR lands.

Capabilities: an exhaustive **eleven-member switch with no `default` arm**. Answers below (§7).

## 4. Pre-adaptation baseline — the lane genuinely worked

Unlike `wicked` and `magnum`, this lane did not need its first-ever build to be the adapted one.
Configured straight from the historical worktree at `261ea700`, with the pinned sokol checkout:

| Gate | Result |
|---|---|
| Configure at the recorded pin | clean — `CNA: sokol pinned at 27b49604… (SOKOL_GLCORE)`, offline `FETCHCONTENT_SOURCE_DIR_SOKOL` route proven |
| Build (`CNA` + backend + the four core harnesses) | **0 errors** |
| `Sokol_Smoke` / `Sokol_2D` / `Sokol_3D` / `Sokol_Lit3D` | **13/13 · 15/15 · 10/10 · 10/10 — 48 checks, 0 failures** |

Those are exactly the figures `plans/plan_sokol.md` recorded, reproduced on a different machine from a
different checkout. **Everything measured after this point is therefore attributable to adaptation,
not to an unknown starting state** — which is what the red-first baseline is for.

## 5. Interface drift and the obligations

**Compile probe: exactly two errors**, both `GpuDrawParams::instanceVb` in
`DrawInstancedPrimitivesEx`. Nothing else in 5 535 lines of backend `.cpp` and 2 335 of `.hpp`
failed against 376 commits of head drift.

### 5.1 REMED-GFX-201/202 — the stream array

`instanceVb` and the three fields beside it were replaced by `vertexStreams`, one array carrying
every active `VertexBufferBinding` on every route. The instanced route now reads
`FirstInstanceStream(params)`, and two properties the old shape could not carry are honoured rather
than assumed: `InstanceFrequency` reaches `sg_vertex_buffer_layout_state.step_rate` (a
`glVertexAttribDivisor` on the GL backends) and joins the pipeline key, and the stream's own
`VertexOffset` becomes slot 1's binding offset in instance records — measured against the pipeline's
64-byte matrix rather than the binding's declared stride, which is 0 for an instance buffer built
without a declaration.

`MultiStreamVertexInput` answers **false** and `Instancing` **true**. Both routes call
`RejectUnsupportedStreamCombination()`, and a per-instance stride that is neither unset nor 64 is
refused by name. Proven at runtime: `InstancedDrawMultiStreamTest` + `OrdinaryDrawMultiStreamTest`
**8/8**, including both explicit *"an unsupported backend must reject deterministically, never render
from a subset of the streams"* cases.

### 5.2 REMED-GFX-DECL-GUARD — a guard that matches the mechanism

The shared `RequireFaithfulVertexDeclaration()` helper is **deliberately not reused**. It models a
backend that infers its native layout from the byte stride and then asks whether the declaration
agrees; this backend does the opposite — it programs `sg_pipeline_desc::layout` from the
declaration's own offsets and formats, so every semantic it binds is faithful **by construction**
and the stride-table rule would refuse correct draws.

The new header-only `RequireFaithfulDeclarationEXT()` refuses what remains: a declared stride the
buffer was not uploaded with (the pipeline would advance records at a pitch the data does not have),
an element outside its record, two elements claiming the same bytes, and a second usage-index set of
a semantic the pipeline binds. A semantic no stock shader reads is explicitly **not** refused —
that is a superset declaration, the same shape as a declaration shorter than the program's input
list. Pure, and called before any pass, pipeline or binding exists.

### 5.3 REMED-GFX-209 — the capability corrected on measurement

The lane reported `WireFrame == false`, on the reading that the flag means *native* polygon-mode
support. `CNA_BACKEND_SOKOL` joined `WireFrameTriangleOracle.hpp`'s pixel set, and the shared
asymmetric-triangle fixture measured:

```
Sokol solid:     total=18176 interior=1089/1089 AB=298 BC=310 CA=329
Sokol wireframe: total=559   interior=0/1089    AB=25  BC=25  CA=25
```

A genuine wireframe by the oracle's own definition. The report is corrected to **`true`**. It
deliberately does not copy EasyGL's `false`, which `GraphicsDeviceCapabilityTests.cpp` records as
the one answer known to be wrong and which `REMED-GFX-219` exists to fix — reproducing it in a newly
added backend would add a second under-report, not inherit a convention. Alternation and recovery
pass too, and `WireFrameIsRefusedDeterministically…` correctly **skips**: this backend renders, so
it must not refuse.

### 5.4 Registration union

The **ninth** of the campaign. 29 existing identities kept token-exact, SOKOL added to:
`BackendSelection.cmake` (docstring, `STRINGS`, option, explicit-selection guard, enabled-list,
dispatch), `BackendLibraries.cmake`, `CnaLibrary.cmake` (the `--start-group` archive-cycle set),
`CMakeLists.txt`, `GraphicsBackendType.hpp` (enum + `#elif` + name table),
`GraphicsBackendTypeTests.cpp`'s `ExpectedNameFor()` arm, the compile-definition count,
`GraphicsDevice::getBackendWindowFlags()`, `README.md`, `docs/README.md` and
`THIRD_PARTY_NOTICES.md`. Four shared `CnaTests` cube-storage gates and three shared render-target
oracle contracts took SOKOL arms as the lane's own commits replayed them.

## 6. The three defects validation found

**6.1 A dropped `#endif`.** The SOKOL-25 replay resolved a conflict in
`SetRenderTargets_OneTarget_DoesNotThrow`: the lane removed its own SOKOL arm there while the head
had grown an unrelated STUB arm in the same place. Restoring the head's arm consumed the closing
`#endif`, leaving an unterminated `#else` that fails to compile in **every** configuration. Caught
by the first `CnaTests` build of the adapted branch. Fixed forward, in its own commit, rather than
by rewriting the replayed commit — the replay's 1:1 `range-diff` is evidence and stays intact.

**6.2 A missing registration-union arm.** `GraphicsBackendTypeTests.cpp`'s `ExpectedNameFor()` is
arm-per-identity with no `default:` precisely so a missing arm fails loudly; the union that added
SOKOL everywhere else never conflicted on that file, so the omission was invisible until the first
full corpus run named it. Exactly the gap class the file's own comments already record for D3D9,
DX2, OPENGL1 and WICKED. The adjacent stale *"27 backend builds"* count was corrected to 30 while
the file was open.

**6.3 A leaked `GraphicsDeviceManager` (lane-owned).** ASan reported one leak with CNA frames in
it, in exactly one of the 37 suites: 488 bytes for a raw-`new`ed `GraphicsDeviceManager` in
`sokol_blendfactor_pipeline_cache_test.cpp`, plus the 40-byte `EventHandler` callback vector
reachable only from it. That file was the only one in `examples/` not holding it in a
`std::unique_ptr` member — its own siblings `sokol_2d_test` and `sokol_wireframe_test` already do,
so the fix matches an established idiom rather than inventing an ownership rule. Post-fix the suite
reports the driver baseline exactly, 240 732 B in 1073 allocations with no CNA frame anywhere.

## 7. Capability table — from code and measurement

| Capability | Answer | Class | Evidence |
|---|---|---|---|
| `ThreeD` | `true` | supported and tested | `Sokol_3D`, `Sokol_Lit3D`, every stock-effect suite |
| `DepthStencilBuffer` | `true` | supported and tested | `Sokol_RenderTarget2D_Depth`, `Sokol_RenderTarget_DepthStencilUsage` |
| `MultiSampleAntiAliasing` | `true` | supported and tested | `Sokol_RenderTarget2D_Msaa` (differential AA proof) |
| `MultipleRenderTargets` | `true` | supported and tested | `Sokol_MRT`, 20 checks |
| `AnisotropicFiltering` | `true` | supported and tested | `Sokol_RenderTarget2D_Mip`, `Sokol_2D` |
| `WireFrame` | **`true`** | supported and tested | §5.3's oracle reading — corrected this session |
| `OcclusionQuery` | `true` (GL only) | supported and tested | the three `Sokol_OcclusionQuery_*` suites |
| `CustomEffects` | `true` (GL only) | supported and tested | `Sokol_ShaderEffect_SpriteBatch`, `Sokol_ShaderEffect_3D`, `Sokol_MRT` |
| `Texture3D` | `true` | supported and tested | `Texture3DTest` (39 cases) in-corpus |
| `MultiStreamVertexInput` | **`false`** | deliberately unsupported | §5.1 — refusal proven 8/8 |
| `Instancing` | **`true`** | supported and tested | `Sokol_Instanced3D`, 5 checks |

Not applicable / blocked by an external limitation, recorded rather than answered by this enum:
`RenderTargetCube` MSAA (**blocked** — sokol_gfx's own validation layer rejects it), PBR shading
(**deliberately unsupported**, `SOKOL-49`), `BlendState.MultiSampleMask` (**blocked** — no upstream
API), `CNA_SOKOL_API` other than `GLCORE` (**deliberately unsupported**, `SOKOL-31`).

## 8. Validation at the merged content

| Gate | Result |
|---|---|
| Build | backend + `CNA` + `CnaTests` + all 37 harnesses, **0 errors, 0 new warnings** |
| Runtime identity | `4.5 (Core Profile) Mesa 25.0.7-2+deb13u1` · **llvmpipe** (LLVM 19.1.7) on Xvfb `:101`, `SDL_VIDEODRIVER=x11` |
| Dedicated suites | **37/37** (`ctest -R "^Sokol"`, serial, 13.8 s) |
| Corpus run 1 (discovery) | 5776 registered · **5769 passed · 1 failed · 6 skips · 0 aborts** (668.9 s). The failure was §6.2, fixed |
| **Corpus official (run 2, fixed content)** | 5776 registered · **5768 passed · 1 failed · 7 truthful skips · 0 aborts/timeouts** (697.2 s) |
| The 1 failure | `TwoProcessLoopbackTest.HostMigration…` — the known networking **Outcome C** coin flip. Passed in run 1; **3/3 green** re-run in isolation. Pre-existing checkpoint residual, not a lane regression |
| The 7 skips | 4 sensor + `WireFrame`-refusal (inapplicable: truthfully `true`) + `Texture3DUnsupported` (inapplicable: genuine Texture3D) + one wall-clock audio `WaveBankTest` — the Wicked/Magnum set plus one audio-class skip |
| Sanitizers (`cmake-build-sokol-asan`, address+undefined, runtimes proven linked by `ldd` and 53 `__asan_`/`__ubsan_` symbols) | **0 ASan errors, 0 UBSan runtime errors** across all 37 suites. Leaks: 240 732 B / 1073 allocs, byte-identical in every suite, every frame inside `libGLX_mesa` — zero CNA frames after §6.3. `detect_leaks=0` control **37/37** |
| EasyGL control (`cmake-build-sokol-easygl`, built from the adapted sources) | 6190 registered · **5894 executed, 5894 passed, 0 genuine failures, 7 truthful skips**. The 296 `(Not Run)` entries are the dedicated EasyGL harnesses, which do not compile in this environment (`PixelTestGame.hpp` cannot resolve `SDL3/SDL.h` under the EasyGL configuration) — **proven pre-existing**: the identical target fails identically on the pre-Sokol tree at the integration head (`cmake-build-noxna`, source `cnaintegration`). Not caused by, and not in scope for, this lane |
| Provenance | original ref and archive tag unchanged; `range-diff` pairs **37/37 in order**; attribution sweep over the adapted range **zero hits**; every commit signature good; `git diff --check` clean |

## 9. Residuals

Unchanged and not claimed: `REMED-GFX-221` (LOW), `REMED-CONTENT-007`/`-008` (HIGH/P1 — this lane
touches no `Content/` file; the inventory's "tests only" assessment re-checked and confirmed),
networking Outcome C, Direct2D OWNER-FROZEN. Lane-local, all pre-existing plan rows:
`SOKOL-49` (PBR), `SOKOL-31` (non-`GLCORE` `CNA_SOKOL_API`), `SOKOL-30` (real-GPU verification —
this campaign's environment is llvmpipe by design), `RenderTargetCube` MSAA and
`BlendState.MultiSampleMask` as permanent upstream boundaries.

One observation recorded without a defect claim: **`MAGNUM` is absent from `README.md`'s
`CNA_GRAPHICS_BACKEND` list** (it appears only in the highlights section above it). A pre-existing
gap in the eleventh lane's own registration union, noticed while adding the SOKOL entry to that same
list and deliberately **not** fixed here — it belongs to `magnum`'s record, not this one.

**New findings: none.**
