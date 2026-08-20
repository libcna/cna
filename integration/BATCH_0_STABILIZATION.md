# BATCH_0_STABILIZATION.md — Batch 0 **intermediate** stabilization and provenance checkpoint

> **This is the INTERMEDIATE Batch 0 checkpoint — three lanes, not four.** Its tag
> `integration/checkpoint-batch0-20260804` → **`e0332214`** is correct, signed and **must not be
> moved, recreated, retargeted or deleted**. It marks the state after `depthcrt` + `gltf` + `ext`,
> and it deliberately predates `dxold`, which §5 below names as the batch's remaining lane.
>
> The **final** four-lane Batch 0 checkpoint is
> `integration/checkpoint-batch0-complete-20260804` → **`990d6b8a`**, recorded in
> **`integration/BATCH_0_COMPLETE.md`**.
>
> **Read §11's "Batch 0 complete" as scoped to this document's own objective** — the batch's
> *process validation* was complete here (history policy, authorship, signing, range-diff
> reporting, all three principal history classes). The batch's *lane set* was not; that is what
> `dxold` closed. See `BATCH_0_COMPLETE.md` §1.

Companion to `INTEGRATION_ORDER.md` (§3 Batch 0, §5 stabilization checkpoints),
`INTEGRATION_HISTORY_POLICY.md` and the three lane cards under `integration/lanes/`.

**Session date:** 2026-08-04 · **Scope:** stabilization only — no lane was integrated.

---

## 1. Base, head and lane count

| Field | Value |
|---|---|
| Phase-1 checkpoint | `d79214e7` · tag `cna-post-audit-remediation-phase1` · annotated, GPG-signed, verifies good |
| Integration branch | `integration/post-audit-phase1` |
| Integration HEAD at session start | **`8a374b9f`** |
| Integration HEAD at session end | **`e0332214`** — two signed stabilization commits, **no additional lane** |
| Published ancestor on `origin` | **`61bd1a1b`** — the `depthcrt` merge. Unchanged; parents still `d79214e7` + `3cca0b19` |
| Integration worktree | `/rv/data/development/github.com/openeggbert/cnaintegration` |
| Planning worktree | `/rv/data/development/github.com/openeggbert/cnaaudit` @ `feature/audit` `359bd776` |

**Lane count re-derived after `git fetch --all --prune --tags`**, by enumerating every
non-`develop`/`master`/`audit`/`integration`/`adapt` branch across local and remote refs — not
restated from a document:

| | |
|---|---|
| Logical lanes | **21** |
| Integrated | **3** — `depthcrt`, `gltf`, `ext` |
| Pending | **18** |

The fetch moved nothing. `origin/feature/audit` is `047c254a`; the local planning branch is three
documentation commits ahead of it, which is expected and unpushed.

---

## 2. Provenance review — all three lanes

Re-verified from the object database this session. Every check below was measured, not carried
forward from the lane cards.

### 2.1 Structural proof common to all three

Each merge's tree delta is **exactly its own lane's scope** — no merge introduced anything else:

| Merge | Range | Files changed |
|---|---|---|
| `61bd1a1b` `depthcrt` | `d79214e7..61bd1a1b` | 13 — 11 new lane files + `NOXNA.md` + `cmake/Examples.cmake` |
| `722a2f5a` `gltf` | `61bd1a1b..722a2f5a` | **1** — `gltfissues.md` |
| `8a374b9f` `ext` | `722a2f5a..8a374b9f` | **1** — `NOXNA.md` |

### 2.2 `depthcrt` — ADAPTED (author/trailer cleanup)

| Check | Result |
|---|---|
| Original head | `f4804469`; `refs/heads/feature/depthcrt` and `origin/feature/depthcrt` both still `f4804469` |
| Archive tag | `archive/preintegration/depthcrt-20260804` → `f4804469`, **Good signature**, unchanged |
| Original commits | 6, all `Claude <noreply@anthropic.com>` author **and** committer, all `%G?` = `N` |
| Adapted commits | 5 — `88244b3a`, `3299c211`, `b9b63809`, `0998acc4`, `3cca0b19`; all Robert Vokac author **and** committer; all `%G?` = `U` |
| Dropped commit | `f05e07c8` (`fix(build): link SDL3 to the audio-mixer-destroy standalone harnesses`) |
| Supersession **proven** | The checkpoint's `cmake/Harnesses.cmake` already links `SDL3::SDL3` to both `cna_audio_mixer_destroy_active_static_voice_harness` and its dynamic counterpart, with the fuller `REMED-BUILD-005` rationale comment. The drop is correct and the fix is present |
| Feature-file equality | **11 of 13 paths blob-identical** between `f4804469` and `3cca0b19` — all five headers, both `.cpp`, both tests, both demo sources, **and `NOXNA.md`** |
| `cmake/Examples.cmake` | The only differing path. Its **added hunks are byte-identical**; only the base blob and the anchor line differ (261 vs 260), because the original forked from `develop` `ac3aaaeb` and the adaptation from checkpoint `d79214e7`. The lane's contribution is lossless |
| Attribution | zero hits |

### 2.3 `gltf` — DIRECT MERGE (history clean)

| Check | Result |
|---|---|
| Original head | `86ada7a7`; local and remote `feature/gltf` both still `86ada7a7` |
| Archive tag | `archive/preintegration/gltf-20260804` → `86ada7a7`, **Good signature**, unchanged |
| Original commit | **preserved as the same object** — Robert Vokac author and committer, `%G?` = `U`, empty body, one file |
| Merge | `722a2f5a`, parents `61bd1a1b` + `86ada7a7`, true `--no-ff`, `%G?` = `U` |
| Blob identity | `86ada7a7:gltfissues.md` = `8a374b9f:gltfissues.md` = `c16bc32d` — unchanged at HEAD |
| Document framing | Explicitly dated analysis/proposal: title *"Analysis of Incorrect glTF Rendering in CNA"*, `Analysis date: 2026-07-28`, executive summary of four contributing issues. It does not claim to be an implemented change |

### 2.4 `ext` — ADAPTED (author/trailer cleanup, one content adaptation)

| Check | Result |
|---|---|
| Original head | `05ab5d3d`; `origin/feature/ext` still `05ab5d3d` (remote-only lane, no local branch) |
| Archive tag | `archive/preintegration/ext-20260804` → `05ab5d3d`, **Good signature**, unchanged |
| Original metadata violations | author **and** committer `Claude <noreply@anthropic.com>`; trailers `Co-Authored-By: Claude Opus 4.8` and `Claude-Session: …` |
| Signature nuance | `%G?` reports `N`, but `git cat-file -p 05ab5d3d` shows a real **`gpgsig -----BEGIN SSH SIGNATURE-----`** header. `%G?` cannot distinguish "unsigned" from "SSH-signed and uncheckable under the unconfigured `gpg.ssh.allowedSignersFile`". The inventory's original "unsigned" claim was corrected on the lane card and that correction is confirmed here |
| Adapted commit | `c6a28036` — Robert Vokac author and committer, trailers stripped, `%G?` = `U`, body documents the single adaptation |
| Merge | `8a374b9f`, parents `722a2f5a` + `c6a28036`, true `--no-ff`, `%G?` = `U` |
| **Zero lost content** | `diff <(git show 05ab5d3d:NOXNA.md) <(git show 8a374b9f:NOXNA.md)` yields **exactly four added lines at 613** — depthcrt's `N26`–`N29`. Nothing from the original rewrite was dropped or altered |
| `N26`–`N29` preservation | The four rows at `8a374b9f` are **byte-identical** to the same rows at `722a2f5a` |
| `range-diff` | Three differences only: author line, trailers→adaptation note, and the `N26`–`N29` preservation hunk |
| Attribution | zero hits |

---

## 3. NOXNA cross-reference repair

`ext` renumbered the whole `NOXNA.md` backlog, silently changing what citations elsewhere in the
tree resolve to. The `ext` lane deliberately left this open and named the Batch 0 checkpoint as its
owner.

**Method.** Every reference was re-resolved **semantically** — read the surrounding text, identify
the concept, locate that concept in the current integrated `NOXNA.md`, take its new ID, then verify
the target row actually describes it. No blind numeric replacement. The reported line numbers and
old IDs were re-swept rather than trusted:

```
git grep -nE '\bN[0-9]{2}\b' -- . ':(exclude)audit/**' ':(exclude)NOXNA.md' \
                                 ':(exclude)third_party/**' ':(exclude)vendor/**'
```

A broader sweep for single-digit and `task N…` forms returned only test-leg labels
(`bound_target_lifetime_test.cpp` "N1", `d3d12_smoke_test.cpp` "N0"–"N5") and one benchmark name
(`docs/devices-benchmark-baseline.jsonl` `Accelerometer.Fanout.N10`) — none is a NOXNA citation.

### 3.1 Repairs applied

| File | Old | Concept referenced | New | Why |
|---|---|---|---|---|
| `include/CNA/Graphics/PbrMaterial.hpp:19` | `N11` | *the consumer that reads `PbrMaterial`'s values and applies them* | **`N52`** | `PbrEffect` itself **now ships** (`NOXNA.md` §8 *Already shipped*, CNB‑56…60). The remaining work is the binding — `NOXNA.md` §5.5 defines `applyMaterial(const PbrMaterial&, PbrEffect&)`, and row `N52` names `applyMaterial` explicitly. New `N11` is EasyGL float FBOs and is unrelated |
| `noxna_devices.md:93` | `N11` | *quotes the header verbatim* | **`N52`** | Tracks the header so the quotation stays faithful |
| `docs/surface-format-support.md:184` | `N20` | RGBA16F support for HDR | **`N11`** | New `N11` = "Thread `RenderTarget2D`'s `SurfaceFormat` into `CreateRenderTarget2DEx`; EasyGL RGBA16F/32F FBOs". New `N20` is `RenderPipeline`/`HdrSceneTarget` — the orchestrator, not the format |
| `docs/surface-format-support.md:220` | `N20` | RGBA16F **and** `VK_FORMAT_R16G16B16A16_SFLOAT` render targets | **`N11` EasyGL, `N12` other backends** | The row spans GL *and* Vulkan; the float-RT work is now split `N11` (EasyGL) / `N12` (Vulkan, SdlGpu, Bgfx, WebGPU, D3D11, D3D12) |
| `plans/plan_postaudit.md:1572-74` | `N50`/`N51`/`N52` + "§4.4 *Geometry & Instancing*" | three adjacent geometry tasks | **`N50`**, **`N51`**, §8 *Geometry helpers* | Old `N51` (instance-VB helper) → new **`N50`** `InstancedRendererEXT`; old `N52` (LOD helper) → new **`N51`** `LodGroupEXT`; old `N50` (`DrawInstancedPrimitives` overload) **has shipped** and is no longer a backlog item, which the repaired sentence now states instead of silently dropping it |

`PbrMaterial.hpp` is a **public header**, so it was checked explicitly: the citation sits inside the
class's Doxygen `@brief` block (`/** … */`), not in a macro, symbol, enumerator or any compiled
value. Documentation only — the API surface and behaviour are untouched.

### 3.2 Deliberately **not** repaired

| Location | Cites | Reason |
|---|---|---|
| `include/CNA/Graphics/DitherMode.hpp:14` | `N70` | **Still resolves.** The sentence tracks "compute-shader support … long-term"; new `N70` = `IComputeShaderBackend`/`IStorageBufferBackend` + EasyGL (GLES 3.1) impl — the Compute section's entry task, the same role old `N70` had. `NOXNA.md`'s own `N27` row carries the identical "(see N70)" reference and was verified consistent |
| `audit/include/CNA/Graphics/PbrMaterial.hpp.audit.md:27` and `:34` | `N11` | **`audit/` is frozen.** Historical evidence quoting the header as it stood at audit time |
| `audit/examples/noxna_settings_example.cpp.audit.md:143` | `N11` | Same — frozen audit evidence |

**Three stale citations remain under `audit/`, in two files, and are correct to leave.** They are
dated evidence of what the header said when it was audited; "repairing" them would falsify the
audit record. No audit exception was created and `audit/` was not modified.

### 3.3 The planning branch must **not** receive this repair

`plans/plan_postaudit.md`, `noxna_devices.md`, `docs/surface-format-support.md` and `PbrMaterial.hpp` all
exist on `feature/audit` carrying the **same** old IDs — but `feature/audit`'s `NOXNA.md` is still
the **pre-`ext`** document, where `N11` *is* `PbrEffect`, `N20` *is* EasyGL RGBA16F, and `N50`/`N51`/
`N52` *are* the old geometry tasks under §4.4.

**On the planning branch those citations are correct.** Applying the repair there would break four
files. The repair is therefore integration-branch-only, and the planning branch receives only the
Batch 0 documentation updates.

### 3.4 Verification

Every live non-`audit` NOXNA citation now resolves to a current row that describes the referenced
concept: `N11`, `N12`, `N50`, `N51`, `N52`, `N70` — six IDs, all present in `NOXNA.md`.
`git diff --check` clean.

---

## 4. `XnbContainerFuzzTest` — reproduced, reclassified, resolved

The integration report's claim — *"meets the correct `REMED-GFX-DECL-GUARD` rejection"* — was
**reproduced and read rather than accepted**, and the cause named in it is **wrong**.

### 4.1 Measured facts

| Field | Value |
|---|---|
| Failing test | `XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly` — **the Model fixture only** |
| Passing siblings | `MutatedRealTexture2DFixture…` and `MutatedRealSoundEffectFixture…` both **OK** |
| Input | `tests/assets/xnb/monogame/windows/uncompressed/BlenderDefaultCube.xnb`, seed `0x584E42`, 1500 deterministic mutations (bit flip, truncation, byte overwrite, byte insertion) |
| Exception | `System::ArgumentException` — *"The VertexDeclaration contains an element outside the uploaded vertex stride. (Parameter 'data')"* |
| Throw site | `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp:183` — inside `VertexBuffer::SetData` |
| Call chain | `ContentManager::Load<Model>()` → XNB Model reader → `VertexBuffer` construction + `SetData` with the mutated declaration/stride/vertex blob. It reaches **content parsing and vertex-buffer upload**; it never reaches a draw |
| Reproduced on | `8a374b9f` tree **and** the checkpoint control (`cnaaudit/cmake-build-debug`, `CNA_NOXNA=OFF`) — **message-identical**, same single failing fixture |

### 4.2 It is not the declaration-fidelity guard

| | `REMED-GFX-DECL-GUARD` | This failure |
|---|---|---|
| Symbol | `RequireFaithfulDeclarationEXT` / `RequireDeclarationFitsStockProgramEXT` | none — inline validation in `VertexBuffer::SetData` |
| Layer | backend (`src/CNA/Internal/Backends/**`) | XNA public API (`Microsoft::Xna::Framework::Graphics`) |
| Fires at | **draw** — first statement of `Draw*Ex` | **upload** — `SetData` |
| Exception | `System::NotSupportedException` | `System::ArgumentException` |

In the EasyGL configuration where this reproduces, the guard exists **only** inside
`DrawPrimitivesEx` / `DrawIndexedPrimitivesEx` / the instanced variant
(`EasyGLGraphicsBackend.cpp:5932`, `:6002`, `:6095`). A `ContentManager::Load<Model>()` never enters
any of them. The two mechanisms are unrelated.

`REMEDIATION_EXIT.md:238` and `integration/lanes/depthcrt.md:359` both attribute the failure to
`REMED-GFX-DECL-GUARD`. **Their conclusion was right — stale test expectation, not a production
defect — but the named cause is not.** Corrected here; see §8.

### 4.3 Disposition — **B, fuzz/robustness contract**

The test's own contract (file header, and the audit note
`audit/tests/CNA/Internal/Xnb/XnbContainerFuzzTests.cpp.audit.md`) is: *mutate a real `.xnb`'s
entire byte stream and assert every mutated input either loads or fails with one of a small set of
expected, clean exception types — never crashes, hangs or corrupts memory.*

The input is **deliberately malformed**. The outcome is a **deterministic, catchable, clean typed
rejection** from the XNA layer's own argument validation, protecting against reading past the
uploaded bytes. That is precisely the behaviour the test exists to confirm — the allowlist simply
never enumerated it, because only the Model path builds a `VertexBuffer` (which is exactly why the
Texture2D and SoundEffect fixtures pass).

**Not disposition C.** The input is not valid content; the guard does not fire before the promised
public contract; and nothing in production is changed or weakened.

### 4.4 Fix — test-only, bounded

`tests/CNA/Internal/Xnb/XnbContainerFuzzTests.cpp` gains **one** catch clause,
`catch (const System::ArgumentException&)`, with a comment naming the exact throw site and reason,
plus the `System/ArgumentException.hpp` include.

Bounded by the sharp-runtime hierarchy — `Exception : std::exception` → `SystemException` →
`ArgumentException` → {`ArgumentNullException`, `ArgumentOutOfRangeException`}. The new clause
therefore accepts the argument-validation family only (including the sibling stride guard at
`VertexBuffer.cpp:153`, which raises the derived `ArgumentOutOfRangeException` on the same path) and
**still fails** on `System::NotSupportedException`, `std::runtime_error`, and every other class.
`std::bad_alloc`'s deliberate `ADD_FAILURE()` is untouched. The test was not disabled, not marked
`WILL_FAIL`, and not skipped by backend.

*(A/B evidence: §6.4.)*

---

## 5. Build and test matrix

**No build tree existed in the integration worktree.** `cmake-build-noxna` and
`cmake-build-noxna-asan` existed only in `cnaintegration-depthcrt`, which is a different source
worktree at `adapt/depthcrt`. Those were **not** reused as an integration baseline — but they were
used, clearly labelled, as an *investigation* vehicle for the fuzz-test triage before the
integration build existed.

Both trees were therefore created in the integration worktree itself, where the 18 remaining lanes
will reuse them. `third_party/{SDL,SDL_image,SDL_mixer}` and `vendor/googletest` were initialised
non-recursively (the tree had no submodules checked out). Nothing was built under `/tmp`,
`/var/tmp` or `/dev/shm`; no build tree was cleaned or recreated.

| Tree | Config | Status |
|---|---|---|
| `cmake-build-noxna` | `Debug`, `EASYGL`, `CNA_NOXNA=ON`, tests **ON**, examples **ON**, `CNA_TEST_DISPLAY=:101` | **new**, ccache **ON** |
| `cmake-build-noxna-asan` | as above + `CNA_SANITIZE=address,undefined`, examples **OFF** | **new**, ccache **ON** |

**`CNA_USE_CCACHE=ON` and `CMAKE_CXX_COMPILER_LAUNCHER=ccache` verified in both caches.**

> **Honest note on ccache — cross-worktree reuse did not materialise.** The builds ran with
> `CCACHE_BASEDIR` + `CCACHE_NOHASHDIR` intending to hit the `depthcrt` worktree's entries. They did
> not. Measured across the session: hits **27 437 → 27 449** (+12) against misses
> **116 604 → 118 986** (+2 382); cache 6.4 → 6.9 GB. The pre-existing entries were stored under the
> **default** hashing scheme, and changing the hashing options changes the key, so nothing could
> match. Both trees were effectively **cold**.
>
> The lesson for the 18 remaining lanes: reuse **these two in-worktree trees** — which is exactly
> what they now exist for — and do **not** pass `CCACHE_BASEDIR`/`CCACHE_NOHASHDIR`, so entries stay
> on the same default scheme as the rest of the project.

> **Process lesson — build targets, not `all`.** `all` builds ~700 `cna_test_easygl_*` example
> executables that none of the required deliverables need, and it dominated both the wall clock and
> the thermal budget. The last leg used
> `--target CNA CnaTests cna_depth_effect_demo cna_crt_effect_demo`, which is what the `depthcrt`
> lane also did and what the next lane should do from the start.

---

## 6. Results

### 6.1 Build

| Check | Result |
|---|---|
| `cmake --build cmake-build-noxna --target CNA CnaTests cna_depth_effect_demo cna_crt_effect_demo` | **exit 0** |
| Demo target registration | `cna_depth_effect_demo` and `cna_crt_effect_demo` both built |

### 6.2 Full `CnaTests` at the integration HEAD — **mandatory run, green**

```
[==========] 5912 tests from 497 test suites ran. (95558 ms total)
[  PASSED  ] 5906 tests.
[  SKIPPED ] 6 tests
```

`CnaTests` **exit 0**. The complete log was grepped end to end —
`grep -c '^\[  FAILED  \]'` → **0** — not a truncated tail.

This run's own numbers are internally consistent: **5906 + 6 + 0 = 5912**.

**Against the last measured integration baseline**, `depthcrt` @ `61bd1a1b`, recorded on
`integration/lanes/depthcrt.md` as *"5912 run · 5904 passed · 13 skipped · 2 failed"*:

| | `61bd1a1b` (as recorded) | HEAD (measured) |
|---|---|---|
| **Failed** | **2** | **0** |
| Passed | 5904 | 5906 |
| Skipped | 13 | 6 |
| Ran | 5912 | 5912 |

The directly comparable and load-bearing fact is **failed 2 → 0**, and both previously-failing tests
were individually confirmed passing by name (§6.3, §7). Total test count is unchanged at **5912** —
the fix added no test and removed none, so nothing was hidden by deletion.

> **Caveat on the baseline figures, not on this run.** The lane card's three components sum to 5919,
> not the 5912 it also records, so one of its numbers is off by 7. This session did not re-run
> `61bd1a1b` to resolve it — that would mean rebuilding a superseded tree — and it does not affect
> the conclusion, since the failure count and the two test names were verified independently. Flagged
> so the figure is not quoted forward as exact.

**Zero integration regressions.**

### 6.3 depthcrt lane tests, demos, screenshots

| Check | Result |
|---|---|
| `--gtest_filter='XnbContainerFuzzTest.*:DepthEffectTest.*:CRTEffectTest.*:ShaderEffectTest.*'` | **22 / 22 PASSED** — 3 + 9 + 9 + 1 |
| The lane's own 19 | **19 / 19** (9 `DepthEffectTest`, 9 `CRTEffectTest`, 1 pre-existing `ShaderEffectTest`) |
| `cna_depth_effect_demo` | **exit 0**, 14 screenshots |
| `cna_crt_effect_demo` | **exit 0**, 4 screenshots |
| Distinctness | **18 PNGs, 18 distinct md5** |
| Entropy ordering | 1-bit undithered 12 777 B → 16-bit 63 150 B; dithering raises size (1-bit: none 12 777 < Bayer4×4 17 310 < Bayer8×8 19 968); Palette16 25 644 < Palette256 35 935 |

**Visual verification — the images were looked at, not inferred from file size:**

- **Depth effect** — bit-depth reduction plainly visible across the mode sweep.
- **Bayer dithering** — `depth_effect_1bit_bw_bayer8x8.png` renders the bottom gradient strip as a
  regular ordered halftone dot matrix rather than a flat two-tone split.
- **Palette effects** — `depth_effect_palette16_none.png` quantises the smooth hue strip into ~16
  discrete flat bands, with sprites snapped to a small saturated set: real nearest-colour matching.
- **CRT mask / curvature / vignette** — `crt_effect_full_crt.png` shows all four in one frame:
  horizontal scanlines, the fine RGB sub-pixel mask, visibly bowed barrel edges, and corner
  vignette falloff.

### 6.4 `XnbContainerFuzzTest` — A/B and negative control

| Contract | Result |
|---|---|
| **Old** oracle, integration tree | **FAILED** — `System::ArgumentException` escaped |
| **Old** oracle, checkpoint control (`cnaaudit/cmake-build-debug`, `CNA_NOXNA=OFF`) | **FAILED — message-identical**, same single fixture. Pre-existing, not an integration regression |
| **New** oracle | **3 / 3 PASSED** |
| **Negative control** — `System::NotSupportedException` injected into the loop | **all 3 fixtures FAILED**. The oracle is not a catch-all, and specifically still rejects the very type `REMED-GFX-DECL-GUARD` throws |

The probe was reverted, rebuilt and re-verified green before committing; the committed source
contains no probe residue.

### 6.5 `CNA_NOXNA=OFF` control

The lane is **entirely** `#ifdef CNA_NOXNA`-guarded — verified on all **11** new files (5 headers,
2 sources, 2 tests, 2 demo sources), plus CMake gating of both demo targets on
`CNA_BUILD_EXAMPLES AND CNA_NOXNA AND CNA_GRAPHICS_BACKEND STREQUAL "EASYGL"`.

Empirically, with the macro absent both new `.cpp` files compile with **zero diagnostics and no
backend macro required** — empty translation units. Adding `-DCNA_NOXNA` immediately descends into
real includes and stops at the missing backend selection, which proves the empty result comes from
the guard and not from a trivially-passing invocation.

**The integrated lane cannot alter any `CNA_NOXNA=OFF` build.**

### 6.6 Sanitizer gate — ASan + UBSan, **clean**

`cmake-build-noxna-asan` (`CNA_SANITIZE=address,undefined`, `CNA_NOXNA=ON`, examples OFF) built
**exit 0**, and the corrected `XnbContainerFuzzTest` executes in that configuration.

```
ASAN_OPTIONS=detect_leaks=1  UBSAN_OPTIONS=print_stacktrace=1
--gtest_filter='XnbContainerFuzzTest.*:DepthEffectTest.*:CRTEffectTest.*:ShaderEffectTest.*'
[==========] 22 tests from 4 test suites ran.
[  PASSED  ] 22 tests.
```

| Class | Count |
|---|---|
| `ERROR: AddressSanitizer` (memory safety) | **0** |
| `runtime error:` (UBSan) | **0** |
| `ERROR: LeakSanitizer` | 1 — analysed below |

**The leak report contains no CNA frame.** 1 964 776 bytes in 8 734 allocations across three blocks;
**every** frame resolves to `/lib/x86_64-linux-gnu/libGLX_mesa.so.0` at offsets `+0x3452e` and
`+0x38538`.

**Control run, this session, same binary** — `--gtest_filter='ShaderEffectTest.*'`, exactly one GL
context: **100 956 bytes in 449 allocations**, again with every frame in Mesa. That is
**byte-identical** to the figure the `depthcrt` lane measured on `cnaaudit/cmake-build-easygl-asan`,
a different build with `CNA_NOXNA=OFF` and none of this code compiled in. The per-GL-context driver
baseline is therefore **unchanged by the integrated lane**; the 22-test figure is that same baseline
across the suite's contexts.

**Zero CNA-originating memory or UB reports.**

### 6.7 Backend compilation controls — the prescribed set is **empty**, and that is the finding

The matrix asks for representative backend libraries affected by shared effect/public headers, and
for cross-builds of D3D configurations *if the NOXNA headers are consumed there*. Measured rather
than assumed:

- The only production-tree file this session changed is `include/CNA/Graphics/PbrMaterial.hpp`, and
  the change is a Doxygen comment **inside `#ifdef CNA_NOXNA`**.
- `PbrMaterial.hpp` is included by exactly **two** files tree-wide — its own
  `src/CNA/Graphics/PbrMaterial.cpp` and `examples/noxna_settings_example.cpp`. **No backend source
  includes it**; `git grep PbrMaterial -- 'src/CNA/Internal/Backends/**'` returns nothing.
- Every pre-existing build tree in this repository is `CNA_NOXNA=OFF`, where the header is an empty
  translation unit.
- The three integrated lanes add **no** backend, so the post-audit backend obligations
  (`RequireFaithfulDeclarationEXT` at draw time, truthful `WireFrame` reporting, header-only
  helpers) have nothing to attach to here.

**There is no backend library whose compilation this session's changes could affect.** No D3D
cross-build was run, because the condition that would require one — NOXNA headers consumed there —
is false. Stating the empty set is the honest result; manufacturing a control to fill the row would
not be evidence of anything.

---

## 7. Residuals

| Residual | Classification | Blocks Batch 0? |
|---|---|---|
| `XnbContainerFuzzTest` Model fixture | **test-contract issue — RESOLVED this session** (§4) | no |
| `TwoProcessLoopbackTest.HostMigration…` | **environmental / flaky** — 30 s timeout at `61bd1a1b`, **passed** in this run | no |
| 6 skipped tests | **platform/runtime limitation** — sensors without hardware, and two declared capability boundaries (`WireFrameIsRefusedDeterministicallyOnThisBackend`, `Texture3DUnsupportedBackendTest`) that skip with a named reason on EasyGL | no |

**No integration regression of any kind was found.**

---

## 8. Corrections recorded rather than silently applied

| Document | Statement | Correction |
|---|---|---|
| `remediation/REMEDIATION_EXIT.md:238` | `XnbContainerFuzzTest` … "known-cause, named by `REMED-GFX-DECL-GUARD`" | The cause is `System::ArgumentException` from `VertexBuffer::SetData` (§4.2). **The exit record is deliberately left unmodified** — it is the frozen statement attached to tag `cna-post-audit-remediation-phase1`, and rewriting a tagged exit document post hoc is exactly what the published-history rule forbids in spirit. The correction lives here and on the lane card |
| `integration/lanes/depthcrt.md:359` | same attribution | Corrected in place — a lane card is living integration documentation |

---

## 9. Carried forward, unchanged

### 9.1 `REMED-CONTENT-007` / `REMED-CONTENT-008`

**OPEN · HIGH / P1 · not touched by this session and not fixable by it.**

- `REMED-CONTENT-007` — `SongContentTypeReader.cpp` / `VideoContentTypeReader.cpp` each define a
  private `ResolveRelativeFilePath()` with no containment check, fed by the `.xnb`'s own embedded
  filename. `include/CNA/Internal/PathContainment.hpp` exists and is unused by either.
- `REMED-CONTENT-008` — `ContentManager.cpp` makes zero calls to
  `IsDisallowedAbsolutePath` / `ResolveContainedPath`, while joining eight manifest-supplied path
  fields onto the content root raw.

**Non-blocking for Batch 0**, re-checked rather than waved through: none of the three integrated
lanes touches path-resolution code. `depthcrt`'s 13 paths are 11 new files plus `NOXNA.md` and
`cmake/Examples.cmake`; `gltf` is `gltfissues.md`; `ext` is `NOXNA.md`. **Required before any public
security-clean claim or release** (`INTEGRATION_ORDER.md` §5, §6).

### 9.2 Direct2D

**OWNER-FROZEN at `9b17e783`** · **FROZEN INCOMPLETE / EXPERIMENTAL** · no further development ·
**not automatically integration-ready** · does **not** block any other lane. Archive tag
`archive/preintegration/direct2d-20260804` verifies good. If the head is ever observed elsewhere,
stop and report the movement.

### 9.3 `feature/gl`

MetaGL development **complete**; EasyGL development **complete**. Outstanding before integration:

1. restore/remove the temporary EasyGL `rvc` `CMakeLists.txt` redirect — **local configuration**,
   not a code change, and it must be restored before integration;
2. MetaGL `feature/followup-audit` → MetaGL `develop`;
3. EasyGL `rvc` → EasyGL `develop`;
4. CNA `feature/gl` updated to both `develop` revisions;
5. CNA `feature/gl` integrated afterwards.

**EasyGL remains internal and hidden.** The public backends `feature/gl` supplies are exactly
**OpenGL ES 3, OpenGL 3, WebGL 1, WebGL 2**.

---

## 10. New findings

**No new remediation ticket was created, and none is warranted.**

Per the session rule, a ticket is opened only for an **independent production defect**. Everything
this session touched falls in an explicitly non-ticketable class:

| Observation | Why it is not a ticket |
|---|---|
| Stale NOXNA cross-references | Named non-ticketable; repaired here (§3) |
| The `ext`/`depthcrt` `NOXNA.md` conflict | Expected, already resolved and recorded on the lane card |
| Original commit metadata (author, trailers, signatures) | Named non-ticketable; that is what adaptation is for |
| `XnbContainerFuzzTest` | **Proven test-only** (§4) — reproduced, root-caused to a deliberate XNA-layer validation guard, and fixed in the oracle. Production behaviour is correct and unchanged |
| `audit/` citations | Frozen evidence; left untouched by owner decision |

**Two documentation-accuracy defects were found and are recorded rather than ticketed** — neither is
a production defect:

1. `REMEDIATION_EXIT.md:238` and `integration/lanes/depthcrt.md:359` mis-attribute the fuzz-test
   failure to `REMED-GFX-DECL-GUARD` (§4.2, §8).
2. `NOXNA.md`'s own row `N04` says `PbrMaterial` is *"extend in N42"*, but `N42` is the IBL
   prefiltered-specular task; the material work is `N52`. This is an internal inconsistency in the
   incoming `ext` rewrite, **not** something this session introduced. It is **not** repaired here:
   the session forbids changing NOXNA numbering to preserve references, and altering the integrated
   lane's own document beyond the recorded conflict resolution would widen the lane after the fact.
   Recorded for whoever next edits `NOXNA.md`.

**No independent production defect was found.**

---

## 11. Decision — **OUTCOME A · READY**

| Completion gate | Result |
|---|---|
| Provenance review clean, all three lanes losslessly traceable | ✅ §2 |
| Cross-references repaired outside `audit/`; every live ID resolves | ✅ §3 |
| `XnbContainerFuzzTest` has a truthful green contract | ✅ §4, §6.4 |
| Current integration HEAD builds | ✅ exit 0 |
| Mandatory full `CnaTests` passes | ✅ **0 failed** |
| depthcrt tests, demos, screenshot smoke | ✅ 19/19 · 2 demos · 18/18 visually verified |
| `CNA_NOXNA=OFF` control | ✅ empty TUs, counter-checked |
| Sanitizer gate clean | ✅ zero CNA-originating reports |
| No new integration regression | ✅ |
| All commits and merges signed; no prohibited attribution | ✅ 12/12 `U`, zero hits |
| Published history not rewritten; `audit/` untouched | ✅ `61bd1a1b` intact, exactly 3 merges |
| `git diff --check`; worktrees clean | ✅ |

### Stabilization commits

| Commit | Branch | Signature |
|---|---|---|
| `f742341b` `docs(NOXNA): repair cross-references invalidated by the backlog renumbering` | `integration/post-audit-phase1` | `U` |
| `e0332214` `test(xnb): accept the XNA argument-validation rejection in the container fuzz oracle` | `integration/post-audit-phase1` | `U` |

Neither integrates a lane. The branch still contains **exactly three** merges — `depthcrt`, `gltf`,
`ext`.

### Checkpoint tag

**`integration/checkpoint-batch0-20260804`** — annotated, GPG-signed (`FB9CE8E20AADA55F`), **local
only, not pushed**.

The name follows this repository's own documented convention for this tag
(`INTEGRATION_ORDER.md` §5, `integration/checkpoint-batch0-<date>`) and the namespaced-and-dated
pattern already used by the 21 `archive/preintegration/<lane>-<date>` tags. The session's fallback
name `cna-integration-batch0` was therefore not needed; renaming is trivial if the owner prefers it.

### Status after this checkpoint

- **Batch 0's process-validation objective complete** — 3 lanes integrated + stabilization
  checkpoint taken. *(Wording scoped 2026-08-04 at the final closeout: the batch's **lane set** was
  not complete here — `dxold` was still to come, as this document's own §5 says. See
  `integration/BATCH_0_COMPLETE.md` §1.)*
- **3 integrated · 18 pending**, re-derived after fetch. *(Superseded by the post-`dxold`
  re-derivation: **4 integrated · 17 pending**.)*
- **All three history classes validated**: adaptation with a dropped superseded commit (`depthcrt`),
  direct merge preserving the original object (`gltf`), and adaptation with a real content conflict
  (`ext`).
- **The next lane was not begun.**

### Next task, and model recommendation

> **Done 2026-08-04.** `dxold` landed as merge `990d6b8a` and closed Batch 0. The recommendation
> below is the historical record of what this checkpoint proposed; for the *current* next action see
> `integration/BATCH_0_COMPLETE.md` §10 and `INTEGRATION_ORDER.md` §3 *Batch 1*.

**`dxold`** — 28 commits, 225 files, the last Batch 0 lane. It is a mixed historical/backend lane:
partial `AUTHOR/TRAILER CLEANUP REQUIRED` (3 of 28), several DX-era backends, and by far the largest
file count integrated so far.

**Recommended model: Fable.** The lane is broad and mechanical rather than deep — 225 files of
replay, metadata cleanup and per-file conflict checking against the integration head — which is the
shape that benefits from throughput. Reserve Opus for a lane whose *design* is contested; `dxold`'s
difficulty is volume and bookkeeping, not judgement.

Two things the next session should carry over from this one:

1. **Build named targets, not `all`** (§5) — `all` costs ~700 example executables that no gate needs.
2. **Reuse `cmake-build-noxna` / `cmake-build-noxna-asan` in the integration worktree**, without
   `CCACHE_BASEDIR`/`CCACHE_NOHASHDIR` (§5).
