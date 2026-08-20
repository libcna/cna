# Lane card — `wicked` (Wicked Engine) · ✅ **INTEGRATED 2026-08-05** · merge `683a00a5` — the tenth lane

> **Outcome. Both blockers were repaired in-lane and the lane merged the same day.** The follow-up
> session resumed exactly where the record below left off: it reproduced `WICKED-77` and
> `WICKED-78` from the preserved reproducers, root-caused both, fixed both, completed the
> previously interrupted full `CnaTests` build incrementally (no clean, no reconfigure, the 244
> preserved objects reused), and ran the complete validation matrix. First contact with the FULL
> corpus then surfaced two further defects — `WICKED-79` (staged texture uploads smeared at narrow
> widths) and a corpus-composition break (the lane's backend-local test directory globbed into
> every other backend's `CnaTests`) — both also fixed in-lane. `adapt/wicked` closed at
> **17 signed commits** (the 12 below plus five: `4c1dadd4` WICKED-78, `9f820697` WICKED-77,
> `4449daaa` WICKED-79, `de70722f` shared-contract armings, `97d5a644` corpus-glob fix) and merged
> as **`683a00a5`** (signed, `--no-ff`, zero conflicts, merged tree byte-identical to the
> validated adaptation). **Nothing was pushed; no other lane was begun.** §13 records the exact
> root causes; §15 records the completion-session validation.
>
> The earlier MERGE BLOCKED record below is retained unrevised as the historical state the
> completion session started from.

| Field | Value |
|---|---|
| Logical lane | `wicked` |
| Refs | **`refs/remotes/origin/claude/wicked-engine-cna-backend-5ffqzd` — remote-only**, no local branch |
| Original head | `91d8587e9a1a760c3275713f15f65bfafa387082` — unchanged locally **and** on `origin` |
| Archive tag | `archive/preintegration/wicked-20260804` → `91d8587e` · GPG **Good** · unchanged |
| Real fork point | **`2338b44f`** on `feature/audit` — audit-stacked |
| Merge base with `develop` | `ac3aaaeb` |
| Own commits / files | **10 / 16** · `+6753, −4` |
| Adaptation branch / head | `adapt/wicked` → **`97d5a644`** (worktree `cnaintegration-wicked`, retained) |
| Adapted commits | **17** (10 replayed + 1 obligation + 1 oracle arming + 5 completion-session fixes) |
| Merge commit | **`683a00a5`** — signed, `--no-ff`, tenth lane merge |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED — total (10/10)** |
| Conflict class | LOW — confirmed |
| Path taken | **ADAPTATION** |

---

## 1. The 10-versus-24 discrepancy — resolved by measurement

The inventory said "10 own commits / 16 files"; `NEXT.md` line 41 said a "**24-commit
history-recreation** classification"; the lane card said "Rebase **24 commits** onto the
checkpoint". Only one of those is a commit count of *work*.

| Figure | What it actually measures | Value |
|---|---|---|
| **Own commits** — the metadata-recreation task | `git rev-list --count 2338b44f..91d8587e` | **10** |
| Inherited from `feature/audit` | `ac3aaaeb..2338b44f` | **755** |
| **Behind the phase-1 checkpoint** ← *this is the "24"* | `2338b44f..d79214e7` | **24** |
| **Behind the current integration head** | `2338b44f..ed607602` | **230** |
| Ahead of `develop` | 755 inherited + 10 own | **765** |

The arithmetic closes exactly: **755 + 10 = 765**. `git rev-list --left-right --count
d79214e7...91d8587e` returns `24  10` — a **behind/ahead pair**, and "24" is the *behind* half. It
was read as a commit count of work and propagated into `NEXT.md`.

**Two corrections follow.** The history recreation is **10 commits, not 24**. And the behind-count
is no longer 24 either: Batch 0 and Batch 1 added 205 commits after it was measured on 2026-08-04,
so the lane is **230 behind the head it actually merges into**. Both are corrected in
`INTEGRATION_ORDER.md`, `INTEGRATION_BRANCH_INVENTORY.md` and `NEXT.md`.

**All 755 inherited commits are already ancestors of the integration head**, so they cost nothing.
No merges exist in the unique range (0), and no WIP/fixup subjects.

---

## 2. History classification — re-verified at the object level

`%G?` reports `N` for all ten, and the inventory recorded "GPG-signed 0/10". Both are true and both
are misleading: `git cat-file -p` shows **all ten carry `gpgsig -----BEGIN SSH SIGNATURE-----`**
from one ed25519 non-maintainer key (`AAAAC3NzaC1lZDI1NTE5AAAAIKy87HxSEheG8vEPhSs9u2KZCtVErAQfpmprtUJCZ2w7`).
The `gpg.ssh.allowedSignersFile` error firing **exactly ten times** is the tell.

| Class | Count |
|---|---|
| Maintainer PGP-signed | **0** |
| Non-maintainer **SSH**-signed | **10** |
| Genuinely unsigned | **0** |
| Authored **and** committed by `Claude <noreply@anthropic.com>` | **10 / 10** |
| `Co-Authored-By:` trailers | **10** |
| `Claude-Session:` trailers | **10** (one session identifier) |
| Merges · WIP/fixup | 0 · 0 |

This is the fourth lane where the `ext` lesson holds: **`%G?` cannot distinguish "unsigned" from
"SSH-signed and uncheckable"**, and the required action is unchanged only by luck.

### 2.1 Attribution and narrative sweep

Multiline-aware (whitespace-collapsed before matching, so `this\nsession` cannot hide):

- **Prohibited, removed:** 10 × `Co-Authored-By: Claude Opus 5`, 10 × `Claude-Session:` URL,
  10 × non-human author **and** committer.
- **§2.2 process narration, removed:** one sentence in `602047e9` — *"the edit that carried them
  was run from the wrong directory and silently did not reach the files"*.
- **Session-scoped prose, reworded:** `f24e36ca`'s *"this environment has no GPU, Vulkan loader or
  display"* and `91d8587e`'s *"cannot be verified anywhere in this environment"*.
- **ALLOWED, kept:** the single `handoff` hit is technical — *"cube type and face survive the
  normalized handoff"* describes descriptor passing, not process.
- The ref name `origin/claude/wicked-engine-cna-backend-5ffqzd` is **factual provenance** and stays
  (policy §2.1).

### 2.2 The per-commit verification refrain was dropped, deliberately

Every original body ended with *"Verification: compiles and links; pipeline-cache-key tests pass
(N of N). Still nothing run on real hardware — WICKED-18/WICKED-74 remain open"*, and the last two
added *"none of the now 22 shader entry points has been through an HLSL compiler yet"*.

All 19 such lines were removed at replay. Two reasons, and the second is the decisive one:

1. Policy §4 **F1** forbids status text in commit messages.
2. **This session falsifies them.** The backend now builds against a real Wicked Engine, its
   shaders now go through a real compiler, and it now runs. Carrying "nothing run on real hardware"
   into permanent history would repeat exactly the failure Batch 1 recorded, where `opengles1`'s
   retracted "87 skipped" survives in two immutable commit bodies
   (`BATCH_1_STABILIZATION.md` §2.2) and can never be corrected.

The lane's real pre-integration validation state is recorded **here and in the merge message**,
once, where it can be read against its evidence. The *technical* boundary content those paragraphs
carried — why there is no feature-matrix column — was preserved and reworded, not dropped.

---

## 3. What the lane actually is — contract established from source, not the branch name

| Field | Value |
|---|---|
| Public identity | **`WICKED`** — `CNA::GraphicsBackendType::Wicked`, `getCurrentGraphicsBackendName()` → `"WICKED"` |
| Selector | `-DCNA_GRAPHICS_BACKEND=WICKED`; option `CNA_BACKEND_WICKED`; define `CNA_BACKEND_WICKED` |
| Backend target / dir | `cna_backend_graphics_wicked` · `src/CNA/Internal/Backends/Wicked` |
| Factory | `CNA::Internal::Backends::CreateGraphicsBackend()` — a free function each backend `.cpp` defines; exactly one backend library links, which is why **no `GraphicsDevice.cpp` arm exists or is needed** |
| Status | **Experimental** — first baseline, now build- and device-validated |
| Platforms | Linux and Windows. **Emscripten is a hard `FATAL_ERROR`** (Wicked has no web device) |
| Underlying API | **Vulkan** via `wi::graphics::GraphicsDevice`. D3D12 exists in Wicked but is **not selectable** (`WICKED-60`: Wicked's HLSL6 path needs a root-signature macro CNA's shader source does not declare), so the Vulkan device is chosen on every platform |
| Dependency | Wicked Engine, **source build**, pinned revision, plus its SDL3 platform patch |
| Tests | `Wicked_PipelineKey` (device-independent) + `Wicked_Demo2D_SmokeTest` (registered only when examples build) |
| Docs | `docs/wicked-backend.md`, `plans/plan_wicked.md` |

**One public CNA backend, not several.** Wicked's internal Vulkan/D3D12 routes are *its* dispatch,
not additional CNA contracts, and are deliberately not counted as such.

### 3.1 Claimed support — and where the boundary genuinely is

Implemented and reported supported: device/swap chain/present, viewport and scissor, blend,
depth/stencil (incl. two-sided), rasterizer (incl. wireframe and depth bias), samplers,
`Texture2D` (with a CPU-generated mip chain), `TextureCube`, `Texture3D`, upload/readback for all
three, vertex and 16/32-bit index buffers with `SetDataOptions` region orphaning, vertex
declarations, **ordinary multi-stream vertex input**, indexed and non-indexed draws, `SpriteBatch`,
**instancing**, `RenderTarget2D` (+ MSAA resolve), `RenderTargetCube`, **MRT up to 4**,
multisampling, the full stock effect set (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
`EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, `SkinnedPbrEffect`), resize/reset and
disposal.

Refused deterministically, reported `false`, device left usable:

| Not supported | Mechanism |
|---|---|
| Custom `ShaderEffect` / `SpriteBatch.Begin(effect)` | `GraphicsCapability::CustomEffects` = **false**; `IEffectBackend` addresses constants **by name**, which needs SPIR-V reflection this backend does not do (`WICKED-57`/`68`) |
| `InstanceFrequency != 1` | Throws at the draw — Wicked's `InputLayout` carries **no instance-step-rate field**, so the rate genuinely cannot be expressed |
| Instancing on strides 48/52/56/68 | Throws — no instanced entry point exists for the wide tangent/skinned layouts |
| Several **per-instance** streams | Throws — the instanced programs declare exactly one instance record |
| A stride outside {16,20,24,32,48,52,56,68} | Throws in `VariantForStride()` |
| MSAA `RenderTargetCube` | Throws — resolving into one cube face needs a per-face resolve subresource view |
| D3D12 device selection | Not selectable (`WICKED-60`) |
| MRT slots 1..3 receiving distinct colours | Stock shaders declare one `SV_Target` — documented, **which is also what XNA does** |

**These are declared boundaries, not silent truncation** — which is precisely why the lane did not
trip the model-escalation gate's "substantial unfinished implementation" trigger. What was missing
was **validation** (`WICKED-18`/`74`/`75`/`76`), not implementation.

---

## 4. Dependency, provenance and licensing

| Field | Value |
|---|---|
| Name | **Wicked Engine** |
| Revision | **`27c0df160d738925474a2181d3f88bfd59edaefe`** (`CNA_WICKED_COMMIT`) |
| Source | `https://github.com/turanszkij/WickedEngine.git` — verified by `git ls-remote`: the pin **is** `refs/heads/master`'s tip |
| License | **MIT** — `LICENSE.txt`, "Copyright (c) 2026 Turánszki János"; GitHub's license endpoint agrees (`spdx_id: MIT`) |
| Status | **Source build**, not vendored and not a submodule. `CNA_WICKED_ROOT` points at a checkout; `CNA_WICKED_AUTO_FETCH` (default **OFF**) can clone the pin instead |
| Local checkout | `~/deps/WickedEngine` — the shared-deps convention, **not** a per-session clone |
| Local modifications | **Yes, by design** — the SDL3 patch is applied *into* the checkout by `cna_wicked_check_sdl3_support()` |
| Build option | `-DCNA_GRAPHICS_BACKEND=WICKED -DCNA_WICKED_ROOT=<path>` |
| Runtime requirement | `libvulkan.so.1` + an ICD; **`libdxcompiler.so` in the process's working directory** |

**Reproducibility, proven rather than asserted.** A fresh `git clone --depth 1 --branch master`
produced exactly `27c0df16` (verified against the pin), and CMake configured it end to end with
**no undocumented developer-machine path**. `--depth 1` is legitimate here because the pin *is*
master's tip; it saved roughly a gigabyte of SSD writes and yields the identical tree.

**The SDL3 patch applied cleanly to the pinned revision** — exactly the six documented files
(`CMakeLists.txt`, `WickedEngine/CMakeLists.txt`, `WickedEngine/Utility/CMakeLists.txt`,
`wiGraphicsDevice_Vulkan.cpp`, `wiInput.cpp`, `wiPlatform.h`), taking `wiPlatform.h` from 0 to 9
SDL3 references. Its necessity is real and documented: upstream's Unix platform layer is SDL2-only
and `CreateSwapChain` has a hard `#error` without SDL2, while SDL2 and SDL3 cannot coexist in one
process.

**`libdxcompiler.so` is a 31 MB prebuilt binary shipped by upstream Wicked Engine**, not by this
lane and not fetched from anywhere else. It is Microsoft's DirectX Shader Compiler, redistributed
inside the MIT-licensed repository at the pinned revision, and it is *copied* next to the build
output rather than modified. Recorded because a prebuilt binary in a dependency chain deserves to
be named, not because its provenance is unclear.

Optional Wicked components are disabled truthfully at configure time: `WICKED_EDITOR`,
`WICKED_TESTS`, `WICKED_IMGUI_EXAMPLE`, `WICKED_LINUX_TEMPLATE`, `WICKED_WINDOWS_TEMPLATE`,
`WICKED_EMBED_SHADERS`, `WICKED_ENABLE_SYMLINKS` — all `OFF`.

---

## 5. Interface drift and the compiler probe — the campaign's cheapest lane

Earlier inspection reported the lane touches **none** of `GraphicsDevice.cpp`,
`IGraphicsBackend.hpp`, `GraphicsCapability.hpp`. **Confirmed by direct measurement** of its 16
changed files from `2338b44f`.

Drift across the 230-commit gap is remarkably small, because this lane forked from `feature/audit`
**late** — after the whole stale-fork drift set had already landed:

| File | fork base → head |
|---|---|
| `IGraphicsBackend.hpp` | 1788 → 1812 lines: **two new virtuals, both with default bodies** (`ITextureCubeBackend::ShareCpuPixels`, `IGraphicsBackend::GetDefaultViewportRect`) plus a `VertexElementFormat` alias — **non-breaking** |
| `GraphicsCapability.hpp` | **10 → 11 members** (`Instancing`, added by the `opengl2` lane) |
| `GraphicsBackendType.hpp` | the registration surface, +91 lines |

**The compile probe is the authority, and it passed with 0 errors.** Every Batch 1 lane needed
11–23 error fixes from stale-fork drift (`opengles1` 6+, `opengl1` 11, `opengl4` 23, `opengl2` 17).
**Wicked needed none**: `fc0dd2a2`'s unified `GpuDrawParams`, the pure-virtual
`SetVertexDeclaration`/`SetRenderTargets`, the `void→bool` readbacks, `preserveContents`,
`BlendWriteState` and the FNA fog vector are all **already present at its fork point**. This is the
first lane in the campaign whose content needed no interface adaptation at all.

---

## 6. Capability audit — one real defect, in the opposite direction from ES1's

`SupportsCapability` had explicit arms for all ten fork-era members and ended in
**`default: return false`**.

That is the *safe* default the policy permits, and it still produced a **wrong answer**:
`GraphicsCapability::Instancing` was added after the fork, fell into the default, and was answered
**`false` by a backend that genuinely implements instancing** through four instanced vertex entry
points (`WICKED-53`).

**This is the mirror image of the `opengles1` hazard, and it is worth stating as its own class.**
ES1's `default: return true` made it *over*-claim `Texture3D` and silence the very test that
detects it. Wicked's `default: return false` made it *under*-claim a capability it has — a caller
gating on the query loses a working feature, and no test fails. **A catch-all default is unsafe in
both directions; only exhaustiveness is truthful.**

Repaired to an **exhaustive eleven-member switch with no `default` arm at all**, matching the
`opengl4`/`opengl1`/`opengl2` convention, so the next member added is a compiler diagnostic rather
than a confident wrong answer either way.

| Capability | Wicked answers | Backed by |
|---|---|---|
| `ThreeD`, `DepthStencilBuffer`, `OcclusionQuery` | true | real depth/stencil state; real `GPUQueryHeap` + readback |
| `WireFrame` | true | `ToWickedFill()` maps XNA `FillMode` ordinal 1 → `wig::FillMode::WIREFRAME` into the pipeline's rasterizer state |
| `MultiSampleAntiAliasing` | true | scene target and `RenderTarget2D` with resolve, device-clamped |
| `AnisotropicFiltering` | true | sampler cache |
| `Texture3D` | true | real volume resource; `SetData`/`GetData` persist and retrieve voxels |
| `MultipleRenderTargets` | true | up to 4 attachments, shared depth from slot 0 |
| `MultiStreamVertexInput` | true | `WICKED-58` re-slotting; several *per-instance* streams refused at the draw |
| **`Instancing`** | **true** (was wrongly false) | four instanced entry points, 64-byte column-major `Matrix` at slot 1 |
| `CustomEffects` | false | no SPIR-V reflection; refused at the call site |

---

## 7. §1.1 post-audit obligations — decided on evidence

**Declaration guard: APPLIES, and it was not a formality.** `VariantForStride()` picks the input
layout *and* the vertex program from the byte stride alone. `SetVertexDeclaration()` kept only the
stride and discarded the element list, and its own comment claimed *"a genuinely custom layout is
refused by `VariantForStride()`"* — **which is false**. `VariantForStride()` refuses an unlisted
*width*; a custom declaration that happens to be one of the eight supported widths (a 32-byte
Position+Color+Color+Color, say) was mapped onto `VertexPositionNormalTexture`'s offsets and
rendered from the wrong bytes, silently, because nothing about it is an error to the GPU.

The declaration is now remembered and `RequireFaithfulDeclarationEXT()` is called in `SubmitDraw()`
— the single funnel behind all five caller-facing draw routes — **before anything native is
touched**, so a rejected draw leaves the device untouched. The check is **asymmetric** (only what
the caller declared is verified, never equality against the backend's template) and **header-only**,
as `cna_backend_graphics_wicked` links only `cna_backend_graphics_common` + SharpRuntime.

This is the `opengles1` shape (stride-dispatching backend ⇒ guard applies), not the `opengl2` shape
(name-bound custom declarations ⇒ satisfied by translation).

**Truthful `WireFrame`: already satisfied.** The `true` report is backed by a real fill-mode
mapping, so the refusal half of `REMED-GFX-209` has no subject here.

---

## 8. Registration union — the seventh time, and the trap was live

Only commit 1 conflicted; commits 2–10 applied with **zero** conflicts. Six files, all resolved as
**semantic unions**:

| File | Union |
|---|---|
| `cmake/BackendSelection.cmake` | 4 hunks — docstring, `STRINGS`, the selection guard, the dispatch chain |
| `include/CNA/GraphicsBackendType.hpp` | 3 hunks — enum, `#elif` chain, name table |
| `CMakeLists.txt` | test-include list |
| `README.md` | selector bullet list |
| `docs/graphics-backend-feature-matrix.md` | two independent "not a column" paragraphs |
| `tests/CNA/GraphicsBackendTypeTests.cpp` | **see below** |

**Token-by-token proof — 27 → 28 identities, zero lost:**

| Surface | HEAD | Adapted | Lost | Added |
|---|---|---|---|---|
| `GraphicsBackendType` enum | 27 | **28** | **0** | `Wicked` |
| `getCurrentGraphicsBackendName()` cases | 27 | **28** | **0** | `"WICKED"` |
| `ExpectedNameFor()` arms | 27 | **28** | **0** | `"WICKED"` |
| `BackendSelection.cmake` `STRINGS` | 27 | **28** | **0** | `"WICKED"` |
| `CNA_BACKEND_*` options | — | — | **0** | `CNA_BACKEND_WICKED` |

Preserved and verified: `DX3` and `FREEDIRECT` remain **distinct**; no live `DX30`; OpenGL 1/2/4
and OpenGL ES 1 remain four independent backends; EasyGL stays internal and unlisted as a new
public identity; **no `feature/gl` identity** (OpenGL ES 3, OpenGL 3, WebGL 1, WebGL 2) was added.

**The test-file conflict was the dangerous one.** The incoming side is the *pre-stabilization*
15-arm switch with no failure arm; taking it would have **reverted the Batch 1 stabilization
repair** (`BATCH_1_STABILIZATION.md` §4) — reinstating a vacuous pass for 13 backends. HEAD's
`ExpectedNameFor()` + `ASSERT_FALSE` guard was kept and the `Wicked` arm added to the helper
instead. **Eighth confirmation that "low conflict class" never means "resolve toward the incoming
side".**

---

## 9. Provenance and losslessness

`git range-diff 2338b44f..91d8587e ed607602..3be87b8b` — **all 10 originals pair 1:1, in order,
none unmatched**. Every difference classified, none unexplained:

| Difference | Count | Class |
|---|---|---|
| `Author:` rewritten to the maintainer | 10 | metadata recreation |
| `Co-Authored-By:` removed | 10 | prohibited attribution |
| `Claude-Session:` removed | 10 | prohibited attribution |
| Verification/status lines removed | 19 | policy F1 (§2.2 above) |
| Process-narration sentence removed | 1 | policy §2.2 |
| Content hunks | — | **confined to the six registration-union files** |

**Blob-level:** **9 of the 16 files are byte-identical** to the original head
(`WickedGraphicsBackend.hpp/.cpp`, `WickedShaderSources.hpp`, `WickedPipelineKeyTest.cpp`,
`plans/plan_wicked.md`, `docs/wicked-backend.md`, `ThirdPartyWicked.cmake`, `WickedTests.cmake`,
`wicked-sdl3-platform.patch` — before the obligation commit edits two of them). The 7 that differ
are exactly the union-resolved registration files. **The lane changed nothing outside its 16
files** — verified by diffing the adapted head against the integration head and filtering them out.

### 9.1 Disposition of every original commit — none disappears silently

| # | Original | Adapted | Disposition |
|---|---|---|---|
| 1 | `f24e36ca` | `b6f9a75d` | **TRANSFERRED** (+ registration union) |
| 2 | `cea52d4b` | `6ac66385` | **TRANSFERRED** |
| 3 | `2bc9c7ab` | `5edef876` | **TRANSFERRED** |
| 4 | `72d7fad3` | `d455adb3` | **TRANSFERRED** |
| 5 | `2ff27820` | `38168235` | **TRANSFERRED** |
| 6 | `602047e9` | `26cedf04` | **TRANSFERRED** (narration sentence removed) |
| 7 | `5bb82991` | `624d19f6` | **TRANSFERRED** |
| 8 | `52f28a34` | `f4fa8da3` | **TRANSFERRED** |
| 9 | `0332f9e8` | `d46de6b4` | **TRANSFERRED** |
| 10 | `91d8587e` | `3be87b8b` | **TRANSFERRED** |
| — | *(new)* | `dc972cbc` | **ADDED** — §1.1 obligations + capability truthfulness |
| — | *(new)* | `1b6ee0a3` | **ADDED** — shared declaration-layout / WireFrame oracle arming |
| — | *(new)* | `4c1dadd4` | **ADDED** — `WICKED-78` device-teardown patch + lifecycle regression + capability-table arm |
| — | *(new)* | `9f820697` | **ADDED** — `WICKED-77` instanced VertexOffset fix + geometry-offset regression |
| — | *(new)* | `4449daaa` | **ADDED** — `WICKED-79` staged-upload mapped-pitch fix |
| — | *(new)* | `de70722f` | **ADDED** — three shared-contract armings the first full corpus run reached |
| — | *(new)* | `97d5a644` | **ADDED** — backend-local test sources excluded from other backends' corpora |

**Zero OMITTED, zero SUPERSEDED, zero DEFERRED.** Unlike `opengl4` (4 `NEXT.md` status commits
omitted) and `depthcrt` (1 superseded), this lane's every original commit carried real content.

---

## 10. Generated files and shaders

**There are none.** All 16 files are text; no `.spv`, no `.dxil`, no embedded byte arrays, no
build-time code generation.

`WickedShaderSources.hpp` is **hand-authored HLSL** held in a C++ raw string, written to a
per-process temporary directory at device creation and compiled at run time by
`wi::shadercompiler::Compile()` into whatever `GraphicsDevice::GetShaderFormat()` reports — SPIR-V
for the Vulkan device. There is therefore no regeneration command and no second-generation diff to
require; the reproducibility question moves to the runtime compiler, whose diagnostics are recorded
in §11.

The only binary anywhere in the chain is upstream Wicked Engine's `libdxcompiler.so` (§4).

---

## 11. Build and validation

### 11.1 Build directory

No compatible tree existed: `cmake/BackendSelection.cmake` issues `add_compile_definitions(CNA_BACKEND_<X>)`
at directory scope, so **every** translation unit hashes differently per backend and there is no
cross-backend ccache reuse to be had (`BATCH_1_STABILIZATION.md` §6). A new persistent in-repo tree
was therefore required and created.

| Field | Value |
|---|---|
| Path | `cnaintegration-wicked/cmake-build-wicked` — in-repo, `.gitignore`d, retained |
| Generator / toolchain | Unix Makefiles · `g++ (Debian 14.2.0-19) 14.2.0` · `CMAKE_BUILD_TYPE=Debug` |
| ccache | **ON** (`CNA_USE_CCACHE=ON`, `CMAKE_CXX_COMPILER_LAUNCHER=ccache`) |
| Display | `CNA_TEST_DISPLAY=:101`. **`:0` never used; `:99` not required** (no Wine route in scope) |
| Dependency | `CNA_WICKED_ROOT=~/deps/WickedEngine` @ `27c0df16` |

Nothing was built under `/tmp`, `/var/tmp` or `/dev/shm`; no build directory was deleted,
recreated or `git clean`ed. The new worktree needed a one-time non-recursive
`git submodule update --init` for the vendored SDL3/gtest checkouts.

### 11.2 Build result

| Target | Result |
|---|---|
| `WickedEngine` (whole engine, from the pinned source + SDL3 patch) | **built, 0 errors** |
| `libcna_backend_graphics_wicked.a` | **built, 0 errors** (4.8 MB) |
| `CNA`, `SHARP_RUNTIME` | built, 0 errors |
| `cna_test_wicked_pipeline_key` | built, 0 errors |
| `cna_demo_2d` | built, 0 errors |
| `libdxcompiler.so` | copied next to the binaries by the POST_BUILD rule, as designed |

**This is the first time this backend has ever been compiled or linked against a real Wicked
Engine.** The lane's `Development status: UNKNOWN` is resolved to *builds and runs*.

### 11.3 Runtime identity — measured

| Field | Value |
|---|---|
| Wicked Engine revision | `27c0df16` |
| Underlying API | **Vulkan** (`wi::graphics::GraphicsDevice_Vulkan`) |
| Device creation | `Created GraphicsDevice_Vulkan (467 ms)` |
| Adapter used | `llvmpipe (LLVM 19.1.7, 256 bits)` — **software path** |
| Adapter also enumerated | `AMD Radeon 780M (RADV PHOENIX)` — real hardware, see §11.5 |
| Shader route | `wi::shadercompiler: loaded ./libdxcompiler.so (version: 1.9)` → HLSL → **SPIR-V at device creation** |
| Window/surface | SDL3 window on `:101`, `SDL_VIDEODRIVER=x11`, via the lane's own SDL3 patch |

**The shaders went through a real compiler for the first time.** All 22 entry points are compiled
at device creation, so a compile failure would abort device creation; device creation succeeded on
4 of 4 runs with **zero shader diagnostics**. This is precisely the claim the original commit
bodies said was outstanding (*"none of the now 22 shader entry points has been through an HLSL
compiler yet"*), and it is why those lines were not carried into integrated history (§2.2).

### 11.4 Test results

| Suite | Result |
|---|---|
| `Wicked_PipelineKey` (`cna_test_wicked_pipeline_key`) | **14 / 14 passed**, 0 failed, 0 skipped |
| `cna_demo_2d --smoke 3` (lavapipe ICD) | **exit 0**, 4 / 4 runs, no errors or warnings |

### 11.5 The registered smoke test fails on this host — and the reason is not the backend

`Wicked_Demo2D_SmokeTest` as registered sets only `SDL_VIDEODRIVER=x11` and `DISPLAY`. The Vulkan
loader then selects the **first** enumerated device, which on this machine is the real AMD Radeon
780M. That device **creates successfully** and then fails to present:

```
Created GraphicsDevice_Vulkan (133 ms)
Adapter: AMD Radeon 780M (RADV PHOENIX)
vulkan: No DRI3 support detected - required for presentation
→ std::runtime_error: "Wicked backend: failed to create the swap chain."
```

**Xvfb provides no DRI3, which RADV requires for X11 presentation.** Forcing the software ICD
(`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`) makes the same binary pass 4/4.

Classified as an **environmental/platform limitation**, not an integration regression and not a
lane defect:

- the backend's own device bring-up works on **both** adapters;
- the failure is a **deterministic, catchable `std::runtime_error` with an accurate message**, which
  is the contract's requirement — the `SIGABRT` is the demo's `main` not catching it, not the
  backend swallowing or approximating anything;
- it is a property of Xvfb + RADV, reproducible independently of CNA.

**The test was not modified, weakened, given a longer timeout or made to force an ICD.** Recorded
as a visible residual with its exact cause, in the same spirit as the networking Outcome C.

---

### 11.6 Bounded runtime probe — the guard and the capabilities, proven at runtime

The full `CnaTests` corpus could not be built here (§11.7), so the evidence for the code **this
adaptation introduced** was obtained the cheap way instead: one translation unit compiled and
linked against the already-built libraries, in place of 1047.

`probe_wicked_guard` — **14 / 14 checks, exit 0**:

| Check | Result |
|---|---|
| All **11** `GraphicsCapability` members answer as documented | ✅ 11/11 — including **`Instancing == true`**, the defect fixed in `dc972cbc`, now proven at runtime rather than by inspection |
| A **custom** 32-byte declaration is refused | ✅ `NotSupportedException`, message naming the exact mismatch (`Color0@12 Color, which this backend's native layout for a 32-byte record does not bind at all`) |
| The refused draw left the render target **unmutated** | ✅ `lit=0` |
| A **stock** 32-byte declaration is still accepted | ✅ the guard is **not over-wide** |
| The stock draw genuinely rasterizes and reads back | ✅ `lit=208` pixels through `RenderTarget2D::GetData` |

> **One self-inflicted trap, recorded because it nearly became a false finding.** The stock draw
> first reported `lit=0`, which reads exactly like "this backend cannot draw". It was the probe's
> own triangle winding: XNA's default is `CullCounterClockwise`, and Wicked's Vulkan device binds a
> **negative-height viewport**, which reverses the effective winding. With `CullMode::None` the
> same draw produces 208 lit pixels. **A culled primitive is indistinguishable from a producer that
> wrote nothing** — the probe now sets cull state explicitly and says why.

### 11.7 What could NOT be validated, and why

`CnaTests` is **1047 translation units** and reached **244** before the build was stopped. Not for
lack of trying, and not a lane property:

- At `-j2` the build peaked at **84.4 °C** and was paused (SIGSTOP) at the policy threshold.
- Restarted at `-j1`, which is demonstrably safe in isolation (the `cna_demo_2d` build peaked at
  **62.1 °C**).
- The machine was nevertheless carrying sustained external load — **load average ≈ 2 with this
  session's build fully stopped** — leaving essentially no thermal headroom. A thermal regulator
  driving only this session's process group (`SIGSTOP`/`SIGCONT`, 5 s sampling, pause 78 °C /
  resume 70 °C) still saw **peaks of 90.5 °C**, because temperature rises ~20 °C in under five
  seconds once a single core engages.
- Measured throughput under that regulator: **6.6 objects/min ⇒ ~2 hours remaining.**

**Continuing would have meant knowingly and repeatedly exceeding the 84 °C ceiling**, so the build
was stopped rather than forced. The 244 objects are preserved; a future run resumes incrementally.

**This is a declared boundary, not a silent omission**: no sanitizer tree was built, no principal
EasyGL control was re-run, and the shared corpus was never executed. **None of it is claimed.**

---

## 13. New findings — two independent production defects

Both were found by arming the shared oracles (`1b6ee0a3`), which is precisely the practice three
prior lanes established. Neither is an integration regression: both are properties of the lane's
own code, on first execution.

### 13.1 `WICKED-77` — instanced draws ignore the geometry `VertexOffset` · **HIGH** · **DISCOVERED AND RESOLVED IN-LANE** (`9f820697`; root cause in §15.1)

`VertexDeclarationLayoutTest.GeometryVertexOffsetAddressesDeclaredRecords` fails on the
**instanced** route only:

```
[ GFX-216 ] Wicked positionColor16/instanced stride=16:
    column 0: (204,45,35,255)  lit=5760 top=8  distinct=1
    column 1: (51,89,70,255)   lit=4320 top=8  distinct=1
    column 2: (102,22,140,255) lit=2880 top=8  distinct=1
    column 3: (0,0,0,0)        lit=0    top=-1 distinct=0
  → "column 3: the quad starts at row -1 instead of 8 -- the POSITION attribute is being
     read from the wrong bytes"
```

**The same geometry, the same declaration and the same stride render correctly through the
ordinary indexed route** (`column 3: (204,89,140,255) lit=1440 top=8`), which rules out the data,
the declaration and the stride table. The per-geometry `VertexOffset` is not reaching the instanced
draw path, so the fourth record's quad never appears. Directly in scope of the campaign's
`VertexOffset` / instanced-offset contract.

### 13.2 `WICKED-78` — `GraphicsDevice` teardown leaves GPU allocations live, aborting the process · **HIGH, validation-blocking** · **DISCOVERED AND RESOLVED IN-LANE** (`4c1dadd4`; root cause in §15.2)

```
vk_mem_alloc.h:10975: void VmaDeviceMemoryBlock::Destroy(VmaAllocator):
Assertion `m_pMetadata->IsEmpty() && "Some allocations were not freed before
destruction of this memory block!"' failed.
```

**Deterministic**: reproduced 2/2 with a `--gtest_filter` narrowed to a **single** test creating a
**single** device, so it is not an artefact of repeated device creation.

**Not universal**, which is what made it diagnosable. `cna_demo_2d --smoke 3` (4/4) and
`probe_wicked_guard` both create a device, do real work and exit **0** — the opposite of a naive
"resources leak" reading, so the trigger was isolated with a three-leg probe, each leg in its own
process so one abort cannot mask another:

| Leg | What the device does before destruction | Result |
|---|---|---|
| `bare` | nothing at all | **ABORT** |
| `query` | one `SupportsCapability` call | **ABORT** |
| `draw` | one triangle into a `RenderTarget2D`, then `GetData` | **exit 0** |

**A `GraphicsDevice` that never renders aborts on teardown; one that renders even a single draw
tears down cleanly.** That is the whole trigger, and it explains both earlier observations: the
demo renders three frames, and `probe_wicked_guard` draws twice.

**It also sets the real severity.** Most tests in the shared corpus construct a device to assert
something *about* it and never draw — which is precisely the aborting shape, and why the corpus
cannot complete.

The mechanism is **localised, not proven**: the device allocates its off-screen scene colour/depth
target at creation, and the release path for those allocations appears to be reached only once a
frame/render pass has actually run. Naming the exact release site, and whether the fix belongs in
the scene-target lifetime or in `Dispose()` ordering, is left to the follow-up — the reproducer
above takes seconds and settles it without guesswork.

**This is the validation blocker.** Every test in the shared corpus constructs a `GraphicsDevice`,
so the corpus cannot complete under this backend regardless of thermal budget. It also directly
contradicts the resource-ownership/disposal contract the campaign requires backends to preserve.

### 13.3 Reproducers, preserved

Both reproducers, their exact compile/link commands and a README live in
**`cnaintegration-wicked/cmake-build-wicked/wicked-repro/`** — a persistent, `.gitignore`d
directory, deliberately *not* the session scratchpad the sources were written in, which is
discarded per session. Nothing there is committed to the lane.

Each rebuilds as **one translation unit** linked against the already-built libraries (the
`cna_test_wicked_pipeline_key` link line with the object swapped), so neither needs the 1047-TU
`CnaTests` binary. The three 158 MB probe executables were deleted after their results were
recorded — **455 MB reclaimed**, ~2 minutes to rebuild.

Run them from `cmake-build-wicked/` (`libdxcompiler.so` is resolved against the **working
directory**, not the executable) with `VK_ICD_FILENAMES` pointing at lavapipe — RADV cannot present
on Xvfb (§11.5).

### 13.4 Not fixed here, deliberately

Fixing either needs iterative build-and-run cycles against a 1047-TU test binary on a machine that
currently has no thermal headroom for one. Guessing at a GPU resource-lifetime bug without being
able to re-run the corpus is how a plausible-but-wrong fix lands. Both are recorded with their
exact reproducers instead.

---

## 14. Residuals and carry-forward

Not touched by this lane, not claimed resolved: **`REMED-GFX-221`** (LOW, open),
**`REMED-CONTENT-007`/`-008`** (HIGH/P1, open — this lane touches no `Content/` file, so it is not
a conditional blocker), the **networking Outcome C** residual, and **Direct2D**'s frozen status.

---

## 15. Completion session — 2026-08-05, blockers repaired, full validation, merged

Resumed from the preserved state above on a cold machine (52.9 °C at start). Both reproducers ran
first and reproduced exactly as recorded before any production change.

### 15.1 `WICKED-77` root cause and fix (`9f820697`)

The two draw-route conventions deliberately differ: the ordinary routes fold the shared per-vertex
`VertexOffset` into `baseVertex`/`vertexStart` and leave each stream-table entry's own offset at
zero, while `DrawInstancedPrimitives` fills the stream table with `foldedOffset = 0` so every
stream carries its whole public offset and the backend owes it at bind time (FNA3D's own D3D11
convention). `SubmitDraw`'s single-geometry-stream branch bound slot 0 with only the buffer's
WICKED-32 region offset — correct for the folded routes, dropping the offset entirely on the
instanced route, at `instanceCount == 1` and above alike. The binding now adds
`VertexStreamByteOffset(FirstPerVertexStream(params))`, exact on every route: the whole offset
where the contract leaves it in the table, zero where it was folded. `Wicked_GeometryVertexOffset`
(5 cases: ordinary control, instanced zero-offset, offset past a whole decoy quad, the oracle's
`instanceCount == 1` shape, and a stride-24 bytes-vs-elements discriminator) fails 3 pre-fix with
"the decoy region lit / target empty" diagnostics and passes 5/5 post-fix; the armed shared oracle
`GeometryVertexOffsetAddressesDeclaredRecords` passes with column 3 at its exact expected reading.

### 15.2 `WICKED-78` root cause and fix (`4c1dadd4`)

**Both mechanisms are upstream, in `GraphicsDevice_Vulkan`'s destructor at pin `27c0df16`,**
isolated with a CNA-free probe driving `wi::graphics` directly (`probe_wicked_raw`, preserved):

- the three null images created beside `nullBuffer` (and their views) are never destroyed, so the
  allocator is torn down with three live allocations and VMA's assertion aborts — on exactly the
  devices that never rendered, because those are the ones whose allocator actually dies;
- the pool-allocated `CommandList_Vulkan` objects are never freed (`cmd_allocator` placement-news
  them; the destructor destroys their Vulkan pools but never runs their destructors), so once any
  command list touched its per-frame linear allocator, the retained `GPUBuffer` pins the
  allocation handler — `VmaAllocator`, `VkDevice` **and** `VkInstance` — forever. A device that
  had drawn therefore *looked* clean while leaking all of it, which is also what masked the
  assertion and produced §13.2's bare/query-abort-draw-passes inversion.

Fixed by `cmake/patches/wicked-device-teardown.patch`, applied by
`cna_wicked_check_device_teardown_fix()` exactly like the SDL3 patch. CNA-owned resources needed
no change — every wrapper already releases before the device member. All 17 previously recorded
probe legs exit 0 post-patch; `Wicked_DeviceLifecycle` (bare, query-only, repeated, explicit
early-disposal, drawing, mixed cycles) aborts pre-patch at the first bare teardown and passes 6/6
post-patch.

### 15.3 What the first FULL corpus run then found — fixed in-lane

Corpus run 1 (5780 ctest cases): 19 failures in four clusters.

- **`WICKED-79` (13 failures, `4449daaa`)**: staged texture uploads smeared at narrow widths —
  upstream's `CreateTexture` initial-data repack stores rows tightly while `CopyTexture` consumes
  the staging texture's ALIGNED mapped pitches, so any upload whose row bytes are not a multiple
  of `optimalBufferCopyRowPitchAlignment` lost rows (wide `Texture2D` uploads divide it exactly,
  which is why 2D never showed it). The staging texture is now written through its own
  `mapped_subresources[0]` pitches, and each staged upload submits before returning — two staged
  copies recorded on one command list interfere (raw-probe measured; a submit between them is
  byte-exact at every width). Readback proven correct in isolation before the fix was chosen.
- **Three shared-contract gaps (`de70722f`)**: the compile-definitions macro count had no WICKED
  entry (the same silently-omitted-registration class its own comments document five times over);
  the instanced multi-stream oracle needed a fourth measured arm (claims `MultiStreamVertexInput`,
  implements instancing, declares exactly one per-instance record — the arm asserts that exact
  refusal); `kRenderTargetCubeAcceptsSetData` defaulted WICKED to "refuses" while the backend
  genuinely stores faces.
- **Corpus composition (`97d5a644`)**: `CnaTests` globs `tests/` unconditionally, so the lane's
  backend-local test directory broke every other backend's corpus configure (its pipeline-key test
  includes the backend header). Caught by the principal control at the adaptation head; excluded
  for non-WICKED backends by the file's own convention.
- **4 audio failures — environmental, not lane-caused, unmodified**: wall-clock-bounded audio
  assertions that fell inside the thermal guard's SIGSTOP windows during run 1. Control evidence:
  the same tests 18/18 on the integration head's principal binary, 20/21 isolated under WICKED
  (the one failure immediately after a heavy heat episode), and **0/4 recurred in corpus run 2**,
  which ran with zero guard pauses.

### 15.4 Validation at the merged content

| Gate | Result |
|---|---|
| Full `CnaTests` build | completed incrementally from the preserved 244 objects; linked; no-change rebuild reports nothing pending at `97d5a644` |
| Full corpus (ctest, run 2, official) | **5780 registered/selected/executed · 5774 passed · 0 failed · 6 skipped · 0 not run** (1483.5 s); no VMA assertion, no SIGABRT, no timeout |
| The 6 skips | 4 sensor + WireFrame-refusal (inapplicable: truthful `WireFrame=true`) + `Texture3DUnsupported` (inapplicable: genuine `Texture3D` support) |
| Dedicated suites | `Wicked_PipelineKey` 14/14 · `Wicked_DeviceLifecycle` 6/6 · `Wicked_GeometryVertexOffset` 5/5 · `Wicked_Demo2D_SmokeTest` pass (lavapipe ICD at invocation) — re-run green post-merge |
| Shared oracles | declaration-layout + capability suites 22 pass / 1 truthful skip; guard refuses correctly, does not over-refuse; `GeometryVertexOffsetAddressesDeclaredRecords` passes both routes |
| Runtime identity | Wicked `27c0df16` · `GraphicsDevice_Vulkan` · llvmpipe (lavapipe ICD; RADV cannot present on Xvfb, §11.5) · `./libdxcompiler.so` 1.9 · all 22 shader entry points compile at every device creation |
| Sanitizers (`cmake-build-wicked-asan`, address+undefined, vptr kept via upstream's `WICKED_ENABLE_RTTI=ON`) | 47 sanitized cases pass; **0 UBSan runtime errors, 0 ASan errors, 0 CNA-originating findings**; every leak stack is two frames ending in `libvulkan_lvp.so` (0 CNA/backend/engine frames); `detect_leaks=0` controls exit 0 |
| Principal EasyGL control (repo root, adaptation head) | **5913 / 5907 / 0 failed / 6 skipped — exactly the Batch 1 baseline, zero regressions**; the networking test incidentally passed (Outcome C remains open as a class) |
| Provenance | original ref and archive tag unchanged at `91d8587e`; 17/17 commits + merge PGP-verify Good; zero attribution hits; `git diff --check` clean; merged tree byte-identical to `adapt/wicked` |

### 15.5 Dependency note

The sanitizer tree sets `-DWICKED_ENABLE_RTTI=ON` — upstream's own switch. Wicked builds
`-fno-rtti` by default and its `ubsan_active` detection reads `CMAKE_CXX_FLAGS`, which misses
CNA's directory-scoped `-fsanitize` options, so the vptr check would otherwise reference typeinfo
the dependency never emits. With RTTI on, the vptr check stays active across the dependency too.

### 15.6 Residuals after integration

Unchanged from §14: `REMED-GFX-221` (LOW, open), `REMED-CONTENT-007`/`-008` (HIGH/P1, open, no
`Content/` file touched), the networking Outcome C, Direct2D frozen. Lane-local and declared:
real-hardware/real-display verification (`WICKED-18`/`WICKED-74`) remains open — everything above
ran on a software Vulkan device; `WICKED-75`/`WICKED-76` remain open; the registered smoke test
still fails without a forced software ICD on this host (§11.5, environmental).

---

## 16. Post-integration addendum — `WICKED-80` resolved (2026-08-06, EliteBook 840 G9)

Everything above is frozen lane evidence; this addendum records the one post-integration event
that touches it. The Batch 2 stabilization's `WICKED-80` (Texture3D staged-transfer corruption,
recorded OPEN at `cbdab0c5`) was **resolved on the integration branch** in the checkpoint-retake
session: the raw-`wi::graphics` control prescribed by §13.4 settled ownership as an **upstream
defect of the pinned revision** — `GraphicsDevice_Vulkan::CreateTexture` allocates
UPLOAD/READBACK staging buffers at the tight texel size while the mapped layout and
`CopyTexture`'s addressing consume `optimalBufferCopyRowPitchAlignment`-aligned pitches, so every
narrow staging transfer addressed out of bounds
(`VUID-vkCmdCopyBufferToImage-pRegions-00171`/`VUID-vkCmdCopyImageToBuffer-pRegions-00183`,
reproduced CNA-free on lavapipe and Intel ANV). That same under-allocation is what §15.3's
WICKED-79 note measured as "two staged copies on one command list interfere". Fixed by the third
carried patch, `cmake/patches/wicked-staging-footprint.patch`; pinned by the new
`Wicked_Texture3DStagedTransfer` byte-exact matrix, whose sequenced narrow leg fails 3/3 against
the unpatched engine. Note on §13.3: the ThinkPad-preserved `wicked-repro/` build-tree directories
did not survive the /rv migration to the EliteBook — both probes were rebuilt from the documented
specification, and the current evidence set lives in the EliteBook WICKED build tree's
`wicked-repro/` (location recorded in `NEXT.md`). Full record:
`integration/BATCH_2_STABILIZATION.md` §12 and `plans/plan_wicked.md` `WICKED-80`.
