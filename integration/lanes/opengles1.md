# Lane `opengles1` — OpenGL ES 1.1 fixed-function graphics backend

**Status: ✅ INTEGRATED 2026-08-05 · ADAPTATION · merge `df6b7cc6`** (signed, `--no-ff`,
parents `99ae7d11` + `b811d76d`). Sixth logical lane, **second of Batch 1**. Nothing was pushed.

---

## 1. Identity

| Field | Value |
|---|---|
| Ref | `refs/heads/feature/opengles1` = `refs/remotes/origin/feature/opengles1` |
| Original head | `3d576da20cbadf87d826f01e2b93eeca6dd01629` |
| Archive tag | `archive/preintegration/opengles1-20260804` → `3d576da2`, **verifies good** (RSA `255C69CC…0AADA55F`), unchanged |
| Merge base | `ac3aaaeb` (`origin/develop`) — develop-forked, **835 commits behind** the integration head |
| Own commits / files | **26 / 23** · `+6607, −4` · 17 new, 6 pre-existing |
| Shared interfaces | `GraphicsDevice.cpp` only (as predicted) |
| Adaptation branch | `adapt/opengles1` → `b811d76d` · worktree `/rv/data/development/github.com/openeggbert/cnaintegration-opengles1` (kept) |
| Adapted commits | **31** = 26 replayed + 1 interface adaptation + 4 validation-driven |
| Path taken | **ADAPTATION** |

---

## 2. Why not a direct merge — both halves failed

`stub` taught that a clean history is not a compatible tree. This lane failed on **both** counts,
which is why the two were checked independently.

### 2.1 History (conditions 3, 4, 5)

Object-level inspection, not `%G?`:

| Class | Count | Detail |
|---|---|---|
| Maintainer **PGP**-signed | **24** | authored *and* committed by Robert Vokac, **zero trailers**, zero attribution |
| **SSH**-signed, non-maintainer | **2** | `cc4c39c0`, `67cfcc5d` — ed25519 `rLzsfFISF4by…`, non-human author *and* committer, **both** prohibited trailers each |
| Genuinely unsigned | **0** | — |

`%G?` reported `N` for the two SSH commits, exactly the trap recorded on the `ext` lane. Git emitted
the `gpg.ssh.allowedSignersFile` error precisely twice, which is the real tell; `git cat-file -p`
settled it.

**The two are the lane's *first* two commits**, so all 24 clean commits descend from them. There was
no way to take the good history without the bad. Zero merges, zero WIP/fixup subjects.

### 2.2 Content (conditions 6, 7, 8) — the compile probe earned its place

Source inspection found the two pure-virtual breakages. **The compiler found four more that no
signature grep would have caught**, because they are *return-type* changes:

| Interface change | Effect on the lane |
|---|---|
| `IVertexBufferBackend::SetVertexDeclaration` now **pure**, `std::vector<VertexElement>` → `VertexDeclaration` | `OpenGLES1VertexBufferBackend` **abstract** |
| `IGraphicsBackend::SetRenderTargets` now **pure**, `IRenderTargetBackend* const*` → `RenderTargetBindingDescriptor` | `OpenGLES1GraphicsBackend` **abstract** |
| `ITextureBackend::GetData` `void` → **`bool`** | conflicting return type |
| `ITextureCubeBackend::SetData` `void` → **`bool`** | conflicting return type |
| `IRenderTargetBackend::GetData` `void` → **`bool`** | conflicting return type |
| `IRenderTargetCubeBackend::GetData` `void` → **`bool`** | conflicting return type |
| `CreateRenderTargetCube` gained `preserveContents` | `override` matched nothing |
| `ApplyBlendState` gained `BlendWriteState` (REMED-GFX-077) | `override` matched nothing |
| `GpuDrawParams::fogStart`/`fogEnd` → `fogVector[4]` (REMED-GFX-010) | 4 compile errors in `ApplyFog` |

`IGraphicsBackend.hpp` grew **1023 → 1788 lines** between the fork point and the head.

**`GpuDrawParams` cost was otherwise zero** — none of the four `fc0dd2a2`-removed fields
(`instanceVb`, `instanceVertexOffset`, `instanceFrequency`, `vertexBufferOffset`) appears anywhere
in the lane.

---

## 3. The fog change was the only one with real semantics behind it

REMED-GFX-010 replaced the scalar `fogStart`/`fogEnd` with FNA's fog **vector**, which every shader
backend dots against the object-space position. **A fixed-function pipeline has no such dot
product** — `glFog` wants the two scalars back, and ES 1.1 has no fog-coordinate extension to route
around it.

They are **recovered exactly, not approximated**. The vector is built from those very scalars and
from the same `world*view` matrix the draw loads into `GL_MODELVIEW` immediately before `ApplyFog`:

```
fogVector.xyz = {M13,M23,M33} * scale     fogVector.w = (M43 + fogStart) * scale
scale         = 1 / (fogStart - fogEnd)
```

Projecting `fogVector.xyz` back onto that matrix's own eye-Z row recovers `scale`; the `w` term then
yields `fogStart`, and `fogEnd` follows. Degenerate and disabled cases are honoured as FNA encodes
them — `{0,0,0,1}` is `fogStart == fogEnd` (fully fogged, the ramp `OPENGLES1-82` introduced), and
all-zero is fog disabled.

**Evidence it is right, not merely compiling:** the lane's own fog test exercises **three distinct
pairs** — `0/1`, `10/20`, and the degenerate `5/5` — and all three produce their own correct result
on a real driver. A wrong inversion could not pass all three.

---

## 4. One independent production defect — found by the drift, not introduced by it

**`SupportsCapability` was claiming two capabilities the backend does not have.**

`GraphicsCapability` grew from **8 members to 10** after the fork (`Texture3D` and
`MultiStreamVertexInput` are both post-fork additions). The switch handles the original 8 and ends
in `default: return true`, so both new members were answered with a confident, wrong **`true`**.

It was not theoretical:

- `Texture3DTest`'s fixture skips on a backend reporting no `Texture3D`. Because this backend
  claimed it, the fixture **ran** and four tests failed inside `Texture3D::SetDataPointerEXT`.
- `Texture3DUnsupportedBackendTest` — the mirror check that exists precisely to catch this —
  **skipped**, because the false claim silenced its own detector.

Both are now answered explicitly and truthfully. This is the failure mode a catch-all `default` has
whenever an interface grows: **the backend answers for members it has never heard of.** Worth
carrying to every remaining lane, all of which fork from bases the head has since outgrown.

> Reported here rather than as a new ticket: it is old-API adaptation fallout, which
> `INTEGRATION_ORDER.md`'s new-findings rule explicitly excludes from ticketing.

---

## 5. `TextureCube` readback — implemented, not declared absent

`OpenGLES1TextureCubeBackend` inherited `ITextureCubeBackend::GetData`'s `return false`, which
REMED-GFX-130 turns into a `System::NotSupportedException`. **That refusal was untrue** — this
backend's `SetData` genuinely uploads all six faces, so it really does own cube pixels. The shared
suite's own premise says it plainly: *storage support and readback support are the same set.*

Implemented with the technique the lane already uses **twice** (`OPENGLES1-89` for `Texture2D`,
`OPENGLES1-84` for the render-target cube): attach the face to a scratch framebuffer, `glReadPixels`.

**It deliberately does not flip Y**, unlike `Texture2D::GetData`. This class's `SetData` hands raw
`x`/`y` straight to `glTexSubImage2D`, i.e. it works in GL's bottom-up space; reading back through
that same space is what makes a round trip exact. **Measured, not assumed** — the flip failed three
sub-rectangle tests that pass without it.

**Level 0 only, and that is a real boundary, not an omission:** `GL_OES_framebuffer_object` requires
an attached texture's level to be 0, so no mip level above 0 can be attached and therefore none can
be read back. Storage of the whole declared chain still works, since `glTexImage2D` takes the level
directly.

---

## 6. §1.1 post-audit obligations — decided on evidence

| Obligation | Decision |
|---|---|
| **`REMED-GFX-DECL-GUARD`** | **Applies, and is now applied.** This backend selects its fixed-function pointer layout from the buffer **stride** (16/20/24/32) — precisely the case the guard exists for. The declaration is remembered and checked at draw time on **all four** routes. Asymmetric: only what the caller declared is verified, never equality against the backend's own template. Helper is **header-only**, as the backend static library cannot see `src/CNA/**.cpp`. Modelled on D3D9, the closest fixed-function analogue. |
| **`REMED-GFX-209` / `WEBGPU-115`** | **Already satisfied.** `WireFrame` reports `true` and is genuinely implemented by `GL_LINES` re-expansion, so the "refuse polygon topologies" half has no subject. |

---

## 7. Public backend contract (Phase 3)

| Field | Value |
|---|---|
| Public? | **Yes** — a real user-selectable backend |
| Enum identity | `CNA::GraphicsBackendType::OpenGLES1` (24th member) |
| Build option | `-DCNA_GRAPHICS_BACKEND=OPENGLES1` / `CNA_BACKEND_OPENGLES1` |
| Backend target | `cna_backend_graphics_opengles1` |
| Classification | **Historical** — a genuine ES 1.1 fixed-function implementation, deliberately independent of EasyGL |
| Dependencies | **System** `libGLESv1_CM` + `GLES/gl.h`,`GLES/glext.h` (Debian: `libgles1`, `libgles-dev`). Hard `find_library`/`find_path` gate with `FATAL_ERROR`, same shape as Vulkan's `find_package(Vulkan REQUIRED)`. **Nothing vendored, nothing downloaded, no absolute local paths.** SDL3 supplies the window/context. |
| Generated files | **None.** Entirely fixed-function — no shaders, no lookup tables, no generated blobs. `CreateEffectBackend()` keeps `IGraphicsBackend`'s `nullptr` default. |

### Supported

Device/window/context lifecycle · `Clear` · `Present` · viewport · scissor · blend state
(incl. separate alpha, Min/Max via `GL_EXT_blend_minmax`) · depth/stencil · culling · **WireFrame**
(`GL_LINES` re-expansion) · sampler filter/address + **anisotropy** · `Texture2D` incl. **`GetData`**
· vertex/index buffers (**16- and 32-bit**) · non-indexed and indexed draws · `DrawUser` ·
`SpriteBatch` · fixed-function transforms/lighting/**fog**/alpha-test · `DualTextureEffect` ·
`EnvironmentMapEffect` (reflection texgen) · `RenderTarget2D` incl. **mip generation** and `GetData`
· `RenderTargetCube` · `TextureCube` incl. **level-0 `GetData`** · backbuffer **MSAA** ·
resize/reset · context-loss restore · disposal.

### Unsupported — every one rejects deterministically, none silently

Custom `ShaderEffect`/GLSL · `SkinnedEffect`/`SkinnedPbrEffect` · `PbrEffect` · instancing ·
**multiple render targets** (refused, not reduced to the first) · `Texture3D` · `OcclusionQuery`
(confirmed impossible in the CM registry) · **multi-stream vertex input** · cube readback above mip
level 0 · `BlendFactor`/`InverseBlendFactor` · two-sided stencil · depth bias · render-target
multisampling · compressed formats · declarations the stride dispatch cannot represent.

**No silent fallback, no process abort, device stays usable.** `SupportsCapability` reports `false`
for `CustomEffects`, `MultipleRenderTargets`, `OcclusionQuery`, `Texture3D`,
`MultiStreamVertexInput`; `MultiSampleAntiAliasing` and `AnisotropicFiltering` are answered from
what the driver actually granted.

---

## 8. Distinction from `feature/gl` (Phase 4) — preserved

- **`OpenGLES1` is its own public enum member and its own build option.** No collision with
  `EasyGL` / `CNA_BACKEND_EASY_GL`.
- **It is not an EasyGL alias and is not routed through EasyGL.** EasyGL targets WebGL2/ES 3.0
  (shader-based) and *cannot create an ES 1.1 context at all*; there is no shared code.
- **This lane touches zero EasyGL or MetaGL files** — measured, not assumed.
- `feature/gl`'s public set is unchanged and still exactly **OpenGL ES 3, OpenGL 3, WebGL 1,
  WebGL 2**, with **EasyGL internal and hidden**. No OpenGL ES 3 selector exists yet, so no
  ES 1 / ES 3 ambiguity is possible at this head.

---

## 9. Commit mapping — no original commit disappeared

**All 26 TRANSFERRED.** None split, combined, superseded, deferred or omitted.

`git range-diff ac3aaaeb..archive/preintegration/opengles1-20260804 99ae7d11..adapt/opengles1~5`:

- **24 of 26 byte-identical (`=`)**
- `cc4c39c0 → ec51bf6b` (`!`): author rewritten under A2, both trailers stripped, **plus the
  recorded registration union** (§10)
- `67cfcc5d → 0e69cfd3` (`!`): author + trailers **only** — zero patch-content change

**File-level losslessness:** all 23 original files present at the adapted head, **zero missing**;
15 blob-identical. The 8 that differ are each accounted for — 3 registration unions, 3 clean merges
against a larger baseline, the backend `.hpp`/`.cpp` adaptation, and the documentation correction.

The five commits beyond the replay:

| Commit | Purpose |
|---|---|
| `1c67139a` | bounded interface adaptation (§2.2) + fog inversion (§3) + declaration guard (§6) |
| `0e3c989c` | `TextureCube::GetData` level-0 readback (§5) |
| `f56a4f8e` | the two false capability claims (§4) |
| `a0d07e88` | seven shared **test** files armed (§10) |
| `b811d76d` | documentation corrected against the code (§11) |

---

## 10. Conflicts — the registration union, for the third time

Only `cc4c39c0` conflicted, on `.gitignore`, `CMakeLists.txt` and `cmake/BackendSelection.cmake`.

**Taking the incoming side would have deleted `STUB`, `FREEDIRECT`, `DX1`, `DX2`, `DX5`, `DX6`,
`DX7`, `DX8` and `D3D10`** — and the lane's own `DX3` token means *free-direct*, which the head has
since renamed `FREEDIRECT`; it is **not** the head's real DirectX 3. Resolved as HEAD's full list
**plus `OPENGLES1`**, then verified token-by-token that all 11 survive. The resolved file is
**purely additive** against the head.

Seven **test** files were also armed — the `stub` precedent, no production defect in any of them:
`GraphicsBackendCompileDefinitionTests` (no OPENGLES1 entry ⇒ the "exactly one backend" count came
out 0), `GraphicsDeviceCapabilityTests` (3 hardcoded `EXPECT_TRUE`), `GraphicsDeviceValidationTests`
(`SetRenderTargets_FourTargets`, a **third** distinct case beside single-target backends and Stub),
`TextureCubeTests` (`kCubeMipReadbackSupported` split out —
`Texture3DTextureCubeContentTypeReaderTests` had **already** made exactly this three-way split, so
this brings the two files back into agreement rather than inventing a convention), and the two
`Cnj*Tests` (gated on `SupportsCapability(CustomEffects)`, matching `CnjTexture3DTests`' own idiom).

---

## 11. Documentation was stale against its own lane

`docs/opengles1-backend.md` was last touched by `ddcbcd0e`, and **eleven later commits in the same
lane** implemented things it still listed as gaps. Every claim was re-checked against the final
source, not against commit subjects. Corrected: MSAA, anisotropic filtering, `RenderTargetCube`,
render-target mip generation, per-vertex `Color` × `DiffuseColor`, 32-bit indices,
`Texture2D::GetData`, and `Min`/`Max` blend. Added: the new refusals and boundaries from §4–§6.

---

## 12. Validation

**Real OpenGL ES 1.1, not a desktop-GL fallback** — every run reported
`OpenGL ES-CM 1.1 Mesa 25.0.7 (git-742a20f48c)`, softpipe, on `DISPLAY=:101`, via the lane's own
`scripts/opengles1-test-env.sh` against `~/deps/mesa-es1-install` (Debian's stock Mesa is built
`-Dgles1=disabled`, the root cause the lane itself established).

| Suite | Result |
|---|---|
| `cna_test_opengles1_clear_readback` | **5/5** |
| `cna_test_opengles1_render_state` | **12/12** |
| `cna_test_opengles1_lighting_fog_alphatest` | **10/10** |
| `cna_test_opengles1_buffers_rendertarget` | **11/11** |
| `cna_test_opengles1_multitexture_contextloss` | **9/9** |
| `cna_test_opengles1_resolution_statetoggles` | **9/9** |
| `cna_test_opengles1_viewport_scissor` | **7/7** |
| **Lane total** | **63/63**, run serially |
| `CnaTests` under `CNA_GRAPHICS_BACKEND=OPENGLES1` | **5733 run · 5689 passed · 43 skipped · 1 failed** — see §12.1 |

**Failures went 22 → 1.** The remaining one is
`TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses`
— a two-process **networking** test that times out under load, **passes 3/3 in isolation**, and did
not fail in this suite's first run. Not graphics, not a regression.

The merged tree is **byte-identical** to the validated `adapt/opengles1` tree, so the merge
introduced nothing that was not built and tested.

### 12.1 Corrected 2026-08-05 — the skip count, reconciled from the raw logs

The row above originally read *"5733 run · 5689 passed · 87 skipped · 1 failed"* — numbers that do
not add up (5689 + 87 + 1 ≠ 5733). Reconciled from the preserved raw gtest logs rather than
re-derived arithmetically:

| Quantity | Value | Evidence |
|---|---|---|
| Tests registered in the binary | **5733** | `[==========] Running 5733 tests from 486 test suites.` — zero `DISABLED` tests, no `--gtest_filter`, so registered = selected |
| Tests selected | **5733** | same banner |
| Tests executed | **5733** | `[==========] 5733 tests from 486 test suites ran.` |
| Passed | **5689** | `[  PASSED  ] 5689 tests.` |
| Failed | **1** | `[  FAILED  ] 1 test` — `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses` |
| Skipped | **43** | `[  SKIPPED ] 43 tests, listed below:` |
| Not run | **0** | 5689 + 43 + 1 = 5733 exactly |

**Where "87" came from:** it is a *log-line* count, not a test count. Each skipped test prints
`[  SKIPPED ]` twice (once inline, once in the final enumeration) and the aggregate header line
matches the same pattern — 43 × 2 + 1 = **87** lines. Counting grep hits instead of reading the
aggregate line is the measurement error; the aggregate lines of all three runs are internally exact.

**The suite was run three times**, all on `DISPLAY=:101` against the Mesa ES1 driver, and every run
executed all 5733:

| Run | Finished | Passed / Skipped / Failed | The failure(s) |
|---|---|---|---|
| 1 (`cnatests_run.log`) | 06:53 | 5706 / 5 / **22** | the false-capability fallout (§4) — Texture3D/TextureCube/Cnj/capability tests |
| 2 (`cnatests_run2.log`) | 07:04 | 5689 / 43 / **1** | `Texture3DTextureCubeContentTypeReaderTest.TextureCubeReaderLoadsRealMonoGameFixtureEndToEnd` |
| 3 (`cnatests_run3.log`) | 07:06 | 5689 / 43 / **1** | the two-process networking test; the run-2 failure passed after the `tb5` incremental rebuild picked up the test-contract change later committed as `a0d07e88` |

Run 3 is the authoritative final reading; `net_retry.log` (07:07) shows the networking test passing
**3/3** in isolation immediately afterwards. Raw logs are preserved in
`cnaintegration-opengles1/cmake-build-opengles1/preserved-validation-logs/` (three full-suite runs,
the isolation re-run, and six of the seven harness logs; the `clear_readback` harness log was not
redirected to a file and only its 5/5 summary survives in this card).

**Sanitizer disposition — explicit, previously unrecorded:** sanitizer validation **did not run**
for this lane. No sanitizer-configured OPENGLES1 build tree exists anywhere, `CNA_SANITIZE` is empty
in the only OPENGLES1 cache, and none of the preserved logs contains sanitizer output. This was an
omission, not a platform limitation — the backend is native Linux/GCC and instrumentable. It is
recorded here as a predecessor gap; shared native paths receive ASan/UBSan coverage in later lanes'
sanitizer gates.

**Build:** `cmake-build-opengles1/` (new persistent in-repo directory — no compatible OPENGLES1
configuration existed anywhere), Unix Makefiles, GCC 14.2.0, **ccache ON**, incremental. No build
under `/tmp`, `/var/tmp` or `/dev/shm`.

---

## 13. Residuals

- Cube readback above mip level 0 — permanent ES 1.1 boundary (§5).
- `Texture3D`, occlusion queries, MRT, instancing, shaders — permanent, now reported truthfully.
- Render-target multisampling and compressed formats — not implemented.
- The flaky two-process networking test is environmental and predates this lane.

**New findings: one, recorded in §4 and fixed in-lane** (false `Texture3D` /
`MultiStreamVertexInput` capability claims). It is adaptation fallout from a grown enum rather than
an independent production defect, so per the new-findings rule it is **not** ticketed.

---

## Post-integration correction (Batch 1 stabilization, 2026-08-05)

Provenance re-verified directly against git: **PROVENANCE CLEAN**. Four corrections:

- §1 says "17 new, 6 pre-existing"; measured **16 new, 7 pre-existing** (23 total).
- §9 says "15 blob-identical, 8 differ"; measured **14 identical, 9 differ**. The card's own
  enumeration of causes sums to 9, so the enumeration is right and the counts are wrong.
- The retracted "87 skipped" figure **survives in two immutable commit bodies** — `a0d07e88` and
  merge `df6b7cc6`. Post-merge commits cannot be amended under this campaign's rules, so the
  correction can only ever be recorded, never applied to history. The corrected arithmetic
  (5733 = 5689 + 43 + 1) was **reproduced exactly at merged HEAD** this session.
- The merged tree still ships an inherited "UBSan clean" claim in `NEXTopengles1.md` that this
  card's own sanitizer disposition cannot verify.

The sanitizer disposition itself is **honest and stands** — it names the omission, refuses the
platform-limitation excuse, and argues against itself.
