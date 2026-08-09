# Lane card — `depthcrt` · **FIRST INTEGRATION LANE** · **ADAPTED**

| Field | Value |
|---|---|
| Logical lane | `depthcrt` |
| Refs | `refs/heads/feature/depthcrt` and `refs/remotes/origin/feature/depthcrt` — **identical**, and **unmodified by the adaptation** |
| Head | `f4804469a6c14fac6215965794ba6786fc6c5b48` |
| Archive tag | **`archive/preintegration/depthcrt-20260804`** → `f4804469` · annotated · GPG-signed · verifies good · local only · **unchanged** |
| Merge base with checkpoint | `ac3aaaeb` (= `origin/develop`) — **develop-forked**, not audit-stacked |
| Own commits / files | **6 / 14** · diff `+1529, −0` — purely additive |
| Ahead / behind `develop` | 6 / 0 |
| Subsystem | `CNA::Graphics` NOXNA post-process effects (not an XNA API surface, not a backend) |
| Shared interfaces | **none** — no `GraphicsDevice.cpp`, no `IGraphicsBackend.hpp`, no `GraphicsCapability.hpp` |
| Dependencies | **none**, internal or external |
| Conflict class | **LOW** — confirmed: zero textual conflicts, two 3-way auto-merges |
| Development status | DEVELOPMENT COMPLETE |
| Integration readiness | **ADAPTED — see §Adaptation record** |

## Dependency chain

None. Fork point is `origin/develop`; nothing gates it and it gates nothing.

## History-cleanup classification

**AUTHOR/TRAILER CLEANUP REQUIRED — total.**

| Measure | Value |
|---|---|
| Authored **and** committed by `Claude <noreply@anthropic.com>` | **6 / 6** |
| GPG-signed | **0 / 6** |
| Trailers present | `Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>`, `Claude-Session: https://claude.ai/code/…` |
| Merge commits | 0 |
| WIP / fixup / squash subjects | 0 |

Every commit needed re-authoring under policy A2, trailer stripping, and a GPG signature. All six were
handled; five were replayed and one was dropped as superseded.

## Adaptation strategy

**`GpuDrawParams` cost: zero.** Grep over all 14 changed files finds none of the four fields removed
by `fc0dd2a2` (`instanceVb`, `instanceVertexOffset`, `instanceFrequency`, `vertexBufferOffset`).

**Post-audit backend obligations: none apply.** The lane adds no backend, so
`RequireFaithfulDeclarationEXT` and the `WireFrame` truthfulness rule are out of scope.

**API adaptation cost: zero — measured, not assumed.** The lane's entire dependency surface is
byte-identical between its fork point `ac3aaaeb` and the checkpoint `d79214e7`:

```
git diff --stat ac3aaaeb d79214e7 -- \
  include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp \
  src/Microsoft/Xna/Framework/Graphics/ShaderEffect.cpp \
  examples/common/ScreenshotEXT.hpp
→ empty
```

No source file needed a single edit for current APIs.

**File collision surface is 3 of 14.** 11 files do not exist at the checkpoint:

| Status | Path |
|---|---|
| new | `include/CNA/Graphics/DepthEffect.hpp`, `DepthEffectMode.hpp`, `DitherMode.hpp` |
| new | `include/CNA/Graphics/CRTEffect.hpp`, `CRTMaskType.hpp` |
| new | `src/CNA/Graphics/DepthEffect.cpp`, `CRTEffect.cpp` |
| new | `tests/CNA/Graphics/DepthEffectTests.cpp`, `CRTEffectTests.cpp` |
| new | `examples/depth_effect_demo_test.cpp`, `crt_effect_demo_test.cpp` |
| exists | `NOXNA.md` — append **N26, N27, N28, N29** (one per adapted commit) |
| exists | `cmake/Examples.cmake` — add two demo targets |
| exists | `cmake/Harnesses.cmake` — **see the drop** |

> **Lane-card correction.** The pre-adaptation draft of this card said NOXNA.md needed
> "N28/N29 only". That was wrong: the checkpoint's NOXNA.md runs `N25` → `N30` with **N26–N29
> unused**, so all four rows are added, one per adapted commit. Verified against
> `git show d79214e7:NOXNA.md`.

### One commit is dropped, not adapted

`f05e07c8` — `fix(build): link SDL3 to the audio-mixer-destroy standalone harnesses` (+2 lines in
`cmake/Harnesses.cmake`).

**Superseded by `REMED-BUILD-005`**, already in the checkpoint. Proof is in §`f05e07c8` below.

It is also **unrelated to CRT/Depth work** and was bundled into this branch incidentally.

Per policy P6 this drop is recorded here with its superseding commit named.

---

# Adaptation record

**Performed 2026-08-04. Status: MERGED into `integration/post-audit-phase1`.**

## Identity

| Field | Value |
|---|---|
| Original ref | `refs/heads/feature/depthcrt` (= `origin/feature/depthcrt`) |
| Original head | `f4804469a6c14fac6215965794ba6786fc6c5b48` |
| Archive tag | `archive/preintegration/depthcrt-20260804` → `f4804469`, signed, verifies good |
| Original merge base | `ac3aaaeb2a5ba27dbd9e22e782c7041e6e40947c` (`origin/develop`) |
| Integration base | `d79214e7600c0411ce912be11f8e762866be23ee` (tag `cna-post-audit-remediation-phase1`) |
| Adaptation branch | `adapt/depthcrt` |
| Adaptation worktree | `/rv/data/development/github.com/openeggbert/cnaintegration-depthcrt` |
| Adapted head | `3cca0b190e6ed0a33fb2023f6e0952d4ee65de7c` |
| Adapted commits | **5**, all authored **and** committed by `Robert Vokac <robertvokac@robertvokac.com>`, all GPG-signed |
| Integration merge commit | **`61bd1a1b6c81e299251443e738699908af158e1f`** — signed (`U`), non-fast-forward, parents `d79214e7` + `3cca0b19` |

## Original-to-adapted commit mapping

Ordered as the originals actually landed (`git log --reverse`), not as the pre-adaptation draft's
table listed them — see §Ordering note.

| # | Original | Original purpose | Adapted | Disposition | Reason | Validation |
|---|---|---|---|---|---|---|
| 1 | `5c4ebf06` | `DepthEffect` colour-depth-reduction post-process + `DepthEffectMode` + tests + N26 | `88244b3a` | **TRANSFERRED** | Patch applied unchanged; author rewritten, trailers stripped, stale sandbox-state paragraph removed, signed | Builds; `DepthEffectTest` green |
| 2 | `e84f0c05` | `cna_depth_effect_demo` harness + `cmake/Examples.cmake` registration | `3299c211` | **TRANSFERRED** | 3-way auto-merge on `Examples.cmake`; the checkpoint's `REMED-BUILD-002` hunk was **not** reverted | Demo target builds and runs |
| 3 | `f05e07c8` | Link `SDL3::SDL3` to the two audio-mixer-destroy harnesses | — | **SUPERSEDED / ALREADY PRESENT** | Checkpoint already carries both lines via `REMED-BUILD-005` (`be8ac0b6`), plus a rationale comment the original lacks | §`f05e07c8` |
| 4 | `c580b3d7` | `DitherMode` ordered Bayer dithering + N27 | `b9b63809` | **TRANSFERRED** | Patch applied unchanged | `DepthEffectTest` dither cases green |
| 5 | `b1525cf4` | `Palette256`/`Palette16` nearest-colour modes + N28 | `0998acc4` | **TRANSFERRED** | Patch applied unchanged | `DepthEffectTest` palette cases green |
| 6 | `f4804469` | `CRTEffect` + `CRTMaskType` + demo + tests + N29 | `3cca0b19` | **TRANSFERRED** | 3-way auto-merge on `Examples.cmake` | `CRTEffectTest` green; demo runs |

**No original commit is unaccounted for.** 5 TRANSFERRED, 1 SUPERSEDED, 0 OMITTED, 0 REIMPLEMENTED,
0 SPLIT, 0 COMBINED.

### Ordering note

The pre-adaptation draft proposed the order `5c4ebf06, c580b3d7, b1525cf4, e84f0c05, f4804469` —
moving the demo commit to position 4. That ordering was **not** used, because it contradicts this
card's own provenance criterion. `c580b3d7` and `b1525cf4` each modify
`examples/depth_effect_demo_test.cpp`, which `e84f0c05` creates; reordering would have forced demo
hunks to migrate between commits and produced exactly the "meaningful patch difference" that P5
forbids without explanation.

Chronological order preserves every original patch **byte-for-byte**, which is why the range-diff
below shows zero content divergence. Policy F2 — "prefer the original commit boundaries where they
are already logical" — is satisfied literally.

## `f05e07c8` — supersession proof

Not taken on the card's word; compared directly.

**What `f05e07c8` does.** `git show f05e07c8` is exactly +2 lines, one `SDL3::SDL3` added to the
`target_link_libraries(... PRIVATE CNA SHARP_RUNTIME)` block of each of:

- `cna_audio_mixer_destroy_active_static_voice_harness`
- `cna_audio_mixer_destroy_active_dynamic_voice_harness`

Its stated intent: those harnesses include `AudioMixer.hpp`, which needs `<SDL3/SDL.h>` directly, so
a fresh `CNA_BUILD_TESTS=ON` configure fails them with `SDL3/SDL.h: No such file or directory`.

**What the checkpoint already contains.** `git show d79214e7:cmake/Harnesses.cmake` carries **both**
`SDL3::SDL3` lines, in exactly those two targets, introduced by `be8ac0b6`
(`fix(Task REMED-BUILD-005/REMED-BUILD-013): D3D9/D3D11 CnaTests never built under Wine`),
confirmed an ancestor of the checkpoint by `git merge-base --is-ancestor be8ac0b6 d79214e7`.

**Equivalent, or stronger?** **Stronger.** `git diff d79214e7:cmake/Harnesses.cmake
f4804469:cmake/Harnesses.cmake` shows the two files differ **only** by content the checkpoint has
and the lane lacks — never the reverse. The checkpoint adds a seven-line `REMED-BUILD-005:`
rationale block explaining that CNA links `SDL3::SDL3` **PRIVATE**, that native GCC builds masked
the defect through a system-wide `/usr/local/include/SDL3`, and that it is a hard failure under
every cross-compile toolchain (Emscripten, D3D9/D3D11 MinGW). `f05e07c8`'s own body diagnoses only
the native fresh-configure symptom.

**Residue check.** `f05e07c8` touches one file, adds no test, no documentation, no registration and
no secondary behaviour. There is nothing left to transfer.

**Disposition: SUPERSEDED / ALREADY PRESENT.** Reapplying it would have been a no-op at best; the
checkpoint's `REMED-BUILD-005` text was neither removed nor weakened.

## What the adapted lane does *not* revert

Two checkpoint-era improvements sit in files this lane also touches. Both survive intact — verified
by diffing the adapted head against the **original** head:

| File | Checkpoint content preserved | Would have been lost by a naive replay |
|---|---|---|
| `cmake/Harnesses.cmake` | `REMED-BUILD-005` rationale block + `REMED-BUILD-013` MinGW runtime / `CROSSCOMPILING_EMULATOR` blocks (36 lines) | yes |
| `cmake/Examples.cmake` | `REMED-BUILD-002` — the `cna_demo_xact` POST_BUILD `copy_directory` was *deliberately removed* at the checkpoint because `examples/demo_xact/` has no `Content/` directory | yes — the lane branch still carries the old `add_custom_command` |

## File-level transfer proof

All **12** feature files are **byte-identical** to the original head (`git rev-parse <ref>:<path>`
blob comparison):

```
IDENTICAL  NOXNA.md
IDENTICAL  examples/crt_effect_demo_test.cpp
IDENTICAL  examples/depth_effect_demo_test.cpp
IDENTICAL  include/CNA/Graphics/CRTEffect.hpp
IDENTICAL  include/CNA/Graphics/CRTMaskType.hpp
IDENTICAL  include/CNA/Graphics/DepthEffect.hpp
IDENTICAL  include/CNA/Graphics/DepthEffectMode.hpp
IDENTICAL  include/CNA/Graphics/DitherMode.hpp
IDENTICAL  src/CNA/Graphics/CRTEffect.cpp
IDENTICAL  src/CNA/Graphics/DepthEffect.cpp
IDENTICAL  tests/CNA/Graphics/CRTEffectTests.cpp
IDENTICAL  tests/CNA/Graphics/DepthEffectTests.cpp
```

The remaining two of the original 14 are the cmake files above, which differ **only** by the
checkpoint content the adaptation deliberately keeps.

**Diffstat, adapted range vs original range:**

| Range | Files | Insertions | Deletions |
|---|---|---|---|
| `ac3aaaeb..f4804469` (original, 6 commits) | 14 | +1529 | −0 |
| `d79214e7..3cca0b19` (adapted, 5 commits) | 13 | +1527 | −0 |

The delta is exactly `cmake/Harnesses.cmake` (−1 file) and its two `SDL3::SDL3` lines (−2
insertions) — i.e. precisely the dropped `f05e07c8`, and nothing else.

## Symbol / API inventory

Every public symbol from the original branch is present in the adapted lane; nothing was added.

| Symbol | Kind | Members |
|---|---|---|
| `CNA::Graphics::DepthEffectMode` | enum class | `Color16Bit`, `Color8Bit`, `Grayscale4Bit`, `Grayscale2Bit`, `Grayscale1Bit`, `Palette256`, `Palette16` |
| `CNA::Graphics::DitherMode` | enum class | `None`, `Bayer4x4`, `Bayer8x8` |
| `CNA::Graphics::CRTMaskType` | enum class | `None`, `ApertureGrille`, `ShadowMask` |
| `CNA::Graphics::DepthEffect` | class : `ShaderEffect` | ctor(`GraphicsDevice&`); `getMode`/`setMode`; `getDitherMode`/`setDitherMode`; `GetTypeName`; `Clone`; `OnApply`; private `EnsurePaletteTextures` |
| `CNA::Graphics::CRTEffect` | class : `ShaderEffect` | ctor(`GraphicsDevice&`); `getScanlineIntensity`/`set…`; `getCurvature`/`set…`; `getVignetteIntensity`/`set…`; `getMaskIntensity`/`set…`; `getMaskType`/`setMaskType`; `GetTypeName`; `Clone`; `OnApply` |

All five headers and both sources carry `// SPDX-License-Identifier: MS-PL` and are wrapped in
`#ifdef CNA_NOXNA`, matching the existing `CNA::Graphics` convention (`PbrMaterial.hpp`).

## Registration inventory

| Kind | Mechanism | Action needed |
|---|---|---|
| Library sources | `cmake/CnaLibrary.cmake:37` `file(GLOB_RECURSE CNA_SOURCES … "src/*.cpp")` | none — `src/CNA/Graphics/*.cpp` picked up automatically |
| Unit tests | `cmake/UnitTests.cmake:21` `file(GLOB_RECURSE CNA_TEST_SOURCES … "tests/*.cpp")` | none — `tests/CNA/Graphics/*.cpp` picked up automatically |
| `CNA_NOXNA` gating | `cmake/CnaLibrary.cmake:84` `$<$<BOOL:${CNA_NOXNA}>:CNA_NOXNA>` | none — the `#ifdef` in each file does the work |
| Demo executables | `cmake/Examples.cmake` | **two explicit blocks added** (`cna_depth_effect_demo`, `cna_crt_effect_demo`), each gated `CNA_BUILD_EXAMPLES AND CNA_NOXNA AND CNA_GRAPHICS_BACKEND STREQUAL "EASYGL" AND NOT EMSCRIPTEN AND NOT ANDROID` |
| NOXNA task ledger | `NOXNA.md` | four rows `N26`–`N29` |

## Generated files and shaders

**None. Nothing in this lane is generated.** Both effects embed their GLSL as inline
`R"(#version 300 es …)"` raw string literals in `src/CNA/Graphics/DepthEffect.cpp` and
`CRTEffect.cpp`, compiled at runtime by EasyGL. `git diff --numstat` over the adapted range reports
zero binary files, and neither the sources nor the demos reference `shaderc`, `.spv`, SPIR-V or any
precompiled blob. There is no generation step to make reproducible, and none was invented.

## Attribution audit

```
git log --format='%H%nSUBJ:%s%nBODY:%b%nAUTH:%an <%ae>%nCOMM:%cn <%ce>%nTRLR:%(trailers)' \
  d79214e7..adapt/depthcrt \
  | grep -inE 'CC OK|Claude|Anthropic|Co-authored-by|generated by|authored by|\bbot\b|\bagent\b|\bAI\b'
→ ZERO HITS
```

Every commit carries an empty trailer set. Text removed during adaptation, beyond the two trailers:

- `5c4ebf06` — a paragraph reporting that "the full CNA target build in this sandbox separately
  fails … due to sibling sharp-runtime/easy-gl repos being freshly cloned at HEAD". Process
  narration about a transient environment (policy §2.2), and no longer true. Its technical claim
  (the code compiles cleanly under `CNA_NOXNA=ON`) is superseded by this lane's real build.

Everything else in every body — including `CRTEffect`'s documented `vTexCoord` limitation and each
commit's own test-count record — is preserved verbatim.

## Signatures

| Adapted | `%G?` | Key | Author = Committer |
|---|---|---|---|
| `88244b3a` | `U` | `FB9CE8E20AADA55F` | Robert Vokac |
| `3299c211` | `U` | `FB9CE8E20AADA55F` | Robert Vokac |
| `b9b63809` | `U` | `FB9CE8E20AADA55F` | Robert Vokac |
| `0998acc4` | `U` | `FB9CE8E20AADA55F` | Robert Vokac |
| `3cca0b19` | `U` | `FB9CE8E20AADA55F` | Robert Vokac |

`U` = good signature from an uncertified key, this repository's normal pass state. No `N`, no `E`.

## Range-diff

```
git range-diff ac3aaaeb..f4804469 d79214e7..adapt/depthcrt
```

```
1:  5c4ebf06 ! 1:  88244b3a feat(NOXNA): add DepthEffect color-depth-reduction post-process for EasyGL
2:  e84f0c05 ! 2:  3299c211 feat(NOXNA): add DepthEffect manual verification demo (cna_depth_effect_demo)
3:  f05e07c8 < -:  -------- fix(build): link SDL3 to the audio-mixer-destroy standalone harnesses
4:  c580b3d7 ! 3:  b9b63809 feat(NOXNA): add ordered (Bayer) dithering to DepthEffect (DitherMode)
5:  b1525cf4 ! 4:  0998acc4 feat(NOXNA): add real Palette256/Palette16 nearest-colour modes to DepthEffect
6:  f4804469 ! 5:  3cca0b19 feat(NOXNA): add CRTEffect post-process (scanlines, RGB mask, curvature, vignette)
```

**Interpretation.** Five originals map 1:1 and in order. Every `!` expands to differences in the
`## Metadata ##` and `## Commit message ##` sections **only** — the `Author:` line, the two stripped
trailers, and (commit 1) the removed sandbox paragraph. Filtering the range-diff for `+`/`-` lines
outside those two sections returns **nothing**: there is zero patch-content divergence. The single
`<` line is `f05e07c8`, dropped as proven above.

This is the P5 outcome exactly: differences confined to author, committer, trailers, signature, and
the recorded drop.

## Test matrix and results

### Build configuration

**A new build directory was genuinely required and is reported as such.** All 26 pre-existing
`cmake-build-*` trees in the repository are `CNA_NOXNA=OFF`, and this lane's entire surface is
`#ifdef CNA_NOXNA`-guarded — none of them can compile a single line of it. Two persistent in-repo
trees were created in the adaptation worktree, both named after the repository's own documented
NOXNA quick-start (`NOXNA.md` §10):

| Directory | Config | Size | Purpose |
|---|---|---|---|
| `cmake-build-noxna` | Debug · `EASYGL` · `CNA_NOXNA=ON` · tests + examples · ccache | 1.5 G | functional matrix |
| `cmake-build-noxna-asan` | same + `CNA_SANITIZE=address,undefined` | 3.1 G | sanitizer matrix |

`ccache` enabled on both (`CMAKE_CXX_COMPILER_LAUNCHER=ccache`). Nothing was built under `/tmp`,
`/var/tmp` or `/dev/shm`; no existing build directory was cleaned, deleted or reconfigured.
Maximum parallelism `-j8`.

### Results

| # | Check | Command | Result |
|---|---|---|---|
| 1 | Library + tests + demos build | `cmake --build cmake-build-noxna --target CNA CnaTests cna_depth_effect_demo cna_crt_effect_demo -j8` | **exit 0**; zero warnings or errors in any lane file (`DepthEffect`/`CRTEffect`/`CNA/Graphics`) |
| 2 | Demo target registration | `ninja -t targets` | `cna_depth_effect_demo`, `cna_crt_effect_demo`, `CnaTests` all present |
| 3 | Lane unit tests | `CnaTests --gtest_filter='DepthEffectTest.*:CRTEffectTest.*:ShaderEffectTest.*'` | **19/19 PASSED** — 9 `DepthEffectTest`, 9 `CRTEffectTest`, 1 pre-existing `ShaderEffectTest` |
| 4 | Full regression | `./cmake-build-noxna/CnaTests`, cwd = source root | **5912 run · 5904 passed · 13 skipped · 2 failed** — both pre-existing, see below |
| 5 | `cna_depth_effect_demo` | `DISPLAY=:101 SDL_VIDEODRIVER=x11` | **exit 0**, 14 screenshots, all distinct |
| 6 | `cna_crt_effect_demo` | `DISPLAY=:101 SDL_VIDEODRIVER=x11` | **exit 0**, 4 screenshots, all distinct |
| 7 | Output correctness | 18 PNGs inspected | 18/18 distinct md5; sizes track quantization entropy (1-bit undithered 12.8 K → 16-bit 63 K; dithering raises size; Palette16 < Palette256). Visually confirmed: ordered Bayer dithering at 1-bit, and CRT scanlines + RGB shadow mask + barrel curvature + vignette |
| 8 | ASAN + UBSAN | `cmake-build-noxna-asan/CnaTests` on the lane filter | **19/19 PASSED · zero AddressSanitizer errors · zero UBSAN runtime errors**. Leak report is `libGLX_mesa.so.0` only — see below |
| 9 | `CNA_NOXNA=OFF` safety | `g++ -std=c++23 -fsyntax-only -Iinclude` on all four new `.cpp` without `-DCNA_NOXNA` | all four compile to **empty translation units** — the lane cannot affect any of the 26 existing non-NOXNA build configurations |
| 10 | Whitespace | `git diff --check d79214e7..adapt/depthcrt` | clean |

**Test-count correction.** The original `f4804469` body claims "27 total across
`CRTEffectTest`/`DepthEffectTest`/`ShaderEffectTest`". The measured total is **19** (9 + 9 + 1);
`ShaderEffectTests.cpp` contains exactly one `TEST` at both `ac3aaaeb` and `d79214e7`, and this lane
does not modify it. The original's per-suite counts (9 and 9) are correct; only its total is not
reproducible. Recorded here rather than repeated.

**Backends.** The lane declares support for **EasyGL only** — both demo targets are gated
`CNA_GRAPHICS_BACKEND STREQUAL "EASYGL"`, and both effects ship GLSL ES 3.00 source. There is no
"unsupported backend" rejection path to exercise, because on every other backend the targets simply
do not exist and the classes are not compiled. No capability flag is claimed and none is needed.

## Residuals and new findings

### Two pre-existing checkpoint residuals — proven, not asserted

Both were reproduced **on the checkpoint build without this lane**, using the prebuilt
`cnaaudit/cmake-build-debug/CnaTests` (`CNA_NOXNA=OFF`, no depthcrt code present at all):

| Test | Failure | Control result |
|---|---|---|
| `TwoProcessLoopbackTest.HostMigrationPromotesOneSurvivorAndTheOtherReconnectsAcrossRealProcesses` | 30 s timeout | **Fails identically on the checkpoint** (30 073 ms). Real two-process networking test; environment-dependent |
| `XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly` | escaping `"The VertexDeclaration contains an element outside the uploaded vertex stride. (Parameter 'data')"` | **Fails identically on the checkpoint**, same exception text |

The second is the more interesting one. Production behaviour is correct — a mutated XNB is refused
with a clean typed exception rather than crashing. What is stale is the **test's**
expected-exception set. This is a test-side coverage gap on the integration base, **not** a
production defect.

> **Cause attribution corrected 2026-08-04 by the Batch 0 stabilization checkpoint.** This card
> originally named the exception the **`REMED-GFX-DECL-GUARD`** rejection. It is not. The throw is
> `System::ArgumentException` from `VertexBuffer::SetData` (`VertexBuffer.cpp:183`) — XNA-layer
> argument validation firing at **upload**, during `ContentManager::Load<Model>()`.
> `REMED-GFX-DECL-GUARD` is `RequireFaithfulDeclarationEXT` /
> `RequireDeclarationFitsStockProgramEXT`, lives in backend **draw** paths, and throws
> `System::NotSupportedException`; in this EasyGL configuration it exists only inside `Draw*Ex`,
> which this test never reaches. **The conclusion above stands unchanged; only the named mechanism
> was wrong.** Resolved as a fuzz-contract oracle completion — see
> `integration/BATCH_0_STABILIZATION.md` §4.

The lane's own contribution to both is provably nil: the adapted range modifies **zero pre-existing
compiled sources**. Its 13 changed paths are 11 brand-new files plus `NOXNA.md` and
`cmake/Examples.cmake`, neither of which is compiled into `CnaTests`.

### Sanitizer leak report — environment baseline, proven

`LeakSanitizer` reports 1 650 484 bytes in 7 336 allocations across three blocks. **Every frame in
every block is `/lib/x86_64-linux-gnu/libGLX_mesa.so.0`** at offsets `+0x38538` and `+0x3452e`; no
CNA frame appears anywhere in the report.

Control: `cnaaudit/cmake-build-easygl-asan/CnaTests --gtest_filter='ShaderEffectTest.*'`
(`CNA_NOXNA=OFF`, no depthcrt) leaks **100 956 bytes in 449 allocations** — the same three block
shapes from the same two Mesa offsets, for one GL context. The lane's figure is that same per-context
baseline multiplied by its 19 context-creating tests. Mesa driver behaviour, not a lane defect.

### New findings

**None.** No new remediation ticket was created. Neither residual is an independent production
defect: one is environment-dependent, the other is a stale test expectation, and both predate this
lane on the integration base. Per the campaign's rule, expected API adaptation, the `f05e07c8`
supersession, commit-message cleanup, the original unsigned commits and the original authorship are
explicitly **not** ticketable, and nothing else surfaced.

The `XnbContainerFuzzTest` / `REMED-GFX-DECL-GUARD` interaction is nevertheless worth an owner
decision — it is a test failing on the integration base itself — and is recorded in `NEXT.md` for
triage rather than silently absorbed here.

## Conditional blockers re-checked for this lane

- **`REMED-CONTENT-007` / `-008`** — re-checked per `INTEGRATION_ORDER.md` §6. This lane touches
  `ContentManager`, `ContentReader`, the XNB type readers and external resource resolution **not at
  all**; its only content interaction is the two demos reading `examples/demo_2d/Content` through an
  ordinary `ContentManager` at a compile-time-fixed root. **Not blockers here.** They remain open
  HIGH/P1 and are carried forward unchanged.
- **`REMED-GFX-DECL-GUARD`, `REMED-GFX-209` / `WEBGPU-115`** — apply only to lanes adding a
  rasterizing backend. This lane adds none. Out of scope.

## Post-merge verification

| Check | Result |
|---|---|
| Merge shape | `--no-ff`, GPG-signed, two parents `d79214e7` + `3cca0b19` |
| Merged tree vs adapted tree | `c4165cd84b8aaa9bf7f1d2d60daba4cac6febc6e` on **both** — the merge introduced no content of its own, so the artifacts validated on `adapt/depthcrt` **are** the merged content, bit for bit. No duplicate build tree was created to re-prove it |
| Checkpoint tag still an ancestor | yes (`git merge-base --is-ancestor cna-post-audit-remediation-phase1 integration/post-audit-phase1`) |
| Archive tag | still `f4804469`, unchanged |
| Original branch | still `f4804469`, unchanged |
| Signatures over `d79214e7..integration/post-audit-phase1` | 6/6 `U` (5 adapted + the merge commit) |
| Attribution sweep over the integrated range | **zero hits**, merge commit included |
| Files changed vs the checkpoint | exactly the 13 intended paths — no other lane, no stray file |
| `git diff --check` | clean |
| Post-merge smoke | lane sources in the integration worktree byte-match the built ones; 19/19 effect tests re-run green |
| Integration worktree | clean |

## Completion criteria

| # | Criterion | State |
|---|---|---|
| 1 | Five adapted commits on `integration/post-audit-phase1`, GPG-signed, human-authored | ✅ `88244b3a`, `3299c211`, `b9b63809`, `0998acc4`, `3cca0b19` — all `U`, all Robert Vokac as author **and** committer |
| 2 | `f05e07c8`'s omission recorded with its supersession reason | ✅ §`f05e07c8` — proven by direct comparison against `be8ac0b6` / `REMED-BUILD-005`, not taken on the card's word |
| 3 | Build green; both test suites green; full log verified rather than tail-sampled | ✅ build exit 0; 19/19 lane tests; full suite grepped end-to-end (`grep -c '^\[  FAILED  \]'`), 2 failures both proven pre-existing by a checkpoint control run |
| 4 | `range-diff` produced and attached to this card | ✅ §Range-diff — zero patch-content divergence |
| 5 | `archive/preintegration/depthcrt-20260804` still points at `f4804469`, and `refs/heads/feature/depthcrt` is unmodified | ✅ re-verified after the merge |
| 6 | Zero attribution hits across the adapted range | ✅ and across the integrated range including the merge commit |
| 7 | `REMED-CONTENT-007`/`-008` re-checked for this lane | ✅ not blockers — this lane touches no content path resolution |

**Lane status: DONE.** `integration/post-audit-phase1` now contains its first integrated feature
lane. `adapt/depthcrt` and its worktree are deliberately retained for post-merge review and are not
deleted by the integrating session. Nothing was pushed.
