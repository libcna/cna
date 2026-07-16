# XNB binary content pipeline: task plan

> **Status: 🧊 FROZEN 2026-07-15 — researched, not adopted; kept as reference only.** See
> [`xnb.md`](xnb.md)'s own status banner and [`cnb.md`](cnb.md)'s "Relationship to `xnb.md`/
> `plan_xnb.md`" section — CNA adopted `.cnb` instead (implemented in full, see `plan_cnb.md`).
> None of the tasks below are planned to be started under the current strategy; every `⬜` in this
> file stays `⬜` indefinitely, not as an oversight.

> Companion task list to [`xnb.md`](xnb.md) (the narrative research/design document — read that
> first for *why* each phase is shaped this way). This file turns that plan into concrete,
> numbered tasks (`XNB-1`, `XNB-2`, ...) the same way `plan_graphics.md` tracks graphics work.
> **Nothing here is implemented yet — every row starts `⬜`.** "Top quality" (broad real-world
> `.xnb` compatibility, hardened against malformed/adversarial files, full XNA-vs-MonoGame-variant
> coverage) is an explicit **later-phase** goal (Phase G below), not part of the first
> deliverable — the first phases target a genuinely working but intentionally narrower loader.
> Phase H (Lua custom readers) and Phase I (official-sample inventory) sit even later still, strictly
> after Phase G, and are not required for "top quality" native `.xnb` loading itself.
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
>
> **Revised a second time after a follow-up review** to add: explicit read/allocation limits from
> the very start (XNB-10A `XnbReadLimits`), a global-factory-vs-per-file-instance split for the
> reader registry (XNB-14A), an early known-unsupported placeholder for the general `EffectReader`
> registered already in Phase B (XNB-14B), faithful `Decimal`/`DateTime`/`TimeSpan` handling instead
> of lossy `double` shortcuts (XNB-18A/B/C), a physical (not just noted) move of
> `Texture3DReader`/`TextureCubeReader` into a new Phase D3 after LZX lands, corrected stock-effect
> reader wording (XNB-32 deserializes the stock effect's own fields, it does not "ignore bytecode"
> it never had), a concurrency/no-global-mutable-state check (XNB-17G), turning the per-reader
> checklist (XNB-47) into a continuously enforced Definition of Done rather than a retroactive-only
> audit, a trimmed single-task Phase 0 (XNB-1), and two new phases: **Phase H** (Lua-scripted custom
> `ContentTypeReader`s, deliberately *after* the native C++ reader framework is solid) and **Phase
> I** (an official XNA 4.0 sample `.xnb` compatibility inventory, to replace guesswork with real
> numbers).
>
> **Revised a third time after a second follow-up review** (rated architecture 9.5/10, protocol
> accuracy 9/10, testability 9.5/10 at this point) to: loosen the shared-resource fixup wording
> (XNB-15) so it guarantees an observable *result* instead of mandating one specific callback
> strategy; prefer a stable CNA-owned `RuntimeTypeId` over a bare `std::type_index` (XNB-16A) so
> Lua custom types, plugins, and no-RTTI builds stay representable; add five explicit phase
> **milestones** (below) as agent-facing stop lines; split the sample scanner into an early,
> payload-agnostic **Phase B3** task (XNB-61a) that only inventories reader *names* and does not
> block on Phase G, versus the full runtime-compatibility work staying in Phase I; add a Lua
> error-context task (XNB-58A); and add an explicit **execution-order mandate** (below) telling an
> autonomous agent to work strictly phase-by-phase, finish the current phase's milestone before
> starting the next, and never open Phase H/I while any mandatory Phase A-G task is incomplete.
>
> **Revised a fourth time after the final review** (rated architecture 9.7/10, test strategy 9.7/10)
> to fix one real bug: Phase B3's scanner cannot cover LZX-compressed files before Phase D's
> decompressor exists, since a compressed file's type-reader table is inside the LZX payload, not
> separately addressable — split into XNB-61a (uncompressed, right after Phase B) and XNB-61b
> (LZX-compressed, after Phase D). Also clarified that a decompressor must process the stream
> sequentially rather than jump to an offset, split the `⛔` legend symbol into `⏸` (deferred/optional,
> not milestone-blocking) vs. `⛔` (blocked by an external dependency) and re-marked XNB-30C `⏸`
> accordingly, since the execution-order mandate's "do not skip a task" rule only ever applied to
> mandatory (`⬜`) tasks.

## Execution-order mandate for autonomous work

> Read this before starting any task in this file.

- Implement strictly in phase order. Do not begin Phase H or Phase I while any mandatory task in
  Phase A-G remains incomplete (Phase B3's XNB-61a/XNB-61b scanners are the one deliberate
  exception - see their own rows; note XNB-61b still cannot start before Phase D exists).
- Do not use Lua (Phase H) as a shortcut for a missing native standard reader - standard readers
  stay native C++ permanently (see Phase H's own intro).
- Work sequentially from the first incomplete task in the current phase. Prioritize finishing the
  current phase and reaching its milestone (below) over starting later, easier-looking tasks.
- Do not skip a blocked/hard task silently by jumping ahead to a later phase to avoid it.
- After each milestone below is reached: run the full test suite, update the support-matrix/status
  rows in this file, re-check that the architecture decisions from Phase A/B still hold, commit a
  consistent state, and only then continue. Do not accumulate dozens of half-implemented tasks
  before stopping to verify.

## Milestones

| Milestone | Reached after | Definition of "done" |
|---|---|---|
| M1 - binary protocol | XNB-17F | An uncompressed, externally-produced test `.xnb` loads end-to-end through `ContentManager` using only the Phase B test-only reader. No graphics, audio, or Lua involved. |
| M2 - real texture | XNB-26 | A real uncompressed XNA 4.0 `Texture2D` `.xnb` loads and uploads through the backend-neutral `GraphicsDevice` path. |
| M3 - common XNA 2D content | end of Phase E | Compressed `Texture2D`, `SpriteFont`, and at least one supported `SoundEffect` variant from the XNB-33 matrix all load correctly. |
| M4 - standard model | XNB-41 | A real multi-mesh, multi-bone XNA `Model` `.xnb` loads with shared resources resolved and native CNA stock effects attached. |
| M5 - release-quality native loader | XNB-47 | Native `.xnb` support is documented (XNB-45), hardened (XNB-43), and fully usable without Lua. |

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

> Per the third follow-up review point 3: `⬜` alone was ambiguous between "mandatory, not started"
> and "optional/deferred, not started" — the execution-order mandate above only makes sense if a
> task's mandatory-vs-deferred status is explicit, so the legend below splits `⛔` into two distinct
> symbols. Any task marked `⏸` is **not** required to reach the milestone whose phase it lives in;
> the execution-order mandate's "finish the current phase's milestone" rule does not wait on `⏸`
> rows.

| Symbol | Meaning |
|--------|---------|
| ⬜ | Mandatory, not started |
| 🔄 | In progress |
| ✅ | Done |
| ⏸ | Deferred / optional, not milestone-blocking |
| ⛔ | Blocked by an external dependency (not yet actionable) |

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

> Per follow-up review point 2: the four confirm/document rows below are already-completed audit
> findings from an earlier session (see the block-quote above), not open work — keeping them as four
> separate `⬜` rows just invites an autonomous agent to re-run the same project search four times.
> Folded into a single revalidation task instead.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-1 | Revalidate the recorded gap audit against the current branch and update only findings that changed since this plan was written: no `System::IO::Stream`/`BinaryReader`-equivalent; no `Content::ContentReader`/`ContentTypeReaderManager`/shared-resource registry (`ContentTypeReader<T>` is loose-file-only); no `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent`; `SurfaceFormat`/`VertexDeclaration`/XNA math structs already exist and are FNA-faithful | ⬜ | Formerly four separate rows (`XNB-1`–`XNB-4`); collapsed per follow-up review — do not re-run the full search from scratch unless something looks stale |
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
| XNB-10A | Introduce `XnbReadLimits` (max file size, max decompressed size, max string bytes, max type-reader count, max shared-resource count, max collection-element count, max object-nesting depth) as part of the binary-cursor architecture itself, not bolted on later in Phase D/G | ⬜ | Per follow-up review point 3 — without this, a validly-bounds-checked reader can still be told to `std::vector<T>(0x7fffffff)` off a malicious count field; every later count-driven read (Phase C collections, Phase D decompressed size, Phase F mesh/bone counts) must consult these limits from day one |

---

## Phase B — XNB container, uncompressed only

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-11 | `ContentReader`/XNB header parse: magic `'XNB'` + platform char, version byte (accept `4`/`5` only), flags byte, total-length int32 | ⬜ | Reject unknown platform/version with a `ContentLoadException`-equivalent, matching FNA's own validation |
| XNB-12 | Type-reader table parse (7-bit count, then per-entry 7-bit string + int32 reader version) | ⬜ | |
| XNB-13 | Reader-name normalization step: a real assembly-qualified-name parser (not `substr(0, name.find(','))`), correctly handling commas nested inside generic-argument brackets (`ListReader\`1[[Microsoft.Xna.Framework.Vector3, Microsoft.Xna.Framework, Version=...]], ...`); produces the canonical key decided in XNB-5 | ⬜ | Naive comma-split truncation breaks on every generic reader name — needs its own mini type-name parser (`struct TypeName{namespaceAndName; genericArguments;}`) |
| XNB-13A | Unit tests for XNB-13 covering: plain reader name, one level of generic nesting (`ListReader<Vector3>`), and at least one doubly-nested generic (`DictionaryReader<K, ListReader<V>>`) | ⬜ | Standalone mini-milestone per review — do not fold silently into XNB-13's own task row |
| XNB-14 | `ContentTypeReader` base class + `ContentTypeReaderRegistry` (register/create by normalized name) — registry starts empty, no readers registered yet | ⬜ | Matches the sketch in `xnb.md` |
| XNB-14A | Separate global reader **factories** (registered once, process-wide) from per-`.xnb`-file reader **instances**: parsing a given file's type-reader table creates one initialized reader instance per table entry, scoped to that `ContentReader`'s lifetime; no reader instance is shared across files | ⬜ | Per follow-up review point 4 — prevents a stateful reader accidentally leaking state between unrelated files and keeps the registry thread-safe-by-construction (factories are stateless, instances are not shared) |
| XNB-14B | Register a `KnownUnsupportedContentTypeReader` placeholder mechanism in the registry, and pre-register the general `Microsoft.Xna.Framework.Content.EffectReader` name under it with `UnsupportedReason::CompiledPlatformShaderBytecode` | ⬜ | Per follow-up review point 9 — if a fixture references the general `EffectReader` before Phase E exists, the error should already read "recognized but unsupported" instead of a generic "unknown content reader"; XNB-32A (Phase E) implements the detailed failure message and any later compiled-FX path, this task only reserves the name early |
| XNB-15 | Shared-resource fixup mechanism: parse shared-resource count; read root object; then read each shared resource *in serialized order*, resolving each pending fixup no earlier than when its referenced resource becomes available; guarantee that **all** pending fixups are resolved before the root asset is returned; fail if a required shared resource index is invalid or resolves to the wrong runtime type | ⬜ | Per second follow-up review point 1 — the requirement is the observable *result* (forward references work, no fixup ever runs before its resource is loaded, fixup order is deterministic, a fixup's exception propagates correctly), not one specific implementation strategy; do not lock the architecture into "read all shared resources, only then resolve fixups" if a per-resource-as-available strategy is simpler to get right. Register a `PendingSharedResource{resourceIndex, fixup, expectedType}` callback for forward references. Test: index 0 = null, out-of-range index, multiple fixups on one resource, wrong runtime type |
| XNB-16 | Root-object dispatch via the **1-based type-reader-index protocol**: read a 7-bit-encoded index; `0` means null; otherwise `index - 1` selects the type-reader table entry. Do **not** assume the root uses the table's first entry | ⬜ | Critical correctness fix — this is also how every nested object reference is dispatched, not just the root |
| XNB-16A | Runtime type-safety for dispatched objects: reader results carry both an instance and a stable, CNA-owned `RuntimeTypeId` (e.g. `ContentObject{RuntimeTypeId type; std::shared_ptr<void> instance; template<class T> std::shared_ptr<T> as() const;}`), not a bare untyped `shared_ptr<void>`; `as<T>()` must check `RuntimeTypeId` and throw/return null on mismatch, never a blind `static_pointer_cast` | ⬜ | Registry entries must record reader name, reader type, produced-object type, and reader version — avoids unsafe blind casts later. `shared_ptr<void>` alone is an acceptable *internal* transport type but must never be the public-facing shape callers interact with |
| XNB-16C | Prefer a stable, CNA-owned `RuntimeTypeId{std::uint64_t value}` (derived from a canonical name such as `Microsoft.Xna.Framework.Graphics.Texture2D` or `MyGame.LevelData`) over a bare `std::type_index` as the long-term identity in `RuntimeTypeInfo{RuntimeTypeId id; std::string canonicalName; std::type_index cppType;}`; `std::type_index` may remain a convenience *inside* a single C++ process but must not be the only identity `.xnb` results carry | ⬜ | Per second follow-up review point 2 — `std::type_index` doesn't survive across plugins, Lua custom types registered only by name (Phase H), no-RTTI builds, or stable diagnostics; keep it as an optional secondary C++-side check, not the primary identity |
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
| XNB-17G | Confirm `ContentReader`, per-file reader instances (XNB-14A), shared-resource fixups (XNB-15), and decompression state (Phase D) contain no global mutable state, so different `.xnb` files can be loaded concurrently without cross-talk | ⬜ | Per follow-up review point 10 — cheap to guarantee now, expensive to retrofit once an async loader exists; does not require actually building an async loader yet |

---

## Phase B3 — Early reader-name inventory scanner (payload-agnostic, does not block on Phase G)

> New phase, per second follow-up review point 5: the full official-sample compatibility work
> (Phase I) can stay last, but a **payload-agnostic** scanner that only reads the header and the
> type-reader-name table (XNB-11/XNB-12/XNB-13) — never the actual object payload — has no
> dependency on any production reader existing yet. Running it early can show, for example, that a
> reader planned for Phase F is used by only one sample while a reader not yet in this plan at all
> is used by twenty.
>
> **Correction from the final review:** this phase originally tried to cover compressed `.xnb`
> files too ("decompress just enough of the header/type-reader-table region if compressed"), but
> that is not actually possible before Phase D exists — a compressed `.xnb`'s type-reader table
> lives *inside* the LZX payload, and block-based LZX has no addressable random-access offset to
> "just" the table; the decompressor must process the stream sequentially from its start. XNB-61a
> below is therefore scoped to **uncompressed** files only, right after Phase B. The compressed
> case becomes XNB-61b, sequenced after Phase D once a real decompressor exists. Neither task
> deserializes the object payload — both only need to decode/decompress far enough to finish
> reading the type-reader table, then stop.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-61a | Reader-name-only scanner, **uncompressed `.xnb` files only**: open each available uncompressed `.xnb`, read the header and type-reader table, list every type-reader name referenced, and aggregate counts across all available files — no object-graph deserialization required | ⬜ | Depends only on Phase B (XNB-11/12/13), not on any Phase C+ reader or on Phase D; the full runtime-compatibility matrix and smoke-test selection stay in Phase I (XNB-61-64) and still wait until Phase G is solid |
| XNB-61b | Extend the XNB-61a scanner to **LZX-compressed** XNA `.xnb` files, using CNA's own decompressor (Phase D) — decompress the payload sequentially from the start until the type-reader table has been fully read (for a first implementation, decompressing the entire payload is acceptable; do not prematurely optimize this into a partial-decompression short-circuit); do not continue into the root object. Verify that a compressed fixture and its uncompressed equivalent produce the same reader-name inventory | ⬜ | Sequenced after Phase D (needs a real LZX decoder to exist) — this is the one part of Phase B3 that is *not* available right after Phase B; do not attempt it earlier |

---

## Phase C — Primitive/math readers + collections + first real Graphics reader

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-18A | Simple primitive readers: `Boolean`/`Byte`/`SByte`/`Int16/32/64`/`UInt16/32/64`/`Single`/`Double`/`Char`/`String` | ⬜ | Thin wrappers over Phase A primitives |
| XNB-18B | `System::Decimal` faithful representation (96-bit integer + sign + scale, matching .NET `Decimal`'s actual layout) and its reader — do **not** approximate as `double reader.readDouble()`; decide up front whether CNA implements a real `Decimal` type or only reads the raw 4×`int32` `DecimalValue` struct verbatim for round-tripping | ⬜ | Split out from XNB-18 per follow-up review point 6 — `.NET Decimal` cannot be safely mapped onto `double` without losing precision/compatibility |
| XNB-18C | `DateTime`/`TimeSpan` faithful tick semantics: `DateTime` is ticks + `DateTimeKind` bits packed into a 64-bit value, not a plain `int64_t` timestamp; `TimeSpan` must preserve ticks exactly | ⬜ | Split out from XNB-18 per follow-up review point 6 |
| XNB-19 | Math readers: `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray` | ⬜ | All backing structs already exist per the Phase 0 audit (XNB-1) — pure field-order wiring |
| XNB-20 | `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent`: add the missing runtime classes/enums, then `CurveReader` | ⬜ | Real new runtime API, not just a reader — flagged by the Phase 0 audit (XNB-1); check FNA's `Curve.cs` for the exact public surface needed |
| XNB-21 | `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>` generic dispatch (recursively invoking another registered reader by an embedded type parameter) | ⬜ | The "generic-dispatch" problem called out in `xnb.md` |
| XNB-22 | `NullableReader<T>` (map to `std::optional<T>` on the CNA side, per this session's audit that CNA has no `Nullable<T>` XNA class of its own) | ⬜ | |
| XNB-23 | `Texture2DReader`, implemented strictly against CNA's **backend-neutral** `Texture2D`/`GraphicsDevice` API (`SurfaceFormat` + width/height/mip levels + per-level `SetData`) — the reader must not reference EasyGL (or any other backend) internals directly | ⬜ | Full code sketch already in `xnb.md`; correct layering is `XNB reader → CNA Texture2D API → active GraphicsDevice backend`, never `XNB reader → EasyGL internals`. Tests may target EasyGL first, but production reader code must stay backend-agnostic |
| XNB-24 | `SurfaceFormat` capability inventory (not immediate blanket conversion): classify each `SurfaceFormat` as native-upload / CPU-conversion / GPU-transcode / unsupported / lossy-fallback per backend (`struct SurfaceFormatCapability{bool nativeUpload; optional<SurfaceFormat> fallback; ConversionFunction converter;}`); `Texture2DReader` only asks `GraphicsDevice`/a formats service for a compatible resource, it does not own per-backend conversion tables itself | ⬜ | `Dxt1/3/5` at minimum. `SurfaceFormat` enum itself already has full coverage per the Phase 0 audit (XNB-1); this is about each backend's texture-upload path, kept out of the reader |
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
| XNB-30C | Phase D2 — investigate/implement MonoGame-supported alternative compression variants under the `XnbCompression` enum from XNB-27 | ⏸ | Deferred until broad MonoGame compatibility work (Phase G/XNB-44); explicitly optional for the first working loader and not required by any milestone M1-M5 — the enum from XNB-27 just needs to not preclude it later |

---

## Phase D3 — Additional texture readers (physically after LZX, not just a "deferred" note)

> Per follow-up review point 7: the previous draft only had a *note* saying `Texture3DReader`/
> `TextureCubeReader` were "deferred until after Phase D" while the task row itself still sat
> physically inside Phase C, which could mislead an autonomous agent into implementing it too early.
> This phase makes that ordering unambiguous by relocating the task itself.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-25 | `Texture3DReader`/`TextureCubeReader`/base `TextureReader` | ⬜ | Same shape as `Texture2DReader` (XNB-23/24), extra dimension/face handling; deliberately sequenced after Phase D so compressed fixtures already work before adding these |

---

## Phase E — `SpriteFont`, stock effects, `SoundEffect`/`Song`

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-31 | `SpriteFontReader` (glyph atlas `Texture2D` + rects + character map + kerning + optional default char via `NullableReader`) | ⬜ | Depends on Phase C's collection/`Nullable` readers |
| XNB-32 | Stock-effect readers: `BasicEffectReader`/`AlphaTestEffectReader`/`DualTextureEffectReader`/`EnvironmentMapEffectReader`/`SkinnedEffectReader` — deserialize the exact stock-effect fields stored by each corresponding XNA reader (colors, texture references, lighting flags, fog parameters, etc.) and construct CNA's own native stock-effect implementation from them; do **not** route the object through the general `EffectReader`'s compiled-bytecode path at all | ⬜ | Reworded per follow-up review point 8 — these readers serialize the *parameters needed to reconstruct the stock effect*, not a generic bytecode blob that gets "ignored"; deliberate scope-narrowing decision from `xnb.md`, avoids any dependency on `plan_graphics.md` Phase 74 |
| XNB-32A | Implement the actual detection/failure path for the general `Microsoft.Xna.Framework.Content.EffectReader` (compiled platform shader bytecode, distinct from the stock-effect readers in XNB-32): precise, documented exception message until real compiled-FX support exists (see `plan_graphics.md` Phase 74) | ⬜ | The name is already reserved as known-unsupported in Phase B (XNB-14B) so an early fixture never produces a bare "unknown content reader"; this task adds the detailed message/diagnostics and any later compiled-FX hookup |
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
| XNB-47 | Final compliance audit against the per-reader checklist below: exact serialized layout confirmed from an authoritative implementation; ≥ 1 real externally-produced fixture; success test; truncated-input test; invalid-count/size test; wrong-reader-version test; asset ownership/unload verified; backend-independent behavior verified; supported producer/platform variants documented | ⬜ | Per follow-up review point 11 — this checklist is **not** retroactive-only: it is the continuous Definition of Done for every reader task starting in Phase C (see the callout above Phase C). This task is the final confirmation sweep, not the first time the checklist is applied |

> **Definition of Done for every reader task from Phase C onward:** a reader task may only be
> marked ✅ once all of the following hold — do not wait until XNB-47 to check these for the first
> time: (1) exact serialized layout confirmed from an authoritative implementation; (2) ≥ 1 real
> externally-produced fixture (never generated by CNA itself) loads correctly; (3) a truncated-input
> test; (4) an invalid-count/size test; (5) a wrong-reader-version test; (6) asset ownership/`Unload()`
> behavior verified; (7) backend-independent behavior verified; (8) supported producer/platform
> variants documented. "Compiles" and "one hand-crafted fixture loads" are both explicitly
> insufficient on their own.

---

## Phase H — Lua-scripted custom `ContentTypeReader` support (strictly after Phase A–G are solid)

> New phase, per the issue's follow-up discussion on custom readers. Lua is a good fit for
> **game/sample-specific data readers** (`SkinningDataReader`, `ParticleSettingsReader`,
> `LevelReader`, ...), letting a portable/sample-specific `.xnb` custom reader be shipped as a
> script instead of requiring a CNA recompile — but it must not become the primary implementation
> path for standard readers, and it must not exist before the native reader framework (Phase A–G)
> is itself correct, since the Lua binding surface would otherwise have to be redesigned repeatedly.
> Standard readers (`Texture2DReader`, `SpriteFontReader`, `ModelReader`, buffer readers, stock
> effects, `ListReader`/`DictionaryReader`, ...) **stay in native C++ permanently** — performance,
> GPU-resource ownership, and validation robustness all argue against moving them to Lua.
>
> An automatic, general-purpose C# `ContentTypeReader` → Lua converter is **not** a realistic goal
> (reflection, `Activator.CreateInstance`, LINQ, delegates, private-member access, and calls into
> arbitrary managed libraries cannot be mechanically translated). The realistic framing is
> **AI-assisted porting** of custom readers to hand-reviewed Lua scripts, verified against a real
> fixture, not a guaranteed-correct automatic translation.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-48 | Define `ILuaContentReaderHost` abstraction (`ContentObject read(const LuaReaderDescriptor&, ContentReader&, const ContentObject& existing)`) | ⬜ | Keeps the Lua integration behind one seam, so the native reader framework from Phase A–G does not need to know Lua exists |
| XNB-49 | Add a sandboxed Lua state dedicated to content readers: only expose `base`/`math`/`string`/`table` subsets plus the `CNA.Content`/`CNA.Math` tables — no `os`/`io`/`package`/`debug` | ⬜ | Reader scripts are executable code, not data; must not be able to touch the filesystem, spawn processes, or introspect arbitrary CNA internals |
| XNB-50 | Bind primitive `ContentReader` operations to Lua (`read_bool`/`read_byte`/`read_int16/32/64`/`read_single`/`read_double`/`read_string`/`read_bytes(count)`) | ⬜ | Thin wrappers over the Phase A primitives already implemented in C++ |
| XNB-51 | Bind XNA math reads and value-type constructors to Lua (`read_vector2/3/4`, `read_matrix`, `read_quaternion`, `read_color`, `read_rectangle`, `read_bounding_sphere`, `CNA.Vector3.new(...)`, etc.) | ⬜ | Reuses the Phase C math readers/structs — no new binary-protocol logic |
| XNB-52 | Add a Lua reader descriptor/manifest format (`{xnbName, script, resultType, versions}` in JSON, per-directory), with explicit manifest lookup preferred over filename-guessing fallback | ⬜ | Explicit manifest avoids silently loading the wrong script for an ambiguous/renamed reader name |
| XNB-53 | Allow `ContentTypeReaderRegistry` (XNB-14/XNB-14A) to register Lua-backed readers alongside native C++ factories, resolved through the same normalized-name lookup from XNB-13 | ⬜ | Must not require two different registries/lookup paths from the caller's perspective |
| XNB-54 | Add typed `ContentObject` factories for custom game data returned from Lua (`CNA.ContentObject.new("MyGame.LevelData", {...})` or explicit typed constructors), never a bare untyped Lua table returned as-is | ⬜ | Mirrors the XNB-16A type-safety requirement — a Lua table alone must not be silently treated as any CNA object |
| XNB-55 | Add bulk array-read bindings (`read_float_array(count)`, `read_vector3_array(count)`, `read_int32_array(count)`, `read_blob(count)`) to avoid per-element Lua call overhead on large payloads | ⬜ | Per-element Lua calls for e.g. large vertex counts would be a real performance problem; large GPU payloads should still prefer a native C++ reader entirely |
| XNB-56 | Add safe shared-resource references for Lua readers: prefer a deferred `read_shared_resource_ref("Texture2D")` handle resolved automatically by C++ after all shared resources are read, over holding a raw Lua closure/callback across the whole read | ⬜ | Holding arbitrary Lua closures alive across `ReadSharedResource` risks lifetime/GC-safety bugs; a resolved-handle model is safer from the C++ side |
| XNB-57 | Add external-reference support for Lua readers, delegating to the native `ContentReader::ReadExternalReference<T>()` from XNB-35 | ⬜ | Do not reimplement path resolution/cycle detection in Lua |
| XNB-58 | Add memory and instruction-count limits to the reader sandbox (custom Lua allocator with a byte budget; instruction-count hook with a hard cap) | ⬜ | Prevents a malicious or buggy custom reader script from hanging or exhausting memory during content load |
| XNB-58A | Lua reader error-context translation: every error surfaced from a Lua reader must preserve the script path, the normalized reader name, the `.xnb` asset path, the Lua stack trace, the current binary offset, and the current object-nesting path (e.g. `Root.Tag.AnimationClips[3]`) | ⬜ | Per second follow-up review point 3 — without this, debugging a faulty Lua reader degrades to guessing; a bare Lua error message alone is not acceptable diagnostics |
| XNB-59 | Port one real custom reader from an official XNA sample to Lua as the first end-to-end proof (`SkinningDataReader` recommended — exercises dictionary/list/matrix/nested-object/custom-runtime-type all at once) | ⬜ | Must be verified against a real externally-produced fixture, per the Phase C+ Definition of Done, not a hand-crafted one |
| XNB-60 | Document the AI-assisted C# `ContentTypeReader` → Lua porting workflow (what a human must still review/verify; explicitly not a guaranteed-correct automatic translation) | ⬜ | Sets expectations correctly — mirrors XNB-45's documentation style |

---

## Phase I — Official XNA 4.0 sample `.xnb` compatibility inventory

> New phase — replaces guesswork about "broad real-world compatibility" with actual numbers, by
> scanning real official sample content instead of only the hand-picked fixture corpus from
> XNB-17A/XNB-44. Per the third revision, the payload-agnostic reader-*name* scan itself already
> happened much earlier as XNB-61a/XNB-61b (Phase B3) — this phase reuses that inventory and adds
> the parts that genuinely do need Phase G's finished reader set: the compatibility classification
> and the smoke-test selection below.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-61 | Re-run/refresh the XNB-61a/XNB-61b (Phase B3) reader-name inventory against the final Phase G reader set if it has gone stale, rather than re-implementing the scan from scratch | ⬜ | Formerly the first scan itself; the scan now lives in Phase B3 (XNB-61a for uncompressed, XNB-61b for LZX) so it doesn't wait for Phase G |
| XNB-62 | Produce a compatibility matrix classifying each reader name found: standard reader (Phase A–F) / custom reader (Phase H candidate) / `ReflectiveReader` (XNB-42A limitation) / general `EffectReader` (XNB-32A limitation) | ⬜ | |
| XNB-63 | Select a representative smoke-test set covering: plain 2D texture, `SpriteFont`, audio, a stock-effect model, one custom-reader sample, one custom-`.fx` sample | ⬜ | Small enough to run routinely, broad enough to catch regressions across phases |
| XNB-64 | Track "`.xnb` loads successfully" compatibility separately from "full sample runs correctly at runtime" compatibility — the two are different claims and must not be conflated in `docs/xnb-content-pipeline-support.md` (XNB-45) | ⬜ | A `.xnb` can deserialize successfully while the sample still fails at runtime for unrelated reasons (input, gamerservices, etc.) — keep the claims separate and honest |

---

## Relationship to other plan files

- [`xnb.md`](xnb.md) — the narrative design document this task list is derived from; read it for
  *why*, not just *what*.
- [`plan_graphics.md`](plan_graphics.md) Phase 74 — compiled `.fx` shader bytecode via MojoShader;
  related but independent (see `xnb.md`'s own "Effect/compiled shader bytecode" section). Phase E's
  stock-effect readers (XNB-32) deliberately avoid needing Phase 74 at all; the general `EffectReader`
  (XNB-32A) is the one place this plan and Phase 74 actually connect.
- [`docs/model-content-pipeline-support.md`](docs/model-content-pipeline-support.md) — the existing,
  already-documented gaps in CNA's current (non-`.xnb`) `ModelTypeReader` that Phase F (XNB-37 to
  XNB-40) closes as part of building the real `.xnb` `ModelReader`.
- Phase H (Lua custom readers) and Phase I (official-sample inventory) are both explicitly
  sequenced *after* Phase A–G — neither should start before the native reader framework and its
  hardening pass are solid. Phase B3 is the one deliberate exception: XNB-61a (uncompressed files)
  is a payload-agnostic reader-*name* scanner that only depends on Phase B and may run at any point
  once Phase B exists; XNB-61b (LZX-compressed files) is the same scanner extended once Phase D's
  decompressor exists — it cannot run any earlier than that, regardless of the Phase B3/Phase I
  split.
- See the "Execution-order mandate" and "Milestones" sections near the top of this file — they are
  the primary agent-facing guardrails for a long autonomous run and should be re-read whenever this
  plan is revised.
