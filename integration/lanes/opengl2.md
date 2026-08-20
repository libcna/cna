# Lane `opengl2` — native desktop OpenGL 2.1 (compatibility profile) graphics backend

**Status: ✅ INTEGRATED 2026-08-05 · ADAPTATION · merge `9e6d62ed`** (signed, `--no-ff`,
parents `c0876fca` + `289410a6`). Ninth logical lane, **fifth and final lane of Batch 1**.
Nothing was pushed.

---

## 1. Identity

| Field | Value |
|---|---|
| Ref | `refs/heads/feature/opengl2` = `refs/remotes/origin/feature/opengl2` |
| Original head | `77d36d9e3bb402fcf12c093177e4007c7bf11fbc` |
| Archive tag | `archive/preintegration/opengl2-20260804` → `77d36d9e`, **verifies good** (RSA `255C69CC…0AADA55F`), unchanged |
| Merge base | `ac3aaaeb` (`origin/develop`) — develop-forked, **934 commits behind** the integration head |
| Own commits / files | **40 / 63** · `+13962, −10` · 52 new, 11 drifted · 0 merges, 0 WIP |
| Shared interfaces | `GraphicsDevice.cpp/.hpp` + `IGraphicsBackend.hpp` + `GraphicsCapability.hpp` + `VertexBuffer.cpp/.hpp` — the batch's first `GraphicsCapability` lane, and wider than the inventory's three-file prediction |
| Adaptation branch | `adapt/opengl2` → `289410a6` · worktree `/rv/data/development/github.com/openeggbert/cnaintegration-opengl2` (kept) |
| Adapted commits | **47** = 40 replayed + 1 interface adaptation + 1 capability + 1 shared-test arming + 1 build fix + 2 harness-contract adaptations + 1 production fix + 1 docs |
| Path taken | **ADAPTATION** |

---

## 2. Why not a direct merge — HISTORY CLEAN re-verified, content 934 commits stale

### 2.1 History — the classification survived object-level re-verification exactly

`git cat-file -p` over all 40 commits: **40/40 maintainer PGP** (`FB9CE8E20AADA55F`), 40/40
authored *and* committed by Robert Vokac, zero trailers, zero attribution hits, linear, 0 merges.

**Nine commit bodies carried session narrative** — more than `opengl1`'s three — and **two of the
nine were caught only by a multiline-aware sweep** (`perl -0`, matching `this\nsession` across a
line wrap) that a line-based grep misses. Reworded minimally at replay, patches untouched.
Subject citations of the plan's own section headings (`(plans/plan_opengl2.md session 13)`) were kept:
`plans/plan_opengl2.md` is organized by `Session N` headings, so these are F3 factual document
references, not process narration.

### 2.2 Content — the probe found 17 errors, 14 distinct drifts

The familiar stale-fork set: pure-virtual descriptor `SetRenderTargets` and
`SetVertexDeclaration(const VertexDeclaration&)`, six `void → bool` readback/upload signatures,
`CreateRenderTargetCube(preserveContents)`, `ApplyBlendState(BlendWriteState)`, the
`fc0dd2a2` unified instanced transport (`instanceVb` removed), the FNA fog vector — **plus the
lane's own two interface additions the head never adopted**, both restored by replaying the
lane's own commits into the current interface text:

- `IGraphicsBackend::GetDefaultViewportRect()` — the defaulted virtual behind the lane's real
  Letterbox/Overscan/Stretch presentation modes (physical-viewport-rect contract in
  `GraphicsDevice::UpdateViewportFromWindow`);
- **`GraphicsCapability::Instancing`** — the enum's 11th member (§5).

Probe-invisible drifts found by reading: the lane's documented *"non-zero baseVertex silently
falls back to 0"* comment collided with the head's ordinary routes folding real vertex offsets
into `baseVertex` — a silent-wrong-result path, fixed exactly by **software base-vertex**
(attribute pointers re-based `baseVertex*stride` bytes; GL 2.1 has no `glDrawElementsBaseVertex`).

---

## 3. Fog, transport, declarations

- **Fog:** the FNA vector is consumed directly (shader backend — the GL4 treatment, not the ES1
  inversion): all six GLSL 1.10 fog sites compute `vFogFactor = 1 − clamp(dot(pos, uFogVector))`,
  the two skinned programs dotting the **post-skin** position; the vector's encoding carries the
  disabled and `FogStart == FogEnd` cases, so the scalar-era epsilon branch is gone.
- **Instanced transport (REMED-GFX-202):** the per-instance stream is the `vertexStreams` entry
  with `instanceFrequency > 0`; its own `VertexOffset` offsets its attribute pointers (GFX-211),
  its own `InstanceFrequency` is the divisor (GFX-213); the mesh stream honors
  `vertexStreams[0].vertexOffset` plus `baseVertex`; `startIndex` now reaches the instanced call.
  Instancing stays custom-`ShaderEffect`-scoped with the honest base-class refusal otherwise.
- **Declarations (§1.1 DECL-GUARD): fidelity by TRANSLATION, not refusal.** Every public draw
  carries a declaration at the head (GFX-043). Dispatch is behavior-preserving: a declaration
  exactly matching the built-in layout its stride implies keeps the fixed-stride path with its
  validated constant-attribute handling (`DeclarationMatchesInferredStrideLayout`, shared
  `InferredLayoutForStride` with `BackendRefusesIt`); a genuinely custom declaration takes the
  lane's own Task-1080 name-driven path, which reads exactly the caller's declared bytes — so
  the refusal-style `RequireFaithfulDeclarationEXT` is deliberately **not** wired: it would
  refuse declarations this backend genuinely renders correctly. A declaration without a Color
  element now pins `aColor` to constant white instead of inheriting stale generic-attribute
  state. Program-unconsumed elements are skipped (XNA semantics); missing program inputs are
  the shared header's blessed "missing input, not reinterpretation" case.

---

## 4. §1.1 post-audit obligations — decided on evidence

| Obligation | Decision |
|---|---|
| **`REMED-GFX-DECL-GUARD`** | Satisfied **by translation** (§3) — the backend renders arbitrary declarations faithfully instead of refusing them; the stride path runs only for exact built-in layouts (faithful by equality) or undeclared internal buffers (nothing declared to betray). |
| **`REMED-GFX-209` / WireFrame** | Reports `true`, genuinely rasterizes via `glPolygonMode(GL_LINE)`, and is now **pixel-oracle-proven**: OPENGL2 joined the shared `WireFrameTriangleOracle` measured set — which is exactly how the lane's one real production defect was found (§7). |

---

## 5. Capability audit — the enum grew to 11, and the lane grew it

The lane's fork-era switch was 4 explicit + `default: return true` — the permissive-default
hazard, fourth lane of five. Under the head's enum it would have claimed
`MultiStreamVertexInput`. Now the exhaustive **eleven-member no-default** switch:
`ThreeD`/`DepthStencilBuffer`/`CustomEffects`/`WireFrame` structural true;
`MultipleRenderTargets`/`OcclusionQuery`/`Texture3D` from runtime-resolved core entry points;
`MultiSampleAntiAliasing`/`AnisotropicFiltering`/`Instancing` runtime-detected;
`MultiStreamVertexInput` false.

**`GraphicsCapability::Instancing` is the lane's own member** — on this backend instancing is an
optional ARB extension pair, not an unconditional core feature, so a game can ask before relying
on it. Growing the enum obligated every backend whose current shape would answer it wrongly —
**four truthful arms added, the rest verified truthful by existing shape**:

| Backend | Why an arm was required |
|---|---|
| OPENGL4 | really instances (GL4-33) but its no-default switch landed on `return false` |
| OPENGLES1 | its trailing `default: return true` would have **claimed instancing the CM profile cannot do** — the ES1 false-claim hazard, again |
| OPENGL1 | trailing false already truthful; made explicit in its structural-false group |
| Software | `default: return true` but no `DrawInstancedPrimitivesEx` override (base throws) |

Verified truthful without edits: unconditional-false (Ascii, Canvas, Stub, SdlRenderer, Dx1,
FreeDirect), default-false (Dx2/3/5/6/7/8, D3D10), really-instancing default-true or
interface-default (EasyGL, Vulkan, bgfx, WebGPU, Headless, D3D9, D3D11, D3D12).

---

## 6. Public backend contract (Phase 3)

| Field | Value |
|---|---|
| Public? | **Yes** — enum `CNA::GraphicsBackendType::OpenGL2` (27th member), `-DCNA_GRAPHICS_BACKEND=OPENGL2`, target `cna_backend_graphics_opengl2` |
| API/profile | Requests **OpenGL 2.1 compatibility** (`SDL_GL_CONTEXT_PROFILE_COMPATIBILITY`); **GLSL 1.10 throughout, proven by construction** — zero `#version` directives, so every runtime-compiled inline program is the 1.10 spec default; attributes name-bound via `glBindAttribLocation`. Validated on the granted `4.5 (Compatibility Profile) Mesa 25.0.7` llvmpipe context — reported as the driver's identity, not the backend's API level |
| Dependencies | platform GL (`find_package(OpenGL REQUIRED)`) + existing SDL3; 66 post-1.1 entry points via `SDL_GL_GetProcAddress` (the Windows opengl32 model); instancing pair from `GL_ARB_draw_instanced`/`GL_ARB_instanced_arrays`. **Nothing vendored, nothing downloaded, no absolute paths, no EasyGL/MetaGL, no generated files** |

**Supported:** see `docs/opengl2-backend.md` — all five stock effects + `PbrEffect`/
`SkinnedPbrEffect`, custom GLSL 1.10 `ShaderEffect` (3D + SpriteBatch), FNA-vector fog, FBO
render targets (2D + MSAA + mips + cube), **real MRT (8, with real per-set depth/MSAA
resolve)**, real occlusion queries, `Texture3D`/`TextureCube`, 16/32-bit indices,
`SetDataOptions` orphan/sub-data uploads, software `baseVertex`, full custom
`VertexDeclaration`s, hardware instancing (extension-gated), real
Letterbox/Overscan/Stretch/FixedHeightDynamicWidth, `SetReferenceStencil`, `BlendFactor`,
context-loss recovery, disposal.

**Unsupported — rejects or reports truthfully, never silently:** multi-stream vertex input
(capability false + device refusal + backend throw); instancing without the ARB pair (truthful
capability + deterministic base refusal); cube faces in an MRT set (throws); distinct per-slot
`ColorWriteChannels` under MRT (throws — `glColorMaski` is GL 3.0+); `MultiSampleMask`
(EasyGL/GL1's same gap); MSAA cube RTs (accepted-and-ignored, documented); CPU upload into an
RT-cube face (inherited refusal, shared contract test); `HalfVector2/4` on drivers without
`ARB_half_float_vertex`; Windows/macOS validation (environment-blocked).

---

## 7. One real production finding — found by the shared oracle, fixed in-lane

**Both RenderTarget2D round trips rendered vertically flipped.** The newly-armed shared
wireframe pixel oracle failed with edge BC missing; a frame dump showed the whole RT scene
upside-down, and an asymmetric-quadrant probe proved both halves of the defect: content drawn at
an RT's top-left read back at the bottom-left through `GetData`, **and** re-rendered at the
screen's bottom-left when the RT was sampled as a texture — the post-processing round trip every
XNA game uses. The lane's own 48 suites never saw it because every RT assertion was
orientation-insensitive (centre pixels, solid fills, symmetric scenes).

Fixed as FNA's own convention (`289410a6`): render-time clip-Y flip while a 2D target (single or
MRT) is bound — WVP, custom-shader `Projection` uniforms, both SpriteBatch mappings — with
`glFrontFace(GL_CW)` winding compensation, direct viewport/scissor/`ReadBackbuffer` row mapping
while flipped, and `GetData` unchanged-and-now-correct. **Cube faces deliberately excluded**
(spec-defined orientation; every cube path already self-consistent). The RT test's diagonal-MSAA
scene was re-authored for the corrected orientation.

Unlike the enum-growth adaptation fallout the new-findings rule excludes, this is a genuine
independent production defect in the lane — the first of Batch 1 — caught precisely because the
campaign arms shared pixel oracles at integration.

Two harness-contract collisions beside it, the class the `opengl1` card predicted: **GFX-165**
(four presentation harnesses read pixels after a raw `SDL_SetWindowSize`; now read through the
backend's own `ReadBackbuffer`, the GL1-precedent fix, production untouched), and one build-time
include-chain drift (`<cstring>` no longer transitive in `VertexBuffer.cpp`).

---

## 8. Commit mapping — no original commit disappeared

**All 40 TRANSFERRED** (none split, combined, superseded, deferred or omitted).
`git range-diff ac3aaaeb..archive/preintegration/opengl2-20260804 c0876fca..6f263e3c`:

- **27 of 40 byte-identical (`=`)**.
- The 13 `!` are each accounted for: 4 content pairs = exactly the 4 predicted drift commits
  (`4a3c16b5` six-file registration union, `aa98f433` `GraphicsDevice.cpp` union keeping the
  head's scissor-reset, `75e78a46` `VertexBuffer` context drift from the 3-way merge,
  `abd2883f` `GraphicsCapability` union appending `Instancing` after `MultiStreamVertexInput`);
  9 message-only rewordings (verified by per-pair `+/-` extraction — patches untouched).

**File-level losslessness at the replay boundary: 52 of 63 byte-identical, 0 missing.** The 11
differing are exactly the 11 head-drifted files, each merged against the larger baseline.

The commits beyond the replay:

| Commit | Purpose |
|---|---|
| `fe9630e1` | bounded interface adaptation — all probe drifts, software base-vertex, unified instanced transport, fog vector, declaration dispatch, multi-stream guards |
| `6a87a091` | exhaustive 11-member capability switch + the four cross-backend `Instancing` arms (§5) |
| `6e9a3e7e` | shared-table arming — compile-definition count entry + WireFrame pixel-oracle membership |
| `ffb37ded` | `<cstring>` include (head chain drift; validation-driven) |
| `afec0041` | four presentation harnesses adapted to the GFX-165 contract |
| `c6edf8ba` | `docs/opengl2-backend.md` + README backend entries |
| `289410a6` | the render-target orientation fix (§7) |

---

## 9. Conflicts — the registration union, for the sixth time

Commit 1 conflicted on six files (`.gitignore`, `CMakeLists.txt`, `cmake/BackendLibraries.cmake`,
`cmake/BackendSelection.cmake` ×4 hunks, `GraphicsBackendType.hpp` ×3, `GraphicsDevice.cpp`).
Taking the incoming side would have deleted **STUB, OPENGLES1, OPENGL4, OPENGL1, FREEDIRECT,
DX1/2/5/6/7/8, D3D10** and reverted the DX3→FREEDIRECT rename. Resolved as HEAD's full
26-identity set **plus `OPENGL2`**, token-verified: every pre-existing identity keeps its exact
HEAD count; `OPENGL2` adds 5 tokens in `BackendSelection.cmake` (the per-backend registration
shape). Later conflicts: `GraphicsDevice.cpp` (#18, head's scissor-reset kept alongside the
lane's physical-rect viewport push) and `GraphicsCapability.hpp` (#35, the union append).

---

## 10. Validation

**Runtime identity** (real context, `DISPLAY=:101`, `SDL_VIDEODRIVER=x11`, GPU tests serial):
vendor **Mesa**, renderer **llvmpipe (LLVM 19.1.7, 256 bits)**, version **4.5 (Compatibility
Profile) Mesa 25.0.7-2+deb13u1**, driver GLSL 4.50 — the compatibility context granted for the
2.1 request; the backend's own path is GL 2.1 entry points + GLSL 1.10 by construction.

| Suite | Result |
|---|---|
| All 48 dedicated `OpenGL2_*` CTest suites | **48/48** (serial; first run 44/48 — §7's GFX-165 quartet; one transient `OpenGL2_TextureCube` abort under full-sweep churn passed 3/3 standalone and on re-run) |
| `CnaTests` under `CNA_GRAPHICS_BACKEND=OPENGL2` (source root) | **5737 run · 5730 passed · 6 skipped · 1 failed · 0 not run** (5730+6+1 = 5737 exactly) |
| The 6 skips | 4 sensor-hardware + `WireFrameIsRefusedDeterministically…` (correct — renders) + `Texture3DUnsupportedBackendTest…` (correct — real `Texture3D`; the positive suite ran, 43 OK) |
| The 1 failure | `TwoProcessLoopbackTest.HostMigration…` — the known pre-existing networking flake, **2/6 in isolation this session** (worse than `opengl1`'s 2/3; the Batch-1 stabilization watch item now has a trend) |
| Negative oracles | `OrdinaryDrawMultiStreamTest`/`InstancedDrawMultiStreamTest` rejection arms ran and passed without trusting the capability answers |
| Principal control (shared production files changed): full EasyGL `CnaTests` at the merged head | **5912 run · 5904 passed · 6 skipped · 2 failed** — both failures are the two documented environmental classes and nothing else: the known `TwoProcessLoopbackTest.HostMigration…` networking flake, and the transient `SDL_InitSubSystem(SDL_INIT_VIDEO): x11 not available` blip (this run's victim: `StockEffectContentTypeReaderTest.AlphaTestEffectReaderParsesHandConstructedBytes`, **3/3 in isolation** immediately after — the same different-victim-each-run behaviour the `opengl4`/`opengl1` cards recorded). Against the `opengl1` merged-head baseline of 5912/5904/6/2, **zero regressions attributable to this merge** |
| Merged tree | **byte-identical** to the validated `adapt/opengl2` tree |

**Sanitizers:** dedicated persistent `cmake-build-opengl2-asan` (`CNA_SANITIZE=address,undefined`,
GCC 14.2), eleven representative suites on the real context: **zero AddressSanitizer errors, zero UBSan runtime errors** (smoke, 2d, 3d, effects, rendertarget2d, rendertargetcube, texture3d, texturecube, mrt_depth_msaa, instancedmodel, context_loss_recovery). Every LeakSanitizer stack roots in `libGLX_mesa` — zero CNA frames in any leak report. Control: `ASAN_OPTIONS=detect_leaks=0` → all eleven exit 0 with every check passing (78 PASS lines, 0 FAIL — the documented LSan-swallows-stdout effect handled the GL4/GL1 way).

**Builds:** `cmake-build-opengl2/` and `cmake-build-opengl2-asan/` — new persistent in-repo
directories in the adaptation worktree (no compatible OPENGL2 configuration existed anywhere),
Unix Makefiles, GCC 14.2, ccache ON, `-DCNA_TEST_DISPLAY=:101`, SDL reused read-only from the
`cnaintegration-opengl1` worktree's `.sdl-prebuilt-Linux-x86_64` via `CNA_SDL_PREBUILT_ROOT` (no
SDL rebuild), submodules initialized non-recursively. Nothing under `/tmp`.

---

## 11. Distinction from the other GL lanes (Phase 4) — preserved

- `OpenGL2` is its own enum member, selector token, build option and backend target — no
  collision with `OPENGLES1`, `OPENGL4`, `OPENGL1` or `EASYGL`; token-verified after the union.
- **Not routed through EasyGL** — zero EasyGL/MetaGL files touched; EasyGL requests an
  ES-profile context and cannot create a desktop 2.1 compatibility context.
- **No feature/gl work absorbed**; its public set is unchanged: exactly **OpenGL ES 3, OpenGL 3,
  WebGL 1, WebGL 2**, EasyGL internal and hidden. No OpenGL 3 selector exists at this head.

---

## 12. Residuals

- The networking flake's isolation pass rate degraded to 2/6 this session — the strongest signal
  yet for the Batch-1 stabilization watch item. Not graphics, not lane-owned, net code untouched.
- `GraphicsBackendTypeTest.NameMatchesTypeForEveryBackend` keeps its pre-existing 15-member list
  (unlisted members unasserted) and the README compact selector still omits `OPENGLES1` — both
  remain the stabilization checkpoint's items; this lane added only its own rows.
- `VertexDeclarationLayoutTests.cpp` keeps its own backend list, not armed for OPENGL2 (as for
  the three prior GL lanes); declaration handling is exercised by the lane's own
  customvertexdeclaration suite and the shared oracles.
- MSAA cube render targets, `MultiSampleMask`, per-slot MRT masks, `HalfVector` formats without
  the extension — permanent GL 2.1 boundaries, reported truthfully.
- Windows GL loading exists and compiles but is environment-blocked from execution here.

## 13. New findings

**One, fixed in-lane (§7, `289410a6`):** the vertically-flipped RenderTarget2D round trips — a
genuine independent production defect (silent wrong result on a supported path), distinct from
the enum-growth adaptation fallout the new-findings rule excludes. Found by the shared pixel
oracle the integration campaign arms; per the rule it is recorded here rather than ticketed,
since it was fixed within the lane that owns it.

---

## Post-integration correction (Batch 1 stabilization, 2026-08-05)

Provenance re-verified directly against git: **PROVENANCE CLEAN**. The RenderTarget2D orientation
fix was confirmed as **real production source** (`289410a6`: 95 of 108 changed lines in
`OpenGL2GraphicsBackend.cpp`, an `RtFlipActive()`-gated render-time clip-Y flip with `glFrontFace`
winding compensation), and `GraphicsCapability` growing 10 → **11** was verified at both merge
parents. Two corrections:

- **Four §2.2 process-narration phrases survive into integrated history**: `b4550cf2` ("so a future
  session doesn't have to re-derive it"), `6f1bb99c` ("for a future session to pick up"), `d14ccf2d`
  ("from the previous session's final summary"), `473f119d` ("Also redid the session-8 … audit
  fresh"). Two of these are in bodies the sweep **did** edit — it removed one phrase per body and
  missed a second. None is a §2 attribution violation and none is an F3 `plans/plan_opengl2.md` citation.
  They cannot be corrected without rewriting merged history, which this campaign forbids, so they are
  recorded as a permanent residual.
- The "nine narrative bodies" count is right under both readings, but the nine bodies that *carry*
  narrative and the nine that were *reworded* overlap in only **8** members (`3773f425` carries
  narrative and is byte-identical; `8d17285b` was edited only to qualify a citation).

The Batch 1 "worsening networking flake trend" this card opened (2/6 vs opengl1's 2/3) is **retracted**
— 20 isolated runs measure exactly 50 %, and the trend was small-sample noise. See
`integration/BATCH_1_STABILIZATION.md` §5.
