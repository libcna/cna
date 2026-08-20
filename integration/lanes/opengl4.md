# Lane `opengl4` — real desktop OpenGL 4.x core-profile graphics backend

**Status: ✅ INTEGRATED 2026-08-05 · ADAPTATION · merge `bc29a976`** (signed, `--no-ff`,
parents `df6b7cc6` + `3f1035de`). Seventh logical lane, **third of Batch 1**. Nothing was pushed.

---

## 1. Identity

| Field | Value |
|---|---|
| Ref | `refs/heads/feature/opengl4` = `refs/remotes/origin/feature/opengl4` |
| Original head | `c49e0ba223fa36f8fa9f7cd643305ea3367bf521` |
| Archive tag | `archive/preintegration/opengl4-20260804` → `c49e0ba2`, **verifies good** (RSA `255C69CC…0AADA55F`), unchanged |
| Merge base | `ac3aaaeb` (`origin/develop`) — develop-forked, **867 commits behind** the integration head |
| Own commits / files | **28 / 41** · `+11869, −5` · 0 merges, 0 WIP |
| Shared interfaces | `GraphicsDevice.cpp` + `IGraphicsBackend.hpp` (as predicted — the batch's first two-interface lane) |
| Adaptation branch | `adapt/opengl4` → `3f1035de` · worktree `/rv/data/development/github.com/openeggbert/cnaintegration-opengl4` (kept) |
| Adapted commits | **28** = 24 replayed + 1 interface adaptation + 1 capability + 1 test arming + 1 docs |
| Path taken | **ADAPTATION** |

---

## 2. Why not a direct merge

### 2.1 History (conditions 3, 4, 5) — disqualifying on its own

Object-level inspection (`git cat-file -p`), never `%G?`:

| Class | Count | Detail |
|---|---|---|
| Maintainer PGP-signed | **0** | — |
| **SSH**-signed, non-maintainer | **28** | all 28 embed the identical `ssh-ed25519 …rLzsfFISF4by8Q+FKz27YpkK1USsBB+mamu1QkJnbDs` key known from every prior lane of this class |
| Genuinely unsigned | **0** | the inventory's "0 PGP / 28 SSH" row re-verified |

All 28 authored **and** committed by `Claude <noreply@anthropic.com>`. The **first 8** carry both
prohibited trailers (`Co-Authored-By: Claude Sonnet 5` + `Claude-Session:`), across **two**
distinct session identifiers; the other 20 carry none. One further sweep hit is the subject
`docs(NEXT.md): add claude/opengl4-tech-tasks-xjevpc session handoff section` — the ref name is
factual provenance (policy §2.1), but the commit itself is session narrative (see §9).

### 2.2 Content (conditions 6, 7, 8) — the probe found what the prior lanes predicted, plus two firsts

`-fsyntax-only` probe of the original backend against the current `Common/` interfaces:
**23 errors, 13 distinct drifts.** The familiar set from `opengles1` — seven `void → bool`
readback/upload signatures (REMED-GFX-127/130/135), pure-virtual
`SetVertexDeclaration(const VertexDeclaration&)` and descriptor-based `SetRenderTargets` (both
classes abstract), `CreateRenderTargetCube(preserveContents)`, `ApplyBlendState(BlendWriteState)`
— plus two this lane is the **first to pay**:

- **`fc0dd2a2` unified instanced transport.** The lane implements real hardware instancing
  (GL4-33) against the removed `GpuDrawParams::instanceVb`; every prior lane predating
  `fc0dd2a2` had no instancing to adapt. Rewritten to `FirstInstanceStream()`, with the divisor
  = the stream's own `InstanceFrequency` (GFX-213) and the stream's `VertexOffset` offsetting
  every attribute pointer by its own records (GFX-211).
- **The FNA fog vector across ten GLSL programs** (REMED-GFX-010). No inversion here, unlike the
  fixed-function `opengles1` lane: the shaders consume the vector directly
  (`vFogFactor = 1 − saturate(dot(pos, uFogVector))`), the three skinned programs dotting the
  **post-skin** position exactly as FNA's `Skin()` ordering requires. This also *fixes* the
  lane's own object-space-only scalar formula, which was exact only for `World=View=Identity`.

The lane's own final commit had added a `VertexElementFormat` alias to `IGraphicsBackend.hpp`
(+1 line, purely additive); it replays cleanly and resolves four of the probe's 23 errors itself.

---

## 3. §1.1 post-audit obligations — decided on evidence

| Obligation | Decision |
|---|---|
| **`REMED-GFX-DECL-GUARD`** | **Applies, and is applied.** `BindProgramForStride`/`ApplyLayout` select the native attribute layout from the byte stride (16/20/24/32/48/52/56/68 — exactly the shared fidelity table's stride family). `RequireFaithfulDeclarationEXT` guards **all five** routes (colored ×2, ordinary ×2, instanced), asymmetric, header-only, `PositionOnlyFallback` for unlisted strides (the measured default-arm binding). Custom-`ShaderEffect` draws are exempt **by construction** — GL4-33's generic path binds attributes from the declaration itself — matching EasyGL's own gating, the closest architectural precedent. |
| **`REMED-GFX-209` / WireFrame** | Reports `true` and genuinely rasterizes via desktop core `glPolygonMode`. Now proven, not merely asserted: the lane was added to the **shared wireframe pixel oracle** (interior empty, all three edges lit, order-of-magnitude coverage difference) — see §7. |

---

## 4. Capability exhaustiveness (Phase 5) — the predicted hazard was real

The backend **never overrode `SupportsCapability`** — every answer was the inherited default,
i.e. `true` for members the fork had never heard of (`GraphicsCapability` grew 8 → 10 after it).
The `opengles1` lesson held for a second consecutive lane.

Now answered **explicitly for all ten members with no default case**, so a future member
surfaces as a `-Wswitch` warning rather than an inherited wrong answer: nine `true`, each
comment-named to its real implementation; `MultiStreamVertexInput` **false** (one per-vertex
stream by construction); `AnisotropicFiltering` from the **driver-granted ceiling**
(`GL_MAX_TEXTURE_MAX_ANISOTROPY`, error-drained probe — the extension is core only in GL 4.6),
with `ApplySamplerState` uploading the sampler parameter only when the driver accepted it.

**The negative oracles do not trust the answer they check:**
`OrdinaryDrawMultiStreamTest.UnsupportedBackendRejectsMultiStreamDeterministically` and
`InstancedDrawMultiStreamTest.UnsupportedBackendRejectsMixedStreamInstancingDeterministically`
both ran and passed under this backend, and every `Draw*Ex` route additionally throws
`System::NotSupportedException` if a multi-stream draw reaches the backend directly.

---

## 5. Public backend contract (Phase 3)

| Field | Value |
|---|---|
| Public? | **Yes** — enum `CNA::GraphicsBackendType::OpenGL4` (25th member), `-DCNA_GRAPHICS_BACKEND=OPENGL4`, target `cna_backend_graphics_opengl4` |
| Version/profile | Requests **4.1 core minimum** (`SDL_GL_CONTEXT_PROFILE_CORE`); validated on a real granted **`OpenGL 4.5 (Core Profile) Mesa 25.0.7`**, GLSL **4.50** — llvmpipe software rasterization, reported as such (a real core-profile runtime, not a compatibility fallback) |
| Dependencies | the platform's own GL library via `find_package(OpenGL REQUIRED)` + SDL3 (existing). GL 1.2+ entry points via the backend's own `GL4Loader` — **zero new third-party dependency, nothing vendored, nothing downloaded, no absolute paths** |
| Generated files | **none** — GLSL 410 core compiled at runtime from inline sources; compile failure throws with the driver's own info log |
| Classification | a real modern-desktop backend, deliberately independent of EasyGL (which requests an **ES 3.0/WebGL2** context and cannot create a desktop core profile at all) |

**Supported:** the full set in `docs/opengl4-backend.md` — all five stock effects +
`PbrEffect`/`SkinnedPbrEffect`, per-pixel/vertex-lit variants, fog (vector form), FBO render
targets 2D/cube/**MRT (8)**, backbuffer + RT **MSAA**, real **occlusion queries** (exact
`GL_SAMPLES_PASSED` counts), `Texture3D`/`TextureCube` with real readback, mip upload +
mip-aware filters, 16/32-bit indices, `baseVertex`, custom GLSL `ShaderEffect` (3D +
SpriteBatch), **hardware instancing**, real WireFrame, per-MRT-slot `ColorWriteChannels` via
`glColorMaski`, dynamic sampler state, window/logical transforms, disposal.

**Unsupported — rejects or reports truthfully, never silently:** multi-stream vertex input
(capability `false` + `GraphicsDevice` refusal + backend-level throw); unfaithful declarations
(draw-time guard); `MultiSampleMask` beyond all-ones (EasyGL's same documented gap); MSAA change
after construction (inherited no-op, EasyGL's same limitation); cube faces in a multi-target set
(throws); MRT depth attachment (EasyGL's same gap); anisotropy without the driver extension
(truthful `false`); context-loss recovery (owner-deferred in `plans/plan_opengl4.md`, 2026-07-22);
Windows/macOS validation (environment-blocked, still open in the plan).

---

## 6. Distinction from `feature/gl` (Phase 4) — preserved

- `OpenGL4` is its own public enum member and build option; **no selector or enum collision**
  with OpenGL 3 (which does not exist at this head) or with EasyGL.
- **Not routed through EasyGL** and not dependent on MetaGL/EasyGL merges — the lane's own first
  commit records the independence rationale (ES-profile vs core-profile), and zero EasyGL/MetaGL
  files are touched.
- `feature/gl`'s public set is unchanged: exactly **OpenGL ES 3, OpenGL 3, WebGL 1, WebGL 2**,
  EasyGL internal and hidden. No OpenGL 1/2 work absorbed.

---

## 7. Commit mapping (Phases 7/9) — no original commit disappeared

**24 TRANSFERRED · 4 OMITTED WITH JUSTIFICATION · 0 lost.**

- The four omissions are `a81d8638`, `7e402c96`, `fe4a0a01`, `16d0b212` — per-session `NEXT.md`
  status summaries (one explicitly a "session handoff section"). Their content is the session
  narrative policy §2.2/F1 excludes, the campaign's own `NEXT.md` at the head supersedes the
  file wholesale, and `plans/plan_opengl4.md` carries the lane's technical continuity in full. Seven
  replayed feat/docs commits also carried incidental `NEXT.md` hunks; those hunks were resolved
  keep-HEAD under the same reasoning (recorded here, per P5/P6).
- Two replayed messages were reworded only to remove session-narrative phrasing (`4a4f1769`,
  `c49e0ba2` — "this session" → "this branch" etc.); their patches are untouched.
- `git range-diff`: every replayed pair is `!` for author/trailer metadata only, except commit 1
  (the six registration unions of §8) and the `.gitignore` one-liner `78e49353 → 259714fa`,
  which range-diff could not pair because its 1-line patch's entire context drifted — the line
  itself (`cmake-build-opengl4/*`) is present at the adapted head.
- **File-level losslessness at the replay boundary: 32 of 41 files byte-identical, 0 missing.**
  The 9 differing are exactly the 7 registration-union files + `NEXT.md` (deliberate keep-HEAD)
  + `IGraphicsBackend.hpp` (the base file itself evolved; the lane's +1 alias line is present).

The four commits beyond the replay:

| Commit | Purpose |
|---|---|
| `6c6968f3` | bounded interface adaptation — all 13 probe drifts (§2.2), declaration guard (§3) |
| `c7357626` | exhaustive truthful `SupportsCapability` + driver-queried anisotropy (§4) |
| `a4f8dd64` | shared-test arming (§below) |
| `3f1035de` | `docs/opengl4-backend.md` + README backend entries |

**Test arming — two shared tables predated this backend and answered for it wrongly:**
`RenderTargetCubeSetDataContractTest` expected every non-EasyGL backend to *refuse* cube-face
`SetData` (GL4 stores it for real — and unlike EasyGL also reads it back), and
`WireFrameTriangleOracle` gained OPENGL4 in its measured set, so the WireFrame claim is now
pixel-proven in the shared suite. Both are test-contract arming, no production defect.

---

## 8. Conflicts — the registration union, for the fourth time

Commit 1 conflicted on six files (`CMakeLists.txt`, `cmake/BackendLibraries.cmake`,
`cmake/BackendSelection.cmake` ×4 hunks, `GraphicsBackendType.hpp` ×3,
`GraphicsDevice.cpp`, `GraphicsBackendCompileDefinitionTests.cpp`), commit 4 on `.gitignore`.
Taking the incoming side would have deleted **`STUB`, `OPENGLES1`, `FREEDIRECT`,
`DX1/2/5/6/7/8`, `D3D10`** and reverted the `DX3 → FREEDIRECT` rename — and the lane's own
`DX3` token again means *free-direct*, not the head's real DirectX 3. Resolved as HEAD's full
set **plus `OPENGL4`**, verified **token-by-token**: all 24 pre-existing identities keep their
exact HEAD counts; `OPENGL4` appears 5× in `BackendSelection.cmake`, matching the per-backend
registration shape.

---

## 9. Validation

**Real `OpenGL 4.5 (Core Profile) Mesa 25.0.7` context** (llvmpipe, `DISPLAY=:101`,
`SDL_VIDEODRIVER=x11`), GPU tests serial:

| Suite | Result |
|---|---|
| All 25 dedicated `OpenGL4_*` CTest suites | **25/25** (22.6 s) |
| `CnaTests` under `CNA_GRAPHICS_BACKEND=OPENGL4`, final run | **5737 executed · 5730 passed · 6 skipped · 1 failed · 0 not run** (5730+6+1 = 5737 exactly) |
| The 6 skips | 4 sensor-hardware skips + `WireFrameIsRefusedDeterministically…` (correct — this backend renders) + `Texture3DUnsupportedBackendTest…` (correct — this backend genuinely supports `Texture3D`; the positive suite ran and passed) |
| The 1 failure | a transient `SDL_InitSubSystem(SDL_INIT_VIDEO): x11 not available` blip that strikes a **different** test each full run (run 1: `ModelContentTypeReader…`; run 2: `SpriteFontContentTypeReader…`), passes **3/3 in isolation** both times — environmental, not a regression |
| First full run (before test arming) | 5733 · 5725 · 5 · 3 — the cube-contract table gap (§7, fixed), plus the x11 blip and the networking flake |
| **Principal control** (shared production files changed): full EasyGL `CnaTests` at the merged HEAD | **5912 executed · 5905 passed · 6 skipped · 1 failed** — the failure is the known flaky `TwoProcessLoopbackTest.HostMigration…`, measured this session at 1-of-3 clean isolation batches under load; against the Batch-0 baseline 5912/5906/6/0 this is **zero regressions** from the three Batch-1 merges |
| Merged tree | **byte-identical** to the validated `adapt/opengl4` tree |

**Sanitizers (gate option A):** dedicated persistent `cmake-build-opengl4-asan`
(`CNA_SANITIZE=address,undefined`, GCC 14.2), nine representative suites run on the real
context: smoke, readback, rendertarget2d, rendertargetcube_mrt, texture3d, texturecube, fog,
instancedmodel, renderstate. **Zero AddressSanitizer errors, zero UBSan runtime errors.** Every
LeakSanitizer report classifies to (a) Mesa GLX driver allocations (all stacks rooted in
`libGLX_mesa`, zero CNA frames — e.g. smoke's entire 100 956-byte report) or (b) the readback
harness's own deliberate program-lifetime `new SpriteBatch` member (backend objects appear only
as indirect leaks reachable from it). Control: `ASAN_OPTIONS=detect_leaks=0` smoke run → 8/8
PASS, exit 0. (LSan's hard exit after its dump also swallows the harness's buffered `[PASS]`
lines — the control run proves the checks execute and pass under instrumentation.)

**Builds:** `cmake-build-opengl4/` and `cmake-build-opengl4-asan/` — new persistent in-repo
directories in the adaptation worktree (no compatible OPENGL4 configuration existed anywhere),
Unix Makefiles, GCC 14.2.0, ccache ON, `-DCNA_TEST_DISPLAY=:101`. Nothing under `/tmp`.

---

## 10. Residuals and observations

- The flaky `TwoProcessLoopbackTest.HostMigration…` (networking, pre-existing) is measurably
  flakier under load than the `opengles1` card recorded — worth watching at the Batch-1
  checkpoint; not graphics, not lane-owned.
- The transient `SDL_INIT_VIDEO: x11 not available` blip under full-suite window churn is
  environmental; different victim each run, isolation-clean.
- **README's compact selector list omits `OPENGLES1`** — a pre-existing head-side omission from
  the previous lane (observed while adding `OPENGL4`; deliberately not fixed here — not this
  lane's file to widen. Owner: the Batch-1 stabilization checkpoint).
- `VertexDeclarationLayoutTests.cpp` keeps its own backend list and is not armed for OPENGL4
  (as it is not for OPENGLES1); the declaration guard is exercised by the shared capability
  oracle set instead.
- MSAA cube faces with `preserveContents` share one resolve path (EasyGL's pre-REMED-GFX-141
  shape); no current test exercises preserve+MSAA cube faces on this backend. Documented, not
  silent.
- Windows/macOS validation remains environment-blocked (`plans/plan_opengl4.md` remaining work).

**New findings: none.** Nothing found in this lane is an independent production defect. The
inherited capability default (§4) is adaptation fallout from a grown enum — the same class the
new-findings rule excluded on `opengles1` — and everything else was either the expected
stale-interface adaptation or test-table arming.

---

## Post-integration correction (Batch 1 stabilization, 2026-08-05)

Provenance re-verified directly against git: **PROVENANCE CLEAN**. The four omitted commits
(`a81d8638`, `7e402c96`, `fe4a0a01`, `16d0b212`) were each confirmed by `git show --name-status` to
touch **`NEXT.md` and nothing else** — no source content was dropped (policy P6 satisfied). Two
corrections:

- **GLSL 4.50 is asserted without a verbatim `GL_SHADING_LANGUAGE_VERSION` string**, and the backend
  contains no `glGetString(GL_SHADING_LANGUAGE_VERSION)` call at all — only `GL_VERSION`. The GL 4.5
  **Core Profile** half is fully evidenced and was re-confirmed at merged HEAD
  (`OpenGL 4.5 (Core Profile) Mesa 25.0.7-2+deb13u1`).
- §10's residual "README's compact selector list omits `OPENGLES1`" is narrower than reality:
  `OPENGLES1` appeared **zero** times anywhere in `README.md`. Repaired in the stabilization session,
  which also found `D3D9` and `SDL_GPU` missing from the same list.
