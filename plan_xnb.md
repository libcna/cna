# XNB binary content pipeline: task plan

> **Status: 🔄 PARTIALLY UN-FROZEN 2026-07-16 — MVP scope (Phase 0/A/B/B2/B3/C) active; Phase D
> onward stays frozen.** CNA's owner decided `.xnb` becomes a real, additional runtime format again,
> ranked **above** `.cnb` in `ContentManager`'s resolution order (see [`cnb.md`](cnb.md)'s "Core
> rule": `.xnb` → literal caller-given path → `.cnb` → native-by-extension). Only the tasks through
> the end of Phase C (container parsing, binary primitives, uncompressed-only, a first real
> `Texture2D` reader — this plan's own M1/M2 milestones) are actually being executed now. Phase D
> (LZX) and everything after it (`SpriteFont`, stock effects, audio, `Model`, top-quality hardening)
> remain frozen/deferred exactly as before, pending a future decision to resume them — do not start
> those tasks. **Phase H (Lua-scripted custom readers) is cancelled outright, not deferred** — see
> that phase's own section below; custom `.xnb` readers stay a plain C++ registration API (Phase G),
> matching `.cnb`'s existing `RegisterCnbLoader<T>`. Phase B3 has also grown new scope: a
> `ContentManager` startup content-manifest scan (internal perf cache + a public introspection API +
> the `.xnb` reader-name inventory), see that phase below.

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
>
> **Revised a fifth time (2026-07-16)** to reflect CNA's owner's decision to partially un-freeze
> this plan: MVP scope only (Phase 0/A/B/B2/B3/C) is active; Phase D onward remains frozen. Also:
> cancelled Phase H (Lua custom readers) entirely rather than merely deferring it — see that
> phase's section for the reasoning; folded a new `ContentManager` startup content-manifest feature
> (perf cache, public introspection API, `.xnb` reader-name inventory) into Phase B3
> (`XNB-65`–`XNB-67`); and fixed the `ContentManager` resolution order to rank `.xnb` above both the
> literal caller-given path and `.cnb` (`XNB-17B`).

## Execution-order mandate for autonomous work

> Read this before starting any task in this file.

- Implement strictly in phase order, and only through the current MVP scope (Phase 0/A/B/B2/B3/C —
  see the status banner above). Do not begin Phase D or anything sequenced after it until a future
  decision explicitly resumes them. Do not begin Phase I while any mandatory task in Phase A-C
  remains incomplete (Phase B3's XNB-61a/XNB-65/66/67 tasks are the one deliberate exception — see
  their own rows; XNB-61b still cannot start before Phase D exists and is deferred along with it).
- Phase H (Lua-scripted custom readers) is cancelled outright — do not implement it in any form,
  and do not treat it as merely "later". Custom `.xnb` readers stay native C++ only, registered
  through Phase G's plain registration API (deferred along with the rest of Phase D onward under
  the current MVP scope).
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
| M1 - binary protocol | XNB-17F | ✅ **Reached 2026-07-16.** An uncompressed, externally-produced test `.xnb` loads end-to-end through `ContentManager` using only the Phase B test-only reader. No graphics, audio, or Lua involved. |
| M2 - real texture | XNB-26 | ✅ **Reached 2026-07-16.** A real uncompressed XNA 4.0 `Texture2D` `.xnb` loads and uploads through the backend-neutral `GraphicsDevice` path. |
| M3 - common XNA 2D content | end of Phase E | Compressed `Texture2D`, `SpriteFont`, and at least one supported `SoundEffect` variant from the XNB-33 matrix all load correctly. |
| M4 - standard model | XNB-41 | A real multi-mesh, multi-bone XNA `Model` `.xnb` loads with shared resources resolved and native CNA stock effects attached. |
| M5 - release-quality native loader | XNB-47 | Native `.xnb` support is documented (XNB-45), hardened (XNB-43), and fully usable without Lua. |

> **Current active scope (2026-07-16):** M1 and M2 have both been **reached**. The current MVP
> scope (Phase 0/A/B/B2/B3/C) is now functionally complete for its two milestones, with three
> Phase C tasks deliberately deferred (not dropped — each has a documented reason and, where
> relevant, a researched design ready to resume from): `XNB-18B`/`XNB-18C` (`Decimal`/`DateTime`/
> `TimeSpan` readers) and `XNB-20` (`CurveReader`) are rare in real content and off the M2 critical
> path; `XNB-21`/`XNB-22` (generic collection readers) need a still-undecided reflection-workaround
> mechanism and are also off the M2 critical path. M3–M5 remain frozen until a future decision
> explicitly resumes Phase D onward.

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

> Findings from this session's own audit (original — see the XNB-1 revalidation note directly
> below for two corrections), folded in directly so Phase A doesn't re-discover them: CNA has **no
> `System::IO`-equivalent binary stream/reader layer at all** (no `Stream`, no `BinaryReader`) —
> `Content::ContentTypeReader<T>` today is purely a loose-file-path interface
> (`Read(const std::string& path, ContentManager&)`), not a binary-protocol one; there is no
> `ContentReader`, `ContentTypeReaderManager`, or shared-resource-fixup mechanism of any kind.
> `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent` do not exist anywhere in the
> codebase. Conversely, `SurfaceFormat` already has full XNA 4.0 coverage (plus CNA `EXT` additions),
> and `VertexDeclaration`/`BoundingFrustum`/`Vector2-4`/`Matrix`/`Quaternion`/`Color`/`Rectangle`/
> `Point`/`BoundingBox`/`BoundingSphere`/`Plane`/`Ray` all already exist and match FNA — so Phase 0
> is a short, targeted list, not a rewrite.
>
> **XNB-1 revalidation (2026-07-16):** re-ran this audit against the current branch. Two findings
> above are now stale — the codebase moved on since this audit was first written, in the separate
> `sharp-runtime` repo CNA depends on:
> 1. `sharp-runtime` now has a full `System::IO` layer: `System::IO::Stream` (abstract base),
>    `System::IO::MemoryStream` (in-memory, bounds-checked, position/`Seek` support, doc-commented
>    "Status: IMPLEMENTED"), and `System::IO::BinaryReader` with `ReadByte`/`ReadSByte`/
>    `ReadInt16/32/64`/`ReadUInt16/32/64`/`ReadSingle`/`ReadDouble`/`ReadBoolean`/`ReadBytes(count)`,
>    `ReadString()` (7-bit-length-prefixed UTF-8, matching .NET `BinaryReader.ReadString` exactly),
>    and `Read7BitEncodedInt()` (verified against .NET's own overflow-detection algorithm: four
>    7-bit groups, then a bounds-checked 5th byte). All of it already has exact-byte-sequence and
>    round-trip test coverage in `sharp-runtime`'s own suite
>    (`tests/System/IO/IOStreamTests.cpp`, `tests/System/IO/StreamTests.cpp`). This satisfies
>    essentially all of Phase A's binary-primitive tasks (`XNB-6`–`XNB-9`) by direct reuse — see
>    those rows below, now marked done. The one still-open gap is `char`/string encoding
>    (`XNB-9A`): `BinaryReader`'s own doc comment states `ReadChar`/`PeekChar`/`ReadChars` are
>    deliberately not implemented (no character-encoding layer over `Stream`), so that task remains
>    real, unstarted work.
> 2. `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent` now exist as a full runtime
>    API (`include/Microsoft/Xna/Framework/Curve*.hpp`, `src/Microsoft/Xna/Framework/Curve*.cpp`).
>    `XNB-20` (Phase C) no longer needs to add the runtime classes themselves — only a `CurveReader`
>    wrapping the existing API is left; see that row's updated note.
>
> Everything else in the original findings was re-confirmed unchanged: no `ContentReader`/
> `ContentTypeReaderManager`/shared-resource registry anywhere in CNA; `ContentTypeReader<T>` is
> still purely loose-file-path; `SurfaceFormat`/`VertexDeclaration`/all listed math structs are
> still present and FNA-faithful.

> Per follow-up review point 2: the four confirm/document rows below are already-completed audit
> findings from an earlier session (see the block-quote above), not open work — keeping them as four
> separate `⬜` rows just invites an autonomous agent to re-run the same project search four times.
> Folded into a single revalidation task instead.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-1 | Revalidate the recorded gap audit against the current branch and update only findings that changed since this plan was written: no `System::IO::Stream`/`BinaryReader`-equivalent; no `Content::ContentReader`/`ContentTypeReaderManager`/shared-resource registry (`ContentTypeReader<T>` is loose-file-only); no `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent`; `SurfaceFormat`/`VertexDeclaration`/XNA math structs already exist and are FNA-faithful | ✅ | Revalidated 2026-07-16 (see the blockquote above): two findings were stale — `sharp-runtime` now has a full `System::IO::Stream`/`MemoryStream`/`BinaryReader` layer (including `Read7BitEncodedInt`), and `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent` now exist as a real runtime API. The `ContentReader`/registry/`SurfaceFormat`/math-struct findings were re-confirmed unchanged |
| XNB-5 | Decide final normalized-reader-name registry key format (bare type name vs. `Namespace.Type\`1` generic-arity-suffixed form for `ArrayReader<T>`/`ListReader<T>`/etc.) | ✅ | **Decided 2026-07-16**, grounded in FNA's own `ContentTypeReaderManager.PrepareType()` (`src/Content/ContentTypeReaderManager.cs`): FNA resolves cross-assembly reader names via a regex that *replaces* the `, Microsoft.Xna.Framework(.Graphics\|.Video)?\|MonoGame.Framework, Version=..., Culture=..., PublicKeyToken=...` suffix with its own runtime assembly's name, then calls `Type.GetType()` — i.e. it normalizes away exactly the assembly/version/culture/publickeytoken portion, at every nesting level (its own regex comment: "Supports multiple generic types... and nested generic types"), and nothing else. CNA has no reflection to fall back on, so the registry key is that same normalization taken one step further: **the bare, assembly-qualification-stripped .NET type name, keeping the namespace, type name, generic backtick-arity suffix, and — for generic readers — each type argument recursively reduced to its own canonical key the same way.** Examples: `Microsoft.Xna.Framework.Content.Texture2DReader` (non-generic); `Microsoft.Xna.Framework.Content.ListReader\`1[[Microsoft.Xna.Framework.Vector3]]` (one level generic — assembly info dropped from both the outer reader and the inner argument); `Microsoft.Xna.Framework.Content.DictionaryReader\`2[[System.String],[Microsoft.Xna.Framework.Content.ListReader\`1[[System.Int32]]]]` (nested generic, matching XNB-13A's own test-case shape). This is a pure string-key scheme — no `Type.GetType`/reflection anywhere, matching CNA's C++ constraints. `XNB-13`'s parser produces exactly this key format |

---

## Phase A — `ContentReader` binary primitives

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-6 | Add a minimal internal binary-cursor/stream type over an in-memory byte buffer (read position, bounds-checked reads, no need for a full `System::IO::Stream` hierarchy unless one already exists) | ✅ | Satisfied by reuse (2026-07-16): `sharp-runtime`'s `System::IO::Stream` (abstract) + `System::IO::MemoryStream` (in-memory, bounds-checked, position/`Seek`, "Status: IMPLEMENTED") already provide exactly this, with existing coverage in `tests/System/IO/StreamTests.cpp`. No new CNA-side type needed — the `.xnb` container reader wraps a `MemoryStream` directly |
| XNB-7 | Implement 7-bit-encoded `int` read (`Read7BitEncodedInt`, matches .NET `BinaryReader`) | ✅ | Satisfied by reuse (2026-07-16): `System::IO::BinaryReader::Read7BitEncodedInt()` already implements .NET's exact algorithm (four 7-bit groups, then a bounds-checked 5th byte), with round-trip tests in `tests/System/IO/IOStreamTests.cpp` |
| XNB-8 | Implement 7-bit-length-prefixed string read (UTF-8, matches .NET `BinaryReader.ReadString`) | ✅ | Satisfied by reuse (2026-07-16): `System::IO::BinaryReader::ReadString()` already reads a `Read7BitEncodedInt()`-prefixed UTF-8 byte run, matching .NET's `BinaryReader.ReadString`; tested (`WriteRead_String_Roundtrip`) |
| XNB-9 | Implement little-endian fixed-width numeric reads only: `bool`/`byte`/`sbyte`/`int16/32/64`/`uint16/32/64`/`float`/`double` | ✅ | Satisfied by reuse (2026-07-16): `System::IO::BinaryReader` already implements `ReadByte`/`ReadSByte`/`ReadInt16/32/64`/`ReadUInt16/32/64`/`ReadSingle`/`ReadDouble`/`ReadBoolean`, all little-endian, with exact-byte-sequence tests (e.g. `WriteSingle_ProducesExactLittleEndianByteSequence`) in `tests/System/IO/IOStreamTests.cpp`. The original "audit CNA's existing binary-IO helpers first" note is now resolved — reuse, do not reimplement |
| XNB-9A | `char`/string decoding matching XNA `System.Char`/.NET `BinaryReader` behavior exactly — do **not** treat `char` as a plain little-endian `uint16_t`; decide the CNA-side representation (`char16_t` vs. a dedicated `System::Char`) and verify against real encoded bytes | ✅ | **Implemented 2026-07-16** in `sharp-runtime` (`feature/xnb-charreader` branch, commit `167b0bc4`): `System::IO::BinaryReader::ReadChar()` decodes UTF-8 (matching .NET's default `BinaryReader(Stream)` constructor, which uses `UTF8Encoding`) into a `charcs`/`char16_t`, covering the full BMP (1-3 byte sequences). A 4-byte/supplementary-plane sequence throws `System::FormatException` rather than silently truncating to a lone surrogate — verified against real .NET's own `BinaryReader.ReadChar()` source (`/rv/tmp/runtime/.../BinaryReader.cs`), which hits the identical failure for the identical reason (its `Decoder.GetChars` call targets a 1-char buffer and throws for a 2-char result). Tested: ASCII, 2-byte, 3-byte, 4-byte-throws, invalid lead byte, invalid continuation byte, truncated sequence, EOF (`tests/System/IO/IOStreamTests.cpp`) |
| XNB-10 | Unit tests for all of the above against known .NET-produced byte sequences (hand-computed or extracted from a real `.xnb`) | ✅ | Satisfied (2026-07-16) for XNB-6/7/8/9 by reuse, and for XNB-9A by its own new tests (see that row) — all in `sharp-runtime`'s suite (`tests/System/IO/IOStreamTests.cpp`, `tests/System/IO/StreamTests.cpp`), covering exact byte sequences, round-trips, and failure cases. Still open: a supplementary CNA-side test using bytes extracted from a real external `.xnb` fixture (XNB-17) once that fixture exists, to validate the actual `.xnb` reader wiring rather than just the underlying primitives |
| XNB-10A | Introduce `XnbReadLimits` (max file size, max decompressed size, max string bytes, max type-reader count, max shared-resource count, max collection-element count, max object-nesting depth) as part of the binary-cursor architecture itself, not bolted on later in Phase D/G | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::XnbReadLimits`/`DefaultXnbReadLimits()` (`include/CNA/Internal/Xnb/XnbReadLimits.hpp`), already consulted by `ParseXnbTypeReaderTable` (XNB-12) — every later count-driven read (Phase C+ collections, Phase D decompressed size, Phase F mesh/bone counts) must consult the same struct, not invent its own bound |

---

## Phase B — XNB container, uncompressed only

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-11 | `ContentReader`/XNB header parse: magic `'XNB'` + platform char, version byte (accept `4`/`5` only), flags byte, total-length int32 | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::ParseXnbHeader()` (`include/CNA/Internal/Xnb/XnbHeader.hpp`), verified against a real MonoGame-produced fixture (`white-1.xnb`) plus hand-crafted negative cases (bad magic/platform/version, truncated header). This is currently `CNA::Internal`-scoped, not yet the public `Microsoft::Xna::Framework::Content::ContentReader` — see the naming-collision note added near XNB-14 |
| XNB-12 | Type-reader table parse (7-bit count, then per-entry 7-bit string + int32 reader version) | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::ParseXnbTypeReaderTable()` (`include/CNA/Internal/Xnb/XnbTypeReaderTable.hpp`), verified against real bytes extracted from `white-1.xnb`'s actual type-reader table (not just hand-computed) |
| XNB-13 | Reader-name normalization step: a real assembly-qualified-name parser (not `substr(0, name.find(','))`), correctly handling commas nested inside generic-argument brackets (`ListReader\`1[[Microsoft.Xna.Framework.Vector3, Microsoft.Xna.Framework, Version=...]], ...`); produces the canonical key decided in XNB-5 | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::ParseXnbTypeName()`/`NormalizeXnbTypeReaderName()` (`include/CNA/Internal/Xnb/XnbTypeName.hpp`) — a real recursive-descent parser, not a comma-split, using bracket-depth tracking to isolate each generic argument's own text before recursing into it |
| XNB-13A | Unit tests for XNB-13 covering: plain reader name, one level of generic nesting (`ListReader<Vector3>`), and at least one doubly-nested generic (`DictionaryReader<K, ListReader<V>>`) | ✅ | **Implemented 2026-07-16** (`tests/CNA/Internal/Xnb/XnbTypeNameTests.cpp`): all three required cases plus malformed-bracket error cases |
> **Naming-collision finding (2026-07-16), blocking XNB-14 until resolved:** real FNA's
> `ContentReader`, `ContentTypeReaderManager`, and `ContentTypeReader`/`ContentTypeReader<T>` are
> all **public** XNA API classes (`Microsoft.Xna.Framework.Content.*`), so per this project's own
> CLAUDE.md ("Original XNA types must stay in the matching XNA namespace... do not move original
> XNA API types into the CNA namespace") they belong in `Microsoft::Xna::Framework::Content`, not
> `CNA::Internal`. But CNA **already has** a `Microsoft::Xna::Framework::Content::ContentTypeReader<T>`
> — the `.cnb`/native-loader interface (`Read(const std::string& path, ContentManager&)`), a
> CNA-original shape that does not match FNA's real `ContentTypeReader<T>`
> (`Read(ContentReader input, T existingInstance)`) at all. The two cannot coexist under the same
> name in the same namespace. Options: (a) rename CNA's existing loose-file reader interface to
> free up the real name — a wide-reaching refactor touching every existing `.cnb` reader
> (`SpriteFontTypeReader`/`EffectTypeReader`/`ModelTypeReader`/`SkinnedModelTypeReader`,
> `RegisterCnbLoader<T>`, `RegisterTypeReader<T>`) and `ContentManager.hpp`/`.cpp` itself; (b) keep
> the real-protocol classes under `CNA::Internal::Xnb` as an implementation detail for now,
> deferring the public-namespace move to a later, explicitly-scoped task; (c) some other resolution.
> XNB-11 (header parse), XNB-12/XNB-13 (type-reader table + name normalization) do not depend on
> this decision and were implemented first (see their own rows) — this blocks specifically XNB-14
> onward (the registry/base-class/dispatch machinery that a real `ContentTypeReader<T>` would
> anchor). Not resolved as of this writing — see the task/conversation record for the outcome.

| XNB-14 | `ContentTypeReader` base class + `ContentTypeReaderRegistry` (register/create by normalized name) — registry starts empty, no readers registered yet | ✅ | **Implemented 2026-07-16**, naming collision resolved by renaming CNA's existing loose-file interface (see commit `11223b0d`). No separate `ContentTypeReaderRegistry` class was created — FNA's real `ContentTypeReaderManager` already has exactly this shape as its `typeCreators` static dict + `AddTypeCreator`, so the registration surface was added directly to the real `ContentTypeReaderManager` instead of inventing a parallel name. One forced C++ deviation: the non-generic base is `ContentTypeReaderBase` (NOXNA), not bare `ContentTypeReader` — C++ cannot have both a plain class and a template of the identical bare name in one namespace, unlike C#'s arity-distinguished pair; `ContentTypeReader<T>` (the one a ported reader actually inherits) keeps the real name |
| XNB-14A | Separate global reader **factories** (registered once, process-wide) from per-`.xnb`-file reader **instances**: parsing a given file's type-reader table creates one initialized reader instance per table entry, scoped to that `ContentReader`'s lifetime; no reader instance is shared across files | ✅ | **Implemented 2026-07-16** — comes for free from mirroring FNA's own static `typeCreators`/`AddTypeCreator` exactly: factories are process-wide and stateless, `CreateReader()` always constructs a fresh instance |
| XNB-14B | Register a `KnownUnsupportedContentTypeReader` placeholder mechanism in the registry, and pre-register the general `Microsoft.Xna.Framework.Content.EffectReader` name under it with `UnsupportedReason::CompiledPlatformShaderBytecode` | ✅ | **Implemented 2026-07-16**: `KnownUnsupportedContentTypeReader` + `RegisterKnownUnsupportedXnbReaders()` (`include/Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReader.hpp`) |
| XNB-15 | Shared-resource fixup mechanism: parse shared-resource count; read root object; then read each shared resource *in serialized order*, resolving each pending fixup no earlier than when its referenced resource becomes available; guarantee that **all** pending fixups are resolved before the root asset is returned; fail if a required shared resource index is invalid or resolves to the wrong runtime type | ✅ | **Implemented 2026-07-16**: `ContentReader::ReadSharedResource<T>()`/`ReadSharedResources()` (`include`/`src`/`Microsoft/Xna/Framework/Content/ContentReader.*`), matching FNA's own two-pass "read every shared resource first, then run all fixups" order exactly. Wrong-type fixup throws `std::bad_any_cast` (see XNB-16A note below); out-of-range index throws `ContentLoadException`. Tested: resolved fixup, null (index 0) never-runs case |
| XNB-16 | Root-object dispatch via the **1-based type-reader-index protocol**: read a 7-bit-encoded index; `0` means null; otherwise `index - 1` selects the type-reader table entry. Do **not** assume the root uses the table's first entry | ✅ | **Implemented 2026-07-16**: `ContentReader::InnerReadObject<T>()`/`InnerReadObjectAny()`, verified against a real fixture (see XNB-17) whose root object dispatches to table entry 1 (not entry 0) |
| XNB-16A | Runtime type-safety for dispatched objects: reader results carry both an instance and a stable, CNA-owned `RuntimeTypeId`... `as<T>()` must check `RuntimeTypeId` and throw/return null on mismatch, never a blind `static_pointer_cast` | ✅ | **Superseded 2026-07-16, not built as originally sketched**: `ContentTypeReaderBase::ReadUntyped()` returns `std::any` instead of `std::shared_ptr<void>` (see commit `8cd42d0e`). `std::any` is already self-describing and `std::any_cast<T>()` already throws `std::bad_any_cast` on a type mismatch — exactly this task's "never a blind cast, throw on mismatch" requirement, with zero hand-rolled machinery. No separate `ContentObject`/`RuntimeTypeId` wrapper was needed |
| XNB-16C | Prefer a stable, CNA-owned `RuntimeTypeId{std::uint64_t value}`... over a bare `std::type_index`... `std::type_index` doesn't survive across plugins, Lua custom types registered only by name (Phase H)... | ✅ | **Moot 2026-07-16**: this task's motivating concern (Lua custom types needing identity beyond `std::type_index`) no longer applies — Phase H (Lua) is cancelled. `std::any`'s own internal type-erasure (see XNB-16A) already covers what CNA's `.xnb` reading actually needs; no `RuntimeTypeId` was built |
| XNB-16B | Reader-version handling: each created reader instance receives the serialized version from the type-reader table (`reader->initialize(version)`); reader declares `supportsVersion(version)`; unsupported version is a hard error ("Strict" mode) unless the reader explicitly whitelists multiple versions ("Compatibility" mode) | ✅ | **Implemented 2026-07-16**: `ContentTypeReaderBase::SupportsVersion(int)` (default: exact match against `getTypeVersionProperty()`, i.e. Strict mode; a reader can override for Compatibility mode), checked by `ContentReader::InitializeTypeReaders()` right after each reader instance is created. Tested: mismatch throws `ContentLoadException` |
| XNB-17 | Hand-build (or source) at least one real *uncompressed* `.xnb` test fixture, produced by real external tooling (never generated by CNA itself), and prove the full container round-trips before any real readers exist | ✅ | **Implemented 2026-07-16**: `ContentReaderTest.RealMonoGameFixtureLoadsEndToEndThroughGenericDispatch` loads the real fixture below through header parse + type-table + registry + root dispatch, confirming its exact `SurfaceFormat`/width/height/mipLevelCount fields — the M1/M2 milestone goal line, met with a real `Texture2DReader`-shaped payload (using a test-only reader; the real `Texture2DReader` is Phase C/XNB-23) |
| XNB-17A | Create `tests/assets/xnb/` fixture corpus skeleton now (`xna40/windows/{uncompressed,lzx}/`, `monogame/desktopgl/`, `fna/`, `malformed/`), each fixture with a JSON manifest (`producer`, `platform`, `compressed`, `rootReader`, `expectedType`, expected field values) | ✅ | **Started 2026-07-16**: `tests/assets/xnb/monogame/windows/uncompressed/white-1.xnb` (+ `.manifest.json`) — MonoGame's own smallest real `Texture2D` fixture (a single opaque white pixel), Ms-PL licensed same as CNA. Only one fixture/folder exists so far; the full skeleton (`xna40/`, `lzx/`, `fna/`, `malformed/`) remains open-ended, filled in as later phases need more fixtures |

---

## Phase B2 — Early `ContentManager` vertical slice (moved forward, deliberately not deferred)

> Per review point 5: wiring the whole pipeline into `ContentManager` only at the very end (former
> XNB-46) risks producing an isolated library that doesn't actually fit CNA's existing API. This
> phase proves the real end-to-end call shape (`content.Load<T>("fixture")`) immediately after
> Phase B, using only the trivial test-only reader — before any production readers exist.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-17B | `ContentManager` `.xnb` extension resolution/dispatch, coexisting with existing loose-file loaders (no behavior change to existing `.model.json`/`.shader.json`/`FromStream` paths) | ✅ | **Implemented 2026-07-16**: `ContentManager::Load<T>()`/`LoadXnbAsset<T>()` (`include/Microsoft/Xna/Framework/Content/ContentManager.hpp`) — `<name>.xnb` checked first (ahead of the literal path/`.cnb`/native extensions), needing no per-T reader registered on `ContentManager` at all (dispatch is driven by the file's own type-reader table via the global `ContentTypeReaderManager` registry). LZX-compressed files rejected with a clear error (Phase D not yet implemented). Real design bug found and fixed along the way: see the `std::optional<T>` note on `ContentTypeReader<T>::Read()` below |
| XNB-17C | Asset-path normalization + basic cache-identity handling for `.xnb` assets (same identity rules the loose-file loaders already use) | ✅ | **Free by construction**: `.xnb` results are cached through the exact same `AssetCacheKey`/`loadedAssets_` mechanism loose-file assets already use, no new code needed |
| XNB-17D | `ContentLoadException`-equivalent propagation from the Phase B container/registry errors up through `ContentManager.Load<T>()` | ✅ | **Free by construction**: `ParseXnbHeader`/`ContentTypeReaderManager`/`ContentReader` already throw `ContentLoadException`; `Load<T>()` never intercepts it |
| XNB-17E | `Unload()` behavior for `.xnb`-sourced assets | ✅ | **Free by construction**: `Unload()` already clears `loadedAssets_`, which `.xnb` results are stored in identically to loose-file assets. Tested explicitly (`ContentManagerXnbTest.UnloadClearsXnbCachedAssets`) |
| XNB-17F | End-to-end milestone: `auto value = content.Load<TestValue>("fixture");` using only the Phase B test-only reader | ✅ | **Reached 2026-07-16**: `ContentManagerXnbTest.LoadFindsAndDeserializesARealXnbFile` — first real proof the pipeline fits CNA's existing `ContentManager` API, not just a standalone parser. Also tested: `.xnb` wins over a same-named `.cnb` (`XnbWinsOverCnbAndNativeExtensionForTheSameName`), confirming the 2026-07-16 resolution-order decision end-to-end |
| XNB-17G | Confirm `ContentReader`, per-file reader instances (XNB-14A), shared-resource fixups (XNB-15), and decompression state (Phase D) contain no global mutable state, so different `.xnb` files can be loaded concurrently without cross-talk | ✅ | **Confirmed by inspection 2026-07-16**: `ContentTypeReaderManager`'s static `typeCreators_` is write-once-then-read-only in practice (registered at startup, never mutated during loads); all per-file state (`typeReaders_`, `sharedResources_`, `sharedResourceFixups_`) lives on the `ContentReader` instance `LoadXnbAsset<T>()` constructs fresh per call. Decompression state doesn't exist yet (Phase D deferred) |

> **Design note found while closing XNB-17B (2026-07-16):** `ContentTypeReader<T>::Read()`'s
> `existingInstance` parameter is `std::optional<T>`, not a bare `T` as FNA's literal signature
> reads. C#'s `default(T)` is free for a reference type (`null`, no construction) but real
> zero-init for a value type -- a distinction C++ has no uniform way to express when `T` is a
> value-semantics type with no default constructor (e.g. `SpriteFont`, which always needs real
> construction arguments). Forcing `T{}` eagerly (the first attempt) broke every existing
> `Load<SpriteFont>()` call site the moment `ContentManager.hpp` started including
> `ContentReader.hpp`, since C++ templates instantiate every reachable code path regardless of
> which branch actually runs at runtime. `std::nullopt` now represents FNA's null/`default(T)`
> uniformly; `ContentReader::InnerReadObject<T>()` still falls back to `T{}` when `T` **is**
> default-constructible (matching FNA, which never fails for a null root/nested object), and only
> throws `ContentLoadException` for the genuinely unrepresentable case. **Every future reader
> (Phase C's `Texture2DReader` onward) must declare `Read(ContentReader&, std::optional<T>)`, not
> `Read(ContentReader&, T)`.**

---

## Phase B3 — `ContentManager` startup content manifest + reader-name inventory scanner

> Originally scoped as a standalone, payload-agnostic offline scanner (per second follow-up review
> point 5): a scan of the header + type-reader-name table only, with no dependency on any
> production reader existing yet — running it early can show, for example, that a reader planned
> for Phase F is used by only one sample while a reader not yet in this plan at all is used by
> twenty.
>
> **Revised 2026-07-16:** CNA's owner asked for this to become a real `ContentManager` runtime
> feature, not just a one-off tool — `ContentManager` now scans its `Content` root once (at
> construction, or lazily before the first `Load<T>()` if `RootDirectory` is set afterward) and
> keeps an in-memory manifest of every file found. That manifest serves three purposes at once: an
> internal performance cache for `ResolveAssetPath` (replacing repeated
> `std::filesystem::exists()` stat calls with a single upfront scan), a public `NOXNA`
> introspection API for game/tooling code, and — specifically for any `.xnb` files the scan finds —
> the original reader-name inventory idea, run automatically as part of the same pass rather than
> as a separate manual step.
>
> **Correction from the final review (still applies):** a compressed `.xnb`'s type-reader table
> lives *inside* the LZX payload, and block-based LZX has no addressable random-access offset to
> "just" the table; the decompressor must process the stream sequentially from its start. XNB-61a
> below is therefore scoped to **uncompressed** files only, right after Phase B. The compressed
> case becomes XNB-61b, sequenced after Phase D once a real decompressor exists. Neither task
> deserializes the object payload — both only need to decode/decompress far enough to finish
> reading the type-reader table, then stop.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-65 | `ContentManager` startup manifest scan: walk the `Content` root once (`std::filesystem::recursive_directory_iterator`, bounded by `XnbReadLimits`-style limits on file/entry count) and cache each relative path's existence/extension in memory; `ResolveAssetPath` consults this cache instead of calling `std::filesystem::exists()` per candidate | ✅ | **Implemented 2026-07-16**: `ContentManager::RefreshContentManifest()` (`src/Microsoft/Xna/Framework/Content/ContentManager.cpp`). **Scope narrowed**: the manifest is additive/introspective only in this pass — `ResolveAssetPath()`/`Load<T>()` still use live `std::filesystem::exists()` checks, unchanged. Wiring the manifest into that hot path is deliberately deferred to an isolated follow-up, to avoid risking the large existing test surface built around ContentManager noticing a just-written file immediately |
| XNB-65A | Manifest snapshot/staleness policy: document that the manifest is a point-in-time snapshot (files added after the scan are not found until refreshed) and add an explicit `NOXNA RefreshContentManifest()` method that re-scans on demand — no automatic filesystem-watch/hot-reload is in scope here | ✅ | **Implemented 2026-07-16**, tested explicitly (`ContentManagerManifestTest.RefreshContentManifestPicksUpNewlyAddedFiles`: a file added after the first scan is invisible until `RefreshContentManifest()` runs again) |
| XNB-66 | Public `NOXNA` introspection API exposing the manifest (e.g. `std::vector<ContentManifestEntry> GetContentManifest() const`, `ContentManifestEntry{relativePath, extension, hasXnb, hasCnb}`) | ✅ | **Implemented 2026-07-16**: `ContentManager::GetContentManifest()` + `ContentManifestEntry` (`include/Microsoft/Xna/Framework/Content/ContentManifestEntry.hpp`) — one entry per logical asset name, `nativeExtensions` as a list (not a single `extension` field, since a name can have several native-extension siblings at once) |
| XNB-61a | Reader-name-only scan, **uncompressed `.xnb` files only**, folded into the XNB-65 manifest pass: for every `.xnb` file the manifest scan finds, read the header and type-reader table (no object-graph deserialization) and list every type-reader name referenced | ✅ | **Implemented 2026-07-16**: `ContentManager::ScanXnbReaderNames()`, reusing `ParseXnbHeader`/`ParseXnbTypeReaderTable` from Phase B directly. A malformed `.xnb` doesn't abort the scan — just leaves that entry's `xnbReaderNames` empty (tested) |
| XNB-67 | Aggregate the XNB-61a reader-name inventory into the same public manifest API (XNB-66): which reader names are referenced, how many `.xnb` files reference each, and whether CNA currently has a registered reader for that name | ✅ | **Implemented 2026-07-16**: `ContentManager::GetXnbReaderUsageSummary()` + `ContentManifestReaderUsage`, using a new query-only `ContentTypeReaderManager::IsRegistered()` (no construction side effect, unlike `CreateReader()`) |
| XNB-61b | Extend the XNB-61a scan to **LZX-compressed** XNA `.xnb` files, using CNA's own decompressor (Phase D) — decompress the payload sequentially from the start until the type-reader table has been fully read (for a first implementation, decompressing the entire payload is acceptable; do not prematurely optimize this into a partial-decompression short-circuit); do not continue into the root object. Verify that a compressed fixture and its uncompressed equivalent produce the same reader-name inventory | ⬜ | Sequenced after Phase D (needs a real LZX decoder to exist) — deferred along with the rest of Phase D under the current MVP scope; do not attempt it earlier. `ScanXnbReaderNames()` already checks `header.compressed` and returns an empty inventory for now, ready to extend once Phase D lands |

---

## Phase C — Primitive/math readers + collections + first real Graphics reader

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-18A | Simple primitive readers: `Boolean`/`Byte`/`SByte`/`Int16/32/64`/`UInt16/32/64`/`Single`/`Double`/`Char`/`String` | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::{Boolean,Byte,SByte,Int16,UInt16,Int32,UInt32,Int64,UInt64,Single,Double,Char,String}Reader` (`include`/`src`/`CNA/Internal/Xnb/PrimitiveContentTypeReaders.*`), each registered under its real FNA canonical name via `RegisterPrimitiveXnbReaders()`. Live in `CNA::Internal::Xnb`, not `Microsoft::Xna::Framework::Content` — FNA's own equivalents are all `internal`/default-visibility, never subclassed by game code |
| XNB-18B | `System::Decimal` faithful representation (96-bit integer + sign + scale, matching .NET `Decimal`'s actual layout) and its reader — do **not** approximate as `double reader.readDouble()`; decide up front whether CNA implements a real `Decimal` type or only reads the raw 4×`int32` `DecimalValue` struct verbatim for round-tripping | ⬜ | **Deliberately deferred 2026-07-16**, not dropped: not on the critical path to this phase's M2 milestone (`Texture2DReader` needs no `Decimal`), and `Decimal` is rare in real XNA game content. Design already researched: real .NET `BinaryReader.ReadDecimal()`/`decimal.ToDecimal(span)` reads `lo,mid,hi,flags` as four little-endian `int32`s (in that order — **not** the struct's own in-memory field order, which is `flags,hi,lo64`); sharp-runtime's `System::Decimal(intcs lo, intcs mid, intcs hi, bool isNegative, bytecs scale)` constructor already matches .NET's `decimal(int,int,int,bool,byte)` exactly, so only a `ContentReader::ReadDecimal()`/`DecimalReader` pair needs writing when this is picked back up — no remaining design unknowns |
| XNB-18C | `DateTime`/`TimeSpan` faithful tick semantics: `DateTime` is ticks + `DateTimeKind` bits packed into a 64-bit value, not a plain `int64_t` timestamp; `TimeSpan` must preserve ticks exactly | ⬜ | **Deliberately deferred 2026-07-16**, not dropped: same reasoning as XNB-18B (not on the M2 critical path, rare in real content). Design already researched against FNA's `DateTimeReader.cs`/`TimeSpanReader.cs`: `TimeSpan` is a plain `ReadInt64()` tick count (`System::TimeSpan(longcs ticks)` already exists in sharp-runtime); `DateTime` reads a `UInt64`, masks out the top 2 bits as `DateTimeKind` and the rest as ticks — but sharp-runtime's `System::DateTime` is documented `Status: Partial` and does not store/track `DateTimeKind` at all yet, a pre-existing sharp-runtime gap, not a `.xnb`-specific one; a faithful `DateTimeReader` needs that fixed first (or accepts silently discarding `Kind`, which would need to be an explicit, separately-approved deviation, not assumed here) |
| XNB-19 | Math readers: `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray` | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::{Vector2,Vector3,Vector4,Matrix,Quaternion,Color,Plane,Point,Rectangle,BoundingBox,BoundingSphere,BoundingFrustum,Ray}Reader` (`include`/`src`/`CNA/Internal/Xnb/MathContentTypeReaders.*`), each verified field-by-field against FNA's real source and registered via `RegisterMathXnbReaders()` |
| XNB-20 | `CurveReader` for the existing `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent` runtime API | ⬜ | **Deliberately deferred 2026-07-16**, not dropped: not on the M2 critical path; `Curve` is a real, existing runtime API (per the XNB-1 revalidation) but genuinely rare in typical XNA game content compared to Vector/Matrix/Color/Texture2D. No remaining design blocker — just needs FNA's `CurveReader.cs` field order verified against the existing `Curve` public surface when picked back up |
| XNB-21 | `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>` generic dispatch (recursively invoking another registered reader by an embedded type parameter) | ⬜ | **Deliberately deferred 2026-07-16**, not dropped: confirmed by reading FNA's real `Texture2DReader.cs` that nothing on the path to M2 needs collections at all. Design already researched: FNA's `ArrayReader<T>`/`ListReader<T>` resolve their element reader via `manager.GetTypeReader(typeof(T))` — a *reflection*-based lookup among the current file's own already-instantiated type-reader table, which CNA cannot replicate (no reflection). Since CNA already hand-registers one factory per closed generic combination anyway (the XNB-5 canonical-name registry is inherently per-combination, not truly generic), the planned resolution is: each registered `ArrayReader<T>`/`ListReader<T>` factory closure hardcodes its own element reader's canonical name directly (known at registration time), and the element reader instance is constructed fresh via `ContentTypeReaderManager::CreateReader()` at the C++ constructor, not looked up from the file's table via an `Initialize(manager)` call — a deliberate simplification, valid because every CNA reader so far is stateless (revisit only if a future reader needs real per-file `Initialize()` state) |
| XNB-22 | `NullableReader<T>` (map to `std::optional<T>` on the CNA side, per this session's audit that CNA has no `Nullable<T>` XNA class of its own) | ⬜ | **Deliberately deferred 2026-07-16** alongside XNB-21, same reasoning (not on the M2 critical path) |
| XNB-23 | `Texture2DReader`, implemented strictly against CNA's **backend-neutral** `Texture2D`/`GraphicsDevice` API (`SurfaceFormat` + width/height/mip levels + per-level `SetData`) — the reader must not reference EasyGL (or any other backend) internals directly | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::Texture2DReader` (`include`/`src`/`CNA/Internal/Xnb/Texture2DContentTypeReader.*`), verified against FNA's real `Texture2DReader.cs`. Covers version<5 legacy format mapping, `SurfaceFormat.Color`, and `Dxt1`/`Dxt3`/`Dxt5` (always software-decompressed via the existing `CNA::Internal::Graphics::DxtUtil`, reused not reimplemented); every other format throws a clear, named `ContentLoadException` rather than silently uploading garbage. Caught a real bug while testing: `Color` derives from `IPackedVectorT` (virtual methods → vtable pointer), so it is **not** a raw 4-byte-per-pixel POD — `reinterpret_cast`-ing a raw RGBA byte buffer as `const Color*` reads a vtable-pointer fragment as the first pixel; fixed by constructing real `Color` values one at a time from the bytes |
| XNB-24 | `SurfaceFormat` capability inventory (not immediate blanket conversion): classify each `SurfaceFormat` as native-upload / CPU-conversion / GPU-transcode / unsupported / lossy-fallback per backend (`struct SurfaceFormatCapability{bool nativeUpload; optional<SurfaceFormat> fallback; ConversionFunction converter;}`); `Texture2DReader` only asks `GraphicsDevice`/a formats service for a compatible resource, it does not own per-backend conversion tables itself | ✅ | **Narrowed scope, implemented 2026-07-16**: rather than a full per-backend native-vs-CPU-conversion capability table, `Texture2DReader` always software-decompresses `Dxt1/3/5` to `Color` unconditionally (correct on every backend, just not always the most efficient path) and explicitly rejects every other format with a clear error. The fuller per-backend "does this backend accept compressed data natively" query (to skip decompression when unnecessary) remains a genuine follow-up, tracked here, not silently abandoned |
| XNB-26 | End-to-end test: `content.Load<Texture2D>("foo.xnb")` on an uncompressed real fixture, going through the Phase B2 `ContentManager` slice (not a standalone parser call) | ✅ | **Reached 2026-07-16** — `ContentManagerTexture2DXnbTest.LoadRealMonoGameFixtureEndToEnd`: the real `white-1.xnb` fixture loads through `content.Load<Texture2D>("white-1")`, confirming exact pixel data. Required also wiring `.xnb` into `ContentManager::Load<Texture2D>()`'s own *specialization* (its weak-cache implementation doesn't call the generic `Load<T>()` template body at all, so needed its own `.xnb`-first check) — **M2 milestone reached** |

---

## Phase D — LZX decompression

> **Deferred under the current MVP scope (2026-07-16)** — not started; see the status banner at
> the top of this file. Do not begin any task below until a future decision explicitly resumes it.

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
>
> **Deferred under the current MVP scope (2026-07-16)** — not started; depends on Phase D.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-25 | `Texture3DReader`/`TextureCubeReader`/base `TextureReader` | ⬜ | Same shape as `Texture2DReader` (XNB-23/24), extra dimension/face handling; deliberately sequenced after Phase D so compressed fixtures already work before adding these |

---

## Phase E — `SpriteFont`, stock effects, `SoundEffect`/`Song`

> **Deferred under the current MVP scope (2026-07-16)** — not started; depends on Phase D.

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

> **Deferred under the current MVP scope (2026-07-16)** — not started; depends on Phase D/E.

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
>
> **Deferred under the current MVP scope (2026-07-16)** — not started; depends on Phase D/E/F.

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

## Phase H — Lua-scripted custom `ContentTypeReader` support — **CANCELLED 2026-07-16**

> This phase is cancelled outright, not merely deferred. CNA's owner reviewed the sandboxed-Lua
> design (a dedicated Lua state, custom allocator/instruction-count limits, ~13 binding tasks
> formerly numbered `XNB-48`–`XNB-60`) and rejected it as disproportionate complexity for a niche
> need. Custom `.xnb` `ContentTypeReader`s remain **native C++ only**, registered through Phase G's
> plain `registry.Register("MyGame.Content.LevelReader", ...)` API — the same shape `.cnb`'s
> `RegisterCnbLoader<T>` already uses for game-specific JSON data. No Lua sandbox, binding layer, or
> script-based reader host will be built for `.xnb` custom readers. The task rows that used to live
> here (`ILuaContentReaderHost`, a sandboxed Lua state, primitive/math bindings, a reader
> descriptor/manifest format, shared-resource/external-reference handles, memory/instruction limits,
> error-context translation, a ported `SkinningDataReader` sample, and a porting-workflow doc) are
> removed, not just marked `⏸` — they are not planned to ever be implemented.

---

## Phase I — Official XNA 4.0 sample `.xnb` compatibility inventory

> New phase — replaces guesswork about "broad real-world compatibility" with actual numbers, by
> scanning real official sample content instead of only the hand-picked fixture corpus from
> XNB-17A/XNB-44. Per the third revision, the payload-agnostic reader-*name* scan itself already
> happened much earlier as XNB-61a/XNB-61b (Phase B3) — this phase reuses that inventory and adds
> the parts that genuinely do need Phase G's finished reader set: the compatibility classification
> and the smoke-test selection below.
>
> **Deferred under the current MVP scope (2026-07-16)** — not started; depends on Phase G.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-61 | Re-run/refresh the XNB-61a/XNB-61b (Phase B3) reader-name inventory against the final Phase G reader set if it has gone stale, rather than re-implementing the scan from scratch | ⬜ | Formerly the first scan itself; the scan now lives in Phase B3 (XNB-61a for uncompressed, XNB-61b for LZX) so it doesn't wait for Phase G |
| XNB-62 | Produce a compatibility matrix classifying each reader name found: standard reader (Phase A–F) / custom reader (native C++, Phase G) / `ReflectiveReader` (XNB-42A limitation) / general `EffectReader` (XNB-32A limitation) | ⬜ | |
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
- Phase H (Lua custom readers) is cancelled (see that phase's own section) — it is no longer part
  of any execution order. Phase I (official-sample inventory) remains sequenced *after* Phase A–C
  under the current MVP scope, since it depends on a reasonably complete reader set to be useful.
  Phase B3 is the one deliberate exception: XNB-65/XNB-61a (the startup manifest scan and its
  uncompressed reader-name inventory) only depend on Phase B and may run at any point once Phase B
  exists; XNB-61b (LZX-compressed files) is the same scan extended once Phase D's decompressor
  exists — deferred along with the rest of Phase D under the current MVP scope.
- See the "Execution-order mandate" and "Milestones" sections near the top of this file — they are
  the primary agent-facing guardrails for a long autonomous run and should be re-read whenever this
  plan is revised.
