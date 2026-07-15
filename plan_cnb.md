# `.cnb` content format — implementation plan for CNA

> **Status: 🚧 Core implementation complete; Phase 9 hardening planned.** Phases 0–8
> (`CNB-1`–`CNB-31`) were completed on 2026-07-15 on `feature/cnb`. A subsequent review found
> the core design sound, but identified important hardening, cache-isolation, capability-contract,
> and headless-verification work that must land before `.cnb` should be treated as a broadly
> extensible, production-ready content substrate. That follow-up is Phase 9 (`CNB-32`–`CNB-39`)
> below; it does not reopen or invalidate the completed migration tasks.
> Full `CnaTests` regression after the post-completion fixes: 4408 tests, 4406 passed, 2 pre-existing unrelated hardware skips
> (`Accelerometer`/`Gyroscope`), 0 failures — a clean baseline both before this plan started and
> after every phase landed. Dedicated `.cnb`-specific coverage: 31/31 passing across 8 gtest
> suites (`ParseCnbEnvelopeTest`, `ValidateCnbEnvelopeTest`, `CnbResolverOrderTest`,
> `CnbSourceFileTest`, `CnbSpriteFontTest`, `CnbEffectTest`, `CnbModelTest`,
> `CnbCustomLoaderTest`). `SkinnedModelTypeReader`/`.skinnedmodel.json` was deliberately kept
> separate, not migrated (`CNB-22`'s recorded decision) — every other reader now speaks `.cnb`.
> `xnb.md`/`plan_xnb.md` are frozen as "researched, not adopted" (`CNB-30`). Not yet merged to
> `develop` — awaiting the project owner's decision on next steps (PR, merge, etc.).
>
> **Post-completion review found and fixed 2 real bugs, same day.** (1) `GenericCnbTypeReader<T>`
> (the `RegisterCnbLoader<T>` dispatch path, `CNB-25`) checked only `"type"`, not `"cnbVersion"` —
> a `.cnb` missing `cnbVersion` entirely could still dispatch to a registered factory, bypassing
> `CNB-2`'s validation contract that every other reader enforces. Fixed: it now checks both, like
> `ValidateCnbEnvelope` does. (2) `CnbEnvelope.hpp`'s field scanner (`CNB-1`) matched the first
> textual occurrence of `"type"`/`"cnbVersion"` anywhere in the document, not just at the JSON
> root — a document with no genuine top-level field but a same-named field nested inside e.g.
> `"meshes"` could be misparsed. Fixed with a proper depth- and string-literal-aware top-level-only
> scanner. Both bugs confirmed real by reverting each fix in isolation and observing the new
> regression tests fail with the exact predicted symptom, not just passing after the fix. 4 new
> tests added (3 for the scoping bug, 1 for the missing-`cnbVersion` bypass); full-suite regression
> after both fixes: 4408 tests, 4406 passed, same 2 pre-existing skips, 0 failures.
>
> Turns [`cnb.md`](cnb.md)'s design into a phased, numbered task list (`CNB-1`, `CNB-2`, ...),
> mirroring how [`plan_xnb.md`](plan_xnb.md) turned [`xnb.md`](xnb.md) into concrete tasks. Read
> `cnb.md` first — this file assumes its design decisions (resolution order, envelope shape,
> `sourceFile`, `RegisterCnbLoader<T>`) as given and does not re-argue them.
>
> Unlike `plan_xnb.md`, this was **mostly a migration of already-working, already-tested-or-
> exercised readers** (`SpriteFontTypeReader`, `EffectTypeReader`, `ModelTypeReader`), not a
> green-field protocol build — confirmed in practice: every phase's full-suite regression stayed
> at 0 failures, and each of the 3 migrated readers' pre-existing example-program behavior (where
> any existed) was re-verified passing after its migration.

## Execution-order mandate for autonomous work

- Implement strictly in phase order. Phase 0/1 (envelope + resolver reorder) must land and pass a
  full regression run *before* any reader migration (Phase 3+) begins — every later phase depends on
  the resolver already trying `.cnb` first.
- After each phase: run the full `CnaTests` suite (regression — this plan must never reduce existing
  pass counts) plus any new `.cnb`-specific tests added in that phase, then commit before continuing.
  Do not accumulate multiple phases of uncommitted work.
- Per this project's git-commit convention: one task = one commit, referencing the task ID
  (e.g. `feat(Task CNB-4): ...`).
- Every task that changes or adds a public `ContentManager`/`ContentTypeReader` method must ship
  with tests in the same commit, per `CHECKLIST.md` — do not defer test-writing to a later task.

## Milestones

| Milestone | Reached after | Definition of "done" |
|---|---|---|
| M1 — envelope + resolver | CNB-6 | `.cnb` envelope parses/validates; `ResolveAssetPath` tries `.cnb` before native extensions for every registered type; full existing suite still green with zero `.cnb` files anywhere in test/example content (purely additive so far). |
| M2 — `sourceFile` proven | CNB-10 | A real `Content/x.png` + `Content/x.cnb` (`sourceFile` + `colorKey`) pair loads end-to-end through `Texture2D`, with passing tests. |
| M3 — four readers migrated | CNB-27 | `SpriteFontTypeReader`, `EffectTypeReader`, `ModelTypeReader` load via `.cnb`; `SkinnedModelTypeReader`'s fate (migrate or stay separate) is decided and recorded; every existing `.font.json`/`.model.json`/`.shader.json`/`.skinnedmodel.json` fixture in the repo is either migrated or explicitly kept per that decision. |
| M4 — custom loaders | CNB-31 | `RegisterCnbLoader<T>` implemented, tested, and documented with a worked example. |
| M5 — sidecar hardening | CNB-39 | `sourceFile` cannot escape the content root, chain into `.cnb`, recurse forever, or mutate the separately-loadable native asset; the documented capability matrix, cache semantics, strict envelope policy, custom-loader registration behavior, and headless test lane are all verified. |

## Scope

**In scope**: the content-format/resolver plumbing `cnb.md` describes — the `.cnb` envelope, the
`.cnb`-first resolution order, `sourceFile` sidecar support, migrating the four existing JSON readers
onto `.cnb`, `RegisterCnbLoader<T>`, and Phase 9's hardening of those mechanisms.

**Out of scope for this plan**:
- The actual XNA→CNA *content* migration/export tooling (`cnb.md`'s "What migration actually
  requires" section) — a separate, later effort, orthogonal to whether CNA's own JSON conventions
  use one extension or four. Worth being explicit about what that later effort actually looks like,
  since it differs sharply by type:
  - **`Texture2D`/`SoundEffect` (native passthrough types)**: usually little to no export tooling
    needed. XNA's `.xnb` for these just wraps the artist's original `.png`/`.wav`; CNA can often
    load that original pre-build source file directly (no `.cnb` involved at all), since it already
    reads native extensions. `.cnb` here is optional enrichment (Phase 2's `sourceFile`), never a
    migration requirement.
  - **`SpriteFont`/`Model`/`Effect` (processed types)**: real export tooling *is* required. The
    pre-`.xnb` source (a `.spritefont` XML description, an `.fbx`/`.x` mesh, an `.fx` HLSL file)
    isn't loadable by CNA at all — and, more importantly, the data that actually goes *into* a
    `.cnb` for these types (a font's glyph atlas + per-character metrics, a model's packed vertex/
    index buffers + bone hierarchy, an effect's resolved parameters) is the **output of XNA's real
    content-pipeline build step** applied to that source, not the source itself. That output doesn't
    exist anywhere until something actually runs XNA's real font rasterizer / FBX importer / HLSL
    compiler. Producing it needs either (a) a real XNA/MonoGame runtime (Windows or Wine) plus a
    small one-time export tool that walks the already-built object graph and dumps it to `.cnb` +
    sidecars, or (b) a from-scratch modern replacement per type (e.g. a glTF/OBJ importer instead of
    XNA's FBX pipeline, a modern bitmap-font generator instead of XNA's `SpriteFont` build step).
    Compiled `.fx` shader bytecode has no solution either way — it must be hand-rewritten for CNA's
    backend regardless of migration strategy.
- Binary `.xnb` reading — that is `plan_xnb.md`, a different, not-adopted strategy.
- New engine capability not already exposed by an existing public API. `sourceFile`'s proof-of-concept
  metadata field (`colorKey`) is included *because* it only needs `Texture2D`'s existing
  `GetData`/`SetData(Color*)` API — anything requiring a genuinely new `Texture2D`/`SoundEffect`
  capability (premultiplied-alpha conversion, atlas sub-rects, mip generation, ...) is explicitly
  deferred (see CNB-10).
- Fixing `ModelTypeReader`'s pre-existing behavioral gaps versus FNA (bone hierarchy, `ParentBone`,
  `BoundingSphere`, `Tag` — see `docs/model-content-pipeline-support.md`) is **not required** by this
  plan, but CNB-21 requires an explicit decision (fix now vs. deliberately deferred with a note), not
  silent carry-forward.

## Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Mandatory, not started |
| 🔄 | In progress |
| ✅ | Done |
| ⏸ | Deferred / optional, not milestone-blocking |

---

## Phase 0 — Envelope & validation primitives

> Foundation only — no resolver behavior changes yet, so nothing in this phase can regress existing
> content loading.

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-1 | Shared `.cnb` envelope parser: extract `cnbVersion` (int), `type` (string), `sourceFile` (optional string) from a `.cnb` document, built on CNA's existing hand-rolled JSON helpers in `ContentManager.cpp` (`ExtractJsonStringField`/`JsonInt`/...) rather than introducing a new JSON library | ✅ | Landed as `CNA::Internal::ParseCnbEnvelope`/`CnbEnvelope` in `include/CNA/Internal/CnbEnvelope.hpp` (header-only, matching the `Utf8Decode.hpp` precedent) rather than inside `ContentManager.cpp`'s anonymous namespace, since the latter has internal linkage and can't be unit-tested directly — this keeps CNB-1/2 independently testable per CNB-3 |
| CNB-2 | Envelope validation helper: given the parsed `type` and the expected type name for the reader that opened the file, throw `ContentLoadException` with a message naming both the expected and actual type on mismatch | ✅ | `CNA::Internal::ValidateCnbEnvelope` in the same header; also rejects a missing `cnbVersion`/`type` (not just a mismatch) — needed for CNB-3's missing-field tests |
| CNB-3 | Unit tests for CNB-1/CNB-2: valid envelope; missing `cnbVersion`; missing `type`; `type` mismatch (verify the thrown message names both types); `sourceFile` present; `sourceFile` absent; unrelated/unknown top-level fields in the document are ignored, not errors | ✅ | `tests/CNA/Internal/CnbEnvelopeTests.cpp` (10 cases) — placed under `tests/CNA/Internal/` to mirror `include/CNA/Internal/`, not `tests/Microsoft/Xna/Framework/Content/`, once CNB-1 moved out of `ContentManager.cpp`. Verified passing both standalone (g++ direct against vendored gtest) and inside the full `CnaTests` binary; full-suite regression run: 4387 tests, 4385 passed, 2 pre-existing hardware skips (Accelerometer/Gyroscope), 0 failures |

---

## Phase 1 — Resolver reorder (`.cnb` first, native fallback)

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-4 | Change `ContentManager::ResolveAssetPath` (`ContentManager.hpp`) to try `<name>.cnb` **before** any extension in `reader.GetExtensions()`, for every registered type — not just the four JSON types | ✅ | One `if (std::filesystem::exists(cnbCandidate)) return cnbCandidate;` check inserted between the literal-path check and the reader-extension loop — applies automatically to all three explicit `Load<T>()` specializations (`Texture2D`/`SoundEffect`/`TextureCube`) too, since they all still call the shared `ResolveAssetPath` |
| CNB-5 | Regression proof: run the full existing `CnaTests` suite with zero `.cnb` files anywhere in test/example content and confirm pass counts are unchanged from `master` | ✅ | Full suite before CNB-4: 4387 tests, 4385 passed, 2 pre-existing hardware skips, 0 failures. After CNB-4+CNB-6: 4390 tests (+3 new), 4388 passed (+3 new), same 2 skips, 0 failures — confirms the reorder is purely additive |
| CNB-6 | New tests: (a) an asset with only a native file still resolves via extension, unchanged; (b) an asset with only a `.cnb` resolves via `.cnb`; (c) an asset with **both** a native file and an unrelated self-contained `.cnb` of the same name resolves to the `.cnb` (the "authoring footgun" `cnb.md` documents — assert it's intended, tested behavior, not a surprise) | ✅ | `tests/Microsoft/Xna/Framework/Content/CnbResolverOrderTests.cpp` (3 cases) — tests through `Texture2DTypeReader` without needing any reader to understand `.cnb` content yet: `Texture2D`'s decoder sniffs real image bytes regardless of extension, so writing a real PNG's bytes to a `*.cnb` path and using pixel color as the observable signal proves which candidate the resolver picked. Milestone M1 reached |

---

## Phase 2 — `sourceFile` sidecar, proven on `Texture2D`

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-7 | Generic `sourceFile` resolution: when a `.cnb` envelope has `sourceFile`, resolve and load that referenced path through the same two-step rule (recursively through the owning `ContentManager`, same caching as any other reference per `cnb.md`'s "Referencing other files") | ✅ | `ResolveSourceFileLogicalName()` in `ContentManager.cpp` reconstructs the logical asset name (root-relative) from the `.cnb`'s own resolved directory, then delegates to `cm.Load<T>(...)` recursively — real `ContentManager` caching, not a bypass |
| CNB-8 | Wire `sourceFile` + a `colorKey` field (`[r,g,b]`) into `Texture2DTypeReader`: load the referenced native image normally, then scan/replace matching pixels via the existing `GetData`/`SetData(Color*, int)` API (`Texture2D.hpp`) — no new `Texture2D` capability required | ✅ | First real, non-JSON-native consumer of `sourceFile`. Side effect: this made `Texture2DTypeReader` actually parse `.cnb` content, which broke 2 of CNB-6's own tests (`OnlyCnbFileResolvesViaExtension`/`CnbTakesPriorityOverNativeFileOfSameName`) — they had written raw PNG *bytes* directly into a `.cnb` path pre-Phase-2, relying on content-sniffing; fixed by rewriting them to use a real envelope + `sourceFile` instead, matching actual `.cnb` semantics |
| CNB-9 | Tests: `ahoj.png` + `ahoj.cnb` (`"sourceFile": "ahoj.png"`, `"colorKey": [255,0,255]`) loads, magenta pixels become transparent (alpha 0), non-keyed pixels unchanged; `ahoj.png` alone (no `.cnb`) still loads byte-for-byte unchanged (regression); a `.cnb` whose `sourceFile` points at a missing file throws `ContentLoadException` | ✅ | `tests/Microsoft/Xna/Framework/Content/CnbSourceFileTests.cpp` (4 cases). One correction from the plan as written: a missing `sourceFile` target throws `std::runtime_error` (from `ImageLoader::Load`), not `ContentLoadException` — matches how a missing native `Texture2D` file already fails today; CNB-7/8 deliberately don't re-wrap that into a different exception type. Full-suite regression: 4394 tests, 4392 passed (+7 net new across CNB-6 fix and CNB-9), same 2 pre-existing hardware skips, 0 failures. Milestone M2 reached |
| CNB-10 | ⏸ Additional `Texture2D` `.cnb` metadata fields (`premultipliedAlpha`, atlas sub-rect, mip-generation hints) | ⏸ | Explicitly deferred — each needs real `Texture2D`/`GraphicsDevice` capability CNA doesn't have today; do not add speculatively (per project convention against designing for hypothetical future requirements) |

---

## Phase 3 — Migrate `SpriteFontTypeReader` (`.font.json` → `.cnb`)

> No existing test or example anywhere references `.font.json`/`SpriteFontTypeReader` (confirmed by
> repo-wide search) — this phase adds the *first* coverage for this reader, not just a re-point.

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-11 | Update `SpriteFontTypeReader::GetExtensions()`/`Read()` to expect the `.cnb` envelope (`cnbVersion`/`type` via CNB-1/CNB-2) wrapped around today's unchanged field parsing (`texture`/`lineSpacing`/`spacing`/`defaultCharacter`/`glyphs[].char`/`source`/`crop`/`kerning`, per `cnb.md`'s worked example) | ✅ | `GetExtensions()` → `{".cnb"}`; `Read()` gains one `ParseCnbEnvelope`+`ValidateCnbEnvelope` call up front, all existing field-scanning code below it untouched — field names really did stay exactly as-is |
| CNB-12 | Add at least one real `.cnb` `SpriteFont` fixture (texture atlas + a handful of glyphs) under `tests/`/`examples/` content | ✅ | Inline fixture generated at test time in `CnbSpriteFontTests.cpp` (16x24 solid-color atlas PNG + a one-glyph `.cnb`), not a checked-in static asset — matches this repo's existing `ScratchContentRoot` test convention rather than adding binary fixture files to the tree |
| CNB-13 | Tests: `Content.Load<SpriteFont>("...")` against the CNB-12 fixture succeeds and matches expected glyph/kerning/spacing values; a `.cnb` with `"type": "Model"` requested as `Load<SpriteFont>()` throws with a message naming both types (exercises CNB-2 through a real reader) | ✅ | `tests/Microsoft/Xna/Framework/Content/CnbSpriteFontTests.cpp` (2 cases) — first-ever gtest coverage of `SpriteFontTypeReader`. Full-suite regression: 4396 tests, 4394 passed, same 2 pre-existing hardware skips, 0 failures |

---

## Phase 4 — Migrate `EffectTypeReader` (`.shader.json` → `.cnb`)

> **Correction**: unlike Phase 3, this assumption was wrong — `examples/easygl_bloom_extract_test.cpp`
> (a standalone example program, not part of `CnaTests`) does exercise this reader via `.shader.json`.
> No *gtest* coverage existed, though (confirmed separately) — that part of the original assumption
> holds.

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-14 | Update `EffectTypeReader::GetExtensions()`/`Read()` to expect the `.cnb` envelope, same shape as CNB-11 | ✅ | Also preserved the reader's pre-existing "append `.shader.json` if the path doesn't already end with it" defensive re-append, just retargeted at `.cnb` |
| CNB-15 | Add at least one real `.cnb` `Effect`/stock-effect-parameter fixture | ✅ | Two fixtures: `CnbEffectTests.cpp`'s inline one, and `examples/easygl_bloom_extract_test.cpp`'s `bloom_extract.shader.json` → `bloom_extract.cnb` (`"type": "Effect"`) migration — rebuilt and re-ran that example standalone (`cna_test_easygl_bloom_extract`), still `[PASS]` after the migration |
| CNB-16 | Tests: `Content.Load<...>()` against the CNB-15 fixture succeeds; type-mismatch test as in CNB-13 | ✅ | `tests/Microsoft/Xna/Framework/Content/CnbEffectTests.cpp` (2 cases) — first-ever *gtest* coverage of `EffectTypeReader`. Full-suite regression: 4398 tests, 4396 passed, same 2 pre-existing hardware skips, 0 failures |

---

## Phase 5 — Migrate `ModelTypeReader` (`.model.json` → `.cnb`)

> Larger blast radius than Phase 3/4: seven `examples/*model_json*.cpp` programs already exercise
> `Content.Load<Model>()` against real `.model.json` fixtures (`easygl_model_json_reader_test.cpp`
> and siblings) — these must be migrated, not just left behind. Per `docs/model-content-pipeline-
> support.md`, there is still **zero gtest (`tests/`) coverage** of `ModelTypeReader` even though
> example-program coverage exists; per `CHECKLIST.md`, this phase must close that gap, not carry it
> forward silently.

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-17 | Update `ModelTypeReader::GetExtensions()`/`Read()` to expect the `.cnb` envelope, same shape as CNB-11, wrapped around today's unchanged `bones`/`meshes[].vertices`/`indices`/`vertexStride`/`texture` fields + binary sidecars | ✅ | Same minimal insertion pattern as SpriteFont/Effect — envelope check right after `ReadTextFile`, none of the ~200 lines of existing bone/mesh/vertex/skinning parsing below it touched |
| CNB-18 | Migrate every existing `*.model.json` fixture referenced by `examples/easygl_model_json_reader_test.cpp`, `..._texture_test.cpp`, `..._bone_hierarchy_test.cpp`, `..._skeleton_test.cpp`, `..._32bit_indices_test.cpp`, `easygl_model_skinned_animation_playback_test.cpp`, `bgfx_model_json_reader_test.cpp` to `.cnb` (rename + add envelope fields); update each example to reference the new filename | ✅ | All 7 migrated (filename + `cnbVersion`/`type` fields added, binary sidecars untouched). 6 of 7 have a runnable target in this build config (`bgfx_model_json_reader_test` needs the BGFX backend, not built here) — all 6 rebuilt and re-run standalone, all still pass in full (`cna_test_easygl_model_json_reader_bone_hierarchy`: 8/8, `..._skeleton`: 14/14, others 2/2) |
| CNB-19 | Add real gtest (`tests/Microsoft/Xna/Framework/Content/`) coverage for `Content.Load<Model>()` via `.cnb` — first-ever gtest coverage for this reader, closing the gap `docs/model-content-pipeline-support.md` flags | ✅ | `tests/Microsoft/Xna/Framework/Content/CnbModelTests.cpp` — adapted the quad fixture from CNB-18's `easygl_model_json_reader_test.cpp`; asserts mesh count/name/part count |
| CNB-20 | Type-mismatch test as in CNB-13, for `ModelTypeReader` | ✅ | Same file, second case. Full-suite regression: 4400 tests, 4398 passed, same 2 pre-existing hardware skips, 0 failures |
| CNB-21 | Explicit decision, recorded in this row: fix `ModelTypeReader`'s known FNA-behavior gaps (bone hierarchy always synthesizes exactly one bone, `ParentBone` never assigned, `BoundingSphere` never set, `Tag` never set — see `docs/model-content-pipeline-support.md`) in this same migration pass, or deliberately defer with a dated note here. Do not silently freeze the gaps into `.cnb` fixtures without a decision either way | ✅ | **Decision (2026-07-15): deferred, not fixed here.** These are pre-existing FNA-fidelity gaps, independent of the file-format/extension change this plan is making — fixing them is substantial new engineering (real multi-bone hierarchy resolution, `ParentBone` wiring, `BoundingSphere` computation), not a mechanical migration, and bundling it into this task would both violate "one task = one commit" and risk new regressions in an already-passing area mid-migration. Left as an explicitly tracked, separate follow-up (`docs/model-content-pipeline-support.md` is where it's already documented) — not silently carried forward unmentioned |

---

## Phase 6 — `SkinnedModelTypeReader` decision (`.skinnedmodel.json`, NOXNA/Avatar-only)

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-22 | Decide and record here: migrate `SkinnedModelTypeReader` to `.cnb` alongside the other three, or keep `.skinnedmodel.json` permanently separate (it is already a deliberately distinct, non-`Model`-shaped `NOXNA` system — see `docs/avatar-real-rendering-ext.md`) | ✅ | **Decision (2026-07-15): keep `.skinnedmodel.json` permanently separate — do not migrate.** Four reasons, weighed after reading `docs/avatar-real-rendering-ext.md` in full: (1) it is explicitly documented as a deliberately separate, non-`Model`-shaped `NOXNA` system — the extension itself already signals "this is Avatar/GPU-skinned content," a distinction worth keeping visible, unlike `SpriteFont`/`Model`/`Effect` which are core XNA concepts that benefit from unification; (2) real, cross-language tooling outside this C++ migration's scope hardcodes the `.skinnedmodel.json` extension — confirmed via `grep`, `tools/avatar_asset_pipeline/convert_avatar.py` and `tools/avatar_builder/generate_wardrobe.py` both emit it, plus two READMEs document it, so migrating would mean coordinating a Python asset-pipeline change alongside the C++ one, not a same-language mechanical rename; (3) it already has real production content (`examples/demo_avatar/Content/avatar/{male,female}` + 2 wardrobe items, Blender-pipeline-generated, not synthetic fixtures) and the most mature existing gtest coverage of any of the four readers (`ContentManagerSkinnedModelTests.cpp`, truncation/corruption edge cases already covered) — the highest blast radius to touch for the least incremental benefit; (4) it has the most documented historical fragility of the four readers (3 real bugs found via integration testing: root-vs-manifest-directory path resolution, a C++ argument-evaluation-order bug, a bone-hierarchy-reordering bug) — minimizing touches to it reduces regression risk. None of this blocks `.cnb`'s adoption for the four core types; it's an explicit, reasoned exception, not an oversight |
| CNB-23 | If CNB-22 chooses migration: update `SkinnedModelTypeReader` same as CNB-11/17, migrate every `examples/demo_avatar/Content/**/*.skinnedmodel.json` fixture, and update `tests/Microsoft/Xna/Framework/Content/ContentManagerSkinnedModelTests.cpp` (existing real gtest coverage — must stay green) | ✅ | **Not performed** — CNB-22 decided against migration, so this task does not apply. `.skinnedmodel.json`/`SkinnedModelTypeReader` are untouched by this plan |

---

## Phase 7 — Custom loader registry (`RegisterCnbLoader<T>`)

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-24 | Implement `ContentManager::RegisterCnbLoader<T>(const std::string& typeName, CnbLoaderFn<T> factory)` per `cnb.md`'s "Custom loaders" section — a second registry keyed by the `.cnb` `"type"` string, separate from the existing `std::type_index`-keyed `RegisterTypeReader<T>()` | ✅ | `ContentManager.hpp` (must live in the header, unlike CNB-1..23 — it's a template). Storage: `cnbNamedLoaders_` nests `std::type_index` → (`.cnb` type string → `std::any`-erased `CnbLoaderFn<T>`). `CnbLoaderFn<T>`'s first parameter is the raw JSON `std::string`, not a `JsonValue` (CNA has no JSON object type) — `cnb.md` updated to match. Marked `NOXNA` per `CLAUDE.md` |
| CNB-25 | Wire the fallback into the `.cnb` dispatch path: built-in per-type `type` values (`"SpriteFont"`, `"Model"`, ...) checked first, then this registry, then a clear "unknown `.cnb` type" `ContentLoadException` naming the unrecognized `type` string | ✅ | Implemented as a private nested `GenericCnbTypeReader<T>`, lazily auto-registered (via the existing `RegisterTypeReader<T>()`) the first time `RegisterCnbLoader<T>()` is called for a `T` with no reader yet. `RegisterCnbLoader<T>` throws `std::logic_error` immediately if a reader already exists for `T`, since that reader would never consult this table — no silent dead registration |
| CNB-26 | Tests: two distinct `.cnb` `type` names registered via two different factories, both producing the same `T`, both load correctly; an unregistered `type` throws the CNB-25 error; a registered factory can recursively `Load<...>()` a file it references | ✅ | `tests/Microsoft/Xna/Framework/Content/CnbCustomLoaderTests.cpp` (4 cases) — also covers the CNB-25 "already owned" guard (not originally listed here, added since it's real, easily-hit-by-mistake behavior) |
| CNB-27 | Worked example (in `docs/` or `examples/`) showing a game registering a custom `.cnb` type end-to-end, matching `cnb.md`'s `"EnemyDefinition"`/`"LootTable"` illustration | ✅ | Added directly to `cnb.md`'s "Custom loaders" section (matches how that section already carried the design, now updated with the real signature + a runnable-equivalent snippet). Full-suite regression: 4404 tests, 4402 passed, same 2 pre-existing hardware skips, 0 failures. Milestone M4 reached |

---

## Phase 8 — Cleanup / final documentation

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-28 | Update `cnb.md`'s status header to reflect what's actually implemented vs. still planned, once the phases above land | ✅ | Status blockquote rewritten to "✅ IMPLEMENTED"; "Relationship to CNA's existing per-type JSON conventions" and the former "Suggested next step" (now "Implementation record") sections rewritten past-tense to match reality instead of contradicting the new status |
| CNB-29 | Full `CnaTests` regression run + a dedicated `.cnb`-specific ctest tally, recorded in this file's status header | ✅ | Final confirmation run (no code changes since Phase 7's own regression): 4404 tests, 4402 passed, same 2 pre-existing hardware skips, 0 failures. Dedicated `--gtest_filter="*Cnb*"` tally: 27/27 passing across 8 suites |
| CNB-30 | Decide the fate of `xnb.md`/`plan_xnb.md` per `cnb.md`'s own "Relationship to `xnb.md`/`plan_xnb.md`" section (freeze as "researched, not adopted" vs. leave open) and record the decision in both files | ✅ | **Decision: froze both, per `cnb.md`'s own pre-existing recommendation.** Added a "🧊 FROZEN — researched, not adopted" status banner to the top of both `xnb.md` and `plan_xnb.md`, pointing at `cnb.md`/`plan_cnb.md` as the adopted strategy. Neither file's content was deleted — both remain as reference material, exactly as `cnb.md` itself already recommended before this plan started |
| CNB-31 | Final compliance sweep: every task above closed, every new/changed public method has Doxygen (per `CLAUDE.md`) and test coverage, no `.font.json`/`.model.json`/`.shader.json`/`.skinnedmodel.json` fixture left un-migrated except by the explicit CNB-22 decision | ✅ | Swept: zero `.font.json`/`.model.json`/`.shader.json` files remain anywhere in the repo (`find` + `GetExtensions()` grep both clean); `.skinnedmodel.json` files remain only per the CNB-22 decision. `CnbEnvelope.hpp`'s public struct/functions and `ContentManager.hpp`'s new `CnbLoaderFn`/`RegisterCnbLoader<T>` all have full Doxygen blocks. Every phase (0–7) closed with a passing regression run. All 31 tasks `✅` |

---

## Phase 9 — Hardening before broader `.cnb` adoption

> **Why this is a new phase, not a retroactive change to Phases 0–8:** the completed work proved the
> envelope, resolver order, three reader migrations, and custom-loader API. Post-completion review
> also established that `.cnb` is now important enough to need stronger guarantees at its boundaries:
> safe sidecar resolution, immutable source-asset semantics, a truthful per-reader capability
> contract, strict envelope/version handling, type-safe caching, registration validation, and a test
> path that does not depend on an interactive display. Implement these tasks in order. Each task is
> one commit, includes its tests, and runs the relevant focused tests plus the full available
> regression suite before the next task begins.

| # | Task | Status | Acceptance criteria / implementation notes |
|---|---|---|---|
| CNB-32 | Make `sourceFile` resolution safe and non-recursive | ✅ | New `CNA::Internal::ResolveCnbSourceFileSafely()` (`include/CNA/Internal/CnbSourceFile.hpp`) replaces the old unchecked `ResolveSourceFileLogicalName()`. Rejects: absolute `sourceFile`; a canonical target outside `RootDirectory` (`fs::weakly_canonical` + prefix check, catches both `..` traversal and symlink escapes); a target whose resolved extension is `.cnb` (explicit chain); a target with no `.cnb` extension but a sibling `<name>.cnb` that the normal resolver would pick first (closes the disguised self-cycle case too — the old code had no path-safety logic at all, every one of these was previously unguarded). 6 new tests in `tests/Microsoft/Xna/Framework/Content/CnbSourceFileSafetyTests.cpp`: in-root sibling, in-root nested payload, `../` escape, absolute path, `.cnb` chain, self-cycle via extensionless name. Full-suite regression: 4414 tests, 4412 passed, same 2 pre-existing hardware skips, 0 failures |
| CNB-33 | Isolate metadata-transformed textures from their native source cache entry | ⬜ | Today `Texture2D` values share a backend/CPU pixels through the weak cache. Loading `ahoj.cnb` first recursively caches `ahoj.png`, then `ApplyColorKey()` mutates that same shared texture; an explicit later `Load<Texture2D>("ahoj.png")` can therefore return the color-keyed image rather than the original native asset. Make the `.cnb` result an independently owned texture before applying metadata, while retaining ordinary caching for the unmodified source. Tests must load sidecar then native and native then sidecar in the same `ContentManager`; in both orders the sidecar has transformed pixels and the explicit native name has its original pixels. |
| CNB-34 | Define and enforce the `.cnb`/`sourceFile` capability matrix for every built-in reader | ⬜ | The resolver deliberately tries `.cnb` for every registered type, but only `Texture2D` currently understands a metadata sidecar. Implement and document explicit behavior instead of letting a native decoder attempt to consume JSON: `Texture2D` supports `sourceFile` + `colorKey`; `SoundEffect` and `TextureCube` support a `sourceFile` delegation with no metadata fields yet; `SpriteFont`, `Effect`, and `Model` reject `sourceFile` clearly because their `.cnb` documents are self-contained descriptors; game-specific `RegisterCnbLoader<T>` factories remain responsible for any source-file convention they define. Update `cnb.md` so it no longer claims universal metadata support before it exists. Add a test for each branch, including a clear `ContentLoadException` for an unsupported sidecar, not an incidental native-decoder error. |
| CNB-35 | Make envelope parsing and version policy strict | ⬜ | Define version 1 as the only supported envelope version for now. Reject missing, zero, negative, future, decimal, and trailing-garbage `cnbVersion` values with `ContentLoadException` that names the file and actual/supported value. Require a valid top-level JSON object and correctly decode JSON string escapes in `type` and `sourceFile`. Do not grow another ad-hoc, partially-validating scanner: adopt an existing approved JSON parser if one is already available, otherwise add one deliberately as a small documented dependency or a single shared, fully tested parser. Add malformed-document, escaped-string, non-object-root, and unsupported-version tests for built-in and custom-loader paths. |
| CNB-36 | Make the general asset cache type-safe | ⬜ | `loadedAssets_` is currently keyed only by normalized logical name. If one non-texture `T` is loaded first and a different `T` is later requested for the same logical `.cnb` name, `std::any_cast` can throw `std::bad_any_cast` before the envelope mismatch check runs. Key the general cache by both `std::type_index` and normalized logical name (or an equivalent collision-free key), preserve `Unload()` behavior, and ensure a second request of a different type reaches normal `.cnb` validation and produces `ContentLoadException` naming expected and actual types. Add a regression test. |
| CNB-37 | Make custom-loader registration deterministic and fail-fast | ⬜ | Reject an empty `typeName`, an empty factory, and duplicate registration of the same `(T, typeName)` with a clear `std::invalid_argument`/`std::logic_error`; do not silently replace a game factory. Retain the existing rejection of registering against an already-owned built-in/normal reader. Document this API contract in Doxygen and `cnb.md`, with tests for all three new guards and for two distinct names still coexisting for one `T`. |
| CNB-38 | Make `.cnb` verification runnable without a display and suitable for CI | ⬜ | Remove unnecessary `GraphicsDevice` fixtures from envelope and custom-loader cases that do not use graphics; keep graphics only in the tests that genuinely create/load textures, effects, or models. Add a documented headless test preset/job using `CNA_GRAPHICS_BACKEND=HEADLESS` (or the project's canonical equivalent) that runs all graphics-independent `.cnb` coverage with no `DISPLAY`, Wayland, or OpenGL context. Preserve the existing real-backend coverage for pixel/asset integration tests. Record the exact focused command and result in this plan. |
| CNB-39 | Update documentation and perform final Phase 9 compliance sweep | ⬜ | Update `cnb.md`, this status header, and worked examples to reflect the enforced version policy, sidecar restrictions, capability matrix, cache semantics, and registration contract. Confirm all CNB-32–CNB-38 public API changes have Doxygen and same-commit tests. Run the full available `CnaTests` suite plus the real-backend and headless `.cnb` focused suites; record totals, known skips, and environment-independent commands here. Milestone M5 is reached only with zero `.cnb` failures. |

---

## Relationship to other plan files

- **`cnb.md`** — the design this plan implements; read it first.
- **`xnb.md`/`plan_xnb.md`** — the alternative, not-adopted (binary `.xnb` reader) strategy; CNB-30
  is the task that formally records `.cnb`'s adoption status relative to it.
- **`docs/model-content-pipeline-support.md`** — documents `ModelTypeReader`'s current FNA-fidelity
  gaps and test-coverage gap; CNB-19/CNB-21 are the tasks that touch it.
- **`docs/avatar-real-rendering-ext.md`** — documents the separate `SkinnedModelEXT`/Avatar system;
  relevant to the CNB-22 decision.
