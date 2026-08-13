# glTF campaign continuity

Read this before `plan_gltf.md`. The plan is 2 700 lines and its own header used to say "nothing in
it was implemented", which stopped being true a long time ago; this file is the short version a new
session needs to start work without re-deriving the state.

## Session status

- **Branch:** `feature/gltf`, with local, intentionally unpushed commits. Never push without
  explicit permission. No pull request has been opened and none should be unless asked. (The campaign ran on
  `claude/gltf-011-center-collapse-swdjna` until 2026-08-12.)
- **Working document:** `plan_gltf.md`, 460 numbered rows. **377 closed (`✔` 252, `✅` 125),
  64 `⬜` remaining.** The other 19 carry a deliberate partial marker: 8 `🔬` (investigation, no
  implementation owed), 7 `✅/⬜`, no `✅/🐛` residue, 2 `🐛` (open:
  `GLTF-157`, `421`), and 2 `⛔` (`GLTF-009` and
  `GLTF-439`, each blocked by this environment for a stated reason).
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
B=/media/robertvokac/claude/tmp/cna/cmake-build-gltf-tests
export CCACHE_DIR=/media/robertvokac/claude/tmp/cna/ccache CCACHE_MAXSIZE=30G
cmake --build "$B" --target CnaTests -j3     # -j3 is the ceiling in openeggbert/CLAUDE.md
ctest --test-dir "$B" -L gltf-conformance     # the 10-rung ladder
"$B"/CnaTests                                 # the whole suite, from the ROOT
"$B"/CnaTests --gtest_filter='*Gltf*'         # every glTF suite -- note the LEADING star
scripts/regenerate-gltf-goldens.sh --check    # the corpus, against its own generator
scripts/regenerate-gltf-goldens.sh --determinism
```

Expected as of this writing:

| Check | Expected |
|---|---|
| `ctest -L gltf-conformance` | **10/10 passed** (the `Perf` rung joined on 2026-08-12) |
| full suite | **6 367 passed, 191 skipped, 18 failed** |
| generator `--check` | **140 assets, 694 files — byte-identical** |
| `*Gltf*` on `STUB` / `HEADLESS` | **474 passed, 26 skipped** / **500 passed, 0 skipped** |

**Those 18 failures are pre-existing and unrelated to glTF.** They are the STUB renderer's
capability expectations (`GraphicsDeviceCapabilityTest.*`), the TextureCube DDS fixtures
(`TextureCubeTest.*`, `Texture3DTextureCubeContentTypeReaderTest.*`,
`XnbBuiltInReaderRegistrationTest.*`) and `CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile`.
Do not attempt to "fix" them as part of this campaign, and do not report a run as clean without
saying they are there.

There is a third tree for the **second renderer**,
`/media/robertvokac/claude/tmp/cna/cmake-build-gltf-headless` (`-DCNA_GRAPHICS_RENDERER=HEADLESS`).
It matters more than it sounds: `HEADLESS` reports `GraphicsCapability::ThreeD`, so the 17
`GltfToCnjToolTest` cases that **skip on `STUB`** actually run there — and two of them were failing
on stale pre-`GLTF-215` effect expectations that the skip had hidden. Compare the two renderers with

```bash
scripts/gltf-renderer-parity.sh "$B" /media/robertvokac/claude/tmp/cna/cmake-build-gltf-headless
```

which fails on any L1–L5 difference and tolerates only `SKIPPED`-vs-`OK`.

There is a second, **sanitiser** build tree beside it —
`/media/robertvokac/claude/tmp/cna/cmake-build-gltf-asan`, configured with
`-DCNA_SANITIZE=address,undefined` and otherwise identical. It is what `GLTF-409` was closed with,
and it is worth re-running after any importer change:

```bash
A=/media/robertvokac/claude/tmp/cna/cmake-build-gltf-asan
cmake --build "$A" --target CnaTests cna_tool_gltf_to_cnj -j2
ASAN_OPTIONS=detect_leaks=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0:exitcode=1 \
  "$A"/CnaTests --gtest_filter='*Gltf*'    # 474 passed, 26 skipped, 0 findings
```

The build directory is `-DCNA_GRAPHICS_RENDERER=STUB -DCNA_BUILD_TESTS=ON`, built **out of the
repository** on the partition the owner designated for build output:
`/media/robertvokac/claude/tmp/cna/cmake-build-gltf-tests`, with
`CCACHE_DIR=/media/robertvokac/claude/tmp/cna/ccache` exported on every configure and build and
`-j3`. Never build in the scratchpad — see `CLAUDE.md`. A fresh worktree needs its submodules
first; clone them sharing the main checkout's object store
(`git submodule update --init --reference <cna>/.git/modules/<path> <path>`, ~26 MB instead of
~500 MB) and copy `.sdl-prebuilt-Linux-x86_64/` from the main worktree rather than rebuilding SDL.

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
default is *no new public glTF API*. Anything additive goes through §1 with its shape justified, and
every internal report (`NodeGraphReportEXT`, `SkinReportEXT`, `MorphReportEXT`, `LightReportEXT`,
`AnimationReportEXT`) is deliberately internal because `GLTF-034`'s public report is deferred there.

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

**45 of the 81 remaining rows cannot be finished here.** Do not mark them done, and do not work
around them by weakening their acceptance. Recounted 2026-08-13: `HEADLESS` was added as a second
renderer, which moved several rows out of the "second renderer" bucket, and `GLTF-404`/`GLTF-419`
turned out not to need CI at all.

| Blocker | Rows | Note |
|---|---|---|
| **L7 / rendered image** | 18 — `GLTF-016`, `175`, `176`, `182`, `189`, `213`, `218`, `230`, `244`, `264`, `268`, `340`, `343`, `344`, `386`, `387`, `390`, `397` | Needs a renderer with a real 3D pipeline. This environment builds `STUB` and `HEADLESS`, neither of which rasterises. `GLTF-343`/`344` now reach shader-ready F0/F90 at L6; consuming those values in every renderer is the blocked residue. |
| **second/third renderer** | 10 — `GLTF-158`, `160`, `168`, `234`, `373`, `379`, `384`, `385`, `389`, `398` | `scripts/gltf-renderer-parity.sh` already performs the comparison; `OPENGLES3`/`VULKAN` need sibling checkouts and a GPU. `GLTF-017`/`382`/`383`/`388` were closable *because* `HEADLESS` builds here. |
| **libdraco** | 8 — `GLTF-271`, `288`, `353`, `359`–`361`, `363`, `364` | `libdraco-dev` is not installed; the Draco decode path is `#ifdef CNA_DRACO_AVAILABLE`. **The cheapest unblock on this list.** |
| **`cna-gltf-viewer` repo** | 12 — `GLTF-323`, `422`–`432` | A separate repository. §27.1 row 20 depends on it, so `GLTF-458` cannot be declared from here. |
| **third-party assets** | 7 — `GLTF-013`, `014`, `018`, `405`–`407`, `411` | Needs pinned, licence-reviewed external sample models. |
| **CI configuration** | 2 — `GLTF-019`, `420` | Needs the repository's CI settings (required-check configuration), not reachable from a working tree. |
| **renderer that loses its context** | 1 — `GLTF-439` | `DebugSimulateContextLoss()` is a no-op on both renderers here, so a test would measure the no-op. |

The remaining **~36 are doable in this environment.**

## Suggested next clusters

Ordered by value, not by number. Each is a coherent unit with its own tests and one commit.
Rewritten 2026-08-12 after that session closed 57 rows; the earlier list is superseded.

1. **`GLTF-399` — finish the corpus (140/144 assets today).** Fourteen owning groups are complete;
   accessors are 13/13, normals 6/6 and container is now 8/8. The generator emits external `.bin`
   and image sidecars from the same fixture source, while GLB twins stay self-contained; exact
   source spellings, BIN padding and byte parity are directly asserted. The four exact missing IDs
   are generated into `manifest.json`; current + missing = target is checked per group, so the
   former 135/136/141 count disagreement cannot recur. **Only Draco 0/4 remains**, blocked on the
   pinned `libdraco` encoder/decoder integration.
   The normals group now has all four named witnesses at L1–L5. `GLTF-175`/`GLTF-176` remain
   partial because their L7 acceptance needs a rasterising renderer; in particular the current
   shaders still omit `sign(det(world))` from mirrored tangent handedness.
2. **Phase 21 viewer rows are the largest *blocked* group and the only path to `GLTF-458`.**
   `GLTF-422`–`GLTF-432` live in `openeggbert/cna-gltf-viewer`. §27.1 row 20 cannot go green
   without them, so **GLTF CORE 2.0 CORRECT cannot be declared from this repository alone** —
   that is the single most useful thing to tell whoever asks why the milestone is still open.
3. **`GLTF-343` + `GLTF-344` are now that defensible split, not a next task.** Raw IOR/specular
   factors survive direct and offline import, both PBR effects and `.cnj`; `GpuDrawParams` carries
   the Khronos-derived dielectric F0/F90; a discriminating analytic/L3/L6 witness pins clamp order,
   defaults and direct/offline parity. Both rows are `✅/⬜`. Do **not** finish them here by editing
   shaders: no renderer in this environment rasterises, and `GLTF-157` already established that an
   unverified renderer change is not a fix. The two optional specular textures remain absent too.
4. **The remaining Draco rows** (`GLTF-271`, `288`, `353`, `359`–`361`, `363`, `364`) need only
   `apt-get install libdraco-dev` — the *cheapest* unblock on the list if the owner allows it, and
   it turns eight blocked rows into ordinary work.
5. **Second-renderer rows** (`GLTF-158`, `160`, `168`, `234`, `373`, `379`, `384`, `385`, `389`,
   `398`). `scripts/gltf-renderer-parity.sh` already does the comparison; what is missing is a
   third and fourth renderer to point it at.

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
| `tests/assets/gltf/` | Generated corpus: **140 assets, 694 files**, including sidecars and `manifest.json`'s defect ledger. Never edited by hand. |
| `modules/content/src/GltfImport/GltfImportCore.cpp` | The importer. Extraction, skeletons, clips, lights, cameras, the extension registry, the stride table. |
| `modules/content/src/Xna/ContentManager.cpp` | The runtime `.gltf` loader **and** the `.cnj` reader. Both must agree; several tests assert exactly that. |
| `tools/gltf_to_cnj/gltf_to_cnj.cpp` | The offline converter — the second loader. |
| `modules/content/tests/CNA/Internal/GltfImport/` | Every `Gltf*` suite, plus the oracles. |
| `cmake/UnitTests.cmake` | `CNA_GLTF_CONFORMANCE_RUNGS` — the ladder's single source of truth (10 rungs since the `Perf` one joined). |
| `scripts/regenerate-gltf-goldens.sh` | Regenerate the corpus, verify it, decode any binary golden that moved, and (`--determinism`) prove two processes emit the same bytes. |
| `scripts/gltf-renderer-parity.sh` | Compare two build directories; fails on any L1–L5 difference, tolerates only `SKIPPED`-vs-`OK`. |
| `docs/gltf-conventions.md` | Every decision with a rationale: transforms, mirroring, colour space, effect selection, lighting, animation, extensions. |
| `docs/gltf-performance.md` | Phase 22's measurements and the decision each led to — the parse/cache costs, the 4× unpack ceiling, the 2× morph duplication, the occlusion codec. Reproduce with `--gtest_filter='GltfPerformance.*' --gtest_output=xml:`. |
| `docs/gltf-limitations.md` | The inverse: what cannot be carried, what is approximated, and the report field that names each loss. Its §1 is generated from the extension registry and its report fields are checked against the header — see `GltfLimitationsDoc`. |
| `docs/gltf-conformance.md` | The oracle ladder and the spec pin. |
| `docs/gltf-api-change-review.md` | `GLTF-025`'s gate. Read §4 before proposing public API. |
| `known_bugs.md` | Defects found outside the plan's own rows. |
