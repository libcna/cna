# XNB binary content pipeline: task plan

> Companion task list to [`xnb.md`](xnb.md) (the narrative research/design document — read that
> first for *why* each phase is shaped this way). This file turns that plan into concrete,
> numbered tasks (`XNB-1`, `XNB-2`, ...) the same way `plan_graphics.md` tracks graphics work.
> **Nothing here is implemented yet — every row starts `⬜`.** "Top quality" (broad real-world
> `.xnb` compatibility, hardened against malformed/adversarial files, full XNA-vs-MonoGame-variant
> coverage) is an explicit **later-phase** goal (Phase F/G below), not part of the first
> deliverable — the first phases target a genuinely working but intentionally narrower loader.

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
| XNB-9 | Implement little-endian fixed-width primitive reads: `bool`/`byte`/`sbyte`/`int16/32/64`/`uint16/32/64`/`float`/`double`/`char` | ⬜ | Audit CNA's existing binary-IO helpers first for reuse before writing new ones (per `xnb.md` Phase A note) |
| XNB-10 | Unit tests for all of the above against known .NET-produced byte sequences (hand-computed or extracted from a real `.xnb`) | ⬜ | No dependency on anything else in this plan |

---

## Phase B — XNB container, uncompressed only

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-11 | `ContentReader`/XNB header parse: magic `'XNB'` + platform char, version byte (accept `4`/`5` only), flags byte, total-length int32 | ⬜ | Reject unknown platform/version with a `ContentLoadException`-equivalent, matching FNA's own validation |
| XNB-12 | Type-reader table parse (7-bit count, then per-entry 7-bit string + int32 reader version) | ⬜ | |
| XNB-13 | Reader-name normalization step (strip assembly/version/culture/public-key-token suffix down to the bare/generic-arity-aware key decided in XNB-5) | ⬜ | |
| XNB-14 | `ContentTypeReader` base class + `ContentTypeReaderRegistry` (register/create by normalized name) — registry starts empty, no readers registered yet | ⬜ | Matches the sketch in `xnb.md` |
| XNB-15 | Shared-resource count parse + placeholder/deferred-fixup mechanism (`ReadSharedResource<T>()` equivalent) | ⬜ | Needed even with zero real readers registered, to prove the container shape end-to-end |
| XNB-16 | Root-object dispatch: look up the table's first-referenced reader via the registry and invoke it (with a trivial test-only reader) | ⬜ | |
| XNB-17 | Hand-build (or source) at least one real *uncompressed* `.xnb` test fixture and prove the full container round-trips before any real readers exist | ⬜ | Goal line from `xnb.md` Phase B |

---

## Phase C — Primitive/math readers + collections + first real Graphics reader

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-18 | Primitive readers: `Boolean`/`Byte`/`SByte`/`Int16/32/64`/`UInt16/32/64`/`Single`/`Double`/`Decimal`/`Char`/`String`/`DateTime`/`TimeSpan` | ⬜ | Thin wrappers over Phase A primitives |
| XNB-19 | Math readers: `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray` | ⬜ | All backing structs already exist per XNB-4 — pure field-order wiring |
| XNB-20 | `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent`: add the missing runtime classes/enums, then `CurveReader` | ⬜ | Real new runtime API, not just a reader — flagged by XNB-3; check FNA's `Curve.cs` for the exact public surface needed |
| XNB-21 | `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>` generic dispatch (recursively invoking another registered reader by an embedded type parameter) | ⬜ | The "generic-dispatch" problem called out in `xnb.md` |
| XNB-22 | `NullableReader<T>` (map to `std::optional<T>` on the CNA side, per this session's audit that CNA has no `Nullable<T>` XNA class of its own) | ⬜ | |
| XNB-23 | `Texture2DReader` (`SurfaceFormat` + width/height/mip levels + per-level `SetData`) | ⬜ | Full code sketch already in `xnb.md`; wire to whichever backend is exercised first (EasyGL, per `xnb.md`'s own recommendation) |
| XNB-24 | `SurfaceFormat` coverage pass for `Texture2DReader` per backend (`Dxt1/3/5` at minimum, plus whatever conversion each backend already lacks) | ⬜ | Scope per-backend, not a single shared conversion table — `SurfaceFormat` enum itself already has full coverage (XNB-4), this is about each backend's texture-upload path |
| XNB-25 | `Texture3DReader`/`TextureCubeReader`/base `TextureReader` | ⬜ | Same shape as `Texture2DReader`, extra dimension/face handling |
| XNB-26 | End-to-end test: `content.Load<Texture2D>("foo.xnb")` on an uncompressed real fixture | ⬜ | Goal line from `xnb.md` Phase C |

---

## Phase D — LZX decompression

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-27 | Port FNA's `LzxDecoder.cs` (~750 lines, self-contained, no external deps) to C++ | ⬜ | Large standalone subtask — do not fold into Phase B |
| XNB-28 | Block-framing loop (2-byte block size, occasional 5-byte extended form) wired into the Phase B header's compressed-payload path | ⬜ | |
| XNB-29 | Malformed/truncated/adversarial-input hardening for the decompressor (bounds checks, no OOB reads on corrupt input) | ⬜ | Explicitly flagged in `xnb.md` as often bigger than the happy path — first real "quality" hardening item |
| XNB-30 | Re-run every Phase B/C fixture through its compressed form and confirm identical results | ⬜ | Goal line from `xnb.md` Phase D |

---

## Phase E — `SpriteFont`, stock effects, `SoundEffect`/`Song`

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-31 | `SpriteFontReader` (glyph atlas `Texture2D` + rects + character map + kerning + optional default char via `NullableReader`) | ⬜ | Depends on Phase C's collection/`Nullable` readers |
| XNB-32 | Stock-effect readers: `BasicEffectReader`/`AlphaTestEffectReader`/`DualTextureEffectReader`/`EnvironmentMapEffectReader`/`SkinnedEffectReader` — construct CNA's own native stock-effect implementations, ignoring embedded platform shader bytecode | ⬜ | Deliberate scope-narrowing decision from `xnb.md`, avoids any dependency on `plan_graphics.md` Phase 74 |
| XNB-33 | `SoundEffectReader` (wave format, PCM vs. compressed data, loop region, duration → CNA audio backend) | ⬜ | Scope against CNA's existing audio plan file, not duplicated here |
| XNB-34 | `SongReader` | ⬜ | |
| XNB-35 | `ExternalReferenceReader` | ⬜ | Simple relative-path reference, low effort |

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
| XNB-43 | Malformed/adversarial-file robustness pass across the *entire* pipeline (not just LZX from Phase D) — truncated tables, bad shared-resource indices, reader-version mismatches | ⬜ | |
| XNB-44 | Broad compatibility test corpus: real files from real XNA 4.0 Windows, MonoGame, and FNA-produced `.xnb` variants, documenting any behavioral differences found | ⬜ | |
| XNB-45 | Full developer documentation (`docs/xnb-content-pipeline-support.md`) covering exactly what is/isn't supported, mirroring the style of `docs/model-content-pipeline-support.md` | ⬜ | |
| XNB-46 | Register every Phase A-F reader that ended up implemented into a single default-populated `ContentTypeReaderRegistry`, wired into `ContentManager` alongside the existing loose-file loaders (`.xnb` extension dispatch, coexisting with `Texture2D::FromStream`/`.model.json`/`.shader.json`) | ⬜ | The actual "turn it on" integration task — deliberately placed last so it only ships once the pieces it wires together are real |

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
