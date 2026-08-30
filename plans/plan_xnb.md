# XNB binary content pipeline: task plan

> **Compiled-effect update (2026-08-13):** XNB-14B/XNB-32A's historical unsupported placeholder
> has been replaced by the canonical general `EffectReader`. It performs bounded exact reads and
> constructs compiled XNA/FNA Effect Framework effects on renderers that advertise
> `GraphicsCapability::CompiledEffects` (currently FNA3D). Non-FNA3D renderers still fail
> explicitly with asset context. Historical rows below are retained rather than rewritten; use
> [`plan_fx.md`](plan_fx.md) and `docs/xnb-content-pipeline-support.md` for current status.

> **Status: 🔄 PARTIALLY UN-FROZEN 2026-07-16 — MVP scope (Phase 0/A/B/B2/B3/C) complete; Phase D
> (LZX decompression) fully complete; Phase E (`SpriteFont`, stock effects, `SoundEffect`/`Song`,
> `ReadExternalReference<T>()`) fully complete; Phase F (`Model`) fully complete; Phase D3
> (`Texture3DReader`/`TextureCubeReader`) fully complete; Phase G (top-quality hardening + custom
> reader ergonomics) fully complete; only Phase H (cancelled) and Phase I (still deferred) remain.**
> CNA's owner decided `.xnb` becomes a real, additional runtime format
> again, ranked **above** `.cnj` in `ContentManager`'s resolution order (see [`cnj.md`](../misc/cnj.md)'s
> "Core rule": `.xnb` → literal caller-given path → `.cnj` → native-by-extension). The MVP scope
> through the end of Phase C (container parsing, binary primitives, uncompressed-only, a first real
> `Texture2D` reader — this plan's own M1/M2 milestones) is fully done. CNA's owner explicitly
> requested Phase D (LZX decompression) next; it is now implemented and real-fixture-verified
> (XNB-28/29/30/30B ✅). Asked to close out the rest of Phase D the same day, XNB-27 (a real
> `XnbCompression` enum, both bit values confirmed against MonoGame's own source) and XNB-30A (fuzz
> + differential testing, which found and fixed a real heap-buffer-overflow) are both done too --
> Phase D has no open tasks left besides XNB-30C, deferred by design (see Phase D's own section).
> CNA's owner then explicitly requested M3 next: `SpriteFontReader` (XNB-31) and a real-fixture-
> verified `SoundEffectReader` covering at least one supported wave format (XNB-33/33A) are both
> done, reaching M3's own Definition of Done. Asked to continue the same day, the rest of Phase E
> followed: `ReadExternalReference<T>()` (XNB-35), the 5 stock-effect readers (XNB-32), the general
> `EffectReader`'s diagnostic path (XNB-32A, already implemented back in Phase B and now also
> test-covered), and `SongReader` (XNB-34) are all done too — Phase E has no open tasks left. CNA's
> owner then explicitly requested Phase F next: `VertexDeclarationReader`/`VertexBufferReader`/
> `IndexBufferReader`/`ModelReader` (XNB-36 through XNB-41) are all done, verified against a real
> multi-bone, shared-resource fixture -- this also found and fixed a real bug in Phase E's own
> stock-effect readers (see Phase F's own XNB-40 note). Phase F has no open tasks left. Everything
> sequenced after Phase F (top-quality hardening, Phase D3/G onward) remains frozen/deferred,
> pending a future decision to resume it — do not start those tasks without checking in first.
> **Phase H (Lua-scripted custom readers) is cancelled outright, not deferred**
> — see that phase's own section below; custom `.xnb` readers
> stay a plain C++ registration API (Phase G), matching `.cnj`'s existing `RegisterCnjLoader<T>`.
> Phase B3 has also grown new scope: a `ContentManager` startup content-manifest scan (internal perf
> cache + a public introspection API + the `.xnb` reader-name inventory), see that phase below.

> Companion task list to [`xnb.md`](../xnb.md) (the narrative research/design document — read that
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
> literal caller-given path and `.cnj` (`XNB-17B`).
>
> **Revised a sixth time (2026-07-16)** after CNA's owner explicitly requested Phase D next: LZX
> decompression (XNB-28/29), the block-framing loop wired into `ContentManager::LoadXnbAsset<T>()`,
> and re-verification of the M2-era fixtures in compressed form (XNB-30B) are done, plus a real
> hardening fix for an LZX `match_offset` bounds gap found during the port (part of XNB-30). XNB-27
> (a dedicated `XnbCompression` enum) was deliberately left open — see that row's note — and XNB-30A
> (fuzzing/differential testing) remains open. Phase D3 onward is still frozen pending a future
> decision.
>
> **Revised a seventh time (2026-07-16)** after CNA's owner explicitly requested M3 next:
> `SpriteFontReader` (XNB-31) and a `SoundEffectReader` covering XNB-33's support-matrix survey
> (XNB-33/33A) are done, reaching M3's own Definition of Done. A real architecture gap was found
> and fixed while wiring up `SoundEffectReader`: `std::any` requires `CopyConstructible`
> unconditionally, which `SoundEffect`'s move-only design doesn't satisfy -- see the design note in
> Phase E's own section. XNB-32/32A/34/35 (the rest of Phase E) were not required by M3's narrower
> definition and remain open; XNB-35 in particular blocks both XNB-32 and XNB-34 and would need to
> be picked up first. Phase F onward is still frozen pending a future decision.
>
> **Revised an eighth time (2026-07-16)** after CNA's owner asked to continue past M3 the same day:
> `ReadExternalReference<T>()` (XNB-35) is done, refined mid-implementation to return
> `std::optional<T>` instead of a bare `T` (see Phase E's own design note for why). The 5
> stock-effect readers (XNB-32) are done, requiring `SetOwnedTexture()`/`SetOwnedTexture2()`/
> `SetOwnedEnvironmentMap()` additions to all 5 effect classes and a `std::shared_ptr<T>` reader
> target instead of a bare `T` (another real, necessary architecture decision -- see that row's
> note). The general `EffectReader`'s diagnostic path (XNB-32A) turned out to already exist from
> Phase B, just untested at the `ReadUntyped()` level; added that test. `SongReader` (XNB-34) is
> done, verified against a real fixture plus its real companion audio file. Phase E is now fully
> complete with no open tasks. Phase F onward remains frozen pending a future decision.
>
> **Revised a ninth time (2026-07-16)** after CNA's owner asked to close out Phase D's own
> remaining gaps: XNB-27 is done -- both compression-bit values (`0x80` LZX, `0x40` Lz4) confirmed
> against MonoGame's real `ContentManager.cs` source rather than guessed, which is what actually
> unblocked this task (the earlier "still open" note's concern was not knowing the real bit value,
> not a lack of time). XNB-30A is done: differential tests against FNA's own unmodified
> `LzxDecoder.cs`, executed via Mono (`mcs`/`mono` are available in this environment) -- both real
> fixtures match byte-for-byte -- plus a deterministic mutation fuzzer, which found and fixed a
> real heap-buffer-overflow in `MakeDecodeTable()`'s long-code table-growth path (the exact gap
> XNB-30's own commit flagged as unaudited). Phase D now has no open tasks besides XNB-30C, still
> correctly deferred to Phase G. Phase D3/F onward remain frozen pending a future decision.
>
> **Revised a tenth time (2026-07-16)** after CNA's owner asked for Phase F next:
> `VertexDeclarationReader`/`VertexBufferReader`/`IndexBufferReader` (XNB-36) and `ModelReader`
> (XNB-37/38/39/40) are done, verified against a real, externally-produced multi-bone fixture
> (XNB-41). Found and fixed a real bug in Phase E's own stock-effect readers along the way: they
> erased to `std::shared_ptr<ConcreteEffectType>` instead of the common `std::shared_ptr<Effect>`
> base, which only broke once something (`ModelReader`'s own `ReadSharedResource<Effect>()`)
> actually exercised polymorphic effect dispatch -- Phase E's own tests never had. Also added two
> small missing runtime-API pieces `ModelReader` needed: `ModelMesh::setBoundingSphereProperty()`
> (a real, pre-existing read-write-property gap; only the getter existed) and
> `ContentReader::ReadObject()` (a non-template overload for type-erased `Tag` fields). Phase F now
> has no open tasks. Phase D3/G onward remain frozen pending a future decision.
>
> **Revised an eleventh time (2026-07-16)** after CNA's owner explicitly requested Phase D3 next:
> `Texture3DReader`/`TextureCubeReader` (XNB-25) are done, same scope as `Texture2DReader`
> (Color/Dxt1/Dxt3/Dxt5 only). Found and fixed a real gap in `Texture3D` along the way: it had
> neither a copy nor a move constructor at all, so it could not be returned by value in any C++ code
> path -- fixed by adding real move semantics, matching `VertexBuffer`/`IndexBuffer`/`TextureCube`'s
> own precedent. Also found and fixed a real gap in `ContentManager::Load<TextureCube>()`'s
> pre-existing specialisation: it predated `.xnb` `TextureCube` support and never checked for a
> `.xnb` file at all, unlike the `Texture2D`/`SoundEffect` specialisations -- fixed to match those
> two. `TextureCubeReader` is verified against a real, externally-produced MonoGame fixture;
> `Texture3DReader` has no real fixture available anywhere in the library, so it is verified via a
> hand-constructed stream instead (see Phase D3's own note). Phase D3 now has no open tasks. Phase G
> onward remains frozen pending a future decision.
>
> **Revised a twelfth time (2026-07-16)** after CNA's owner explicitly requested Phase G next: all
> seven tasks (XNB-42/42A/43/44/45/46/47) are done — see Phase G's own section for full detail.
> Highlights: XNB-42 needed no new API (`ContentTypeReaderManager::AddTypeCreator()` was already the
> extension point); XNB-46's `RegisterAllBuiltInXnbReaders()` closed a real gap where no single call
> site to register every built-in reader had ever existed; XNB-43's whole-container fuzz test found
> and fixed a real heap-buffer-overflow (declared-vs-actual byte count mismatch after a truncated
> read) plus two related allocation-bomb gaps; XNB-47's own final compliance sweep broadened that
> fuzzer to `Texture2D`/`SoundEffect` directly and found **three more real heap-buffer-overflows**
> (texture dimension/byteCount cross-validation, `DxtUtil`'s dead `dataSize` bound, an unvalidated
> `totalLength` header field), all fixed and re-verified clean under ASan+UBSan. **Phase G is now
> fully complete**, every task `⬜ → ✅`, reaching M5. Only Phase H (cancelled outright) and Phase I
> (still deferred) remain in this plan.

## Execution-order mandate for autonomous work

> Read this before starting any task in this file.

- Implement strictly in phase order, and only through the current active scope (Phase
  0/A/B/B2/B3/C, Phase D, Phase E, Phase F, Phase D3, and Phase G — all explicitly un-frozen and
  requested by CNA's owner, see the status banner above). Phase H is cancelled outright (see its
  own row below). Do not begin Phase I until CNA's owner explicitly requests it by name — Phase G's
  completion satisfies Phase I's former blocking *dependency*, but is not itself the go-ahead; see
  Phase I's own banner.
- Phase H (Lua-scripted custom readers) is cancelled outright — do not implement it in any form,
  and do not treat it as merely "later". Custom `.xnb` readers stay native C++ only, registered
  through `ContentTypeReaderManager::AddTypeCreator()` (Phase G's XNB-42, already implemented — see
  that row's note and `docs/xnb-content-pipeline-support.md`'s "Custom readers" section).
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
| M3 - common XNA 2D content | end of Phase E | ✅ **Reached 2026-07-16** -- initially against its own narrower Definition of Done, then against the literal "end of Phase E" too once the rest of Phase E was picked up the same day (see the note below the table). Compressed `Texture2D`, `SpriteFont`, and at least one supported `SoundEffect` variant from the XNB-33 matrix all load correctly. |
| M4 - standard model | XNB-41 | ✅ **Reached 2026-07-16.** A real multi-bone XNA `Model` `.xnb` loads with shared resources (`VertexBuffer`/`IndexBuffer`/`Effect`) resolved and a native CNA stock effect (`BasicEffect`) attached, verified against a real, externally-produced fixture. Multi-*mesh* (as opposed to multi-*mesh-part*) coverage is limited to what the one available real fixture actually has (1 mesh) -- the underlying mechanism (`ContentReader::ReadSharedResource<T>`'s per-index dedup) is generic and already tested independently of `ModelReader`, so this isn't considered a gap in the milestone itself. |
| M5 - release-quality native loader | XNB-47 | ✅ **Reached 2026-07-16.** Native `.xnb` support is documented (XNB-45), hardened (XNB-43, plus 3 more real heap-buffer-overflows found and fixed by XNB-47's own broadened compliance sweep), and fully usable without Lua (custom readers register through XNB-42's plain `AddTypeCreator()` extension point instead). |

> **Current active scope (2026-07-16):** M1 and M2 have both been **reached**, and — as of the
> same day, once `XNB-18B`/`XNB-18C`/`XNB-20`/`XNB-21`/`XNB-22` were picked back up and closed —
> **the entire MVP scope (Phase 0/A/B/B2/B3/C) is fully complete**, every task `⬜ → ✅`, not just
> its two named milestones. CNA's owner then explicitly requested Phase D next: LZX decompression
> (XNB-28/29) and re-verification of real fixtures in compressed form (XNB-30B) are done, plus a
> genuine hardening fix found during the port (part of XNB-30). XNB-27 (a dedicated
> `XnbCompression` enum) and XNB-30A (fuzzing/differential testing) remain open by design — see
> their rows in Phase D. `XNB-25` (`Texture3DReader`/`TextureCubeReader`) is now complete, see Phase
> D3's own section below (never part of Phase C itself). CNA's owner then explicitly requested M3
> next:
> `SpriteFontReader` (XNB-31) and a `SoundEffectReader` covering at least one real variant
> (XNB-33/XNB-33A) are both done and real-fixture-verified, reaching M3's own Definition of Done.
> Asked to continue the same day, the rest of Phase E followed: `ReadExternalReference<T>()`
> (XNB-35, returning `std::optional<T>` -- see that row's note for why), the 5 stock-effect readers
> (XNB-32, each targeting `std::shared_ptr<T>` and needing new `SetOwnedTexture()`-family methods
> on all 5 effect classes -- see that row's note), the general `EffectReader`'s diagnostic path
> (XNB-32A, already implemented in Phase B, now also test-covered), and `SongReader` (XNB-34,
> real-fixture-verified) are all done too. **Phase E is now fully complete**, every task `⬜ → ✅`,
> matching the milestone table's literal "end of Phase E" for M3, not just its own narrower
> Definition of Done. CNA's owner then explicitly requested Phase F next: `VertexDeclarationReader`/
> `VertexBufferReader`/`IndexBufferReader` (XNB-36) and `ModelReader` (XNB-37/38/39/40) are done and
> real-fixture-verified (XNB-41), reaching M4. This also **corrected** the 5 stock-effect readers'
> own erased type from `std::shared_ptr<ConcreteEffectType>` to the common `std::shared_ptr<Effect>`
> base (a real bug in Phase E's own work, only exposed once `ModelReader`'s
> `ReadSharedResource<Effect>()` actually exercised polymorphic effect dispatch) -- see XNB-40's own
> note. **Phase F is now fully complete**, every task `⬜ → ✅`. CNA's owner then explicitly
> requested Phase D3 next: `Texture3DReader`/`TextureCubeReader` (XNB-25) are done, same scope as
> `Texture2DReader`. This also **fixed** two real gaps found along the way: `Texture3D` had neither
> a copy nor a move constructor at all (added real move semantics, matching
> `VertexBuffer`/`IndexBuffer`/`TextureCube`'s own precedent), and
> `ContentManager::Load<TextureCube>()`'s pre-existing specialisation never checked for a `.xnb`
> file at all (fixed to match the `Texture2D`/`SoundEffect` specialisations). **Phase D3 is now
> fully complete**, every task `⬜ → ✅`. CNA's owner then explicitly requested Phase G next:
> XNB-42/42A (the custom-reader extension point, already `AddTypeCreator()`, plus the
> `ReflectiveReader<T>` decision), XNB-46 (`RegisterAllBuiltInXnbReaders()`, the single call site
> that never existed before), XNB-44 (real-world compatibility verified against two independent
> third-party sources), and XNB-45 (`docs/xnb-content-pipeline-support.md`) are all done. XNB-43's
> whole-container fuzz test found and fixed a real heap-buffer-overflow (`ReadBytesExactOrThrow()`,
> now used everywhere a reader trusts a declared byte count) plus two related allocation-bomb gaps
> (`CheckCollectionElementCount()`, and a `sharp-runtime` `BinaryReader` hardening fix). XNB-47's own
> final compliance sweep broadened that same fuzzer to target `Texture2D`/`SoundEffect` directly
> (not just `Model`) and found **three more real heap-buffer-overflows**: `Texture2D`/`3D`/`Cube`
> readers never cross-checked declared byteCount against width/height/depth (plus fixed an
> always-broken, unrelated compressed-volume-texture slicing bug in `Texture3DReader` found along
> the way), `DxtUtil::DecompressDxt1/3/5` never actually bounds-checked their own `dataSize`
> parameter, and `ContentManager::LoadXnbAsset<T>()`'s Lzx branch trusted an unvalidated
> file-declared `totalLength`. All fixed and re-verified clean under
> `-DCNA_SANITIZE=address,undefined`. **Phase G is now fully complete**, every task `⬜ → ✅`,
> reaching M5. Only Phase H (cancelled outright) and Phase I (still deferred pending a future
> decision) remain.

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
> — the `.cnj`/native-loader interface (`Read(const std::string& path, ContentManager&)`), a
> CNA-original shape that does not match FNA's real `ContentTypeReader<T>`
> (`Read(ContentReader input, T existingInstance)`) at all. The two cannot coexist under the same
> name in the same namespace. Options: (a) rename CNA's existing loose-file reader interface to
> free up the real name — a wide-reaching refactor touching every existing `.cnj` reader
> (`SpriteFontTypeReader`/`EffectTypeReader`/`ModelTypeReader`/`SkinnedModelTypeReader`,
> `RegisterCnjLoader<T>`, `RegisterTypeReader<T>`) and `ContentManager.hpp`/`.cpp` itself; (b) keep
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
| XNB-17B | `ContentManager` `.xnb` extension resolution/dispatch, coexisting with existing loose-file loaders (no behavior change to existing `.model.json`/`.shader.json`/`FromStream` paths) | ✅ | **Implemented 2026-07-16**: `ContentManager::Load<T>()`/`LoadXnbAsset<T>()` (`include/Microsoft/Xna/Framework/Content/ContentManager.hpp`) — `<name>.xnb` checked first (ahead of the literal path/`.cnj`/native extensions), needing no per-T reader registered on `ContentManager` at all (dispatch is driven by the file's own type-reader table via the global `ContentTypeReaderManager` registry). LZX-compressed files now decompress and load through the same path since Phase D landed (2026-07-16). Real design bug found and fixed along the way: see the `std::optional<T>` note on `ContentTypeReader<T>::Read()` below |
| XNB-17C | Asset-path normalization + basic cache-identity handling for `.xnb` assets (same identity rules the loose-file loaders already use) | ✅ | **Free by construction**: `.xnb` results are cached through the exact same `AssetCacheKey`/`loadedAssets_` mechanism loose-file assets already use, no new code needed |
| XNB-17D | `ContentLoadException`-equivalent propagation from the Phase B container/registry errors up through `ContentManager.Load<T>()` | ✅ | **Free by construction**: `ParseXnbHeader`/`ContentTypeReaderManager`/`ContentReader` already throw `ContentLoadException`; `Load<T>()` never intercepts it |
| XNB-17E | `Unload()` behavior for `.xnb`-sourced assets | ✅ | **Free by construction**: `Unload()` already clears `loadedAssets_`, which `.xnb` results are stored in identically to loose-file assets. Tested explicitly (`ContentManagerXnbTest.UnloadClearsXnbCachedAssets`) |
| XNB-17F | End-to-end milestone: `auto value = content.Load<TestValue>("fixture");` using only the Phase B test-only reader | ✅ | **Reached 2026-07-16**: `ContentManagerXnbTest.LoadFindsAndDeserializesARealXnbFile` — first real proof the pipeline fits CNA's existing `ContentManager` API, not just a standalone parser. Also tested: `.xnb` wins over a same-named `.cnj` (`XnbWinsOverCnjAndNativeExtensionForTheSameName`), confirming the 2026-07-16 resolution-order decision end-to-end |
| XNB-17G | Confirm `ContentReader`, per-file reader instances (XNB-14A), shared-resource fixups (XNB-15), and decompression state (Phase D) contain no global mutable state, so different `.xnb` files can be loaded concurrently without cross-talk | ✅ | **Confirmed by inspection 2026-07-16**: `ContentTypeReaderManager`'s static `typeCreators_` is write-once-then-read-only in practice (registered at startup, never mutated during loads); all per-file state (`typeReaders_`, `sharedResources_`, `sharedResourceFixups_`) lives on the `ContentReader` instance `LoadXnbAsset<T>()` constructs fresh per call. Decompression state (`CNA::Internal::Xnb::LzxDecoder`'s `LzxState`) similarly lives entirely on the local `LzxDecoder` instance `DecompressXnbPayload()` constructs fresh per call — no static/global state either |

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
| XNB-66 | Public `NOXNA` introspection API exposing the manifest (e.g. `std::vector<ContentManifestEntry> GetContentManifest() const`, `ContentManifestEntry{relativePath, extension, hasXnb, hasCnj}`) | ✅ | **Implemented 2026-07-16**: `ContentManager::GetContentManifest()` + `ContentManifestEntry` (`include/Microsoft/Xna/Framework/Content/ContentManifestEntry.hpp`) — one entry per logical asset name, `nativeExtensions` as a list (not a single `extension` field, since a name can have several native-extension siblings at once) |
| XNB-61a | Reader-name-only scan, **uncompressed `.xnb` files only**, folded into the XNB-65 manifest pass: for every `.xnb` file the manifest scan finds, read the header and type-reader table (no object-graph deserialization) and list every type-reader name referenced | ✅ | **Implemented 2026-07-16**: `ContentManager::ScanXnbReaderNames()`, reusing `ParseXnbHeader`/`ParseXnbTypeReaderTable` from Phase B directly. A malformed `.xnb` doesn't abort the scan — just leaves that entry's `xnbReaderNames` empty (tested) |
| XNB-67 | Aggregate the XNB-61a reader-name inventory into the same public manifest API (XNB-66): which reader names are referenced, how many `.xnb` files reference each, and whether CNA currently has a registered reader for that name | ✅ | **Implemented 2026-07-16**: `ContentManager::GetXnbReaderUsageSummary()` + `ContentManifestReaderUsage`, using a new query-only `ContentTypeReaderManager::IsRegistered()` (no construction side effect, unlike `CreateReader()`) |
| XNB-61b | Extend the XNB-61a scan to **LZX-compressed** XNA `.xnb` files, using CNA's own decompressor (Phase D) — decompress the payload sequentially from the start until the type-reader table has been fully read (for a first implementation, decompressing the entire payload is acceptable; do not prematurely optimize this into a partial-decompression short-circuit); do not continue into the root object. Verify that a compressed fixture and its uncompressed equivalent produce the same reader-name inventory | ⬜ | **Unblocked 2026-07-16** — Phase D's decompressor (`DecompressXnbPayload`) now exists, so this task is no longer waiting on a dependency, but it has not been picked up yet. `ScanXnbReaderNames()` (`src/Microsoft/Xna/Framework/Content/ContentManager.cpp`) still just checks `header.compressed` and returns an empty inventory early for compressed files — that early-return is the exact spot to replace with a real decompress-then-scan call |

---

## Phase C — Primitive/math readers + collections + first real Graphics reader

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-18A | Simple primitive readers: `Boolean`/`Byte`/`SByte`/`Int16/32/64`/`UInt16/32/64`/`Single`/`Double`/`Char`/`String` | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::{Boolean,Byte,SByte,Int16,UInt16,Int32,UInt32,Int64,UInt64,Single,Double,Char,String}Reader` (`include`/`src`/`CNA/Internal/Xnb/PrimitiveContentTypeReaders.*`), each registered under its real FNA canonical name via `RegisterPrimitiveXnbReaders()`. Live in `CNA::Internal::Xnb`, not `Microsoft::Xna::Framework::Content` — FNA's own equivalents are all `internal`/default-visibility, never subclassed by game code |
| XNB-18B | `System::Decimal` faithful representation (96-bit integer + sign + scale, matching .NET `Decimal`'s actual layout) and its reader — do **not** approximate as `double reader.readDouble()`; decide up front whether CNA implements a real `Decimal` type or only reads the raw 4×`int32` `DecimalValue` struct verbatim for round-tripping | ✅ | **Implemented 2026-07-16**: `sharp-runtime`'s `System::IO::BinaryReader::ReadDecimal()` (`feature/xnb-charreader` branch, commit `2505d58c`, reads `lo,mid,hi,flags` in that exact on-disk order) + CNA's `CNA::Internal::Xnb::DecimalReader` (`include`/`src`/`CNA/Internal/Xnb/DecimalDateTimeContentTypeReaders.*`). Guarded out on MSVC (`#if !defined(_MSC_VER)`), matching `System::Decimal`'s own `unsigned __int128` requirement |
| XNB-18C | `DateTime`/`TimeSpan` faithful tick semantics: `DateTime` is ticks + `DateTimeKind` bits packed into a 64-bit value, not a plain `int64_t` timestamp; `TimeSpan` must preserve ticks exactly | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::TimeSpanReader` (plain `ReadInt64()` tick count) and `DateTimeReader` (reads `UInt64`, masks out `DateTimeKind` from the top 2 bits and ticks from the rest, matching FNA exactly). **Documented deviation**: `DateTimeKind` is parsed but discarded — `System::DateTime` remains `Status: Partial` (does not store `Kind` at all); fixing that pre-existing sharp-runtime gap would mean touching every existing `DateTime` constructor, judged out of scope for this reader task and deferred separately |
| XNB-19 | Math readers: `Vector2/3/4`, `Matrix`, `Quaternion`, `Color`, `Plane`, `Point`, `Rectangle`, `BoundingBox`, `BoundingSphere`, `BoundingFrustum`, `Ray` | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::{Vector2,Vector3,Vector4,Matrix,Quaternion,Color,Plane,Point,Rectangle,BoundingBox,BoundingSphere,BoundingFrustum,Ray}Reader` (`include`/`src`/`CNA/Internal/Xnb/MathContentTypeReaders.*`), each verified field-by-field against FNA's real source and registered via `RegisterMathXnbReaders()` |
| XNB-20 | `CurveReader` for the existing `Curve`/`CurveKey`/`CurveContinuity`/`CurveLoopType`/`CurveTangent` runtime API | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::CurveReader` (`include`/`src`/`CNA/Internal/Xnb/CurveContentTypeReader.*`), field order verified against FNA's real `CurveReader.cs` |
| XNB-21 | `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>` generic dispatch (recursively invoking another registered reader by an embedded type parameter) | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<TKey,TValue>` (`include/CNA/Internal/Xnb/CollectionContentTypeReaders.hpp`, header-only templates). Resolves the reflection problem exactly as planned: each concrete instantiation takes its element reader's canonical name explicitly (constructor parameter), constructing a fresh instance via `ContentTypeReaderManager::CreateReader()` rather than an `Initialize(manager)`-based file-table lookup. `T[]`/`List<T>` both map to `std::vector<T>`; `Dictionary<TKey,TValue>` maps to `std::unordered_map`. Faithfully reproduces two easy-to-miss FNA behaviors verified against the real source: `ListReader` never clears `existingInstance` (appends after it); `DictionaryReader` does clear it first. No global pre-registration — a future reader registers the specific combination it needs, since none exists yet |
| XNB-22 | `NullableReader<T>` (map to `std::optional<T>` on the CNA side, per this session's audit that CNA has no `Nullable<T>` XNA class of its own) | ✅ | **Implemented 2026-07-16** alongside XNB-21: `CNA::Internal::Xnb::NullableReader<T>` (same file), maps `T?` to `std::optional<T>` exactly as planned |
| XNB-23 | `Texture2DReader`, implemented strictly against CNA's **backend-neutral** `Texture2D`/`GraphicsDevice` API (`SurfaceFormat` + width/height/mip levels + per-level `SetData`) — the reader must not reference EasyGL (or any other backend) internals directly | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::Texture2DReader` (`include`/`src`/`CNA/Internal/Xnb/Texture2DContentTypeReader.*`), verified against FNA's real `Texture2DReader.cs`. Covers version<5 legacy format mapping, `SurfaceFormat.Color`, and `Dxt1`/`Dxt3`/`Dxt5` (always software-decompressed via the existing `CNA::Internal::Graphics::DxtUtil`, reused not reimplemented); every other format throws a clear, named `ContentLoadException` rather than silently uploading garbage. Caught a real bug while testing: `Color` derives from `IPackedVectorT` (virtual methods → vtable pointer), so it is **not** a raw 4-byte-per-pixel POD — `reinterpret_cast`-ing a raw RGBA byte buffer as `const Color*` reads a vtable-pointer fragment as the first pixel; fixed by constructing real `Color` values one at a time from the bytes |
| XNB-24 | `SurfaceFormat` capability inventory (not immediate blanket conversion): classify each `SurfaceFormat` as native-upload / CPU-conversion / GPU-transcode / unsupported / lossy-fallback per backend (`struct SurfaceFormatCapability{bool nativeUpload; optional<SurfaceFormat> fallback; ConversionFunction converter;}`); `Texture2DReader` only asks `GraphicsDevice`/a formats service for a compatible resource, it does not own per-backend conversion tables itself | ✅ | **Narrowed scope, implemented 2026-07-16**: rather than a full per-backend native-vs-CPU-conversion capability table, `Texture2DReader` always software-decompresses `Dxt1/3/5` to `Color` unconditionally (correct on every backend, just not always the most efficient path) and explicitly rejects every other format with a clear error. The fuller per-backend "does this backend accept compressed data natively" query (to skip decompression when unnecessary) remains a genuine follow-up, tracked here, not silently abandoned |
| XNB-26 | End-to-end test: `content.Load<Texture2D>("foo.xnb")` on an uncompressed real fixture, going through the Phase B2 `ContentManager` slice (not a standalone parser call) | ✅ | **Reached 2026-07-16** — `ContentManagerTexture2DXnbTest.LoadRealMonoGameFixtureEndToEnd`: the real `white-1.xnb` fixture loads through `content.Load<Texture2D>("white-1")`, confirming exact pixel data. Required also wiring `.xnb` into `ContentManager::Load<Texture2D>()`'s own *specialization* (its weak-cache implementation doesn't call the generic `Load<T>()` template body at all, so needed its own `.xnb`-first check) — **M2 milestone reached**. **SAMPLE-013 correction 2026-08-24:** level-scoped `new ContentManager(game.Services, "Content")` now resolves `IGraphicsDeviceService` from its supplied service provider just like XNA, instead of requiring the non-XNA `setGraphicsDevice` extension. `ResolvesGraphicsDeviceFromServiceProvider` locks the real XNB load path. |

> **SAMPLE-014 cache correction — 2026-08-24.** The weak Texture2D specialization described in
> the historical XNB-26 closure diverged from FNA's `Dictionary<string, object> loadedAssets` and
> XNA lifetime semantics. Texture2D now follows the generic strong cache through `Unload()`; the
> XNB-first behavior remains in that single generic path. Ownership and unload tests were updated
> to distinguish cache identity from external wrapper lifetime. The generic `Load<T>` diagnostic
> now also runs only after a cache miss: Spacewar faithfully calls `Content.Load` from `Render()`,
> and logging every cache hit generated two browser-console messages (with JavaScript stacks) per
> frame even though no asset was actually loaded.

---

## Phase D — LZX decompression

> **Fully complete 2026-07-16** — un-frozen the same day, in two rounds: first LZX decompression
> itself (XNB-28/29/30/30B), then the two remaining gaps (XNB-27's real compression enum, XNB-30A's
> fuzz/differential tests) once CNA's owner explicitly asked for them to be closed out. XNB-30C
> (MonoGame's alternate compression variants) remains deliberately deferred to Phase G -- see that
> row's own note.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-27 | Header-level compression enum, not a boolean: `enum class XnbCompression{None, Lzx, Lz4, Unknown}` parsed from the flags byte — do **not** hardcode `compressed == true → LZX` anywhere in the architecture | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::XnbCompression` (`include/CNA/Internal/Xnb/XnbHeader.hpp`), replacing `XnbHeader::compressed` (bool). Both bit values confirmed against MonoGame's own `ContentManager.cs` (`ContentCompressedLzx = 0x80`, `ContentCompressedLz4 = 0x40`) rather than guessed -- this is what unblocked the task (the earlier "still open" note's concern was not knowing the real Lz4 bit value; that's now confirmed, independent of XNB-30C's still-unstarted *decoding* work). Both bits set simultaneously maps to `Unknown`. `ContentManager::LoadXnbAsset<T>()` now switches on the enum: `Lzx` decompresses (unchanged), `Lz4`/`Unknown` throw a specific `ContentLoadException` instead of mis-decoding or producing a confusing error |
| XNB-28 | Phase D1 — Port FNA's `LzxDecoder.cs` (~750 lines, self-contained, no external deps) to C++ for the real XNA `Lzx` case | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::LzxDecoder` (`include`/`src`/`CNA/Internal/Xnb/LzxDecoder.*`), a line-by-line port preserving FNA's own control flow (including its `goto case` fallthrough, translated to native C++ switch-fallthrough) and error-as-return-code style, so it can be diffed against the original directly |
| XNB-29 | Block-framing loop (2-byte block size, occasional 5-byte extended form) wired into the Phase B header's compressed-payload path | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::DecompressXnbPayload()` (`include`/`src`/`CNA/Internal/Xnb/XnbDecompression.*`), matching FNA's own block-framing loop in `ContentManager.GetContentReaderFromXnb` byte-for-byte; wired into `ContentManager::LoadXnbAsset<T>()` so compressed files load exactly like uncompressed ones |
| XNB-30 | Malformed/truncated/adversarial-input hardening for the decompressor: bounds checks, integer-overflow guards on decompressed-size fields, decompression-bomb output-size limits, no OOB reads on corrupt input | ✅ | **Implemented 2026-07-16, completed via XNB-30A the same day**: `XnbReadLimits` consulted on both compressed and decompressed sizes before any allocation (decompression-bomb guard); an out-of-range LZX `match_offset` is explicitly rejected (found during the initial port). The one gap this row originally flagged as unaudited -- `MakeDecodeTable`'s long-code table-growth path -- was found and fixed by XNB-30A's fuzz test the same day (a real heap-buffer-overflow), closing this row out completely rather than leaving it narrowed |
| XNB-30A | Fuzz tests + differential tests against a reference LZX implementation for the decompressor | ✅ | **Implemented 2026-07-16**: `LzxDecoderDifferentialTests.cpp` compares CNA's output byte-for-byte against FNA's own, unmodified `LzxDecoder.cs` run under Mono (`mcs`/`mono`, both available in this environment) -- a genuine independent cross-implementation check, not a re-derivation; both real fixtures match exactly (SHA-256-identical), reference bytes vendored under `tests/assets/xnb/monogame/windows/lzx/reference-decompressed/` with a reproduction README. `LzxDecoderFuzzTests.cpp` adds a deterministic mutation fuzzer over the same real payloads. This **found and fixed a real heap-buffer-overflow** in `MakeDecodeTable()`'s long-code table-growth path -- exactly the array-access gap XNB-30's own commit flagged as unaudited; confirmed clean afterward under an `-DCNA_SANITIZE=address,undefined` build. Closes out Phase D with no open tasks (besides XNB-30C, deferred by design) |
| XNB-30B | Re-run every Phase B/C fixture through its compressed form and confirm identical results | ✅ | **Implemented 2026-07-16, with a scope note**: verified against 2 *real, externally-produced* LZX-compressed fixtures (MonoGame's own `Explosion.xnb`/`FontCalibri14.xnb`, `tests/assets/xnb/monogame/windows/lzx/`) rather than re-compressing the existing synthetic Phase B/C test fixtures — CNA has no LZX *encoder* (by design; writing `.xnb` is permanently out of scope) to compress them with, and real external fixtures are stronger evidence of true format compatibility than self-compressed ones would be anyway. `Explosion.xnb` verifies a single-block decode end-to-end through `content.Load<Texture2D>()` with real non-uniform pixel data; `FontCalibri14.xnb` verifies multi-block state persistence and recovers the exact reader set (including nested generic names) a real `SpriteFont` needs |
| XNB-30C | Phase D2 — investigate/implement MonoGame-supported alternative compression variants under the `XnbCompression` enum from XNB-27 | ⏸ | Deferred until broad MonoGame compatibility work (Phase G/XNB-44); explicitly optional for the first working loader and not required by any milestone M1-M5 — the enum from XNB-27 just needs to not preclude it later |

---

## Phase D3 — Additional texture readers (physically after LZX, not just a "deferred" note)

> Per follow-up review point 7: the previous draft only had a *note* saying `Texture3DReader`/
> `TextureCubeReader` were "deferred until after Phase D" while the task row itself still sat
> physically inside Phase C, which could mislead an autonomous agent into implementing it too early.
> This phase makes that ordering unambiguous by relocating the task itself.
>
> **Fully complete 2026-07-16** — un-frozen the same day CNA's owner explicitly requested it.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-25 | `Texture3DReader`/`TextureCubeReader`/base `TextureReader` | ✅ | **Implemented 2026-07-16; base-reader omission corrected by SAMPLE-005 on 2026-08-23.** `CNA::Internal::Xnb::Texture3DReader`/`TextureCubeReader` (`include`/`src`/`CNA/Internal/Xnb/Texture3DContentTypeReader.*`, `TextureCubeContentTypeReader.*`) implement the concrete volume/cube formats. The original closure text incorrectly claimed the inert FNA `TextureReader` base was registered too; `ReachGraphicsDemo_4_0/sky.xnb` proved it was absent because the reflective `Sky.Texture` field declares the abstract base in its reader manifest. Added the correctly named inert `ContentTypeReader<Texture*>`, which consumes no bytes and returns the existing instance exactly like FNA, plus registration/idempotence/behavior tests. `Texture3DReader` targets `std::shared_ptr<Texture3D>` because the asset is move-only; `TextureCubeReader` targets the move-only value directly. `TextureCubeReader` remains verified end-to-end against `SampleCube64DXT1Mips.xnb` (six faces, DXT1 mip chain 64→1), while no real volume fixture is available. `ContentManager::Load<TextureCube>()` also continues to probe `.xnb` before the loose DDS path. |

> **SAMPLE-014 TextureCube correction — 2026-08-24.** TextureCube is now a copyable
> shared-resource wrapper, so its reader's value target participates in generic strong caching.
> Removing the uncached specialization preserves the XNB-first route while preventing repeated
> decode/upload; a real cubemap fixture proves cache identity, retention and a fresh resource after
> `Unload()`.

---

## Phase E — `SpriteFont`, stock effects, `SoundEffect`/`Song`

> **Fully complete 2026-07-16**, un-frozen the same day as Phase D across two rounds of
> CNA-owner-requested work: first the M3-critical slice (`SpriteFontReader` XNB-31,
> `SoundEffectReader` XNB-33/33A), reaching M3's own Definition of Done (compressed `Texture2D` --
> already done in Phase D -- plus a real `SpriteFont` plus at least one supported `SoundEffect`
> variant); then, requested again the same day, the rest of the phase (`ReadExternalReference<T>()`
> XNB-35, the 5 stock-effect readers XNB-32, the general `EffectReader`'s diagnostic path XNB-32A,
> `SongReader` XNB-34) closing out every remaining task. See each row's own note for what was
> actually implemented and any scope narrowing.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-31 | `SpriteFontReader` (glyph atlas `Texture2D` + rects + character map + kerning + optional default char via `NullableReader`) | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::SpriteFontReader` (`include`/`src`/`CNA/Internal/Xnb/SpriteFontContentTypeReader.*`), plus concrete `ListReader<Rectangle>`/`ListReader<char16_t>`/`ListReader<Vector3>` registrations it depends on. Verified against 2 real, externally-produced fixtures: MonoGame's uncompressed `Default.xnb` (whose glyph atlas happens to be Dxt3-compressed, exercising Texture2DReader's compressed path too) and the already-vendored LZX-compressed `FontCalibri14.xnb` (ties Phase D and Phase E together end-to-end for the first time). The `existingInstance`-provided reload path is FNA's own dead code (`CanDeserializeIntoExistingObject` defaults false, never overridden) and throws a documented `NotImplementedException` instead of adding a SpriteFont mutator that would serve no other purpose. Uncovered a real architecture gap while wiring this up: see the `std::any`/move-only-type note below |
| XNB-32 | Stock-effect readers: `BasicEffectReader`/`AlphaTestEffectReader`/`DualTextureEffectReader`/`EnvironmentMapEffectReader`/`SkinnedEffectReader` — deserialize the exact stock-effect fields stored by each corresponding XNA reader (colors, texture references, lighting flags, fog parameters, etc.) and construct CNA's own native stock-effect implementation from them; do **not** route the object through the general `EffectReader`'s compiled-bytecode path at all | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::{BasicEffect,AlphaTestEffect,DualTextureEffect,EnvironmentMapEffect,SkinnedEffect}Reader` (`include`/`src`/`CNA/Internal/Xnb/StockEffectContentTypeReaders.*`), each targeting `std::shared_ptr<T>` rather than a bare `T` (CNA's stock effect classes have a private clone-only copy constructor and no move constructor, so a bare-value `ContentTypeReader<T>` cannot compile a `Read()` returning `T` -- matches an existing CNA convention already used for GC-tracked `Effect` references elsewhere). Added `SetOwnedTexture()`/`SetOwnedTexture2()`/`SetOwnedEnvironmentMap()` (NOXNA) to all 5 effect classes so a standalone content-loaded effect can keep its own texture reference alive, matching real XNA's GC-tracked `Effect.Texture` (the existing `setTextureProperty(Texture2D*)` stays a non-owning pointer, used by `Model`'s shared texture pool, unchanged). `BasicEffectReader` verified against a real fixture slice (MonoGame's `BlenderDefaultCube.xnb`, byte-located and confirmed by direct inspection since `ModelReader`/Phase F doesn't exist yet); the other 4 use hand-constructed streams verified field-by-field against FNA's own source (no real fixture found in the available library for any of them) |
| XNB-32A | Implement the actual detection/failure path for the general `Microsoft.Xna.Framework.Content.EffectReader` (compiled platform shader bytecode, distinct from the stock-effect readers in XNB-32): precise, documented exception message until real compiled-FX support exists (see `plan_graphics.md` Phase 74) | ✅ | **Already implemented in Phase B (XNB-14B)**, pre-emptively, before Phase E existed; confirmed complete 2026-07-16 by adding the one thing that was actually missing -- a test that dispatches through a real `ContentReader` and asserts `ReadUntyped()`'s `ContentLoadException` message names both the target type and the reason (`tests/Microsoft/Xna/Framework/Content/KnownUnsupportedContentTypeReaderTests.cpp`); previously only registration was tested |
| XNB-33 | Audio-fixture survey task (run *before* writing `SoundEffectReader`): collect real fixtures for XNA-Windows PCM, XNA-Windows ADPCM/other supported compressed form, MonoGame DesktopGL, and an FNA-compatible build; produce a support matrix (`XNA Windows PCM SoundEffect` / `XNA Windows compressed SoundEffect` / `Song as external file` / `platform-specific codec` → supported / converted / explicitly rejected) | ✅ | **Implemented 2026-07-16**: 6 real MonoGame `tone_*.xnb` fixtures (`tests/assets/xnb/monogame/windows/uncompressed/audio/`) covering PCM 16-bit mono/stereo, PCM 8-bit, IEEE float, MS-ADPCM, and IMA-ADPCM, each with a manifest recording its exact WAVEFORMATEX fields and `supportMatrixStatus`. Matrix: 16-bit PCM → supported (CNA's `SoundEffect` PCM constructors are `SDL_AUDIO_S16LE`-only); 8-bit PCM/float/MS-ADPCM/IMA-ADPCM/XMA2 → explicitly rejected, no conversion path yet. `Song`-as-external-file and platform-specific codecs are XNB-34's own scope, not surveyed here. **Superseded 2026-07-17 (`plan_audio.md` AUD-06, see XNB-33A's own update below):** 8-bit PCM/float/MS-ADPCM/IMA-ADPCM are no longer rejected -- a conversion path (WAV-wrapping through SDL3's own native decoder) was added. Only XMA2 remains explicitly rejected, unchanged. |
| XNB-33A | `SoundEffectReader` (wave format, PCM vs. compressed data, loop region, duration → CNA audio backend), scoped to the matrix from XNB-33 | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::SoundEffectReader` (`include`/`src`/`CNA/Internal/Xnb/SoundEffectContentTypeReader.*`), a line-by-line port of FNA's own `SoundEffectReader.cs` (including its Xbox-only endian-swap helpers and XMA2 extra-field parsing, both dead code on every real fixture used here but ported rather than dropped, matching this plan's precedent for the LZX decoder's Intel E8 path). Rejects anything outside XNB-33's "supported" row with a documented `ContentLoadException` instead of silently constructing a `SoundEffect` that would play back as noise. **Updated 2026-07-17 (`plan_audio.md` AUD-06-002..009, 2026-07-17 deep audit):** widened to support 8-bit PCM, IEEE float, and MS/IMA ADPCM by wrapping the raw bytes in a synthetic in-memory WAV file and decoding via `SoundEffect::FromStream`/SDL3's native WAV decoder, instead of rejecting them outright. Found and fixed a real defect en route: MonoGame's MS-ADPCM XNB fixture has `cbSize=0` (no coefficient table embedded at all), which SDL3's MS-ADPCM decoder cannot decode without -- fixed by synthesizing the standard industry MS-ADPCM coefficient table when the XNB doesn't supply a usable one. Only XMA2 remains rejected (no decode path anywhere in this stack). See `plan_audio.md` for full details and test evidence. |
| XNB-34 | `SongReader`, scoped to the matrix from XNB-33 (external-file case in particular) | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::SongReader` (`include`/`src`/`CNA/Internal/Xnb/SongContentTypeReader.*`), a line-by-line port of FNA's `SongReader.cs` (path resolution + strip-last-4-characters-then-reprobe `.ogg`/`.oga`/`.qoa` extension normalization). Verified against a real fixture (MonoGame's `one_two_three.xnb`) together with its real companion `.ogg` file, vendored alongside it, so the extension-probing actually finds a real file on disk. Found a real, pre-existing CNA behavior (not a bug in this reader): unlike FNA, which defers a still-unresolved path's failure to actual playback, CNA's own `Song` constructor eagerly validates with `std::filesystem::exists()` and throws immediately -- documented, not worked around |
| XNB-35 | `ContentReader::ReadExternalReference<T>()` (not a standalone `ExternalReferenceReader` type): read the referenced asset name/path, resolve it relative to the current asset, load it through the owning `ContentManager`, detect dependency cycles, preserve cache identity when the same asset is referenced more than once, and perform a type check against `T` | ✅ | **Implemented 2026-07-16**, then refined the same day once XNB-32 surfaced a real gap: returns `std::optional<T>` rather than FNA's literal bare `T` (a bare `T` can't express "no reference was present" for CNA's value-type assets the way C#'s reference-type `null` does -- matches the `std::optional<T>` idiom already used for `existingInstance`). Path resolution mirrors FNA's `FileHelpers.ResolveRelativePath` via `std::filesystem::path`'s segment-collapsing; a path that escapes the content root is rejected outright (hardening beyond FNA, which just lets the OS fail to find it). Solves a real circular-include problem (`ContentReader.hpp`/`ContentManager.hpp` can't include each other) via explicit template instantiation in `ContentReader.cpp`, for `Texture2D` and `TextureCube` (the two concrete types XNB-32 needs) |
| XNB-35A | Preserve the concrete type of FNA's `ExternalReferenceReader` / `ReadExternalReference<object>()` | ✅ | **Implemented by SAMPLE-053 on 2026-08-30.** The old reader hard-coded `Texture2D`, so an `EffectMaterial` parameter pointing at the sample's real `TextureCubeReader` asset escaped as `std::bad_any_cast`. The type-erased path now dispatches the referenced XNB through its own root reader, caches that concrete object under the resolved logical name, and returns it unchanged. `EffectMaterialReader` applies and retains `Texture2D`, `Texture3D`, and `TextureCube` values. A focused test references the same real 64x64 DXT1 cube XNB twice through `Dictionary<String,Object>`, proves both values remain `TextureCube`, and proves cache identity by their shared renderer resource; the sample's official `seattle_0.xnb` is the end-to-end XNA-produced case. |
| XNB-36 | `VideoReader` -- not originally planned in this file (Phase E's own scope ended at `SongReader`/XNB-34); added by `plan_media.md`'s Phase 5 (`MEDIA-70`..`MEDIA-75`), cross-referenced here for consistency with XNB-34's own precedent | ✅ | **Implemented 2026-07-17**: `CNA::Internal::Xnb::VideoReader` (`include`/`src`/`CNA/Internal/Xnb/VideoContentTypeReader.*`), structured identically to `SongReader` (XNB-34) -- same strip-last-4-characters-then-reprobe extension normalization (`.ogv`/`.ogg` instead of `.ogg`/`.oga`/`.qoa`), same real-`ContentManager`-required design. Reads all 5 of FNA's real `VideoReader.cs` fields (durationMS/width/height/framesPerSecond/soundTrackType) via direct typed reads (`ReadInt32`/`ReadSingle`), matching `SongReader`'s own code style rather than replicating FNA's own internal inconsistency (FNA's `VideoReader` reads its fields via the more generic `input.ReadObject<T>()`, a pure C# implementation-detail difference with no effect on the actual binary format). No FNA logic existed to port for a CNA-specific fixture-availability gap this task surfaced: `ContentManagerSongXnbTests.cpp`'s real MonoGame-produced `.xnb` fixture has no Video equivalent obtainable in this environment (no MonoGame/dotnet content-pipeline tooling installed) -- `VideoContentTypeReaderTests.cpp` covers the reader's real logic via hand-constructed in-memory buffers instead (the same technique `SongContentTypeReaderTests.cpp` itself already uses for its own non-full-round-trip tests), with the full-container round-trip gap recorded in `NEXTmedia.md` as a real, honest limitation rather than silently skipped |

> **Design note found while closing XNB-31/XNB-33A (2026-07-16):** `std::any` (the type-erased
> transport `ContentTypeReaderBase::ReadUntyped()` uses, see XNB-16A's decision above) requires its
> held type to be `CopyConstructible` -- unconditionally, not just for the move path. `SoundEffect`
> is move-only by design (its per-owner `Dispose()`-cascade tracking, T-3G, needs a single
> unambiguous owner), so `ContentTypeReader<SoundEffect>::ReadUntyped()` could not compile against
> the reader base as originally written: `std::any(std::move(result))` is simply not a valid
> overload for a move-only `result`. Fixed by branching at compile time
> (`if constexpr (std::is_copy_constructible_v<T>)`) in both `ContentTypeReader<T>::ReadUntyped()`
> and `ContentReader::InnerReadObject<T>()`: a copy-constructible `T` is boxed exactly as before; a
> move-only `T` is boxed as `std::shared_ptr<T>` instead, unwrapped back out symmetrically. Every
> existing reader (all copy-constructible `T`s) is unaffected -- confirmed by the full suite still
> passing unchanged. **Any future reader for a move-only asset type must expect this same branch**;
> no new reader code needs to know about it directly, since the branching lives entirely in the two
> shared base-class methods.

---

## Phase F — `Model` and shared-resource-heavy readers

> **Fully complete 2026-07-16** — un-frozen and explicitly requested by CNA's owner the same day
> as the rest of this plan's late-stage work. Reused `ContentManager.cpp`'s own `.model.json`
> `ModelTypeReader` ownership pattern almost exactly (a `unique_ptr`/`shared_ptr`-owning resources
> struct kept alive via `Model::setOwnedResources()`), confirming that precedent was the right thing
> to mirror rather than reinvent.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-36 | `VertexBufferReader`/`IndexBufferReader`/`VertexDeclarationReader` | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::{VertexDeclaration,VertexBuffer,IndexBuffer}Reader` (`include`/`src`/`CNA/Internal/Xnb/ModelContentTypeReaders.*`). `VertexDeclarationReader` targets a bare `VertexDeclaration` (confirmed copy-constructible via `std::is_copy_constructible_v`, not assumed); `VertexBufferReader`/`IndexBufferReader` target `std::shared_ptr<T>` (both are move-only *and* genuinely shared resources across mesh parts, matching `ReadSharedResource<T>()`'s own purpose). `VertexDeclaration` itself is read via `ReadRawObject<VertexDeclaration>()` against a directly-constructed reader instance (no per-file "search by target type" lookup exists in this architecture, unlike FNA's own reflection-based one) |
| XNB-37 | `ModelReader`: bone parent/child hierarchy (multiple bones, not just one) | ✅ | **Implemented 2026-07-16**, verified against a real 2-bone fixture (`RootNode` -> `Cube`). The scalar per-bone parent-index field is read (for stream-position correctness) but not used to mutate state -- `ModelBone::AddChild()` (called from each bone's own child-index list) already sets the child's `Parent` when the true parent processes it, and CNA's `ModelBone` exposes no separate `SetParent()` to duplicate that with |
| XNB-38 | `ModelReader`: per-mesh `ParentBone` assignment via the existing `ModelMesh::setParentBoneProperty` setter | ✅ | **Implemented 2026-07-16** via `Model`'s own 5-argument constructor's `meshParentBones` parameter (which itself assigns `ModelMesh::parentBone_` directly) rather than a separate `setParentBoneProperty()` call -- that constructor overload's own doc comment already named `ModelReader` as its intended first real caller |
| XNB-39 | `ModelReader`: real per-mesh `BoundingSphere`, model/mesh/mesh-part `Tag` | ✅ | **Implemented 2026-07-16; custom Tag completion added by SAMPLE-005 on 2026-08-23.** `ModelMesh::setBoundingSphereProperty()` and the type-erased `ContentReader::ReadObject()` path remain the basis. The earlier implementation rejected every non-null Tag, but `ReachGraphicsDemo_4_0/dude.xnb` carries its processor-generated `SkinnedModel.SkinningData` in `Model.Tag`, making that rejection an actual XNA incompatibility rather than an exotic case. `ModelReader` now accepts a custom reader erased explicitly as `std::shared_ptr<System::Object>`, assigns model/mesh/mesh-part Tags, and retains their owners with the Model's other reader-owned resources. A synthetic real-XNB test proves the Tag survives `ContentManager::Load<Model>()`; SAMPLE-005 proves the complete official `dude.xnb` Tag graph, animation clip dictionary, skin transforms and `SkinnedEffect` render path. Unsupported erasure types are still refused by name instead of being dropped. |
| XNB-40 | `ModelReader`: shared-resource dedup across mesh parts (`VertexBuffer`/`IndexBuffer`/`Effect` reused via `ReadSharedResource<T>`, not always freshly allocated) | ✅ | **Implemented 2026-07-16**. Found and fixed a real, necessary bug in Phase E's own stock-effect readers along the way: they erased to `std::shared_ptr<ConcreteEffectType>` (e.g. `shared_ptr<BasicEffect>`), but `ReadSharedResource<std::shared_ptr<Effect>>()` (the only correct call here, since a `ModelMeshPart`'s `Effect` can be *any* of the 5 concrete types) needs every reader to agree on one erased base type -- unlike FNA's own C# cast, which succeeds across a concrete-to-base relationship via ordinary RTTI, `std::any_cast<T>` requires an *exact* type match. All 5 stock-effect readers now target `std::shared_ptr<Effect>` (each `Read()` still constructs the real concrete type internally; the upcast happens implicitly on return) |
| XNB-41 | End-to-end test: a real multi-bone, shared-resource `.xnb` model loads correctly, matching the Task 431-439 runtime-API audit's expectations | ✅ | **Implemented 2026-07-16**: verified against a real, externally-produced fixture (MonoGame's `BlenderDefaultCube.xnb`, already vendored for XNB-32) -- 2 bones, 1 mesh with a real `BoundingSphere`/`ParentBone`, 1 mesh part with real `VertexBuffer`/`IndexBuffer`/`BasicEffect` shared resources, every field value independently verified with a standalone Python parser before the reader was written. Also confirms `Model` participates in `ContentManager`'s generic asset cache correctly (a second `Load<Model>()` for the same name returns the same underlying GPU resources, not a fresh decode) |

---

## Phase G — Top-quality hardening + custom reader ergonomics (deliberately last)

> This is where "špičková kvalita" (top-tier quality) actually lives — not attempted before every
> earlier phase is solid, per `xnb.md`'s own "why this is still not urgent" framing.
>
> **Fully complete 2026-07-16** — un-frozen the same day CNA's owner explicitly requested it.
> XNB-47's own compliance sweep (broadening the fuzz coverage from XNB-43 to also target
> `Texture2D`/`SoundEffect` directly, not just `Model`) found and fixed three more real
> heap-buffer-overflows beyond XNB-43's own findings — see that row's note.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-42 | Public API for a CNA game to register its own custom `ContentTypeReader` (`registry.Register("MyGame.Content.LevelReader", ...)`) | ✅ | **Implemented 2026-07-16**: no new API needed — `ContentTypeReaderManager::AddTypeCreator()` (XNB-14/14A) is already the public, generic extension point, matching FNA's own real internal method of the same name/shape. `CustomContentTypeReaderTests.cpp` proves it end-to-end with a fully game-defined type/reader, and confirms the design against a real, independent third-party custom reader (`prime31/Nez`'s `BitmapFontReader`, correctly rejected as unregistered) |
| XNB-42A | `ReflectiveReader<T>` compatibility decision: CNA will support only manually registered, explicit C++ `ContentTypeReader`s for custom content (no generalized C++ reflection-driven reader) — document this decision and what a game author must do instead when their `.xnb` was built with an implicit `ReflectiveReader` | ✅ | **Documented 2026-07-16** in `docs/xnb-content-pipeline-support.md`'s own "ReflectiveReader<T>" section: explicit decision plus the concrete workaround (rebuild with an explicit `ContentTypeWriter`/reader pair, then register the C++ equivalent) |
| XNB-42B | Extend `ReflectiveTypeReaderBuilder<T>` for `[ContentSerializer(SharedResource = true)]` fields | ✅ | **Implemented by SAMPLE-052 on 2026-08-30.** `SharedResourceField()` reads the serialized 1-based shared-resource index and queues the assignment through `ContentReader::ReadSharedResource`, preserving XNA's root-first/resources-second/fixups-last order. Because the fixup is deferred, the builder requires `RegisterShared()` for a stable `std::shared_ptr` target and rejects value-shaped `Register()` with `std::logic_error`. A synthetic full-container test proves the root returns with its shared payload assigned; the sample's official `tank.xnb` supplies the real processor-produced case with two shared `BasicEffect` instances. |
| XNB-43 | Malformed/adversarial-file robustness pass across the *entire* pipeline (not just LZX from Phase D) — truncated tables, bad shared-resource indices, reader-version mismatches | ✅ | **Implemented 2026-07-16**: a whole-container deterministic fuzz test (`XnbContainerFuzzTests.cpp`, mutating a real Model fixture's entire byte stream through the real `ContentManager::Load<T>()` path) found a real heap-buffer-overflow — `VertexBufferReader`/`IndexBufferReader` (and by the same pattern `Texture2D`/`Texture3D`/`TextureCube`/`SoundEffect`'s raw-byte-blob reads) trusted `ReadBytes()`'s *requested* length instead of its *actual returned* length (which silently trims on EOF, matching real .NET). Fixed via `ContentReader::ReadBytesExactOrThrow()`, now used by all 8 such call sites. Also fixed: `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<TKey,TValue>` had no bound on their declared element count before allocating (`ContentReader::CheckCollectionElementCount()`, wiring up the previously-dead `XnbReadLimits::maxCollectionElementCount`); a malformed generic type name leaked `XnbTypeName`'s own `std::invalid_argument` instead of the pipeline's one consistent `ContentLoadException`. Also hardened `sharp-runtime`'s `BinaryReader::ReadString()`/`ReadBytes(int)` against an allocation-bomb from an attacker-controlled length prefix (a seekable stream's own remaining length now bounds the allocation) |
| XNB-44 | Broad compatibility test corpus: real files from real XNA 4.0 Windows, MonoGame, and FNA-produced `.xnb` variants, documenting any behavioral differences found | ✅ | **Implemented 2026-07-16**: verified against two independent real-world sources beyond MonoGame's own test assets — `prime31/Nez` (MIT-licensed; its MacOSX-platform `'X'` texture loads correctly, its custom `BitmapFontReader`-based font is correctly rejected) and `openeggbert/speedyblupi.com` (its WebAssembly-platform `'b'` files are correctly rejected, matching FNA's real `targetPlatformIdentifiers` exactly — MonoGame added `'b'` after FNA's fork point). Neither large binary was vendored into the repo; the platform-acceptance finding is captured as a small hand-crafted header test instead (`XnbHeaderTests.cpp`'s `MonoGameWebAssemblyPlatformIsNotAcceptedMatchingFnaExactly`) |
| XNB-45 | Full developer documentation (`docs/xnb-content-pipeline-support.md`) covering exactly what is/isn't supported, mirroring the style of `docs/model-content-pipeline-support.md` | ✅ | **Implemented 2026-07-16**: supported-readers table, compression/platform/audio support matrices, the XNB-42 custom-reader extension point with a runnable example, the XNB-42A decision, the XNB-43 hardening summary, and the XNB-44 compatibility findings. Also corrected `docs/model-content-pipeline-support.md`, which had gone stale since Phase F added a real binary `ModelReader` (that doc only ever covered the older `.model.json` loose-file path) |
| XNB-46 | Register every Phase A-F reader that ended up implemented into the single `ContentTypeReaderRegistry` first stood up in Phase B2, alongside the existing loose-file loaders | ✅ | **Implemented 2026-07-16**: `CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders()` — before this, no single call site existed anywhere, even in production code, to register every built-in reader; a real game would have had to discover and call all thirteen individual `Register*XnbReader()` functions itself. Deliberately not auto-invoked from `ContentManager`'s constructor (would defeat many existing tests' `ClearTypeCreators()`-based isolation) |
| XNB-47 | Final compliance audit against the per-reader checklist below: exact serialized layout confirmed from an authoritative implementation; ≥ 1 real externally-produced fixture; success test; truncated-input test; invalid-count/size test; wrong-reader-version test; asset ownership/unload verified; backend-independent behavior verified; supported producer/platform variants documented | ✅ | **Implemented 2026-07-16**: confirmed backend-independence by inspection (zero backend-specific references anywhere under `CNA::Internal::Xnb`); confirmed `Texture2D`'s own weak-cache `Unload()` path specifically (a different code path from the generic asset cache XNB-17D already covered) with a new dedicated test; broadened `XnbContainerFuzzTests.cpp` to fuzz `Texture2D` and `SoundEffect` directly as root dispatch targets, not just `Model` — this found and fixed **three more real heap-buffer-overflows**: `Texture2D`/`3D`/`CubeReader` never cross-checked their declared byteCount against width/height/depth (plus an always-broken, never-exercised bug in `Texture3DReader`'s compressed-volume-texture path, which decompressed a whole multi-slice level as one 2D image); `DxtUtil::DecompressDxt1/3/5` accepted a `dataSize` parameter but never actually bounds-checked against it; `ContentManager::LoadXnbAsset<T>()`'s Lzx branch used the file's own unvalidated `totalLength` header field to size a read from the just-read file buffer. All three confirmed fixed under `-DCNA_SANITIZE=address,undefined` |

> **SAMPLE-014 ownership correction — 2026-08-24.** XNB-47's weak-cache wording is retained
> above as historical evidence, but the current compliance result is the opposite: XNA/FNA keeps
> Texture2D and TextureCube assets strongly reachable from ContentManager until `Unload()`.
> Dedicated cycle, teardown and real-fixture tests now pin that behavior.

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
> plain `registry.Register("MyGame.Content.LevelReader", ...)` API — the same shape `.cnj`'s
> `RegisterCnjLoader<T>` already uses for game-specific JSON data. No Lua sandbox, binding layer, or
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
> **Deferred (2026-07-16)** — not started. Phase G (its former blocking dependency) is now
> complete, but this phase still requires its own explicit go-ahead before starting, matching every
> other phase transition in this plan (each one only began once CNA's owner explicitly requested it
> by name) — do not treat Phase G's completion as an implicit green light to begin Phase I.

| # | Task | Status | Notes |
|---|------|--------|-------|
| XNB-61 | Re-run/refresh the XNB-61a/XNB-61b (Phase B3) reader-name inventory against the final Phase G reader set if it has gone stale, rather than re-implementing the scan from scratch | ⬜ | Formerly the first scan itself; the scan now lives in Phase B3 (XNB-61a for uncompressed, XNB-61b for LZX) so it doesn't wait for Phase G |
| XNB-62 | Produce a compatibility matrix classifying each reader name found: standard reader (Phase A–F) / custom reader (native C++, Phase G) / `ReflectiveReader` (XNB-42A limitation) / general `EffectReader` (XNB-32A limitation) | ⬜ | |
| XNB-63 | Select a representative smoke-test set covering: plain 2D texture, `SpriteFont`, audio, a stock-effect model, one custom-reader sample, one custom-`.fx` sample | ⬜ | Small enough to run routinely, broad enough to catch regressions across phases |
| XNB-64 | Track "`.xnb` loads successfully" compatibility separately from "full sample runs correctly at runtime" compatibility — the two are different claims and must not be conflated in `docs/xnb-content-pipeline-support.md` (XNB-45) | ⬜ | A `.xnb` can deserialize successfully while the sample still fails at runtime for unrelated reasons (input, gamerservices, etc.) — keep the claims separate and honest |

---

## Relationship to other plan files

- [`xnb.md`](../xnb.md) — the narrative design document this task list is derived from; read it for
  *why*, not just *what*.
- [`plan_graphics.md`](plan_graphics.md) Phase 74 — compiled `.fx` shader bytecode via MojoShader;
  related but independent (see `xnb.md`'s own "Effect/compiled shader bytecode" section). Phase E's
  stock-effect readers (XNB-32) deliberately avoid needing Phase 74 at all; the general `EffectReader`
  (XNB-32A) is the one place this plan and Phase 74 actually connect.
- [`docs/model-content-pipeline-support.md`](../docs/model-content-pipeline-support.md) — the existing,
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

---

## Handoff notes for a future session (2026-07-16)

> Written at the point Phase G was just closed out, specifically so a **fresh agent with no memory
> of the conversation that did this work** can pick this plan back up correctly. Read this section
> first if you are starting cold on `plan_xnb.md`.

### Where things stand right now

Every phase through **Phase G is fully complete** (every task in Phase 0/A/B/B2/B3/C/D/D3/E/F/G is
`✅`), on branch `feature/xnb`, most recently at commit `6501c456` ("docs(Task XNB-47): mark Phase G
complete in plan_xnb.md"). The full test suite passes clean: **4637 tests, 0 failures** (2 unrelated
environment-gated skips: `Accelerometer`/`GyroscopeTests`). `.xnb` loading is a real, hardened,
documented, wire-compatible content format now — see `docs/xnb-content-pipeline-support.md` for the
complete supported-readers/compression/platform/audio matrices and known limitations, and this
file's own status banner + Phase G section for the full history of how it got there.

**Only two things remain in this plan, and neither should be started without the project owner
explicitly asking for it by name** — every phase transition in this plan's entire history happened
that way (never inferred from context or "seems like the natural next step"):

- **Phase H** — cancelled outright, not deferred. There is nothing to do here, ever, under the
  current design (native C++ custom readers only, via `ContentTypeReaderManager::AddTypeCreator()` —
  already implemented, XNB-42). Do not resurrect it unless the project owner explicitly reverses
  that decision.
- **Phase I** — official XNA 4.0 sample compatibility inventory (XNB-61 through XNB-64). Genuinely
  useful future work, but deliberately gated behind an explicit request (see Phase I's own banner
  and the execution-order mandate above) — Phase G's completion satisfies its former *dependency*,
  not its go-ahead.

### If the project owner asks "what's left" or "what should we do next"

Offer, in order of how directly related they are to this plan:

1. **Phase I** (this plan's own remaining phase) — real official-sample compatibility numbers,
   replacing the hand-picked fixture corpus's implicit claims with an actual inventory + smoke tests.
2. A **real, confirmed, currently-unfixed bug outside this plan's scope**, found incidentally during
   this work and only ever *documented*, never fixed: `NetworkSession::Dispose()` is not idempotent
   (missing an `isDisposed_` guard other `Dispose()` methods in this codebase have) — a genuine
   heap-use-after-free when called twice. Full root cause, a concrete fix, and test guidance are
   already written up in [`plan_net.md`](plan_net.md)'s **Phase 12** (`Task 12.1`,
   `src/Microsoft/Xna/Framework/Net/NetworkSession.cpp:278-294`). This is unrelated to `.xnb` and
   belongs to that plan file, not this one — mentioned here only so a fresh session doesn't have to
   rediscover that it exists.
3. Anything else the project owner has in mind that isn't reflected in either plan file yet.

Do not start any of the above speculatively — surface the options and let the project owner choose.

### Working conventions this plan's history has consistently followed

If continuing work in this plan (Phase I, or a future revision), keep doing what got every prior
phase here to a clean, verified state — see `CLAUDE.md` for the full, authoritative project rules,
but the specific habits this plan's own history leaned on hardest:

- Real, externally-produced fixtures only for "does this reader work" claims — never a fixture CNA
  itself generated. When no real fixture exists anywhere in the available library (this happened for
  `Texture3DReader`, for instance), say so explicitly rather than presenting a hand-built stream as
  equivalent.
- Build and run the *full* test suite (`grep -c "FAILED"` on the untruncated output, never just the
  tail) before every commit, not just the tests for the file just touched.
- One task = one commit, explicit `git add <files>` (never `-A`), commit message references the task
  ID and explains *why*, not just what.
- Update this plan file's status markers (`⬜ → ✅`) in a **separate** commit immediately after the
  implementation commit, once the implementation is actually verified — not bundled into the same
  commit, and not deferred to "later".
- When a fuzz/hardening pass finds a real crash, treat it as a first-class deliverable, not a
  distraction from the task at hand — Phase G's XNB-43 and XNB-47 rows both ended up finding and
  fixing real heap-buffer-overflows this way, confirmed under `-DCNA_SANITIZE=address,undefined`
  (`cmake -S . -B cmake-build-asan -DCMAKE_BUILD_TYPE=Debug -DCNA_SANITIZE=address,undefined`) before
  being considered actually fixed, not just "probably fixed".
