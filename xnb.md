# XNB binary content pipeline: implementation plan for CNA

> **Status: 🔄 PARTIALLY UN-FROZEN 2026-07-16 — MVP scope complete, Phase D (LZX) fully complete,
> Phase E (`SpriteFont`, stock effects, `SoundEffect`/`Song`, `ReadExternalReference<T>()`) fully
> complete; broad coverage past that still deferred.** CNA's owner decided `.xnb` should become a
> real, additional runtime format again, alongside the already-implemented
> [`cnj.md`](misc/cnj.md)/[`plans/plan_cnj.md`](plans/plan_cnj.md) `.cnj` strategy — not a replacement for it.
> `ContentManager`'s resolution order now ranks `.xnb` **above** both the literal caller-given path
> and `.cnj` (see `cnj.md`'s "Core rule"). [`plans/plan_xnb.md`](plans/plan_xnb.md)'s Phase 0/A/B/B2/B3/C
> (container parsing, binary primitives, the uncompressed-only case, and a first real `Texture2D`
> reader — this plan's own M1/M2 milestones) is fully complete. CNA's owner then explicitly
> requested Phase D (LZX decompression) next: it is now implemented and verified against real
> compressed fixtures. Asked to close out Phase D's own remaining gaps the same day: XNB-27 (a real
> `XnbCompression` enum, both bit values confirmed against MonoGame's own source rather than
> guessed) and XNB-30A (fuzz + differential testing against FNA's own reference `LzxDecoder.cs` run
> under Mono, which found and fixed a real heap-buffer-overflow) are both done -- Phase D now has no
> open tasks besides XNB-30C, still correctly deferred to Phase G; see `plans/plan_xnb.md`'s Phase D
> section for exact scope. CNA's owner then explicitly requested M3 next: `SpriteFontReader` and a
> `SoundEffectReader`
> covering at least one real wave-format variant, reaching M3's own Definition of Done; asked to
> continue the same day, the rest of Phase E followed too -- `ReadExternalReference<T>()`, the 5
> stock-effect readers, the general `EffectReader`'s diagnostic path, and `SongReader` -- so Phase E
> now has no open tasks left; see `plans/plan_xnb.md`'s Phase E section for exactly what each task
> required. Phase D3/F onward — `Model` and the top-quality hardening pass — remain frozen/deferred
> pending a future decision to resume them; nothing there is started. **Phase H (Lua-scripted custom
> `ContentTypeReader` support) is rejected outright, not merely deferred** — see `plans/plan_xnb.md`'s own
> note; custom readers stay a plain C++ registration API (this document's own Phase G), the same
> shape `.cnj`'s `RegisterCnjLoader<T>` already uses. A new content-manifest feature (`ContentManager`
> scans the `Content` root once — internal perf cache + a public introspection API + the `.xnb`
> reader-name inventory) is now part of the active scope too, folded into `plans/plan_xnb.md`'s Phase B3.
> See `plans/plan_xnb.md`'s own top-of-file note for the complete list of what changed in this revision.
> **Superseded 2026-09-02 for the write side.** This document's original scope note said writing
> `.xnb` was permanently out of scope. That is no longer CNA's direction: CNA now produces `.xnb`
> files natively through its own content pipeline. This plan and `plans/plan_xnb.md` continue to
> own the **reader**; the writer is a separate subsystem with its own plan
> ([`plans/plan_xnapipeline.md`](plans/plan_xnapipeline.md)) and its own documentation
> ([`docs/xna-content-pipeline.md`](docs/xna-content-pipeline.md)). Nothing else in this document
> changes: the reader remains the authority on how a `.xnb` file is consumed, and the writer treats
> it as one of its two normative specification sources.

**Status: planning document only. Nothing described here is implemented yet.** This document
replaces the previous "analysis only, low priority" version of `xnb.md` with an actual phased
plan for adding real `.xnb` loading to CNA. It is a plan to be executed later, task by task — no
code changes are part of this document itself.

**Numbered task breakdown: [`plans/plan_xnb.md`](plans/plan_xnb.md)** — turns the phases below into concrete
`XNB-1`, `XNB-2`, ... tasks (mirroring `plans/plan_graphics.md`'s convention), including a Phase 0 gap
audit of what CNA was missing when this plan was first written (no binary stream/reader layer, no
`Curve` classes, etc.) — **since revalidated 2026-07-16 (`XNB-1`): `sharp-runtime` now has a full
`System::IO::Stream`/`MemoryStream`/`BinaryReader` layer, and `Curve`/`CurveKey`/etc. now exist as a
runtime API; see `plans/plan_xnb.md`'s Phase 0 for the corrected findings.** Read this file first for the
*why*; read `plans/plan_xnb.md` for the *what, in order*.

**Changelog:** `plans/plan_xnb.md` was revised after an independent review that flagged several XNB
protocol inaccuracies and one implementation-strategy gap in its first draft — most importantly
that the root object is dispatched via a 1-based type-reader index (not "the first reader"),
that assembly-qualified generic reader names need a real parser (commas nest inside `[[...]]`),
and that `ContentManager` integration should happen early (a new Phase B2) rather than only in the
very last task. See `plans/plan_xnb.md`'s own top-of-file note for the full list of fixes.

**Changelog (follow-up review):** `plans/plan_xnb.md` was revised a second time to add explicit
`XnbReadLimits` from the start, a global-factory-vs-per-file-reader-instance split, an early
known-unsupported placeholder for the general `EffectReader`, faithful `Decimal`/`DateTime`/
`TimeSpan` handling, a physical (not just noted) move of `Texture3DReader`/`TextureCubeReader`
into a Phase D3 after LZX, corrected stock-effect reader wording, a concurrency/no-global-state
check, a continuously-enforced per-reader Definition of Done instead of a retroactive-only audit,
a trimmed single-task Phase 0, and two new later phases: **Phase H** (Lua-scripted custom
`ContentTypeReader` support, for game/sample-specific data readers only, strictly after the native
reader framework is solid) and **Phase I** (an official XNA 4.0 sample `.xnb` compatibility
inventory). See `plans/plan_xnb.md`'s own top-of-file note for the full list.

**Changelog (second follow-up review):** `plans/plan_xnb.md` was revised a third time (rated 9.5/10
architecture, 9/10 protocol accuracy at that point) to: loosen the shared-resource fixup wording so
it guarantees the observable result rather than one specific callback strategy, prefer a stable
CNA-owned `RuntimeTypeId` over a bare `std::type_index`, add five explicit phase milestones plus an
"execution-order mandate" telling an autonomous agent to work strictly phase-by-phase, split out an
early payload-agnostic reader-name inventory scanner into a new **Phase B3** that doesn't block on
Phase G, and add a Lua error-context task. See `plans/plan_xnb.md`'s own top-of-file note for the full
list.

**Changelog (final review):** `plans/plan_xnb.md` was revised a fourth time (rated 9.7/10 architecture,
9.7/10 test strategy at that point) to fix one real protocol-sequencing bug: Phase B3's scanner
cannot actually cover LZX-compressed `.xnb` files before Phase D's decompressor exists, since a
compressed file's type-reader table lives inside the LZX payload with no addressable offset to
"just" the table. Split into **XNB-61a** (uncompressed files, right after Phase B) and **XNB-61b**
(LZX-compressed files, after Phase D). Also split the `⛔` legend symbol into `⏸` (deferred/
optional, not milestone-blocking) vs. `⛔` (blocked by an external dependency) and re-marked
`XNB-30C` (optional MonoGame compression variants) as `⏸` accordingly. This is expected to be the
last plan-document revision before implementation begins — see `plans/plan_xnb.md`'s own top-of-file
note for the full list.

Grounded in FNA's real, current source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Content/`),
read directly rather than summarized from memory, plus a second-opinion breakdown from an
independent review (effort estimates, phase shape, reader-registry sketch) that has been folded
in and reconciled with FNA's actual behavior below.

## Why `.xnb` is a real project, not a container-unwrap

`.xnb` is not "a PNG/FBX/WAV zipped into an archive". It is the binary output of the XNA/MonoGame/
FNA **Content Pipeline** build tool: a serialized *object graph*, not a flat asset dump. A real XNA
or FNA game never ships loose source assets — a build-time compiler bakes them into `.xnb`, and
`ContentManager.Load<T>()` at runtime only ever deserializes that compiled binary format via a
table of `ContentTypeReader` classes (one per serializable .NET type) that reconstruct the graph
node by node, including cross-references between nodes (shared resources).

CNA today does **not** read `.xnb` at all. Every asset type it supports is loaded from a loose,
human-authored/exported file instead: `Texture2D::FromStream` for PNG/JPG, a CNA-original
`.model.json` + binary sidecar scheme for `Model` (see `docs/model-content-pipeline-support.md`),
`.wav`/project data for audio, `.shader.json` + hand-written GLSL for `Effect`. This plan adds a
**new, additional** loading path (real `.xnb` files) alongside the existing one — it does not
replace it, mirroring how FNA itself keeps a handful of loose-extension fallbacks
(`texture2DExtensions`/`soundEffectExtensions`/`effectExtensions` in `ContentManager`) next to its
primary `.xnb` path. Both entry points should keep working side by side, e.g.:

```cpp
content.Load<Texture2D>("player");        // player.xnb (new path)
Texture2D::FromStream(device, stream);    // player.png (existing path, unchanged)
```

## File layout (confirmed from FNA's `ContentManager`/`ContentReader`)

```text
4-byte magic + platform char   'X' 'N' 'B' <platform: 'w'/'d'/'g'/'l'/...>
1-byte version                 4 or 5 (FNA rejects anything else)
1-byte flags                   bit 0x80 = LZX-compressed payload
4-byte int32                   total file length
[if compressed] 4-byte int32   decompressed size, then the LZX-compressed payload
type-reader table               7-bit-encoded count, then per entry: 7-bit-encoded string
                                (reader type name + assembly) + 4-byte int32 reader version
shared-resource count           7-bit-encoded int
root object                     read via the table's first-referenced reader, recursively
shared resources                read last, each fixed up into earlier placeholder references
```

Primitives needed to read any of the above: 7-bit-encoded int (`BinaryReader.Read7BitEncodedInt`
equivalent), 7-bit-length-prefixed strings, little-endian fixed-width integers/floats. Shared
resources matter because the object graph is not strictly tree-shaped — e.g. a `Model`'s mesh parts
can reference a `VertexBuffer` shared with another part; anything read mid-stream via
`ContentReader.ReadSharedResource<T>()` gets a deferred fixup once the real object is read from the
tail section.

### Compression: LZX

FNA's `LzxDecoder.cs` (~750 lines) is a from-scratch reimplementation of the Microsoft Cabinet LZX
algorithm, fixed 64KB/16-bit window, block-framed with a small per-block header (2-byte block size,
occasionally a 5-byte extended form). It has no external dependencies and is not a call into zlib
or any common library — a C++ port would either hand-port this file line-by-line or vendor an
existing LZX implementation. Most real-world FNA/XNA `.xnb` files are compressed, so this is not
optional if the goal is broad compatibility with existing content, but it is also cleanly separable
from everything else (see Phase B below — the container and a handful of readers can be proven
uncompressed-only first).

### The reflection problem (why this needs a hand-maintained registry, not "just parse the type name")

`ContentTypeReaderManager` resolves each type-reader table entry via .NET's `Type.GetType(name)` +
reflection instantiation — a runtime string→type mechanism C++ does not have. FNA itself already
had to solve exactly this on AOT platforms (iOS, no reflection-based type discovery): it keeps a
static `Dictionary<string, Func<ContentTypeReader>>` map, hand-populated per build, plus a
`falseflag`-guarded dead-code path whose only job is stopping an over-eager linker from stripping
reader types nothing else visibly references. This is the direct precedent for CNA's own approach:
a hand-maintained, compile-time reader registry —

```cpp
class ContentTypeReader
{
public:
    virtual ~ContentTypeReader() = default;
    virtual std::shared_ptr<void> Read(ContentReader& input, std::shared_ptr<void> existingInstance) = 0;
};

class ContentTypeReaderRegistry
{
public:
    void Register(const std::string& normalizedReaderName, std::function<std::unique_ptr<ContentTypeReader>()> factory);
    std::unique_ptr<ContentTypeReader> Create(const std::string& normalizedReaderName) const;
};
```

— sized to however many of the ~40 real FNA reader types CNA chooses to support, not a generic
reflection substitute. The table stores assembly-qualified .NET type names on disk (e.g.
`Microsoft.Xna.Framework.Content.Texture2DReader, Microsoft.Xna.Framework.Graphics,
Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553`); the registry lookup needs a
small normalization step (strip assembly/version/culture/public-key suffix down to the bare type
name) before the map lookup, or compatibility will be fragile against reader strings that differ
only in assembly metadata (MonoGame vs. real XNA vs. FNA all use different assembly names for the
same conceptual reader).

## The ~40 real type readers, grouped by realistic priority

FNA ships one reader class per serializable type (`src/Content/ContentReaders/*.cs`, 40 files).

**Primitives/math** — trivial once `ContentReader`'s own binary primitives exist; thin wrappers
around `ReadSingle`/`ReadInt32`/etc., or a few fields of a math struct CNA already has:
`BooleanReader`, `ByteReader`, `SByteReader`, `Int16/32/64Reader`, `UInt16/32/64Reader`,
`SingleReader`, `DoubleReader`, `DecimalReader`, `CharReader`, `StringReader`, `DateTimeReader`,
`TimeSpanReader`, `Vector2/3/4Reader`, `MatrixReader`, `QuaternionReader`, `ColorReader`,
`PlaneReader`, `PointReader`, `RectangleReader`, `BoundingBoxReader`, `BoundingSphereReader`,
`BoundingFrustumReader`, `RayReader`, `CurveReader`.

**Collections/generics** — need a real generic-dispatch mechanism (another shape of the reflection
problem, since `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>`/`NullableReader<T>` all
recursively invoke another type reader keyed by a generic parameter baked into the reader-name
string at write time): `ArrayReader`, `ListReader`, `DictionaryReader`, `NullableReader`,
`ReflectiveReader` (FNA's fallback for arbitrary `[ContentSerializer]`-decorated POCOs — genuinely
reflection-based in FNA and the single hardest piece to port faithfully; CNA should use per-type
hand-written readers instead of attempting a generic fallback, at least initially).

**Graphics-relevant** (what CNA's own Graphics namespace actually needs):
`Texture2DReader`/`Texture3DReader`/`TextureCubeReader`/`TextureReader` (base), `ModelReader` (the
big one — bones, hierarchy, meshes, shared `VertexBuffer`/`IndexBuffer`/`Effect` resources; see
"Relationship to CNA's existing Model loader" below), `EffectReader`/`EffectMaterialReader`
(compiled `.fxb` shader bytecode — a separate, already-tracked concern, see "Relationship to
Phase 74" below), `BasicEffectReader`/`AlphaTestEffectReader`/`DualTextureEffectReader`/
`EnvironmentMapEffectReader`/`SkinnedEffectReader` (stock-effect material parameter blocks),
`SpriteFontReader` (glyph atlas + kerning tables — CNA's `SpriteFont` runtime API is already fully
implemented, only content *loading* is the gap, same shape as `Model`), `VertexBufferReader`/
`IndexBufferReader`/`VertexDeclarationReader`.

**Non-Graphics** (listed for completeness, out of this plan's primary scope):
`SongReader`, `SoundEffectReader`, `VideoReader`, `ExternalReferenceReader`.

## `Texture2D`: the best first real target

Unlike a naive "unzip a PNG" assumption, an XNB texture is usually already-processed GPU-ready mip
data in a target `SurfaceFormat`, not embedded PNG bytes:

```text
SurfaceFormat (int32)
width (int32)
height (int32)
mip level count (int32)
per level: byte count (int32) + raw level data
```

```cpp
std::shared_ptr<Texture2D> Texture2DReader::Read(ContentReader& reader, std::shared_ptr<void>)
{
    const auto format = static_cast<SurfaceFormat>(reader.ReadInt32());
    const auto width = reader.ReadInt32();
    const auto height = reader.ReadInt32();
    const auto levelCount = reader.ReadInt32();

    auto texture = std::make_shared<Texture2D>(reader.GetGraphicsDevice(), width, height, levelCount > 1, format);
    for (int level = 0; level < levelCount; ++level)
    {
        const auto byteCount = reader.ReadInt32();
        texture->SetData(level, reader.ReadBytes(byteCount));
    }
    return texture;
}
```

The real work is `SurfaceFormat` coverage: `Color`, `Bgr565`, `Bgra5551`, `Bgra4444`, `Dxt1`,
`Dxt3`, `Dxt5`, `NormalizedByte2/4`, `Rgba1010102`, `Rg32`, `Rgba64`, `Alpha8`, `Single`, `Vector2`,
`Vector4`, `HalfSingle`, `HalfVector2/4`, `HdrBlendable`. Some map directly onto an existing bgfx/
backend texture format; others need a conversion pass at load time. This should be scoped per
backend (SDL_Renderer, EasyGL, Vulkan, Bgfx each have their own existing `SurfaceFormat` handling to
extend, not replace) rather than assumed to be a single shared conversion table.

## `SpriteFont`: reasonable once the generic-collection readers exist

`SpriteFontReader` reconstructs several linked pieces: a glyph-atlas `Texture2D`, glyph rectangles,
cropping rectangles, a character map, line spacing, spacing, kerning vectors, and an optional
default character (`Nullable<char>`). This needs nested-object reading, generic list/dictionary
readers, and `NullableReader<T>` to already work — but once those exist (Phase B/C below),
`SpriteFontReader` itself is not conceptually hard.

## `Model`: substantially harder, and where CNA already has real, tracked gaps

`ModelReader` is not just a vertex dump — it must load and correctly wire together `ModelBone`
parent/child hierarchy, `ModelMesh`/`ModelMeshPart`, `VertexBuffer`/`IndexBuffer`/`VertexDeclaration`
(translated to a backend vertex layout), `Effect` instances, and per-mesh `BoundingSphere`, using
the shared-resource mechanism (a `VertexBuffer` can legitimately be shared across mesh parts) with
deferred fixups resolved only after the whole graph is read.

This directly overlaps existing, already-documented gaps in CNA's *own* current (non-`.xnb`)
`ModelTypeReader`, per `docs/model-content-pipeline-support.md`: it always synthesizes exactly one
`ModelBone`, never assigns `ModelMesh::ParentBone` even though the runtime API added the setter
needed to do so (Task 439/`setParentBoneProperty`), never sets a real per-mesh `BoundingSphere`, and
never deduplicates shared `VertexBuffer`/`IndexBuffer`/`Effect` instances across mesh parts. A real
`ModelReader` for `.xnb` would need all of this to be genuinely correct (multi-bone hierarchy,
per-mesh `ParentBone`, real `BoundingSphere`, shared-resource dedup) — so implementing it is a
natural opportunity to close those pre-existing gaps at the same time, rather than building a second
model loader with the exact same limitations as the first.

## `Effect`/compiled shader bytecode: deliberately deferred, same boundary as Phase 74

An XNA `.xnb`-embedded effect is not source `.fx` text — it is a platform-dependent *compiled*
shader bytecode blob (D3D9-oriented on a real Windows-built XNB), which none of CNA's backends
(EasyGL/GLSL, Vulkan/SPIR-V, Bgfx) can consume directly. `plans/plan_graphics.md` Phase 74 already tracks
interpreting compiled `.fx` bytecode via MojoShader for `Effect`'s bytecode constructor (currently
`System::NotImplementedException` in `Effect.cpp`). The two efforts are related but independently
scoped: a general XNB loader is the superset container format everything (including compiled
effects) ships inside, but fixing Phase 74 does not require a general XNB loader (a raw `.fxb` file
can already be read directly, matching FNA's own loose `effectExtensions` support), and a general
XNB loader still needs Phase 74's own bytecode work before an `EffectReader`-loaded asset is usable.
Recommended approach for this plan, to avoid blocking on Phase 74 entirely: recognize the small set
of **stock-effect** readers (`BasicEffectReader`, `SkinnedEffectReader`, etc.) and construct CNA's
own native implementation of those stock effects directly, ignoring the embedded platform bytecode
— this alone unblocks most real XNA content, since custom `.fx` effects are the minority case in
practice and already require Phase 74 regardless.

## Custom/game-specific readers: an explicit, permanent limitation

`.xnb` is extensible — a real game can ship its own `ContentTypeReader` subclass (e.g.
`MyGame.Content.LevelReader, MyGame`). No amount of "supporting XNB" can make CNA automatically
understand a reader type it has never seen; this is inherent to the format, not a gap to be closed.
The registry design above should let a CNA game register its own reader the same way it registers
a built-in one:

```cpp
contentTypeReaderRegistry.Register("MyGame.Content.LevelReader", [] { return std::make_unique<MyLevelReader>(); });
```

so the ceiling for "how much of a given third-party XNA game's content can load" is always
"everything using standard readers, plus whatever custom readers the porting developer chooses to
reimplement" — never "100% automatically, no matter the source game."

## Phased plan

Each phase below should become a normal task block in `plans/plan_graphics.md` once this plan is
approved for scheduling — this document only defines the shape and ordering, not task numbers.

### Phase A — `ContentReader` binary primitives

- 7-bit-encoded int/string reads matching .NET's `BinaryReader.Read7BitEncodedInt` exactly.
- Little-endian fixed-width primitive reads (already partially implicit in CNA's own binary I/O
  helpers — audit for reuse before writing new ones).
- No dependencies on anything else in this plan; needed by every later phase.

### Phase B — XNB container, uncompressed only

- Header parse (magic/platform/version/flags/length) with `ContentLoadException`-equivalent
  validation matching FNA's own accepted platform/version set.
- Type-reader table parse + the reader-name normalization step described above.
- `ContentTypeReaderRegistry` (empty at this point — no readers registered yet) and the
  shared-resource placeholder/fixup mechanism.
- Goal: prove the container format end-to-end on a hand-built or FNA-produced *uncompressed* test
  fixture before LZX exists at all.

### Phase C — Primitive/math readers + first real Graphics reader

- All primitive/math readers listed above (cheap, no dependencies beyond Phase A/B).
- `ArrayReader<T>`/`ListReader<T>`/`DictionaryReader<K,V>`/`NullableReader<T>` generic dispatch.
- `Texture2DReader`, including at least the common uncompressed/`Dxt1/3/5` `SurfaceFormat`
  coverage for whichever backend is exercised first (EasyGL is the natural first target, matching
  its role elsewhere in this project as the default/reference backend).
- Goal: `content.Load<Texture2D>("foo.xnb")` genuinely works end-to-end on an uncompressed fixture.

### Phase D — LZX decompression

- Decision point flagged explicitly: most real-world FNA/XNA `.xnb` files are compressed, so this
  is not optional for broad compatibility, but it is a real, self-contained ~750-line algorithmic
  port with no external dependencies (`LzxDecoder.cs`) and deserves its own dedicated
  implementation + fuzz/malformed-input testing pass, not a quick addition folded into Phase B.
- Goal: the exact same fixtures/readers from Phase B/C also work when compressed.

### Phase E — `SpriteFont`, stock effects, `SoundEffect`/`Song`

- `SpriteFontReader` (now straightforward given Phase C's generic collection readers).
- Stock-effect readers (`BasicEffectReader`/`SkinnedEffectReader`/etc.) constructing CNA's native
  stock-effect implementations directly, per the "deliberately deferred" note above — no
  dependency on Phase 74.
- `SoundEffectReader`/`SongReader` (wave format, PCM vs. compressed data, loop region, duration —
  needs mapping onto whichever audio backend CNA uses; scope this against CNA's existing audio
  plan file rather than duplicating audio-specific detail here).

### Phase F — `Model` and the shared-resource-heavy readers

- `VertexBufferReader`/`IndexBufferReader`/`VertexDeclarationReader` (the vertex-layout-to-backend
  translation step already exists in some form for CNA's current JSON model loader — reuse rather
  than reinvent).
- `ModelReader` itself, explicitly designed to close the pre-existing `ModelTypeReader` gaps
  documented in `docs/model-content-pipeline-support.md` (multi-bone hierarchy, per-mesh
  `ParentBone`, real `BoundingSphere`, `Tag`, shared-resource dedup) as part of the same task,
  rather than shipping a second loader with the first loader's known limitations.
- `EffectReader`/`EffectMaterialReader` for genuinely custom (non-stock) effects remain out of
  scope here — tracked entirely under Phase 74, picked up only once/if that phase lands.

### Phase G (open-ended, not scheduled) — custom reader ergonomics

- Public API for a CNA game to register its own `ContentTypeReader` (see snippet above).
- Documentation for porting a real third-party XNA game's custom readers to C++.
- No fixed scope — success is measured per adopting game, not as a CNA-internal completion state.
- This stays plain C++ permanently. `plans/plan_xnb.md` briefly explored a sandboxed-Lua alternative
  (its former Phase H) for game-specific custom readers; that idea was rejected outright on
  2026-07-16 as disproportionate complexity for a niche need — see `plans/plan_xnb.md`'s own note.

## Content manifest: startup `Content`-root scan (new, 2026-07-16)

Alongside un-freezing the MVP scope above, `ContentManager` gains a startup manifest feature,
folded into `plans/plan_xnb.md`'s Phase B3 (`XNB-65`–`XNB-67`): it scans its `Content` root once (at
construction, or lazily before the first `Load<T>()` if `RootDirectory` is set afterward) and keeps
an in-memory index of every file found. This single scan serves three purposes:

1. **An internal performance cache.** `ResolveAssetPath` today calls `std::filesystem::exists()`
   once per candidate extension, per `Load<T>()` call. The manifest replaces that with a single
   upfront directory walk, consulted in memory afterward.
2. **A public `NOXNA` introspection API** (`ContentManager::GetContentManifest()`) so game/tooling
   code can enumerate what assets actually exist under `Content`, without its own filesystem code.
3. **The `.xnb` reader-name inventory** originally proposed as a standalone offline scanner
   (`plans/plan_xnb.md`'s former "Phase B3" scanner): for every `.xnb` file the manifest scan finds, read
   just its header and type-reader table (never the object payload) and record which reader names
   are referenced. Surfaced through the same public API, this turns "which standard readers does
   CNA still need to implement for this game's content" from a manual audit into something the
   running game/tooling can query directly.

The manifest is a point-in-time snapshot — a file added to `Content` after the scan is not found
until an explicit `RefreshContentManifest()` rescan. No filesystem-watch/hot-reload mechanism is
part of this feature. See `plans/plan_xnb.md`'s Phase B3 for the concrete task breakdown.

## Relationship to CNA's existing content approach

CNA's `ContentManager` (`src/Microsoft/Xna/Framework/Content/ContentManager.cpp`) already has a
working, tested, file-extension-based loading scheme for every asset type it currently supports.
None of it is wire-compatible with real FNA-produced `.xnb` assets, and this plan does not change
that existing scheme — `.xnb` loading is a new, additional path, selected by file extension exactly
like FNA's own loose-extension fallbacks, coexisting with `Texture2D::FromStream`, `.model.json`,
`.shader.json`, etc.

## Why broad coverage is still not a "do it now" item

> Superseded in part 2026-07-16: the MVP slice (Phase 0/A/B/B2/B3/C — container, primitives,
> uncompressed `Texture2D`, the content manifest) is now active, per the status banner at the top
> of this file. Superseded a second time the same day: Phase D (LZX decompression) was also
> explicitly requested and is now done. Superseded a third time the same day: Phase E (`SpriteFont`,
> stock effects, `SoundEffect`/`Song`, `ReadExternalReference<T>()`) was requested too and is now
> fully complete. The reasoning below still applies to everything past that point (Phase D3/F
> onward).

- **A general reader registry covering all ~40 FNA reader types is a real, standalone engineering
  effort**, not a quick addition to an existing file — see its own section above. (Phase D itself
  is no longer an example of this — it's done; see `plans/plan_xnb.md`'s Phase D section.)
- **The practical benefit beyond the MVP slice is real but narrow**: broad `.xnb` coverage lets CNA
  load asset files originally compiled for a real XNA/FNA game — valuable for reusing an existing
  game's *compiled* content without re-exporting it, but `docs/migration-guide.md` already tells a
  migrating developer to expect to re-export/rewrite their content pipeline rather than assume full
  `.xnb` compatibility.

`plans/plan_xnb.md` schedules this work as ordered, independently-completable phases (A through I) rather
than as one large, unscoped "implement XNB" task, specifically so a future decision to resume
Phase D onward doesn't have to re-derive that breakdown from scratch.
