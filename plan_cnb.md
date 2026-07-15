# `.cnb` content format — implementation plan for CNA

> **Status: planning document only. Nothing described here is implemented yet.** Turns
> [`cnb.md`](cnb.md)'s design into a phased, numbered task list (`CNB-1`, `CNB-2`, ...), mirroring
> how [`plan_xnb.md`](plan_xnb.md) turned [`xnb.md`](xnb.md) into concrete tasks. Read `cnb.md`
> first — this file assumes its design decisions (resolution order, envelope shape, `sourceFile`,
> `RegisterCnbLoader<T>`) as given and does not re-argue them.
>
> Unlike `plan_xnb.md`, this is **mostly a migration of four already-working, already-tested-or-
> exercised readers** (`SpriteFontTypeReader`, `EffectTypeReader`, `ModelTypeReader`,
> `SkinnedModelTypeReader`), not a green-field protocol build. Scope and risk are correspondingly
> much smaller — see `cnb.md`'s own "Relationship to CNA's existing per-type JSON conventions".

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

## Scope

**In scope**: the content-format/resolver plumbing `cnb.md` describes — the `.cnb` envelope, the
`.cnb`-first resolution order, `sourceFile` sidecar support, migrating the four existing JSON readers
onto `.cnb`, and `RegisterCnbLoader<T>`.

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
  deferred (see CNB-11).
- Fixing `ModelTypeReader`'s pre-existing behavioral gaps versus FNA (bone hierarchy, `ParentBone`,
  `BoundingSphere`, `Tag` — see `docs/model-content-pipeline-support.md`) is **not required** by this
  plan, but CNB-25 requires an explicit decision (fix now vs. deliberately deferred with a note), not
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
| CNB-4 | Change `ContentManager::ResolveAssetPath` (`ContentManager.hpp`) to try `<name>.cnb` **before** any extension in `reader.GetExtensions()`, for every registered type — not just the four JSON types | ⬜ | Per `cnb.md`'s "The core rule" — this single resolver change is what makes `.cnb` sidecars possible for `Texture2D`/`SoundEffect`/etc. later, without touching those readers yet |
| CNB-5 | Regression proof: run the full existing `CnaTests` suite with zero `.cnb` files anywhere in test/example content and confirm pass counts are unchanged from `master` | ⬜ | Confirms the reorder is purely additive — every existing native/`.font.json`/`.model.json`/`.shader.json`/`.skinnedmodel.json` asset must still resolve exactly as before, since none of them have a same-named `.cnb` sibling yet |
| CNB-6 | New tests: (a) an asset with only a native file still resolves via extension, unchanged; (b) an asset with only a `.cnb` resolves via `.cnb`; (c) an asset with **both** a native file and an unrelated self-contained `.cnb` of the same name resolves to the `.cnb` (the "authoring footgun" `cnb.md` documents — assert it's intended, tested behavior, not a surprise) | ⬜ | Milestone M1 |

---

## Phase 2 — `sourceFile` sidecar, proven on `Texture2D`

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-7 | Generic `sourceFile` resolution: when a `.cnb` envelope has `sourceFile`, resolve and load that referenced path through the same two-step rule (recursively through the owning `ContentManager`, same caching as any other reference per `cnb.md`'s "Referencing other files") | ⬜ | Depends on CNB-1/CNB-4 |
| CNB-8 | Wire `sourceFile` + a `colorKey` field (`[r,g,b]`) into `Texture2DTypeReader`: load the referenced native image normally, then scan/replace matching pixels via the existing `GetData`/`SetData(Color*, int)` API (`Texture2D.hpp`) — no new `Texture2D` capability required | ⬜ | First real, non-JSON-native consumer of `sourceFile` — proves the mechanism generalizes beyond the four JSON types |
| CNB-9 | Tests: `ahoj.png` + `ahoj.cnb` (`"sourceFile": "ahoj.png"`, `"colorKey": [255,0,255]`) loads, magenta pixels become transparent (alpha 0), non-keyed pixels unchanged; `ahoj.png` alone (no `.cnb`) still loads byte-for-byte unchanged (regression); a `.cnb` whose `sourceFile` points at a missing file throws `ContentLoadException` | ⬜ | Milestone M2 |
| CNB-10 | ⏸ Additional `Texture2D` `.cnb` metadata fields (`premultipliedAlpha`, atlas sub-rect, mip-generation hints) | ⏸ | Explicitly deferred — each needs real `Texture2D`/`GraphicsDevice` capability CNA doesn't have today; do not add speculatively (per project convention against designing for hypothetical future requirements) |

---

## Phase 3 — Migrate `SpriteFontTypeReader` (`.font.json` → `.cnb`)

> No existing test or example anywhere references `.font.json`/`SpriteFontTypeReader` (confirmed by
> repo-wide search) — this phase adds the *first* coverage for this reader, not just a re-point.

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-11 | Update `SpriteFontTypeReader::GetExtensions()`/`Read()` to expect the `.cnb` envelope (`cnbVersion`/`type` via CNB-1/CNB-2) wrapped around today's unchanged field parsing (`texture`/`lineSpacing`/`spacing`/`defaultCharacter`/`glyphs[].char`/`source`/`crop`/`kerning`, per `cnb.md`'s worked example) | ⬜ | Field names stay exactly as-is per `cnb.md` — only the envelope + extension change |
| CNB-12 | Add at least one real `.cnb` `SpriteFont` fixture (texture atlas + a handful of glyphs) under `tests/`/`examples/` content | ⬜ | |
| CNB-13 | Tests: `Content.Load<SpriteFont>("...")` against the CNB-12 fixture succeeds and matches expected glyph/kerning/spacing values; a `.cnb` with `"type": "Model"` requested as `Load<SpriteFont>()` throws with a message naming both types (exercises CNB-2 through a real reader) | ⬜ | First gtest coverage of `SpriteFontTypeReader` at all |

---

## Phase 4 — Migrate `EffectTypeReader` (`.shader.json` → `.cnb`)

> Same situation as Phase 3: no existing test or example references `.shader.json`/`EffectTypeReader`.

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-14 | Update `EffectTypeReader::GetExtensions()`/`Read()` to expect the `.cnb` envelope, same shape as CNB-11 | ⬜ | |
| CNB-15 | Add at least one real `.cnb` `Effect`/stock-effect-parameter fixture | ⬜ | |
| CNB-16 | Tests: `Content.Load<...>()` against the CNB-15 fixture succeeds; type-mismatch test as in CNB-13 | ⬜ | First gtest coverage of `EffectTypeReader` at all |

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
| CNB-17 | Update `ModelTypeReader::GetExtensions()`/`Read()` to expect the `.cnb` envelope, same shape as CNB-11, wrapped around today's unchanged `bones`/`meshes[].vertices`/`indices`/`vertexStride`/`texture` fields + binary sidecars | ⬜ | |
| CNB-18 | Migrate every existing `*.model.json` fixture referenced by `examples/easygl_model_json_reader_test.cpp`, `..._texture_test.cpp`, `..._bone_hierarchy_test.cpp`, `..._skeleton_test.cpp`, `..._32bit_indices_test.cpp`, `easygl_model_skinned_animation_playback_test.cpp`, `bgfx_model_json_reader_test.cpp` to `.cnb` (rename + add envelope fields); update each example to reference the new filename | ⬜ | Binary `.verts.bin`/`.idx.bin` sidecars are untouched — only the JSON descriptor's extension/envelope changes |
| CNB-19 | Add real gtest (`tests/Microsoft/Xna/Framework/Content/`) coverage for `Content.Load<Model>()` via `.cnb` — first-ever gtest coverage for this reader, closing the gap `docs/model-content-pipeline-support.md` flags | ⬜ | Can adapt fixtures/assertions from the CNB-18 example programs rather than authoring from scratch |
| CNB-20 | Type-mismatch test as in CNB-13, for `ModelTypeReader` | ⬜ | |
| CNB-21 | Explicit decision, recorded in this row: fix `ModelTypeReader`'s known FNA-behavior gaps (bone hierarchy always synthesizes exactly one bone, `ParentBone` never assigned, `BoundingSphere` never set, `Tag` never set — see `docs/model-content-pipeline-support.md`) in this same migration pass, or deliberately defer with a dated note here. Do not silently freeze the gaps into `.cnb` fixtures without a decision either way | ⬜ | Not required to be *fixed* by this plan (out of scope, see above) — only required that the choice is explicit |

---

## Phase 6 — `SkinnedModelTypeReader` decision (`.skinnedmodel.json`, NOXNA/Avatar-only)

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-22 | Decide and record here: migrate `SkinnedModelTypeReader` to `.cnb` alongside the other three, or keep `.skinnedmodel.json` permanently separate (it is already a deliberately distinct, non-`Model`-shaped `NOXNA` system — see `docs/avatar-real-rendering-ext.md`) | ⬜ | `cnb.md`'s "Suggested next step" leaves this open; resolve it here rather than deferring indefinitely |
| CNB-23 | If CNB-22 chooses migration: update `SkinnedModelTypeReader` same as CNB-11/17, migrate every `examples/demo_avatar/Content/**/*.skinnedmodel.json` fixture, and update `tests/Microsoft/Xna/Framework/Content/ContentManagerSkinnedModelTests.cpp` (existing real gtest coverage — must stay green) | ⬜ | Conditional on CNB-22 |

---

## Phase 7 — Custom loader registry (`RegisterCnbLoader<T>`)

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-24 | Implement `ContentManager::RegisterCnbLoader<T>(const std::string& typeName, CnbLoaderFn<T> factory)` per `cnb.md`'s "Custom loaders" section — a second registry keyed by the `.cnb` `"type"` string, separate from the existing `std::type_index`-keyed `RegisterTypeReader<T>()` | ⬜ | Not part of the XNA 4.0 API surface — mark `NOXNA` per `CLAUDE.md` |
| CNB-25 | Wire the fallback into the `.cnb` dispatch path: built-in per-type `type` values (`"SpriteFont"`, `"Model"`, ...) checked first, then this registry, then a clear "unknown `.cnb` type" `ContentLoadException` naming the unrecognized `type` string | ⬜ | |
| CNB-26 | Tests: two distinct `.cnb` `type` names registered via two different factories, both producing the same `T`, both load correctly; an unregistered `type` throws the CNB-25 error; a registered factory can recursively `Load<...>()` a file it references | ⬜ | |
| CNB-27 | Worked example (in `docs/` or `examples/`) showing a game registering a custom `.cnb` type end-to-end, matching `cnb.md`'s `"EnemyDefinition"`/`"LootTable"` illustration | ⬜ | Milestone M4 |

---

## Phase 8 — Cleanup / final documentation

| # | Task | Status | Notes |
|---|------|--------|-------|
| CNB-28 | Update `cnb.md`'s status header to reflect what's actually implemented vs. still planned, once the phases above land | ⬜ | Mirrors how `plan_ascii.md`/`plan_xnb.md` keep their own top-of-file status current |
| CNB-29 | Full `CnaTests` regression run + a dedicated `.cnb`-specific ctest tally, recorded in this file's status header | ⬜ | Follow `plan_ascii.md`'s completion-note style as the template |
| CNB-30 | Decide the fate of `xnb.md`/`plan_xnb.md` per `cnb.md`'s own "Relationship to `xnb.md`/`plan_xnb.md`" section (freeze as "researched, not adopted" vs. leave open) and record the decision in both files | ⬜ | |
| CNB-31 | Final compliance sweep: every task above closed, every new/changed public method has Doxygen (per `CLAUDE.md`) and test coverage, no `.font.json`/`.model.json`/`.shader.json`/`.skinnedmodel.json` fixture left un-migrated except by the explicit CNB-22 decision | ⬜ | |

---

## Relationship to other plan files

- **`cnb.md`** — the design this plan implements; read it first.
- **`xnb.md`/`plan_xnb.md`** — the alternative, not-adopted (binary `.xnb` reader) strategy; CNB-30
  is the task that formally records `.cnb`'s adoption status relative to it.
- **`docs/model-content-pipeline-support.md`** — documents `ModelTypeReader`'s current FNA-fidelity
  gaps and test-coverage gap; CNB-19/CNB-21 are the tasks that touch it.
- **`docs/avatar-real-rendering-ext.md`** — documents the separate `SkinnedModelEXT`/Avatar system;
  relevant to the CNB-22 decision.
