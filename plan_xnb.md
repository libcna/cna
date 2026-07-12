# XNB binary content pipeline: task plan

> Companion task list to [`xnb.md`](xnb.md) (the narrative research/design document — read that
> first for *why* each phase is shaped this way). This file turns that plan into concrete,
> numbered tasks (`XNB-1`, `XNB-2`, ...) the same way `plan_graphics.md` tracks graphics work.
> **Nothing here is implemented yet — every row starts `⬜`.** "Top quality" (broad real-world
> `.xnb` compatibility, hardened against malformed/adversarial files, full XNA-vs-MonoGame-variant
> coverage) is an explicit **later-phase** goal (Phase G below), not part of the first
> deliverable — the first phases target a genuinely working but intentionally narrower loader.
>
> **Revised after an independent review of the first draft** (see `xnb.md`'s changelog note) to fix
> several protocol-accuracy issues and one implementation-strategy issue: the root-object dispatch
> algorithm (XNB-16), assembly-qualified generic reader-name parsing (XNB-13/XNB-13A), the exact
> shared-resource fixup ordering (XNB-15), an early `ContentManager` integration slice (new Phase
> B2, instead of only at the very end), backend-neutral `Texture2DReader` layering (XNB-23/XNB-24),
> a real `ContentReader::ReadExternalReference<T>()` shape instead of a standalone reader (XNB-35),
> an explicit unsupported-content decision for the general `EffectReader` (XNB-32A), an audio
> support matrix requirement (XNB-33), reader-version enforcement (XNB-16B), and a
> `ReflectiveReader<T>` compatibility decision (XNB-42A).

## Scope

**In scope**: everything needed to *load* (deserialize) an already-built, real `.xnb` file at
runtime — the `ContentTypeReader`/`ContentReader` side of XNA's Content Pipeline, matching FNA's
own `ContentManager`/`ContentReader`/`ContentTypeReaderManager`.

**Out of scope, permanently, for this plan**: anything that *produces* `.xnb` files —
`ContentCompiler`, `ContentImporter`, `ContentProcessor`, `ContentTypeWriter`, MSBuild/XNA project
build-tool integration, or any "author content in CNA, bake it to `.xnb`" workflow. CNA only ever
needs to consume `.xnb` files produced elsewhere (real XNA/MonoGame/FNA tooling); it has no need to
generate them. This mirrors the boundary MonoGame itself draws between its `Content.Pipeline`
(build-time, writer-side) and `Framework.Content` (runtime, reader-side) assemblies — this plan is
entirely the second one.

## Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Not started |
| 🔄 | In progress |
| ✅ | Done |
| ⛔ | Blocked / deferred |

## Phase 0 — CNA gap audit (confirms what's missing before any XNB-specific code)

> Findings from this session's own audit, folded in directly so Phase A doesn't re-discover them:
> CNA has **no `System::IO`-equivalent binary stream/reader layer at all** (no `Stream`, no
> `BinaryReader`) — `Content::ContentTypeReader<T>` today is purely a loose-file-path interface
> (`Read(const std::string& path, ContentManager&)`), not a binary-protocol one; there is no
> `ContentReader`, `ContentTypeReaderManager`, or shared-resource-fixup mechanism of any kind.
> `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent` do not exist anywhere in the
> codebase. Conversely, `SurfaceFormat` already has full XNA 4.0 coverage (plus CNA `EXT` additions),
> and `VertexDeclaration`/`BoundingFrustum`/`Vector2-4`/`Matrix`/`Quaternion`/`Color`/`Rectangle`/
> `Point`/`BoundingBox`/`BoundingSphere`/`Plane`/`Ray` all already exist and match FNA — so Phase 0
> is a short, targeted list, not a rewrite.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-1 | Confirm/document: no `System::IO::Stream`/`BinaryReader`-equivalent exists in CNA today | ⬜ | Confirmed this session via project search — record as a short note in this file's own history, no code yet |
| XNB-2 | Confirm/document: no `Content::ContentReader`, `ContentTypeReaderManager`, or shared-resource registry of any kind exists — `ContentTypeReader<T>` is loose-file-only | ⬜ | Confirmed this session |
| XNB-3 | Confirm/document: `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent` do not exist | ⬜ | Needed later for `CurveReader` (Phase C); flag here so Phase C doesn't rediscover it |
| XNB-4 | Confirm/document: `SurfaceFormat`, `VertexDeclaration`, and all XNA math structs needed by the primitive/math readers already exist and are FNA-faithful | ⬜ | No action needed — just avoids Phase C wasting time re-checking |
| XNB-5 | Decide final normalized-reader-name registry key format (bare type name vs. `Namespace.Type\`1` generic-arity-suffixed form for `ArrayReader<T>`/`ListReader<T>`/etc.) | ⬜ | Design decision blocking Phase B/C's registry; must handle MonoGame vs. real-XNA vs. FNA assembly-qualified-name differences described in `xnb.md` |

---

## Phase A — `ContentReader` binary primitives

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-6 | Add a minimal internal binary-cursor/stream type over an in-memory byte buffer (read position, bounds-checked reads, no need for a full `System::IO::Stream` hierarchy unless one already exists) | ⬜ | Scope tightly to what XNB reading needs — do not build a general-purpose `Stream` abstraction as a side effect |
| XNB-7 | Implement 7-bit-encoded `int` read (`Read7BitEncodedInt`, matches .NET `BinaryReader`) | ⬜ | |
| XNB-8 | Implement 7-bit-length-prefixed string read (UTF-8, matches .NET `BinaryReader.ReadString`) | ⬜ | |
| XNB-9 | Implement little-endian fixed-width numeric reads only: `bool`/`byte`/`sbyte`/`int16/32/64`/`uint16/32/64`/`float`/`double` | ⬜ | Audit CNA's existing binary-IO helpers first for reuse before writing new ones (per `xnb.md` Phase A note) |
| XNB-9A | `char`/string decoding matching XNA `System.Char`/.NET `BinaryReader` behavior exactly — do **not** treat `char` as a plain little-endian `uint16_t`; decide the CNA-side representation (`char16_t` vs. a dedicated `System::Char`) and verify against real encoded bytes | ⬜ | Split out from XNB-9 per review — `.NET` chars go through an encoding, they are not a fixed-width primitive by default |
| XNB-10 | Unit tests for all of the above against known .NET-produced byte sequences (hand-computed or extracted from a real `.xnb`) | ⬜ | No dependency on anything else in this plan |

---

## Phase B — XNB container, uncompressed only

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-11 | `ContentReader`/XNB header parse: magic `'XNB'` + platform char, version byte (accept `4`/`5` only), flags byte, total-length int32 | ⬜ | Reject unknown platform/version with a `ContentLoadException`-equivalent, matching FNA's own validation |
| XNB-12 | Type-reader table parse (7-bit count, then per-entry 7-bit string + int32 reader version) | ⬜ | |
| XNB-13 | Reader-name normalization step: a real assembly-qualified-name parser (not `substr(0, name.find(','))`), correctly handling commas nested inside generic-argument brackets (`ListReader\`1[[Microsoft.Xna.Framework.Vector3, Microsoft.Xna.Framework, Version=...]], ...`); produces the canonical key decided in XNB-5 | ⬜ | Naive comma-split truncation breaks on every generic reader name — needs its own mini type-name parser (`struct TypeName{namespaceAndName; genericArguments;}`) |
| XNB-13A | Unit tests for XNB-13 covering: plain reader name, one level of generic nesting (`ListReader<Vector3>`), and at least one doubly-nested generic (`DictionaryReader<K, ListReader<V>>`) | ⬜ | Standalone mini-milestone per review — do not fold silently into XNB-13's own task row |
| XNB-14 | `ContentTypeReader` base class + `ContentTypeReaderRegistry` (register/create by normalized name) — registry starts empty, no readers registered yet | ⬜ | Matches the sketch in `xnb.md` |
| XNB-15 | Shared-resource fixup mechanism: parse shared-resource count; read root object; then read each shared resource *in order*; then resolve all deferred fixup callbacks; fail if a required shared resource index is invalid or resolves to the wrong runtime type | ⬜ | Must NOT return a final value at read-time for forward references — register a `PendingSharedResource{resourceIndex, fixup, expectedType}` callback applied only after all shared resources are read. Test: index 0 = null, out-of-range index, multiple fixups on one resource, wrong runtime type |
| XNB-16 | Root-object dispatch via the **1-based type-reader-index protocol**: read a 7-bit-encoded index; `0` means null; otherwise `index - 1` selects the type-reader table entry. Do **not** assume the root uses the table's first entry | ⬜ | Critical correctness fix — this is also how every nested object reference is dispatched, not just the root |
| XNB-16A | Runtime type-safety for dispatched objects: reader results carry both an instance and a `RuntimeTypeId`/`std::type_index` (e.g. `ContentObject{RuntimeTypeId type; std::shared_ptr<void> instance;}`), not a bare untyped `shared_ptr<void>` | ⬜ | Registry entries must record reader name, reader type, produced-object type, and reader version — avoids unsafe blind casts later |
| XNB-16B | Reader-version handling: each created reader instance receives the serialized version from the type-reader table (`reader->initialize(version)`); reader declares `supportsVersion(version)`; unsupported version is a hard error ("Strict" mode) unless the reader explicitly whitelists multiple versions ("Compatibility" mode) | ⬜ | XNB-12 currently only *parses* the version int and never uses it — this closes that gap |
| XNB-17 | Hand-build (or source) at least one real *uncompressed* `.xnb` test fixture, produced by real external tooling (never generated by CNA itself), and prove the full container round-trips before any real readers exist | ⬜ | Goal line from `xnb.md` Phase B |
| XNB-17A | Create `tests/assets/xnb/` fixture corpus skeleton now (`xna40/windows/{uncompressed,lzx}/`, `monogame/desktopgl/`, `fna/`, `malformed/`), each fixture with a JSON manifest (`producer`, `platform`, `compressed`, `rootReader`, `expectedType`, expected field values) | ⬜ | Moved up from the former Phase G corpus task — must exist from Phase B onward so tests don't validate against CNA's own possibly-wrong interpretation |

---

## Phase B2 — Early `ContentManager` vertical slice (moved forward, deliberately not deferred)

> Per review point 5: wiring the whole pipeline into `ContentManager` only at the very end (former
> XNB-46) risks producing an isolated library that doesn't actually fit CNA's existing API. This
> phase proves the real end-to-end call shape (`content.Load<T>("fixture")`) immediately after
> Phase B, using only the trivial test-only reader — before any production readers exist.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-17B | `ContentManager` `.xnb` extension resolution/dispatch, coexisting with existing loose-file loaders (no behavior change to existing `.model.json`/`.shader.json`/`FromStream` paths) | ⬜ | |
| XNB-17C | Asset-path normalization + basic cache-identity handling for `.xnb` assets (same identity rules the loose-file loaders already use) | ⬜ | |
| XNB-17D | `ContentLoadException`-equivalent propagation from the Phase B container/registry errors up through `ContentManager.Load<T>()` | ⬜ | |
| XNB-17E | `Unload()` behavior for `.xnb`-sourced assets | ⬜ | |
| XNB-17F | End-to-end milestone: `auto value = content.Load<TestValue>("fixture");` using only the Phase B test-only reader | ⬜ | First real proof the pipeline fits CNA's existing `ContentManager` API, not just a standalone parser |

---

## Phase C — Primitive/math readers + collections + first real Graphics reader

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-18 | Primitive readers: `Boolean`/`Byte`/`SByte`/`Int16/32/64`/`UInt16/32/64`/`Single`/`Double`/`Decimal`/`Char`/`String`/`DateTime`/`TimeSpan` | ⬜ | Thin wrappers over Phase A primitives |
| XNB-19 | Math readers: `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray` | ⬜ | All backing structs already exist per XNB-4 — pure field-order wiring |
| XNB-20 | `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent`: add the missing runtime classes/enums, then `CurveReader` | ⬜ | Real new runtime API, not just a reader — flagged by XNB-3; check FNA's `Curve.cs` for the exact public surface needed |
| XNB-21 | `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>` generic dispatch (recursively invoking another registered reader by an embedded type parameter) | ⬜ | The "generic-dispatch" problem called out in `xnb.md` |
| XNB-22 | `NullableReader<T>` (map to `std::optional<T>` on the CNA side, per this session's audit that CNA has no `Nullable<T>` XNA class of its own) | ⬜ | |
| XNB-23 | `Texture2DReader`, implemented strictly against CNA's **backend-neutral** `Texture2D`/`GraphicsDevice` API (`SurfaceFormat` + width/height/mip levels + per-level `SetData`) — the reader must not reference EasyGL (or any other backend) internals directly | ⬜ | Full code sketch already in `xnb.md`; correct layering is `XNB reader → CNA Texture2D API → active GraphicsDevice backend`, never `XNB reader → EasyGL internals`. Tests may target EasyGL first, but production reader code must stay backend-agnostic |
| XNB-24 | `SurfaceFormat` capability inventory (not immediate blanket conversion): classify each `SurfaceFormat` as native-upload / CPU-conversion / GPU-transcode / unsupported / lossy-fallback per backend (`struct SurfaceFormatCapability{bool nativeUpload; optional<SurfaceFormat> fallback; ConversionFunction converter;}`); `Texture2DReader` only asks `GraphicsDevice`/a formats service for a compatible resource, it does not own per-backend conversion tables itself | ⬜ | `Dxt1/3/5` at minimum. `SurfaceFormat` enum itself already has full coverage (XNB-4); this is about each backend's texture-upload path, kept out of the reader |
| XNB-25 | `Texture3DReader`/`TextureCubeReader`/base `TextureReader` | ⬜ | Same shape as `Texture2DReader`, extra dimension/face handling; deferred until after Phase D per review (avoid needing compression support before it exists) |
| XNB-26 | End-to-end test: `content.Load<Texture2D>("foo.xnb")` on an uncompressed real fixture, going through the Phase B2 `ContentManager` slice (not a standalone parser call) | ⬜ | Goal line from `xnb.md` Phase C |

---

## Phase D — LZX decompression

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-27 | Header-level compression enum, not a boolean: `enum class XnbCompression{None, Lzx, Lz4, Unknown}` parsed from the flags byte — do **not** hardcode `compressed == true → LZX` anywhere in the architecture | ⬜ | Needed because Phase G (XNB-44) targets MonoGame variants too, which use different compression |
| XNB-28 | Phase D1 — Port FNA's `LzxDecoder.cs` (~750 lines, self-contained, no external deps) to C++ for the real XNA `Lzx` case | ⬜ | Large standalone subtask — do not fold into Phase B |
| XNB-29 | Block-framing loop (2-byte block size, occasional 5-byte extended form) wired into the Phase B header's compressed-payload path | ⬜ | |
| XNB-30 | Malformed/truncated/adversarial-input hardening for the decompressor: bounds checks, integer-overflow guards on decompressed-size fields, decompression-bomb output-size limits, no OOB reads on corrupt input | ⬜ | Explicitly flagged in `xnb.md` as often bigger than the happy path — first real "quality" hardening item |
| XNB-30A | Fuzz tests + differential tests against a reference LZX implementation for the decompressor | ⬜ | |
| XNB-30B | Re-run every Phase B/C fixture through its compressed form and confirm identical results | ⬜ | Goal line from `xnb.md` Phase D |
| XNB-30C | Phase D2 (deferred, not MVP-blocking) — investigate/implement MonoGame-supported alternative compression variants under the `XnbCompression` enum from XNB-27 | ⬜ | Explicitly optional for the first working loader; the enum from XNB-27 just needs to not preclude it later |

---

## Phase E — `SpriteFont`, stock effects, `SoundEffect`/`Song`

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-31 | `SpriteFontReader` (glyph atlas `Texture2D` + rects + character map + kerning + optional default char via `NullableReader`) | ⬜ | Depends on Phase C's collection/`Nullable` readers |
| XNB-32 | Stock-effect readers: `BasicEffectReader`/`AlphaTestEffectReader`/`DualTextureEffectReader`/`EnvironmentMapEffectReader`/`SkinnedEffectReader` — construct CNA's own native stock-effect implementations, ignoring embedded platform shader bytecode | ⬜ | Deliberate scope-narrowing decision from `xnb.md`, avoids any dependency on `plan_graphics.md` Phase 74 |
| XNB-32A | Explicit unsupported-content decision for the *general* `Microsoft.Xna.Framework.Content.EffectReader` (compiled platform shader bytecode, distinct from the stock-effect readers in XNB-32): detect it and fail with a precise, documented exception until real compiled-FX support exists (see `plan_graphics.md` Phase 74) | ⬜ | Without this task it stays ambiguous whether the general `EffectReader` is forgotten or deliberately unsupported — this makes the decision explicit and testable |
| XNB-33 | Audio-fixture survey task (run *before* writing `SoundEffectReader`): collect real fixtures for XNA-Windows PCM, XNA-Windows ADPCM/other supported compressed form, MonoGame DesktopGL, and an FNA-compatible build; produce a support matrix (`XNA Windows PCM SoundEffect` / `XNA Windows compressed SoundEffect` / `Song as external file` / `platform-specific codec` → supported / converted / explicitly rejected) | ⬜ | Prevents declaring the task "done" off a single fixture that happens to work |
| XNB-33A | `SoundEffectReader` (wave format, PCM vs. compressed data, loop region, duration → CNA audio backend), scoped to the matrix from XNB-33 | ⬜ | Scope against CNA's existing audio plan file, not duplicated here |
| XNB-34 | `SongReader`, scoped to the matrix from XNB-33 (external-file case in particular) | ⬜ | |
| XNB-35 | `ContentReader::ReadExternalReference<T>()` (not a standalone `ExternalReferenceReader` type): read the referenced asset name/path, resolve it relative to the current asset, load it through the owning `ContentManager`, detect dependency cycles, preserve cache identity when the same asset is referenced more than once, and perform a type check against `T` | ⬜ | Reframed per review — this is a `ContentReader` operation, not a production `ContentTypeReader`. Must also cover: relative-path normalization across separators, and (mode-dependent) rejecting paths that escape the content root |

---

## Phase F — `Model` and shared-resource-heavy readers

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-36 | `VertexBufferReader`/`IndexBufferReader`/`VertexDeclarationReader` | ⬜ | Reuse the vertex-layout-to-backend translation already used by CNA's current JSON model loader rather than reinventing it |
| XNB-37 | `ModelReader`: bone parent/child hierarchy (multiple bones, not just one) | ⬜ | Closes a pre-existing gap in `docs/model-content-pipeline-support.md`'s `ModelTypeReader`, not just a new-format concern |
| XNB-38 | `ModelReader`: per-mesh `ParentBone` assignment via the existing `ModelMesh::setParentBoneProperty` setter | ⬜ | Runtime API already supports this (Task 439/`setParentBoneProperty`) — this reader is the first real caller |
| XNB-39 | `ModelReader`: real per-mesh `BoundingSphere`, model/mesh `Tag` | ⬜ | |
| XNB-40 | `ModelReader`: shared-resource dedup across mesh parts (`VertexBuffer`/`IndexBuffer`/`Effect` reused via `ReadSharedResource<T>`, not always freshly allocated) | ⬜ | |
| XNB-41 | End-to-end test: a real multi-bone, shared-resource `.xnb` model loads correctly, matching the Task 431-439 runtime-API audit's expectations | ⬜ | |

---

## Phase G — Top-quality hardening + custom reader ergonomics (deliberately last)

> This is where "špičková kvalita" (top-tier quality) actually lives — not attempted before every
> earlier phase is solid, per `xnb.md`'s own "why this is still not urgent" framing.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-42 | Public API for a CNA game to register its own custom `ContentTypeReader` (`registry.Register("MyGame.Content.LevelReader", ...)`) | ⬜ | Permanent, explicit limitation of the format itself — not "fixable", just needs a clean extension point |
| XNB-42A | `ReflectiveReader<T>` compatibility decision: CNA will support only manually registered, explicit C++ `ContentTypeReader`s for custom content (no generalized C++ reflection-driven reader) — document this decision and what a game author must do instead when their `.xnb` was built with an implicit `ReflectiveReader` | ⬜ | Per review: not automatically implementing this limits "broad real-world compatibility", so the limitation must be an explicit, documented decision, not a silent gap |
| XNB-43 | Malformed/adversarial-file robustness pass across the *entire* pipeline (not just LZX from Phase D) — truncated tables, bad shared-resource indices, reader-version mismatches | ⬜ | |
| XNB-44 | Broad compatibility test corpus: real files from real XNA 4.0 Windows, MonoGame, and FNA-produced `.xnb` variants, documenting any behavioral differences found | ⬜ | Corpus skeleton itself already exists from XNB-17A; this task is about filling and expanding it |
| XNB-45 | Full developer documentation (`docs/xnb-content-pipeline-support.md`) covering exactly what is/isn't supported, mirroring the style of `docs/model-content-pipeline-support.md` | ⬜ | |
| XNB-46 | Register every Phase A-F reader that ended up implemented into the single `ContentTypeReaderRegistry` first stood up in Phase B2, alongside the existing loose-file loaders | ⬜ | This is now a small "finish populating the registry" task, not the first integration point — that already happened in Phase B2 |
| XNB-47 | Per-reader-task mandatory checklist, applied retroactively as an audit pass across every reader implemented in Phases C–F: exact serialized layout confirmed from an authoritative implementation; ≥ 1 real externally-produced fixture; success test; truncated-input test; invalid-count/size test; wrong-reader-version test; asset ownership/unload verified; backend-independent behavior verified; supported producer/platform variants documented | ⬜ | Codifies the review's closing checklist as an actual task rather than leaving it as prose guidance only |

---

## Relationship to other plan files

- [`xnb.md`](xnb.md) — the narrative design document this task list is derived from; read it for
  *why*, not just *what*.
- [`plan_graphics.md`](plan_graphics.md) Phase 74 — compiled `.fx` shader bytecode via MojoShader;
  related but independent (see `xnb.md`'s own "Effect/compiled shader bytecode" section). Phase E's
  stock-effect readers (XNB-32) deliberately avoid needing Phase 74 at all.
- [`docs/model-content-pipeline-support.md`](docs/model-content-pipeline-support.md) — the existing,
  already-documented gaps in CNA's current (non-`.xnb`) `ModelTypeReader` that Phase F (XNB-37 to
  XNB-40) closes as part of building the real `.xnb` `ModelReader`.
