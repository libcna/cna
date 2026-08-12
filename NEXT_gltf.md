# glTF campaign continuity

Read this before `plan_gltf.md`. The plan is 2 700 lines and its own header used to say "nothing in
it was implemented", which stopped being true a long time ago; this file is the short version a new
session needs to start work without re-deriving the state.

## Session status

- **Branch:** `feature/gltf_`, pushed. Never push elsewhere without explicit permission. No pull
  request has been opened and none should be unless asked. (The campaign ran on
  `claude/gltf-011-center-collapse-swdjna` until 2026-08-12.)
- **Working document:** `plan_gltf.md`, 460 numbered rows. **340 closed (`✔` 219, `✅` 121),
  103 `⬜` remaining**, plus `GLTF-388`'s and `GLTF-449`'s new `✅/⬜` partials, plus `GLTF-449`'s new `✅/⬜`. The other 18 carry a deliberate partial marker: 8 `🔬` (investigation, no
  implementation owed), 3 `✅/⬜` and 2 `✅/🐛` (landed with a named residue — `GLTF-093`, `252`,
  `265`, `289`, `449`; `GLTF-064`/`067`/`068` were completed on 2026-08-12), 2 `🐛` (open: `GLTF-157`, `421`), and 1 `⛔` (`GLTF-009`,
  blocked by this environment).
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
ctest --test-dir "$B" -L gltf-conformance    # the 9-rung ladder
"$B"/CnaTests                                # the whole suite, from the ROOT
scripts/regenerate-gltf-goldens.sh --check    # the corpus, against its own generator
```

Expected as of this writing:

| Check | Expected |
|---|---|
| `ctest -L gltf-conformance` | **9/9 passed** |
| full suite | **6 311 passed, 18 failed** |
| generator `--check` | **74 assets, 359 files — byte-identical** |
| `*Gltf*` on `STUB` / `HEADLESS` | **424 passed, 23 skipped** / **447 passed, 0 skipped** |

**Those 18 failures are pre-existing and unrelated to glTF.** They are the STUB renderer's
capability expectations (`GraphicsDeviceCapabilityTest.*`), the TextureCube DDS fixtures
(`TextureCubeTest.*`, `Texture3DTextureCubeContentTypeReaderTest.*`,
`XnbBuiltInReaderRegistrationTest.*`) and `CnjCapabilityMatrixTest.TextureCubeDelegatesViaSourceFile`.
Do not attempt to "fix" them as part of this campaign, and do not report a run as clean without
saying they are there.

There is a third tree for the **second renderer**,
`/media/robertvokac/claude/tmp/cna/cmake-build-gltf-headless` (`-DCNA_GRAPHICS_RENDERER=HEADLESS`).
It matters more than it sounds: `HEADLESS` reports `GraphicsCapability::ThreeD`, so the 15
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
  "$A"/CnaTests --gtest_filter='Gltf*'     # 417 passed, 15 skipped, 0 findings
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

A fixture's `.gltf`, `.glb`, `.expected.json` and L5 `.bin` goldens all come from one description,
so they cannot drift apart. Stdlib only — no third-party dependency is permitted in the generator.

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

45 of the 141 remaining rows cannot be finished here. Do not mark them done, and do not
work around them by weakening their acceptance.

| Blocker | Rows | Note |
|---|---|---|
| **L7 / rendered image** | 17 — `GLTF-016`, `175`, `176`, `182`, `189`, `213`, `218`, `235`, `244`, `264`, `268`, `384`–`386`, … | Needs a renderer with a real 3D pipeline. The environment builds `STUB`. `GLTF-235`'s BRDF spot-checks are here because the BRDF exists only inside per-renderer shader source strings — it is not callable from C++. |
| **second renderer** | 6 — `GLTF-158`, `160`, `168`, `234`, `373`, `398` | Differential validation needs `OPENGLES3`/`VULKAN` built and running. |
| **libdraco** | 8 — `GLTF-271`, `288`, `353`, `359`–`361`, `363`, `364` | `libdraco-dev` is not installed; the Draco decode path is `#ifdef CNA_DRACO_AVAILABLE`. |
| **`cna-gltf-viewer` repo** | 7 — `GLTF-128`, `323`, `342`, `425`, `427`, `430`, `431` | A separate repository, not attached to this session. |
| **third-party assets** | 3 — `GLTF-013`, `341`, `407` | Needs pinned external sample models. |
| **CI** | 4 — `GLTF-019`, `399`, `404`, `420` | Needs the CI configuration, not reachable from here. |

The remaining **96 are doable in this environment.**

## Suggested next clusters

Ordered by value, not by number. Each is a coherent unit with its own tests and one commit.

1. **`GLTF-236` + `GLTF-237` — the material data model.** The largest structural row left.
   `MeshOut` carries loose material fields; `CNAEXT.md` sketches a `PbrMaterial` carrier. Gated by
   `GLTF-025`, so it needs a `docs/gltf-api-change-review.md` §1 entry first. `GLTF-244` (material
   L6/L7 regression) and several Phase 18 rows sit behind it.
2. ~~**`GLTF-034` + `GLTF-035` — the structured import report.**~~ **Both rows now state their own
   deferral** (2026-08-12): every drop `GLTF-035` lists already has its report entry and its
   fixture; what is missing is one structured object to read them from, and the review gate
   deferred that surface *for want of a consumer* — which is `GLTF-431`, in the viewer's own
   repository. Do not implement it here without either a consumer in this repository or an
   explicit decision to revisit the gate.
3. **Phase 18 effect-boundary rows** — `GLTF-365`, `367`, `368`, `370`–`372`, `377`, `380`–`383`.
   Most are assertable at L6 through `CaptureDrawParamsEXT` on `STUB`, which is how `GLTF-267` and
   the lighting rows were done. Check each row for whether it truly needs a renderer before
   classifying it as blocked.
4. **`GLTF-343` + `GLTF-344` — `KHR_materials_ior` / `_specular`.** Both are `F0` plumbing and share
   a shader change; the registry already classifies them `PARSED_BUT_IGNORED` with that reason.
5. **`GLTF-039` + `GLTF-040` — overflow checks and a container fuzz seed.** `GLTF-102`'s attribute
   fuzz is the template: seeded, valid-by-construction, asserting a property rather than absence of
   a crash.
6. **Phase 23 documentation rows** — `GLTF-445`–`GLTF-457`. Cheap, and several are already
   *substantially* true (`docs/gltf-conventions.md` and `docs/gltf-conformance.md` have grown
   throughout); each row wants a specific check rather than new prose. `GLTF-448` (rewrite
   `CNAEXT.md` §3.2 to match reality) is the one with real risk of being stale.

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
| `tests/assets/gltf/` | Generated corpus: 69 assets, 338 files, including `manifest.json`'s defect ledger. |
| `modules/content/src/GltfImport/GltfImportCore.cpp` | The importer. Extraction, skeletons, clips, lights, cameras, the extension registry, the stride table. |
| `modules/content/src/Xna/ContentManager.cpp` | The runtime `.gltf` loader **and** the `.cnj` reader. Both must agree; several tests assert exactly that. |
| `tools/gltf_to_cnj/gltf_to_cnj.cpp` | The offline converter — the second loader. |
| `modules/content/tests/CNA/Internal/GltfImport/` | Every `Gltf*` suite, plus the oracles. |
| `cmake/UnitTests.cmake` | `CNA_GLTF_CONFORMANCE_RUNGS` — the ladder's single source of truth. |
| `docs/gltf-conventions.md` | Every decision with a rationale: transforms, mirroring, colour space, effect selection, lighting, animation, extensions. |
| `docs/gltf-limitations.md` | The inverse: what cannot be carried, what is approximated, and the report field that names each loss. Its §1 is generated from the extension registry and its report fields are checked against the header — see `GltfLimitationsDoc`. |
| `docs/gltf-conformance.md` | The oracle ladder and the spec pin. |
| `docs/gltf-api-change-review.md` | `GLTF-025`'s gate. Read §4 before proposing public API. |
| `known_bugs.md` | Defects found outside the plan's own rows. |
