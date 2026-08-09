# Lane `opengl1` — legacy desktop OpenGL 1.x fixed-function graphics backend

**Status: ✅ INTEGRATED 2026-08-05 · ADAPTATION · merge `c0876fca`** (signed, `--no-ff`,
parents `bc29a976` + `91344935`). Eighth logical lane, **fourth of Batch 1**. Nothing was pushed.

---

## 1. Identity

| Field | Value |
|---|---|
| Ref | `refs/heads/feature/opengl1` = `refs/remotes/origin/feature/opengl1` |
| Original head | `fc14f37b98706befb6c98a713be6dc107c029199` |
| Archive tag | `archive/preintegration/opengl1-20260804` → `fc14f37b`, **verifies good** (RSA `255C69CC…0AADA55F`), unchanged |
| Merge base | `ac3aaaeb` (`origin/develop`) — develop-forked, **896 commits behind** the integration head |
| Own commits / files | **31 / 43** · `+5969, −7` · 32 new, 11 drifted · 0 merges, 0 WIP |
| Shared interfaces | `GraphicsDevice.cpp` + `IGraphicsBackend.hpp` (as predicted) |
| Adaptation branch | `adapt/opengl1` → `91344935` · worktree `/rv/data/development/github.com/openeggbert/cnaintegration-opengl1` (kept) |
| Adapted commits | **37** = 31 replayed + 1 interface adaptation (`6f77c95a`) + 1 capability (`f5c6612f`) + 3 test (`424e5bf9`, `49fa4940`, `cddabbb1`) + 1 docs (`91344935`) |
| Path taken | **ADAPTATION** |

---

## 2. Why not a direct merge — history clean, content stale, one narrative residue

### 2.1 History (conditions 1–4) — the first HISTORY CLEAN lane of Batch 1, re-verified at the object level

`git cat-file -p` over all 31 commits, never `%G?` alone:

| Class | Count |
|---|---|
| Maintainer **PGP**-signed (`BEGIN PGP SIGNATURE`, key `FB9CE8E20AADA55F`) | **31** |
| SSH-signed / genuinely unsigned / non-human authored | **0 / 0 / 0** |

All 31 authored **and** committed by Robert Vokac; zero trailers; zero hits on the exact policy
attribution regex; linear, single-parent, no WIP. **The inventory's 31/31-PGP row is correct.**

### 2.2 Condition 5 — three bodies carry session narrative

`fcfcfc0f` (*"every phase done this session"*), `1499feb7` (*"an independent adversarial-review
fork of tonight's earlier commits"*), `08a0abc0` (*"with explicit user go-ahead"*, *"from earlier
tonight"*). Under the `opengl4` precedent these were minimally reworded at replay — patch content
untouched — so even a content-compatible tree could not have been direct-merged unmodified.

### 2.3 Content (conditions 6–8) — the compile probe found 11 errors, 10 distinct drifts

The `-fsyntax-only` probe of the original backend against the head's `Common/` interfaces:

| Drift | Effect |
|---|---|
| `IVertexBufferBackend::SetVertexDeclaration` pure, takes `VertexDeclaration` | `OpenGL1VertexBufferBackend` **abstract** |
| `IGraphicsBackend::SetRenderTargets` pure, descriptor-based | `OpenGL1GraphicsBackend` **abstract** |
| `ITextureCubeBackend::SetData`/`GetData` void → **bool** | conflicting return types |
| `IRenderTargetBackend::GetData`, `IRenderTargetCubeBackend::GetData` void → **bool** | conflicting return types |
| `ITextureCubeBackend::ShareCpuPixels(int, shared_ptr)` | the lane's **own** interface addition, never adopted at the head — restored by replaying `08a0abc0` into the current interface text |
| `CreateRenderTargetCube` gained `preserveContents` | `override` matched nothing |
| `ApplyBlendState` gained `BlendWriteState` (REMED-GFX-077) | `override` matched nothing |
| `GpuDrawParams::fogStart/fogEnd` → `fogVector[4]` (REMED-GFX-010) | 2 compile errors in the fog block |

**Verified-unchanged where it mattered:** `alphaTest[4]` is byte-identical fork→head (the lane's
`GL_GEQUAL` approximation still reads the right encoding), and the `fc0dd2a2` cost is **zero** —
none of the four removed fields appears anywhere in the lane (no instancing to adapt).

---

## 3. The fog inversion — the OPENGLES1 contract, verbatim semantics

The scalar `glFogf(GL_FOG_START/END, params->fogStart/fogEnd)` block was replaced by
`ApplyFogFromVector()`: the FNA fog vector is inverted against the same `world*view` matrix
`SetupMatrices()` just loaded into `GL_MODELVIEW` (read back via `glGetFloatv`), projecting
`fogVector.xyz` onto that matrix's eye-Z row to recover the scale, then `w` yields
`FogStart`/`FogEnd` — the identical math the ES1 lane validated. All-zero honoured as FNA's
"fog disabled"; `{0,0,0,1}` (degenerate `FogStart == FogEnd`) lands on the fully-fogged
`start=-1, end=0` ramp.

**Oracle:** the lane's fog test asserted only monotonicity, which cannot tell a correct inversion
from a wrong sign, wrong scale or mishandled degenerate. `49fa4940` adds a **three-pair oracle**
against a quad at fixed eye distance 100, each pair with its own expected result: `(200,20000)` →
unfogged pure green; `(50,150)` → exact mid-ramp ~50/50 blend (a 0-or-1 wrong answer cannot
satisfy it); `(100,100)` → fully fogged. All three pass on the real driver. This backend has no
transformed/skinned position paths (no shaders), so object-space geometry is the only fog input.

---

## 4. §1.1 post-audit obligations — decided on evidence

| Obligation | Decision |
|---|---|
| **`REMED-GFX-DECL-GUARD`** | **Applies, and is applied.** The backend selects its immediate-mode emit layout from the vertex stride alone (16/20/24/32 — exactly the guard's target case). `RequireFaithfulDeclarationEXT` now guards **all four** routes (colored ×2, ordinary ×2 — the backend has no separate instanced or DrawUser route), asymmetric, header-only. `PositionOnlyFallback` for unlisted strides — the measured default arm emits position-at-offset-0 with constant white. |
| **`REMED-GFX-209` / WireFrame** | Reports `true` and genuinely rasterizes via desktop `glPolygonMode(GL_LINE)`. Now **pixel-oracle-proven**: `424e5bf9` adds OPENGL1 to the shared `WireFrameTriangleOracle` measured set (the same arming OPENGL4 received). The lane's own capability test additionally cross-checks a `glGetIntegerv(GL_POLYGON_MODE)` readback. |

---

## 5. Capability exhaustiveness — the safest inherited shape of the campaign, made explicit

Unlike ES1 (`default: return true`) and GL4 (no override at all), this lane's fork-era switch had
**no default case and a trailing `return false`** — the two post-fork members (`Texture3D`,
`MultiStreamVertexInput`) already fell through to a truthful false, by accident of shape.
`f5c6612f` makes all ten members explicit with no default case (the GL4 convention): three
structural `true`s, `AnisotropicFiltering`/`OcclusionQuery` from runtime detection,
`MultiSampleAntiAliasing` from what the driver genuinely granted, four structural `false`s.
The shared negative oracles that do not trust the answer under test all run here: the multi-stream
rejection tests, `Texture3DUnsupportedBackendTest` (runs — the false claim that silenced it on ES1
never existed here), and the MRT refusal arm added to `GraphicsDeviceValidationTests`.

---

## 6. Public backend contract (Phase 3)

| Field | Value |
|---|---|
| Public? | **Yes** — enum `CNA::GraphicsBackendType::OpenGL1` (26th member), `-DCNA_GRAPHICS_BACKEND=OPENGL1`, target `cna_backend_graphics_opengl1` |
| Classification | **Historical** — a genuine fixed-function implementation, like OPENGLES1's class |
| API vs driver | Requests a **legacy 1.1 context** (`SDL_GL_CONTEXT_MAJOR/MINOR_VERSION=1/1`, no profile mask) and uses only fixed-function calls — **zero shader entry points in the whole backend**, immediate-mode emission. The host grants a compatibility context reporting its own version (validated: `4.5 (Compatibility Profile) Mesa 25.0.7`, llvmpipe, `:101`); that is the driver's identity, not the backend's API level, and is reported as such |
| Platforms | desktop Linux and Windows only (configure-time `FATAL_ERROR` gate) |
| Dependencies | the platform's own GL via `find_package(OpenGL REQUIRED)` + existing SDL3. 1.2–1.5-era entry points via `SDL_GL_GetProcAddress` — **no loader library, nothing vendored, nothing downloaded, no absolute paths, no EasyGL/MetaGL** |
| Generated files | **none** — entirely fixed-function; `CreateEffectBackend()` keeps the interface `nullptr` default |

**Supported:** device/window/context (GLX visual attribs — depth 24/stencil 8/MSAA — requested
before `SDL_CreateWindow`) · Clear/Present · viewport/scissor (RT-aware Y-flip) · blend incl.
constant colour, equations, separate alpha (core 1.4) · slot-0 `ColorWriteChannels`
(`glColorMask`) · depth/stencil · culling · WireFrame · depth bias · fog (exact inversion) ·
3-light fixed-function lighting + specular + emissive · alpha test (coarse, documented) ·
`Texture2D` (3-tier mip generation, mip-aware filters, anisotropy, Mirror wrap) · `TextureCube` +
`EnvironmentMapEffect` reflection subset · `DualTextureEffect` (GL_COMBINE modulate2x) · CPU-side
vertex/index buffers (16/32-bit) · indexed/non-indexed draws · `DrawUser*` · `SpriteBatch` ·
`RenderTarget2D` (FBO, mip regen on unbind, MSAA + blit resolve, readback) · `RenderTargetCube`
(readback, context-loss aware) · backbuffer MSAA (driver-granted, honestly read back) · occlusion
queries (exact `GL_SAMPLES_PASSED` counts) · virtual resolution/presentation modes · runtime
`SetSwapInterval` · `ReadBackbuffer` · context-loss recovery registry (re-binds the target that
was active at loss) · disposal.

**Unsupported — rejects or reports truthfully, never silently:** custom effects/GLSL,
`SkinnedEffect`/PBR (`CreateEffectBackend` nullptr, capability false); MRT (capability false +
descriptor-route throw, never reduced to the first target); `Texture3D`; instancing/multi-stream;
unfaithful declarations (draw-time guard); CPU upload into an RT-cube face (inherited interface
refusal, asserted by the shared contract test); Fresnel/`EnvironmentMapSpecular` (fixed-function
inexpressible, behaves as disabled); exact `AlphaTestEffect` `CompareFunction` semantics (single
`GL_GEQUAL` approximation); `BlendState.MultiSampleMask` (`GL_SAMPLE_MASK` is GL 3.2+ — EasyGL's
same documented gap).

---

## 7. Distinction from the other GL lanes (Phase 5) — preserved

- `OpenGL1` is its own enum member, selector token, option, and backend target — **no collision**
  with `OPENGLES1`, `OPENGL4`, or `EASYGL`; token-verified after the registration union.
- **Not routed through EasyGL** — zero EasyGL/MetaGL files touched (measured), no shared code;
  EasyGL cannot create a legacy desktop context (ES-profile) and this backend uses no shaders.
- **No OpenGL 2 work absorbed** — every replayed patch is the lane's own; `feature/opengl2` was
  not read, cherry-picked or referenced.
- `feature/gl`'s public set is unchanged: exactly **OpenGL ES 3, OpenGL 3, WebGL 1, WebGL 2**,
  EasyGL internal and hidden.

---

## 8. Commit mapping (Phases 8/9) — no original commit disappeared

**All 31 TRANSFERRED** (none split, combined, superseded, deferred or omitted), plus the
adaptation commits. `git range-diff ac3aaaeb..archive/preintegration/opengl1-20260804
bc29a976..adapt/opengl1`:

- **24 of 31 byte-identical (`=`)**, including the Étoile mangle/restore pair (net-zero across
  the lane, replayed as-is; the asset keeps its correct UTF-8 name).
- The 7 `!` are each accounted for: `05b1be40` (six-file registration union), `fab91cb5`
  (`.gitignore` union), `6287d8ac`/`34288c9b` (capability-test union into the head's `kExpect*`
  idiom — the lane's inline `#ifdef` arms became an OPENGL1 constants arm; its WireFrame rewrite
  is superseded by the head's REMED-GFX-209 per-backend test, whose default arm already answers
  `true` for OPENGL1), `fcfcfc0f`/`1499feb7` (message-only rewording), `08a0abc0` (rewording + the
  `TextureCube::SetData` union: head's REMED-GFX-135 throw/bool contract kept, the lane's CPU
  shadow updated only after an accepted store).

The commits beyond the replay:

| Commit | Purpose |
|---|---|
| `6f77c95a` | bounded interface adaptation — all probe drifts (§2.3), fog inversion (§3), declaration guard (§4) |
| `f5c6612f` | exhaustive explicit `SupportsCapability` (§5) |
| `424e5bf9` | shared-table arming — MRT-refusal arm + WireFrame pixel oracle (§4) |
| `49fa4940` | the three-pair fog oracle (§3) |
| `cddabbb1` | three lane harnesses adapted to post-fork device contracts (§10) |
| `91344935` | `docs/opengl1-backend.md` + README backend entries |

**File-level losslessness at the replay boundary: 32 of 43 byte-identical, 0 missing.** The 11
differing are exactly the 11 files that drifted at the head — every one merged against the larger
baseline, none lost.

---

## 9. Conflicts — the registration union, for the fifth time

Commit 1 conflicted on `cmake/BackendSelection.cmake` (3 hunks) and `GraphicsBackendType.hpp`
(3 hunks); `BackendLibraries.cmake` and `GraphicsDevice.cpp` auto-merged and were verified
hunk-by-hunk. Taking the incoming side would have deleted **`STUB`, `OPENGLES1`, `OPENGL4`,
`FREEDIRECT`, `DX1/2/5/6/7/8`, `D3D10`** and reverted the `DX3 → FREEDIRECT` rename — the lane's
own `DX3` token again means *free-direct*. Resolved as HEAD's full 25-identity set **plus
`OPENGL1`**, token-verified. Later conflicts: `.gitignore` (union), the two shared test files
(head-idiom unions), `TextureCube.cpp` (§8).

---

## 10. Three lane harnesses collided with post-fork contracts — mechanism-diagnosed, not patched around

The first full suite run was 35/38. Each failure was instrumented to its mechanism (`cddabbb1`):

| Test | Mechanism | Class |
|---|---|---|
| `OpenGL1_RenderTarget2D_Mip` | **REMED-GFX-081**: `SpriteBatch::Begin` now applies its FNA-faithful `CullCounterClockwise` through the device property and — real XNA semantics — it persists after `End()`. The test's one-time `CullNone` was overwritten and its quads (authored under CullNone) were entirely culled — proven by an in-backend probe: correct matrices/viewport/fill/masks, zero fragments framebuffer-wide, `GL_CULL_FACE` on. Re-asserts `CullNone` where its 3D draws need it | test-contract |
| `OpenGL1_PresentationMode` | **GFX-165**: `GetBackBufferData` validates against `PresentationParameters`, which the test's raw `SDL_SetWindowSize` deliberately does not update. Now reads the framebuffer row it actually wants via the backend's own `ReadBackbuffer` | test-contract |
| `OpenGL1_SwapInterval` | the display's GLX no longer exposes swap control — **control-proven with a CNA-independent raw-SDL probe** (`SDL_GL_SetSwapInterval` fails with *"That operation is not supported"* for 0 and 1 alike). When the direct SDL call refuses too, the vsync half is an honest skip; the interval-0 half still proves the override forwards | environment |

**Production code needed no change for any of the three.** After `cddabbb1`: **38/38**.

---

## 11. Validation

**Runtime identity** (real context on `DISPLAY=:101`, `SDL_VIDEODRIVER=x11`, GPU tests serial):
vendor **Mesa**, renderer **llvmpipe (LLVM 19.1.7, 256 bits)**, version **4.5 (Compatibility
Profile) Mesa 25.0.7-2+deb13u1** — a compatibility context whose fixed-function pipeline is what
the backend actually drives (zero shader entry points; the backend's own log prints `GL 4.5` with
its detected 1.2–1.5-era feature set). Reported honestly as the host driver's identity, not the
backend's API level.

| Suite | Result |
|---|---|
| All 38 dedicated `OpenGL1_*` CTest suites | **38/38** (serial) |
| Capability oracle (`OpenGL1_GraphicsCapability`) | passes with independent cross-checks: `glGetIntegerv(GL_POLYGON_MODE)` readback, `GL_EXTENSIONS` scans, a real occlusion query counting exactly 512 unoccluded pixels |
| Fog three-pair oracle | all three pairs produce their own expected result (§3) |
| `CnaTests` under `CNA_GRAPHICS_BACKEND=OPENGL1` | **5737 run · 5692 passed · 44 skipped · 1 failed · 0 not run** (5692+44+1 = 5737 exactly) |
| The 44 skips | **39** `Texture3DTest` (the positive suite skips because this backend truthfully reports no `Texture3D` — its unsupported-backend mirror ran) + **4** sensor-hardware skips + **1** `WireFrameIsRefusedDeterministically…` (correct — this backend renders) |
| The 1 failure | `TwoProcessLoopbackTest.HostMigration…` — the known pre-existing two-process networking flake (2/3 in isolation this session, consistent with the `opengl4` card's under-load observation). Not graphics, not a regression |
| Principal control (shared production files changed): full EasyGL `CnaTests` at the merged head | **5912 run · 5904 passed · 6 skipped · 2 failed** — both failures are the two documented environmental classes and nothing else: the known `TwoProcessLoopbackTest.HostMigration…` networking flake, and the transient `SDL_INIT_VIDEO: x11 not available` blip (this run's victim: `XnbBuiltInReaderRegistrationTest.RegistersEveryOtherBuiltInReader`, **3/3 in isolation** immediately after — the same different-victim-each-run behaviour the `opengl4` card recorded). Against the Batch-0/GL4 baseline of 5905/6/1, **zero regressions attributable to this merge** |
| Merged tree | **byte-identical** to the validated `adapt/opengl1` tree (`HEAD^{tree}` equality), so every result above applies to the merged head by construction |

One measurement error reproduced from the `stub` card before the numbers above: running the
`CnaTests` binary from the build directory produces ~118 spurious Media/Video/Audio/Xnb
fixture-path failures and the documented `MediaLibraryTestFixture` SIGSEGV — artifacts of the
wrong working directory. The reading above is from the source root, where `tests/assets` resolves.

**Sanitizers (gate option A):** dedicated persistent `cmake-build-opengl1-asan`
(`CNA_SANITIZE=address,undefined`, GCC 14.2), nine representative suites on the real context:
fog, context-loss, rendertarget2d sample/mip/msaa, rendertargetcube, mipmap generation,
samplerstate bind-order, graphics capability. **Zero AddressSanitizer errors, zero UBSan runtime
errors.** Every LeakSanitizer stack roots in `libGLX_mesa` — zero CNA frames in any leak report.
Control: `ASAN_OPTIONS=detect_leaks=0` → all nine exit 0 with every check passing (11/11, 7/7,
2/2, 3/3, 4/4, 2/2, 2/2, 13/13 + the sample suite's own pass output), proving the checks execute
under instrumentation (the GL4-recorded LSan-swallows-stdout effect handled the same way)

**Builds:** `cmake-build-opengl1/` and `cmake-build-opengl1-asan/` — new persistent in-repo
directories in the adaptation worktree (no compatible OPENGL1 configuration existed anywhere —
searched every `cmake-build-*` cache), Unix Makefiles, GCC 14.2, ccache ON,
`-DCNA_TEST_DISPLAY=:101`. Submodules initialized non-recursively from the repository's own
`.git/modules` store (no re-clone). Nothing under `/tmp`.

---

## 12. Residuals

- `GraphicsBackendTypeTest.NameMatchesTypeForEveryBackend` keeps a 15-member switch missing every
  backend since SDL_GPU (incl. OPENGLES1/OPENGL4) — a pre-existing head-side gap neither prior GL
  lane armed; unlisted members are unasserted, not failing. Left per precedent; owner: the
  Batch-1 stabilization checkpoint, together with the README compact-selector `OPENGLES1`
  omission the `opengl4` card already handed there.
- `VertexDeclarationLayoutTests.cpp` keeps its own backend list, not armed for OPENGL1 (as for
  OPENGLES1/OPENGL4); the guard is exercised by the shared capability oracle set instead.
- The display's GLX swap-control refusal (§10) is an environment property; the vsync half of
  `OpenGL1_SwapInterval` skips honestly until run on a display with real vblank support.
- Exact `AlphaTestEffect` semantics, Fresnel/`EnvironmentMapSpecular`, MRT, `Texture3D`,
  instancing, custom effects — permanent fixed-function boundaries, reported truthfully.
- Windows validation — environment-blocked here (Linux sandbox), as for every desktop lane.

## 13. New findings

**None.** Nothing found in this lane is an independent production defect: the interface drifts
are expected stale-fork adaptation, the capability work is the enum-growth class the new-findings
rule excludes, the three harness failures are post-fork test-contract collisions with production
code proven correct, and the swap-control refusal is an environment limitation, control-proven.

---

## Post-integration correction (Batch 1 stabilization, 2026-08-05)

Provenance re-verified directly against git: **PROVENANCE CLEAN**. One correction:

- §2.2 lists "with explicit user go-ahead" among the three narrative phrases and says all three were
  reworded at replay. That phrase was **reworded, not removed**: adapted `99f041a1` still reads
  "with the project owner's explicit go-ahead for the cross-cutting interface change this was
  previously deferred over". The time-of-production markers ("this session", "tonight's earlier
  commits", "from earlier tonight") are genuinely gone; what survives is a human-approval rationale
  carrying no authorship claim and no session identifier. Defensible under §2.1/F1 — the defect is
  **disclosure**, not content.

The fixed-function proof is confirmed independent of the host's reported `4.5 (Compatibility
Profile)` string: **zero shader entry points** anywhere in the backend, `1/1` context request with no
profile mask.
