# glTF campaign continuity

Read this before `plan_gltf.md`. The plan is 2 700 lines and its own header used to say "nothing in
it was implemented", which stopped being true a long time ago; this file is the short version a new
session needs to start work without re-deriving the state.

## Session status

- **Branch:** `feature/gltf`, with local commits. The owner explicitly requested a push when the
  current autonomous run reaches its weekly-limit cutoff; no pull request has been requested. (The campaign ran on
  `claude/gltf-011-center-collapse-swdjna` until 2026-08-12.)
- **Working document:** `plan_gltf.md`, 460 numbered rows. **448 closed (`✔` 276, `✅` 172),
  6 `⬜` remaining.** The other 6 carry a deliberate partial marker: 1 `🔬` (investigation, no
  implementation owed) and 5 `✅/⬜`; there is no `✅/🐛` residue, standalone `🐛`, or `⛔`.
- **Draco is no longer optional local state:** `third_party/draco` is a gitlink pinned to
  Draco **1.5.7** (`8786740086a9f4d83f44aa83badfbea4dce7a1b5`). The normal build uses it; the sanitizer
  workflow also runs `CNA_ENABLE_DRACO=OFF` so the named refusal path cannot rot.
- **All eight audited defects (D1–D8) are `fixed`** in the corpus defect ledger
  (`tests/assets/gltf/manifest.json` → `defectLedger`). One entry is
  `partially-remediated`: `GLTF-241`, whose residue is owned by `GLTF-238`.
- The campaign has **not** been declared complete. `GLTF-458` (**GLTF CORE 2.0 CORRECT**) and
  `GLTF-459` (**GLTF ROBUST**) are both still `⬜`, and neither should be flipped without the
  evidence its own row demands.

## How to verify, exactly

Run everything from the repository root — the corpus paths are relative to it, and running the
test binary from inside the build directory produces ~80 spurious failures and a segfault that
have nothing to do with the code.

```bash
B=build
export CCACHE_DIR=/tmp/cna-gltf-ccache CCACHE_MAXSIZE=4G
cmake --build "$B" --target CnaTests cna_tool_gltf_to_cnj --parallel 3
ctest --test-dir "$B" -L gltf-conformance     # the 10-rung ladder
"$B"/CnaTests                                 # the whole suite, from the ROOT
"$B"/CnaTests --gtest_filter='*Gltf*'         # every glTF suite -- note the LEADING star
scripts/regenerate-gltf-goldens.sh --check    # the corpus, against its own generator
scripts/regenerate-gltf-goldens.sh --determinism
PYTHONPATH=tools python3 -m gltf_fixtures --fetch-validator /tmp/cna-gltf-validator
GLTF_VALIDATOR=/tmp/cna-gltf-validator/gltf_validator \
  scripts/regenerate-gltf-goldens.sh --check  # all 290 containers through the pinned Validator
cmake -S . -B /tmp/cnagltf-nodraco-build -G Ninja \
  -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_BUILD_TESTS=ON -DCNA_ENABLE_DRACO=OFF
cmake --build /tmp/cnagltf-nodraco-build --target CnaTests --parallel 3
ctest --test-dir /tmp/cnagltf-nodraco-build -L gltf-conformance
```

Expected as of this writing:

| Check | Expected |
|---|---|
| `ctest -L gltf-conformance` | **10/10 passed** (the `Perf` rung joined on 2026-08-12) |
| full suite | **6 372 passed, 191 skipped, 18 failed** |
| generator `--check` / `--determinism` | **145 assets, 729 files — byte-identical** |
| pinned Khronos Validator | **270 valid, 20 expected-invalid, 42 warnings** |
| `*Gltf*`, HEADLESS + vendored Draco | **549 tests: 548 passed, 1 opt-in ChronographWatch skip** (21.5 s locally) |
| `*Gltf*`, HEADLESS without Draco | **539 tests: 538 passed, 1 opt-in skip**; the ten-test difference is the real decoder/encoder evidence, while the unavailable-path checks still run |
| conformance ladder, Draco `ON` / `OFF` | **10/10 passed** / **10/10 passed** |
| pinned reference renderer subset | **12/12 passed**; minimum foreground IoU 0.999579, coverage ratio 0.999891–1.000422, worst foreground RGB MAE 67.60 |
| final pinned viewer retake | **14/14 rows, 15/15 cases passed** through two byte-identical viewer captures plus the exact viewer camera in the Khronos renderer; report in `docs/gltf-viewer-retake-report.json` |

**Those 18 failures are pre-existing and unrelated to glTF.** They are the STUB renderer's
capability expectations (`GraphicsDeviceCapabilityTest.*`), the TextureCube DDS fixtures
(`TextureCubeTest.*`, `Texture3DTextureCubeContentTypeReaderTest.*`,
`XnbBuiltInReaderRegistrationTest.*`) and `CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile`.
Do not attempt to "fix" them as part of this campaign, and do not report a run as clean without
saying they are there.

HEADLESS reports `GraphicsCapability::ThreeD`, so STUB's capability-gated cases really run there.
OPENGLES3 additionally supplies the real framebuffer evidence. When two compatible disposable
trees are available, compare them with

```bash
scripts/gltf-renderer-parity.sh /path/to/headless-build /path/to/opengles3-build
```

which fails on any L1–L5 difference and tolerates only `SKIPPED`-vs-`OK`.

A disposable **sanitizer** tree should be re-created after importer changes:

```bash
A=/tmp/cnagltf-sanitize-build
cmake -S . -B "$A" -G Ninja -DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=ON \
  -DCNA_ENABLE_DRACO=ON -DCNA_SANITIZE=address,undefined
cmake --build "$A" --target CnaTests cna_tool_gltf_to_cnj --parallel 3
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0:exitcode=1 \
  "$A"/CnaTests --gtest_filter='*Gltf*'
```

`detect_leaks=0` is only for this ptrace-restricted local environment; the sanitizer CI keeps its
own leak-detection policy. AddressSanitizer and UndefinedBehaviorSanitizer remain active here.

The current ordinary tree is `-DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_BUILD_TESTS=ON
-DCNA_ENABLE_DRACO=ON`. Always cap compilation at `--parallel 3`. A fresh worktree needs recursive
submodules, including the pinned Draco gitlink, before configuration. Build trees and the ccache are
disposable and are deliberately removed at the end of an autonomous run.

## What a new session must know before touching anything

**The corpus is generated, not hand-edited.** `tests/assets/gltf/*` is emitted by
`tools/gltf_fixtures/`. Edit the Python, then:

```bash
scripts/regenerate-gltf-goldens.sh    # GLTF-020: regenerate, verify, and report what changed
```

A fixture's `.gltf`, `.glb`, `.expected.json`, L5 `.bin` goldens and any external buffer/image
sidecars all come from one description, so they cannot drift apart. Stdlib only — no third-party
dependency is permitted in the generator.

**Every new `Gltf*` test suite must be registered** in `CNA_GLTF_CONFORMANCE_RUNGS`
(`cmake/UnitTests.cmake`), or `GltfConformanceLadder` fails: it parses that list and asserts the
partition over registered suites is total in both directions. A suite that matches no rung would
still run under a plain `ctest` while silently sitting outside the conformance label.

**`plan_gltf.md` §19's extension table is generated.** Its source of truth is
`GltfExtensionRegistryEXT()` in `modules/content/src/GltfImport/GltfImportCore.cpp`, which the
`extensionsRequired` gate also reads. `GltfExtensionRegistry.Section19AgreesWithTheRegistryOnEveryRow`
compares them row by row and **prints the corrected table on failure** — paste that, do not edit the
rows by hand.

**The vertex stride ABI is a table, not a literal.** Query
`InferredLayoutForStride` (`CNA/Internal/Graphics/VertexDeclarationFidelity.hpp`) rather than
hardcoding an offset; that is `GLTF-278`'s lesson, and a test carrying its own copy of an offset
asserts the copy. The packed GPU layouts are `BuiltInVertexStreams.hpp`'s stream structs, **not** the
public `VertexPosition*` types (`sizeof(VertexPositionColor)` is 40, not 16).

**New public API is gated.** `docs/gltf-api-change-review.md` is `GLTF-025`'s record; the standing
default is *no new public glTF API*. Anything additive goes through §1 with its shape justified.
The owner explicitly reopened `GLTF-034` after naming the viewer as its first consumer, so public
`GltfImportReportEXT` now survives both direct and offline loads and is exposed by `Model`; its
computation-oriented source reports (`NodeGraphReportEXT`, `SkinReportEXT`, `MorphReportEXT`,
`LightReportEXT`, `AnimationReportEXT`) remain internal.

**Stage commits by explicit name.** `CLAUDE.md` forbids `git add -A` / `git add .` — this repo
routinely carries unrelated local changes.

## Working conventions that produced the closed rows

These are not style preferences; they are what made the campaign find things.

1. **A fixture must discriminate.** Author values so that a wrong answer is a *different* answer,
   not a plausible one. The recurring failure is a fixture whose expected value coincides with a
   default: every corpus normal was `(0,0,1)` until `normal-absent` was tilted out of the XY plane,
   and every world 3×3 was diagonal — so its own transpose — until an animation fixture put a
   rotation in one, which immediately exposed a latent transpose bug in the L6 oracle.
2. **Write the control.** Nearly every real find came from the negative case: a decoder that ignores
   percent-encoding passes the positive URI test, so the control names the file literally
   `my%20geometry.bin` and requires *failure*. A report hardwired to fire passes every assertion
   that expects it to fire.
3. **Sweeps must be total in both directions, and must say how much they covered.** Two bugs this
   session were sweeps reporting success over a set they had silently shrunk — an L3 comparison that
   iterated the manifest and so skipped a fixture declaring nothing, and a fuzz that carried
   `JOINTS_0`/`WEIGHTS_0` with no `skins` entry so nothing was ever skinned. Assert a floor.
4. **A loss must be named where it happens.** The campaign's thesis is that every silent drop is a
   bug even when the render looks fine. If something cannot be carried, count it and report it, at a
   severity matching the size of the loss — warning for data that does not arrive, debug for an
   exact-but-renumbering transformation.
5. **Match FNA over preference,** and when CNA deviates, say so in the source. A comment calling a
   divergence harmless is a claim that needs a test: `Matrix::Decompose`'s "edge case with no
   practical impact" was losing every axis-aligned rotation.

## What is blocked in this environment, and why

There is no longer a Draco, viewer-write or Khronos-reference blocker. The remaining limits are
narrower than the old table implied:

| Boundary | Rows | Note |
|---|---|---|
| **format/material breadth** | `GLTF-184`, `244`, `344` | §27.1 row 7 is green: strides 60/76 carry two sampled UV sets, direct/offline selectors agree, and EasyGL OPENGLES2/3 plus the pinned Khronos comparison prove the image. Per-map `KHR_texture_transform` matrices and the two `KHR_materials_specular` texture inputs remain. `GLTF-244` still requires its full material matrix at L7 on two rasterisers. |
| **renderer/platform residue** | `GLTF-379`, `385`–`387` | The seven completed semantic-audit slices stay recorded under the investigation row. Vulkan, DirectX11/DXVK and SOFTWARE have strong numerical/native evidence, but no renderer-specific whole-corpus L7 capture/tolerance policy. |
| **milestone chain** | `GLTF-449`, `458`–`460` | All 20 §27.1 rows are now green, so `GLTF-458` is ready for an evidence-backed declaration and `FUTURE.md` update. Evaluate ROBUST separately against all twelve §27.2 rows before writing the retrospective. |

## Suggested next clusters

Ordered by value, not by number. Rewritten 2026-08-14 after the 145-asset corpus, vendored Draco,
viewer integration and pinned reference subset became green.

1. **Finish per-map `KHR_texture_transform` state (`GLTF-184`) on the landed UV2 foundation.**
   Strides 60/76 and the five map selectors are green; the remaining loss is a different transform
   per texture reference. Move it to renderer-consumed PBR state, keep legacy 48/68 behaviour
   stable, and retire the named baking diagnostic only after the L7 witness passes.
2. **Complete the two-renderer material raster gate (`GLTF-244`).** Reuse the final viewer harness
   and keep numerical L6 first; define the second renderer's measured pixel policy instead of
   copying EasyGL's tolerances without evidence.
3. **Finish the remaining `KHR_materials_specular` texture inputs (`GLTF-344`)** on top of the
   per-map UV selector, with the specular scalar texture kept linear and the colour texture decoded
   from sRGB.
4. **Finish renderer/platform residue (`GLTF-379`, `385`–`387`)** with explicit renderer-specific
   image evidence or a documented release-boundary decision; do not turn existing L1–L6/native
   success into an unstated L7 claim.
5. **Declare CORE now, then close the remaining milestone chain in order** (`GLTF-449`,
   `458`–`460`). CORE and ROBUST are separate gates: the optional-extension and cross-renderer
   residue above does not reopen a green §27.1 row, but it still blocks the broader ROBUST claim.

**Before starting anything, read `docs/gltf-conformance.md` §3.7 and §3.8.** They now record how to
add a fixture and when a document belongs inline instead — both were learned the expensive way.

## What the 2026-08-12 session found (read this before trusting a green run)

Three of the four findings came from **running something that had never run**, which is worth more
than any single fix:

- **`RuntimeGltfModelTest` was outside every gate.** Its name does not begin with `Gltf`, so the
  ladder's suite check, its CTest registration and the sanitizer job's `--gtest_filter='Gltf*'` all
  missed it. Four of its cases had been failing since `GLTF-215` on any renderer with a 3D pipeline;
  `STUB` skips them, so a green run said nothing. **The filter is `*Gltf*` now — keep the leading
  star.**
- **`cgltf_validate` walked bytes before CNA's alignment check refused them.** Found by the new
  container fuzz under UBSan. Fixed by ordering: the metadata-only checks (alignment, then span
  arithmetic) now run *before* `cgltf_validate`, so it only ever walks bytes something has vouched
  for.
- **Two allocator escapes.** An accessor with no `bufferView` (§3.6.2.1 reads it as zeros, so
  *nothing in the file bounds its count*) and a `reserve` firing before any decode both surfaced as
  `std::length_error` from inside the allocator. Both refuse by name now.
- **`ContentManager` already caches**, so `GLTF-433`'s premise ("re-parsed on every call") was
  wrong. Two `Load`s of one name return the *same instance* — mutating one mutates "both".

## Defects found and fixed outside plan rows

Recorded in `known_bugs.md`, both found *by* new fixtures rather than by inspection:

- **`Matrix::Decompose` lost every axis-aligned rotation.** `std::signbit` is true for `-0.0`, and a
  quarter turn has exact zeros in every row, so `(-1)*0` flipped a row's sign and the normalised 3×3
  became a reflection — the rotation came back as the identity quaternion. FNA's guard is
  `Math.Sign(x) < 0`, which takes the `+1` branch at negative zero.
- **The L6 normal-matrix oracle transposed its world one time too many.** Invisible for the suite's
  whole life because every corpus world 3×3 was diagonal. The renderer was right throughout.

Both have their own regression tests, and the L6 sweep now fails if it sees no asymmetric world.

## File map

| Path | What it is |
|---|---|
| `plan_gltf.md` | The 460-row campaign record. Each closed row carries its own evidence. |
| `tools/gltf_fixtures/` | The corpus generator. Edit here, never the assets. |
| `tests/assets/gltf/` | Generated corpus: **145 assets, 729 files**, including sidecars and `manifest.json`'s defect ledger. Never edited by hand. |
| `modules/content/src/GltfImport/GltfImportCore.cpp` | The importer. Extraction, skeletons, clips, lights, cameras, the extension registry, the stride table. |
| `modules/content/src/Xna/ContentManager.cpp` | The runtime `.gltf` loader **and** the `.cnj` reader. Both must agree; several tests assert exactly that. |
| `tools/gltf_to_cnj/gltf_to_cnj.cpp` | The offline converter — the second loader. |
| `modules/content/tests/CNA/Internal/GltfImport/` | Every `Gltf*` suite, plus the oracles. |
| `cmake/UnitTests.cmake` | `CNA_GLTF_CONFORMANCE_RUNGS` — the ladder's single source of truth (10 rungs since the `Perf` one joined). |
| `scripts/regenerate-gltf-goldens.sh` | Regenerate the corpus, verify it, decode any binary golden that moved, and (`--determinism`) prove two processes emit the same bytes. |
| `scripts/gltf-renderer-parity.sh` | Compare two build directories; fails on any L1–L5 difference, tolerates only `SKIPPED`-vs-`OK`. |
| `scripts/gltf-reference-renderer-compare.py` | Reproduce the 13-asset OPENGLES3-vs-pinned-Khronos capture and metric report. |
| `scripts/gltf-viewer-retake.py` | Reproduce the final pinned 14-row/15-case Gate C viewer retake. |
| `docs/gltf-viewer-retake-report.json` | Committed Gate C provenance, per-process hashes, camera records and comparison metrics. |
| `docs/gltf-conventions.md` | Every decision with a rationale: transforms, mirroring, colour space, effect selection, lighting, animation, extensions. |
| `docs/gltf-performance.md` | Phase 22's measurements and the decision each led to — the parse/cache costs, the 4× unpack ceiling, the 2× morph duplication, the occlusion codec. Reproduce with `--gtest_filter='GltfPerformance.*' --gtest_output=xml:`. |
| `docs/gltf-limitations.md` | The inverse: what cannot be carried, what is approximated, and the report field that names each loss. Its §1 is generated from the extension registry and its report fields are checked against the header — see `GltfLimitationsDoc`. |
| `docs/gltf-conformance.md` | The oracle ladder and the spec pin. |
| `docs/gltf-api-change-review.md` | `GLTF-025`'s gate. Read §4 before proposing public API. |
| `known_bugs.md` | Defects found outside the plan's own rows. |
